/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2020 University of Utah, The Trustees of Columbia University in
the City of New York, and others.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/
#include "stdafx.h"
#include "FEContinuousElasticDamage.h"
#include <FECore/FEModel.h>
#ifdef HAVE_GSL
#include "gsl/gsl_sf_expint.h"
#endif

// Macauley Bracket
#define MB(a) ((a)>0.0?(a):0.0)
#define SIGN(a) ((a)>0.0?1.0:0.0)

//=========================================================================================

class FEFiberDamagePoint : public FEMaterialPoint
{
public:
	FEFiberDamagePoint(FEMaterialPoint* pm) : FEMaterialPoint(pm)
	{
		m_D = 0.0;
		m_psi_f0_ini = 0.0;
		m_psi_f0 = 0.0;
		m_psi_f0_prev = 0.0;
		m_bt_ini = 0.0;
		m_bt = 0.0;
		m_bt_prev = 0.0;
		m_gamma = 0.0;
		m_gamma_prev = 0.0;
		m_psf_c = 0.0;
		m_init = false;
	}

	void Init() override
	{
		m_D = 0.0;
		m_psi_f0_ini = m_psf_c;// we set this to the initial value
		m_psi_f0 = m_psf_c;		// we set this to the initial value
		m_psi_f0_prev = m_psf_c; // we set this to the initial value
		m_bt_ini = 0.0;
		m_bt = 0.0;
		m_bt_prev = 0.0;
		m_gamma = 0.0;
		m_gamma_prev = 0.0;
		m_init = false;

		FEMaterialPoint::Init();
	}

	void Update(const FETimeInfo& timeInfo) override
	{
		m_gamma_prev = m_gamma;
		m_bt_prev = m_bt;
		m_psi_f0_prev = m_psi_f0;
	}

public:
	bool	m_init;	// initialization flag
	double	m_D;		// accumulated damage

	double	m_psi_f0_ini, m_psf_c;
	double	m_psi_f0, m_psi_f0_prev;

	double	m_bt_ini;
	double	m_bt, m_bt_prev;

	double	m_gamma, m_gamma_prev;
};

//=========================================================================================

BEGIN_FECORE_CLASS(FEDamageElasticFiber, FEElasticFiberMaterial)
	ADD_PARAMETER(m_tinit, FE_RANGE_GREATER_OR_EQUAL(0.0), "t0");
	ADD_PARAMETER(m_Dmax, FE_RANGE_CLOSED(0.0, 1.0), "Dmax");
	ADD_PARAMETER(m_beta_s, FE_RANGE_GREATER(0.0), "beta_s");
	ADD_PARAMETER(m_gamma_max, FE_RANGE_GREATER(0.0), "gamma_max");
END_FECORE_CLASS();

FEDamageElasticFiber::FEDamageElasticFiber(FEModel* fem) : FEElasticFiberMaterial(fem)
{
	m_tinit = 1e9;	// large value so, no damage accumulation by default
	m_Dmax = 1.0;

	m_beta_s = 0.0;
	m_gamma_max = 0.0;

	// Looks like these are hard-coded
	m_r_s = 0.99;
	m_r_inf = 0.99;
}

double FEDamageElasticFiber::Damage(FEMaterialPoint& mp)
{
	FEFiberDamagePoint& damagePoint = *mp.ExtractData<FEFiberDamagePoint>();
	return damagePoint.m_D;
}

//! Strain energy density
double FEDamageElasticFiber::FiberStrainEnergyDensity(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	FEFiberDamagePoint& damagePoint = *mp.ExtractData<FEFiberDamagePoint>();

	double D = damagePoint.m_D;
	double psi0 = Psi0(mp, a0);
	double P = (1.0 - D)*psi0 - damagePoint.m_psf_c;
	double psi = m(P);

	return psi;
}

// calculate stress in fiber direction a0
mat3ds FEDamageElasticFiber::FiberStress(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	FEFiberDamagePoint& damagePoint = *mp.ExtractData<FEFiberDamagePoint>();

	mat3d F = pt.m_F;

	// get internal variables
	double D = damagePoint.m_D;
	double bt_prev = damagePoint.m_bt_prev;
	double psi_f0_prev = damagePoint.m_psi_f0_prev;
	double gamma_prev = damagePoint.m_gamma_prev;

	// get current simulation time.
	double t = GetFEModel()->GetTime().currentTime;

	// (i) compute trans-iso strain energy
	double psi_f0 = Psi0(mp, a0);

	// (ii) check initial damage state
	double eps = 1e-9; // NOTE: should be machine eps
	if (t >= (m_tinit - eps))
	{
		// (b) compute bt
		double bt = bt_prev + MB(psi_f0 - psi_f0_prev);

		// init damage 
		if (damagePoint.m_init == false)
		{
			damagePoint.m_psi_f0_ini = psi_f0;
			damagePoint.m_bt_ini = bt;
			damagePoint.m_init = true;
		}

		// (iii) calculate max damage saturation value 
		// trial criterion
		double phi_trial = MB(psi_f0 - damagePoint.m_psi_f0_ini) - gamma_prev;

		// check algorithmic saturation criterion
		double gamma = 0;
		if (phi_trial > eps) gamma = MB(psi_f0 - damagePoint.m_psi_f0_ini);
		else gamma = gamma_prev;
		assert(gamma >= gamma_prev);

		// compute damage saturation value
		double Ds = m_Dmax * (1.0 - exp(log(1.0 - m_r_inf)*gamma / m_gamma_max));

		// (iv) compute internal variable
		double beta = MB(bt - damagePoint.m_bt_ini);

		// (v) evaluate damage function
		D = Ds * (1.0 - exp(log(1.0 - m_r_s)*beta / m_beta_s));

		// update internal variables
		damagePoint.m_bt = bt;
		damagePoint.m_psi_f0 = psi_f0;
		damagePoint.m_gamma = gamma;
		damagePoint.m_D = D;
	}

	double P = (1.0 - D)*(psi_f0) - damagePoint.m_psf_c;
	if (P < 0.0) P = 0.0;
	double dm = dm_dP(P);

	mat3ds S0 = dPsi0_dC(mp, a0)*2.0;
	mat3ds s0 = pt.push_forward(S0);
	mat3ds s = s0*(dm*(1.0 - D));

	return s;
}

// Spatial tangent
tens4ds FEDamageElasticFiber::FiberTangent(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	FEFiberDamagePoint& damagePoint = *mp.ExtractData<FEFiberDamagePoint>();

	mat3d F = pt.m_F;
	double J = pt.m_J;

	double D = damagePoint.m_D;

	double psi0 = Psi0(mp, a0);
	double P = (1.0 - D)*(psi0)-damagePoint.m_psf_c;
	if (P < 0) P = 0.0;
	double dm = dm_dP(P);
	double d2m = d2m_dP(P);

	mat3ds S0 = dPsi0_dC(mp, a0)*2.0;
	mat3ds s0 = pt.push_forward(S0);
	tens4ds sxs = dyad1s(s0);

	// elastic stiffness 
	tens4ds C0 = d2Psi0_dC(mp, a0)*2.0;
	tens4ds c0 = pt.push_forward(C0);
	tens4ds ceD = sxs * (J*d2m*(1.0 - D)) + c0 * ((1.0 - D)*dm);

	tens4ds c = ceD;

	// damage stiffness
	double bt = damagePoint.m_bt;
	double gamma = damagePoint.m_gamma;
	double Ds = m_Dmax * (1.0 - exp(log(1.0 - m_r_inf)*gamma / m_gamma_max));
	double beta = MB(bt - damagePoint.m_bt_ini);

	double psi0_prev = damagePoint.m_psi_f0_prev;
	double psi0_ini = damagePoint.m_psi_f0_ini;
	double gamma_prev = damagePoint.m_gamma_prev;

	double dD_dbeta = -Ds * (log(1 - m_r_s) / m_beta_s)*exp(log(1 - m_r_s)*beta / m_beta_s);
	double dDs_dgamma = -m_Dmax * (log(1 - m_r_inf) / m_gamma_max)*exp(log(1 - m_r_inf)*gamma / m_gamma_max);
	double dD_dDs = 1.0 - exp(log(1 - m_r_s)*beta / m_beta_s);
	double dbeta_dpsi0 = 0.25*(SIGN(bt - damagePoint.m_bt_ini) + 1)*(SIGN(psi0 - psi0_prev) + 1);
	double dgamma_dpsi0 = 0.5*(SIGN(psi0 - psi0_ini) + 1);

	double meps = 1e-9; // NOTE: should be machine eps
	if (psi0 - psi0_prev > meps)
	{
		tens4ds Cd = sxs * ((dm + d2m * (1 - D)*psi0)*dD_dbeta*dbeta_dpsi0);
		c -= Cd;
	}

	double phi_trial = MB(psi0 - damagePoint.m_psi_f0_ini) - gamma_prev;
	if (phi_trial > meps)
	{
		tens4ds Cd = sxs * ((dm + d2m * (1 - D)*psi0)*dD_dDs*dDs_dgamma*dgamma_dpsi0);
		c -= Cd;
	}

	return c;
}

double FEDamageElasticFiber::Psi0(FEMaterialPoint& mp, const vec3d& a0) { return 0.0; }
mat3ds FEDamageElasticFiber::dPsi0_dC(FEMaterialPoint& mp, const vec3d& a0) { return mat3ds(0.0); }
tens4ds FEDamageElasticFiber::d2Psi0_dC(FEMaterialPoint& mp, const vec3d& a0) { return tens4ds(0.0); }

double FEDamageElasticFiber::m(double P) { return 0.0; }
double FEDamageElasticFiber::dm_dP(double P) { return 0.0; }
double FEDamageElasticFiber::d2m_dP(double P) { return 0.0; }

//=================================================================================================
BEGIN_FECORE_CLASS(FEDamageFiberPower, FEDamageElasticFiber)
	ADD_PARAMETER(m_a1, FE_RANGE_GREATER_OR_EQUAL(0.0), "a1");
	ADD_PARAMETER(m_a2, FE_RANGE_GREATER(1.0), "a2");
	ADD_PARAMETER(m_kappa, FE_RANGE_CLOSED(0.0, 2.0/3.0), "kappa");
END_FECORE_CLASS();

FEDamageFiberPower::FEDamageFiberPower(FEModel* fem) : FEDamageElasticFiber(fem)
{
	m_a1 = 0.0;
	m_a2 = 0.0;
	m_kappa = 0.0;
}

FEMaterialPoint* FEDamageFiberPower::CreateMaterialPointData()
{
	FEFiberDamagePoint* mp = new FEFiberDamagePoint(new FEElasticMaterialPoint);
	mp->m_psf_c = 2.0;
	return mp;
}

double FEDamageFiberPower::Psi0(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	mat3ds C = pt.RightCauchyGreen();
	mat3ds C2 = C.sqr();
	double I1 = C.tr();

	double I4 = a0 * (C*a0);
	double I5 = a0 * (C2*a0);
	double K3 = I1 * I4 - I5;

	double Psi0 = m_kappa * I1 + (1.0 - 3.0*m_kappa / 2.0)*K3;

	return Psi0;
}

mat3ds FEDamageFiberPower::dPsi0_dC(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	mat3d F = pt.m_F;
	mat3ds C = pt.RightCauchyGreen();
	mat3ds C2 = C.sqr();
	mat3ds b = pt.LeftCauchyGreen();
	double I1 = C.tr();

	double I4 = a0 * (C *a0);
	double I5 = a0 * (C2*a0);
	double K3 = I1 * I4 - I5;

	double k = 1.0 - 3.0*m_kappa / 2.0;

	mat3dd I(1.0);
	mat3ds M = dyad(a0);
	mat3ds ACA = dyads(a0, C*a0);
	mat3ds T = I * I4 + M * I1 - ACA;
	mat3ds S0 = I*m_kappa + T * k;

	return S0;
}

tens4ds FEDamageFiberPower::d2Psi0_dC(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	FEFiberDamagePoint& damagePoint = *mp.ExtractData<FEFiberDamagePoint>();

	double k = 1.0 - 3.0*m_kappa / 2.0;

	mat3ds M = dyad(a0);
	mat3dd I(1.0);
	tens4ds AIA = dyad4s(a0, I, a0)*(2.0);
	tens4ds IoM = dyad1s(I, M);

	tens4ds c0 = (IoM - AIA)*k;

	return c0;
}

double FEDamageFiberPower::m(double P)
{
	return m_a1 * pow(P, m_a2);
}

double FEDamageFiberPower::dm_dP(double P)
{
	return m_a1 * m_a2*pow(P, m_a2 - 1.0);
}

double FEDamageFiberPower::d2m_dP(double P)
{
	return m_a1 * m_a2*(m_a2 - 1.0)*pow(P, m_a2 - 2.0);
}

//=================================================================================================
BEGIN_FECORE_CLASS(FEDamageFiberExponential, FEDamageElasticFiber)
	ADD_PARAMETER(m_k1, FE_RANGE_GREATER_OR_EQUAL(0.0), "k1");
	ADD_PARAMETER(m_k2, FE_RANGE_GREATER(1.0), "k2");
	ADD_PARAMETER(m_kappa, FE_RANGE_CLOSED(0.0, 1.0/3.0), "kappa");
END_FECORE_CLASS();

FEDamageFiberExponential::FEDamageFiberExponential(FEModel* fem) : FEDamageElasticFiber(fem)
{
	m_k1 = 0.0;
	m_k2 = 0.0;
	m_kappa = 0.0;
}

FEMaterialPoint* FEDamageFiberExponential::CreateMaterialPointData()
{
	FEFiberDamagePoint* mp = new FEFiberDamagePoint(new FEElasticMaterialPoint);
	mp->m_psf_c = 1.0;
	return mp;
}

//! Strain energy density
double FEDamageFiberExponential::Psi0(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	mat3ds C = pt.RightCauchyGreen();
	double I1 = C.tr();
	double I4 = a0 * (C*a0);

	double psi0 = m_kappa * I1 + (1.0 - 3.0*m_kappa)*I4;

	return psi0;
}

// calculate stress in fiber direction a0
mat3ds FEDamageFiberExponential::dPsi0_dC(FEMaterialPoint& mp, const vec3d& a0)
{
	double k = 1.0 - 3.0*m_kappa;

	mat3dd I(1.0);
	mat3ds M = dyad(a0);
	mat3ds S0 = (I*m_kappa + M*k);

	return S0;
}

// Spatial tangent
tens4ds FEDamageFiberExponential::d2Psi0_dC(FEMaterialPoint& mp, const vec3d& a0)
{
	tens4ds c0(0.0);
	return c0;
}

double FEDamageFiberExponential::m(double P)
{
	if (P <= 0) return 0;
	return (0.5*m_k1 / m_k2)*(exp(m_k2*P*P) - 1);
}

double FEDamageFiberExponential::dm_dP(double P)
{
	if (P <= 0) return 0;
	return (m_k1 * P)*exp(m_k2*P*P);
}

double FEDamageFiberExponential::d2m_dP(double P)
{
	if (P <= 0) return 0;
	return m_k1*(1.0 + (2.0 * m_k2 * P * P))*exp(m_k2*P*P);
}

//=================================================================================================
BEGIN_FECORE_CLASS(FEDamageFiberExpLinear, FEDamageElasticFiber)
	ADD_PARAMETER(m_c3, FE_RANGE_GREATER_OR_EQUAL(0.0), "c3");
	ADD_PARAMETER(m_c4, FE_RANGE_GREATER_OR_EQUAL(0.0), "c4");
	ADD_PARAMETER(m_c5, FE_RANGE_GREATER_OR_EQUAL(0.0), "c5");
	ADD_PARAMETER(m_lamax, FE_RANGE_GREATER(0.0), "lambda");
END_FECORE_CLASS();

FEDamageFiberExpLinear::FEDamageFiberExpLinear(FEModel* fem) : FEDamageElasticFiber(fem)
{
	m_c3 = 0.0;
	m_c4 = 0.0;
	m_c5 = 0.0;
	m_lamax = 0.0;
}

FEMaterialPoint* FEDamageFiberExpLinear::CreateMaterialPointData()
{
	FEFiberDamagePoint* mp = new FEFiberDamagePoint(new FEElasticMaterialPoint);
	mp->m_psf_c = 1.0;
	return mp;
}

double FEDamageFiberExpLinear::Psi0(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	mat3ds C = pt.RightCauchyGreen();
	double I4 = a0 * (C*a0);

	double Psi0 = sqrt(I4);
	return Psi0;
}

mat3ds FEDamageFiberExpLinear::dPsi0_dC(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	mat3ds C = pt.RightCauchyGreen();
	double I4 = a0 * (C*a0);
	double l = sqrt(I4);

	mat3ds M = dyad(a0);
	mat3ds S0 = M * (0.5 / l);
	return S0;
}

tens4ds FEDamageFiberExpLinear::d2Psi0_dC(FEMaterialPoint& mp, const vec3d& a0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	mat3ds C = pt.RightCauchyGreen();
	double I4 = a0 * (C*a0);
	double l = sqrt(I4);

	mat3ds M = dyad(a0);
	tens4ds c0 = dyad1s(M)*( - 1.0 / (4.0*l*l*l));

	return c0;
}

#ifdef HAVE_GSL
inline double Ei(double x) { return gsl_sf_expint_Ei(x); }
#endif

double FEDamageFiberExpLinear::m(double P)
{
	double m = 0.0;

#ifdef HAVE_GSL
	double Pmax = m_lamax - 1.0;
	if (P <= Pmax)
	{
		m = m_c3 * (exp(-m_c4)*(Ei(m_c4*(P+1.0)) - Ei(m_c4)) - log(P + 1));
	}
	else
	{
		double c6 = m_c3 * (exp(m_c4*Pmax) - 1) - (Pmax + 1.0) * m_c5;
		m = m_c5 * P + c6 * log(P + 1.0);
	}
#endif

	return m;
}

double FEDamageFiberExpLinear::dm_dP(double P)
{
	double dm = 0.0;
	double Pmax = m_lamax - 1.0;
	if (P <= Pmax)
	{
		dm = m_c3 * (exp(m_c4*P) - 1) / (P+1.0);
	}
	else
	{
		double c6 = m_c3 * (exp(m_c4*Pmax) - 1) - (Pmax + 1.0) * m_c5;
		dm = m_c5 + c6 / (P + 1.0);
	}

	return dm;
}

double FEDamageFiberExpLinear::d2m_dP(double P)
{

	double d2m = 0.0;
	double Pmax = m_lamax - 1.0;
	if (P <= Pmax)
	{
		double expP = exp(m_c4*P);
		d2m = m_c3 * m_c4*expP / (P+1) - m_c3*(expP - 1) / ((P+1)*(P+1));
	}
	else
	{
		double c6 = m_c3 * (exp(m_c4*Pmax) - 1) - (Pmax + 1.0) * m_c5;
		d2m = -c6 / ((P+1)*(P+1));
	}

	return d2m;
}
