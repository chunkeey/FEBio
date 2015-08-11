#include "FEBiphasicSoluteDomain.h"
#include "FECore/FEMaterial.h"
#include "FECore/FEModel.h"
#include "FECore/log.h"
#include "FECore/DOFS.h"

//-----------------------------------------------------------------------------
FEBiphasicSoluteDomain::FEBiphasicSoluteDomain(FEMesh* pm, FEMaterial* pmat) : FESolidDomain(FE_BIPHASIC_SOLUTE_DOMAIN, pm)
{
	m_pMat = dynamic_cast<FEBiphasicSolute*>(pmat);
	assert(m_pMat);
}

//-----------------------------------------------------------------------------
bool FEBiphasicSoluteDomain::Initialize(FEModel &fem)
{
	// initialize base class
	FESolidDomain::Initialize(fem);

	// initialize local coordinate systems (can I do this elsewhere?)
	FEElasticMaterial* pme = m_pMat->GetElasticMaterial();
	for (size_t i=0; i<m_Elem.size(); ++i)
	{
		FESolidElement& el = m_Elem[i];
		for (int n=0; n<el.GaussPoints(); ++n) pme->SetLocalCoordinateSystem(el, n, *(el.GetMaterialPoint(n)));
	}
	
	return true;
}


//-----------------------------------------------------------------------------
//! Unpack the element LM data. 
void FEBiphasicSoluteDomain::UnpackLM(FEElement& el, vector<int>& lm)
{
    // get nodal DOFS
    DOFS& fedofs = *DOFS::GetInstance();
    int MAX_NDOFS = fedofs.GetNDOFS();
    int MAX_CDOFS = fedofs.GetCDOFS();
    
	int N = el.Nodes();
	lm.resize(N*MAX_NDOFS);
	
	for (int i=0; i<N; ++i)
	{
		int n = el.m_node[i];
		FENode& node = m_pMesh->Node(n);

		vector<int>& id = node.m_ID;

		// first the displacement dofs
		lm[3*i  ] = id[0];
		lm[3*i+1] = id[1];
		lm[3*i+2] = id[2];

		// now the pressure dofs
		lm[3*N+i] = id[6];

		// rigid rotational dofs
		lm[4*N + 3*i  ] = id[7];
		lm[4*N + 3*i+1] = id[8];
		lm[4*N + 3*i+2] = id[9];

		// fill the rest with -1
		lm[7*N + 3*i  ] = -1;
		lm[7*N + 3*i+1] = -1;
		lm[7*N + 3*i+2] = -1;

		lm[10*N + i] = id[10];
		
		// concentration dofs
		for (int k=0; k<MAX_CDOFS; ++k)
			lm[(11+k)*N + i] = id[11+k];
	}
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::Reset()
{
	// reset base class
	FESolidDomain::Reset();
	
	const int nsol = 1;
	const int nsbm = 1;
    
	for (int i=0; i<(int) m_Elem.size(); ++i)
	{
		// get the solid element
		FESolidElement& el = m_Elem[i];
		
		// get the number of integration points
		int nint = el.GaussPoints();
		
		// loop over the integration points
		for (int n=0; n<nint; ++n)
		{
			FEMaterialPoint& mp = *el.GetMaterialPoint(n);
			FEBiphasicMaterialPoint& pt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
			FESolutesMaterialPoint&  ps = *(mp.ExtractData<FESolutesMaterialPoint >());
			
			// initialize referential solid volume fraction
			pt.m_phi0 = m_pMat->m_phi0;
            
			// initialize multiphasic solutes
			ps.m_nsol = nsol;
			ps.m_c.assign(nsol,0);
			ps.m_ca.assign(nsol,0);
			ps.m_gradc.assign(nsol,vec3d(0,0,0));
			ps.m_k.assign(nsol, 0);
			ps.m_dkdJ.assign(nsol, 0);
			ps.m_dkdc.resize(nsol, vector<double>(nsol,0));
			ps.m_j.assign(nsol,vec3d(0,0,0));
			ps.m_nsbm = nsbm;
			ps.m_sbmr.assign(nsbm,0);
			ps.m_sbmrp.assign(nsbm,0);
			ps.m_sbmrhat.assign(nsbm,0);
		}
	}
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::InitElements()
{
	FESolidDomain::InitElements();

	const int NE = FEElement::MAX_NODES;
	vec3d x0[NE], xt[NE], r0, rt;
	FEMesh& m = *GetMesh();
	for (size_t i=0; i<m_Elem.size(); ++i)
	{
		FESolidElement& el = m_Elem[i];
		int neln = el.Nodes();
		for (int i=0; i<neln; ++i)
		{
			x0[i] = m.Node(el.m_node[i]).m_r0;
			xt[i] = m.Node(el.m_node[i]).m_rt;
		}

		int n = el.GaussPoints();
		for (int j=0; j<n; ++j) 
		{
			r0 = el.Evaluate(x0, j);
			rt = el.Evaluate(xt, j);

			FEMaterialPoint& mp = *el.GetMaterialPoint(j);
			FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
			pt.m_r0 = r0;
			pt.m_rt = rt;

			pt.m_J = defgrad(el, pt.m_F, j);

			mp.Init(false);
		}
	}
    
	// store previous mesh state
	// we need it for receptor-ligand complex calculations
	for (int i=0; i<(int) m_Elem.size(); ++i)
	{
		// get the solid element
		FESolidElement& el = m_Elem[i];
		
		// get the number of integration points
		int nint = el.GaussPoints();
		
		// loop over the integration points
		for (int n=0; n<nint; ++n)
		{
			FEMaterialPoint& mp = *el.GetMaterialPoint(n);
			FEBiphasicMaterialPoint& pt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
			FESolutesMaterialPoint&  ps = *(mp.ExtractData<FESolutesMaterialPoint >());
			
			// reset referential solid volume fraction at previous time
			pt.m_phi0p = pt.m_phi0;
            
			// reset referential receptor-ligand complex concentration at previous time
			ps.m_sbmrp[0] = ps.m_sbmr[0];
		}
	}
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::InternalForces(FEGlobalVector& R)
{
	int NE = m_Elem.size();
	#pragma omp parallel for shared (NE)
	for (int i=0; i<NE; ++i)
	{
		// element force vector
		vector<double> fe;
		vector<int> lm;
		
		// get the element
		FESolidElement& el = m_Elem[i];

		// get the element force vector and initialize it to zero
		int ndof = 3*el.Nodes();
		fe.assign(ndof, 0);

		// calculate internal force vector
		ElementInternalForce(el, fe);

		// get the element's LM vector
		UnpackLM(el, lm);

		// assemble element 'fe'-vector into global R vector
		//#pragma omp critical
		R.Assemble(el.m_node, lm, fe);
	}
}

//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces for solid elements

void FEBiphasicSoluteDomain::ElementInternalForce(FESolidElement& el, vector<double>& fe)
{
	int i, n;

	// jacobian matrix, inverse jacobian matrix and determinants
	double Ji[3][3], detJt;

	double Gx, Gy, Gz;
	mat3ds s;

	const double* Gr, *Gs, *Gt;

	int nint = el.GaussPoints();
	int neln = el.Nodes();

	double*	gw = el.GaussWeights();

	// repeat for all integration points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.GetMaterialPoint(n);
		FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());

		// calculate the jacobian
		detJt = invjact(el, Ji, n);

		detJt *= gw[n];

		// get the stress vector for this integration point
		s = pt.m_s;

		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);

		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];

			// calculate internal force
			// the '-' sign is so that the internal forces get subtracted
			// from the global residual vector
			fe[3*i  ] -= ( Gx*s.xx() +
				           Gy*s.xy() +
					       Gz*s.xz() )*detJt;

			fe[3*i+1] -= ( Gy*s.yy() +
				           Gx*s.xy() +
					       Gz*s.yz() )*detJt;

			fe[3*i+2] -= ( Gz*s.zz() +
				           Gy*s.yz() +
					       Gx*s.xz() )*detJt;
		}
	}
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::InternalFluidWork(vector<double>& R, double dt)
{
	const int NE = (int)m_Elem.size();

	#pragma omp parallel for
	for (int i=0; i<NE; ++i)
	{
		// get the element
		FESolidElement& el = m_Elem[i];
		int neln = el.Nodes();
			
		// calculate fluid internal work
		vector<double> fe(neln);
		ElementInternalFluidWork(el, fe, dt);

		// unpack the element
		vector<int> elm;
		UnpackLM(el, elm);

		// add fluid work to global residual
		#pragma omp critical
		{
			int neln = el.Nodes();
			for (int j=0; j<neln; ++j)
			{
				int J = elm[3*neln+j];
				if (J >= 0) R[J] += fe[j];
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::InternalFluidWorkSS(vector<double>& R, double dt)
{
	const int NE = (int)m_Elem.size();

	#pragma omp parallel for
	for (int i=0; i<NE; ++i)
	{
		// get the element
		FESolidElement& el = m_Elem[i];
		int neln = el.Nodes();
			
		// calculate fluid internal work
		vector<double> fe(neln);
		ElementInternalFluidWorkSS(el, fe, dt);

		// unpack the element
		vector<int> elm;
		UnpackLM(el, elm);

		// add fluid work to global residual
		#pragma omp critical
		{
			int neln = el.Nodes();
			for (int j=0; j<neln; ++j)
			{
				int J = elm[3*neln+j];
				if (J >= 0) R[J] += fe[j];
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::InternalSoluteWork(vector<double>& R, double dt)
{
	const int NE = (int)m_Elem.size();

	#pragma omp parallel for
	for (int i=0; i<NE; ++i)
	{
		// get the element
		FESolidElement& el = m_Elem[i];
		int neln = el.Nodes();
			
		// calculate fluid internal work
		vector<double> fe(neln);
		ElementInternalSoluteWork(el, fe, dt);

		// unpack the element
		vector<int> elm;
		UnpackLM(el, elm);

		// add fluid work to global residual
		#pragma omp critical
		{
			int neln = el.Nodes();
			int dofc = DOF_C + m_pMat->GetSolute()->GetSoluteID();
			for (int j=0; j<neln; ++j)
			{
				int J = elm[dofc*neln+j];
				if (J >= 0) R[J] += fe[j];
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::InternalSoluteWorkSS(vector<double>& R, double dt)
{
	const int NE = (int)m_Elem.size();
	#pragma omp parallel for
	for (int i=0; i<NE; ++i)
	{
		// get the element
		FESolidElement& el = m_Elem[i];
		int neln = el.Nodes();
			
		// calculate fluid internal work
		vector<double> fe(neln);
		ElementInternalSoluteWorkSS(el, fe, dt);

		// unpack the element
		vector<int> elm;
		UnpackLM(el, elm);

		// add fluid work to global residual
		#pragma omp critical
		{
			int neln = el.Nodes();
			int dofc = DOF_C + m_pMat->GetSolute()->GetSoluteID();
			for (int j=0; j<neln; ++j)
			{
				int J = elm[dofc*neln+j];
				if (J >= 0) R[J] += fe[j];
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces due to the fluid work
//! Note that we only use the first n entries in fe, where n is the number
//! of nodes

bool FEBiphasicSoluteDomain::ElementInternalFluidWork(FESolidElement& el, vector<double>& fe, double dt)
{
	int i, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	// jacobian
	double Ji[3][3], detJ, J0i[3][3];
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz, GX, GY, GZ;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	
	// gauss-weights
	double* wg = el.GaussWeights();
	
	FEMesh& mesh = *GetMesh();
	
	vec3d rp[FEElement::MAX_NODES];
	for (i=0; i<neln; ++i) 
	{
		rp[i] = mesh.Node(el.m_node[i]).m_rp;
	}
	
	zero(fe);
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.GetMaterialPoint(n);
		FEElasticMaterialPoint&  ept = *(mp.ExtractData<FEElasticMaterialPoint >());
		FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);
		
		// we need to calculate the divergence of v. To do this we use
		// the formula div(v) = 1/J*dJdt, where J = det(F)
		invjac0(el, J0i, n);
		
		// next we calculate the deformation gradient
		mat3d Fp;
		Fp.zero();
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			GX = J0i[0][0]*Gr[i]+J0i[1][0]*Gs[i]+J0i[2][0]*Gt[i];
			GY = J0i[0][1]*Gr[i]+J0i[1][1]*Gs[i]+J0i[2][1]*Gt[i];
			GZ = J0i[0][2]*Gr[i]+J0i[1][2]*Gs[i]+J0i[2][2]*Gt[i];
			
			Fp[0][0] += rp[i].x*GX; Fp[1][0] += rp[i].y*GX; Fp[2][0] += rp[i].z*GX;
			Fp[0][1] += rp[i].x*GY; Fp[1][1] += rp[i].y*GY; Fp[2][1] += rp[i].z*GY;
			Fp[0][2] += rp[i].x*GZ; Fp[1][2] += rp[i].y*GZ; Fp[2][2] += rp[i].z*GZ;
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
		}
		
		// next we get the determinant
		double Jp = Fp.det();
		double J = ept.m_J;
		
		// and then finally
		double divv = ((J-Jp)/dt)/J;
		
		// get the flux
		vec3d& w = ppt.m_w;
		
		// update force vector
		for (i=0; i<neln; ++i)
		{
			fe[i] -= dt*(B1[i]*w.x+B2[i]*w.y+B3[i]*w.z - divv*H[i])*detJ*wg[n];
		}
	}
	
	return true;
}


//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces due to the fluid work
//! for a steady-state analysis (zero solid velocity)
//! Note that we only use the first n entries in fe, where n is the number
//! of nodes

bool FEBiphasicSoluteDomain::ElementInternalFluidWorkSS(FESolidElement& el, vector<double>& fe, double dt)
{
	int i, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	// jacobian
	double Ji[3][3], detJ;
	
	double *Gr, *Gs, *Gt;
	double Gx, Gy, Gz;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	
	// gauss-weights
	double* wg = el.GaussWeights();
	
	zero(fe);
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEBiphasicMaterialPoint& ppt = *(el.GetMaterialPoint(n)->ExtractData<FEBiphasicMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);
				
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
		}
		
		// get the flux
		vec3d& w = ppt.m_w;
		
		// update force vector
		for (i=0; i<neln; ++i)
		{
			fe[i] -= dt*(B1[i]*w.x+B2[i]*w.y+B3[i]*w.z)*detJ*wg[n];
		}
	}
	
	return true;
}


//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces due to the fluid work
//! Note that we only use the first n entries in fe, where n is the number
//! of nodes

bool FEBiphasicSoluteDomain::ElementInternalSoluteWork(FESolidElement& el, vector<double>& fe, double dt)
{
	int i, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	// jacobian
	double Ji[3][3], detJ, J0i[3][3];
	
	double *Gr, *Gs, *Gt, *H;
	double *Grr, *Gsr, *Gtr, *Grs, *Gss, *Gts, *Grt, *Gst, *Gtt;
	double Gx, Gy, Gz, GX, GY, GZ;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	
	// gauss-weights
	double* wg = el.GaussWeights();
	
	FEMesh& mesh = *GetMesh();

	int id0 = m_pMat->GetSolute()->GetSoluteID();
	
	const int NE = FEElement::MAX_NODES;
	vec3d r0[NE], rt[NE], rp[NE], vt[NE];
	double cp[NE];
	for (i=0; i<neln; ++i) 
	{
		r0[i] = mesh.Node(el.m_node[i]).m_r0;
		rt[i] = mesh.Node(el.m_node[i]).m_rt;
		rp[i] = mesh.Node(el.m_node[i]).m_rp;
		cp[i] = mesh.Node(el.m_node[i]).m_cp[id0];
		vt[i] = mesh.Node(el.m_node[i]).m_vt;
	}
	
	zero(fe);
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.GetMaterialPoint(n);
		FEElasticMaterialPoint& ept = *(mp.ExtractData<FEElasticMaterialPoint>());
		FESolutesMaterialPoint& spt = *(mp.ExtractData<FESolutesMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);

		vec3d g1(Ji[0][0],Ji[0][1],Ji[0][2]);
		vec3d g2(Ji[1][0],Ji[1][1],Ji[1][2]);
		vec3d g3(Ji[2][0],Ji[2][1],Ji[2][2]);
		
		// we need to calculate the divergence of v. To do this we use
		// the formula div(v) = 1/J*dJdt, where J = det(F)
		invjac0(el, J0i, n);
		vec3d G1(J0i[0][0],J0i[0][1],J0i[0][2]);
		vec3d G2(J0i[1][0],J0i[1][1],J0i[1][2]);
		vec3d G3(J0i[2][0],J0i[2][1],J0i[2][2]);
		
		// next we calculate the deformation gradient and the solid velocity
		mat3d Fp;
		Fp.zero();
		vec3d vs(0,0,0);
		vec3d gradJ(0,0,0);
		double cprev = 0;
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		Grr = el.Grr(n); Grs = el.Grs(n); Grt = el.Grt(n);
		Gsr = el.Gsr(n); Gss = el.Gss(n); Gst = el.Gst(n);
		Gtr = el.Gtr(n); Gts = el.Gts(n); Gtt = el.Gtt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			GX = J0i[0][0]*Gr[i]+J0i[1][0]*Gs[i]+J0i[2][0]*Gt[i];
			GY = J0i[0][1]*Gr[i]+J0i[1][1]*Gs[i]+J0i[2][1]*Gt[i];
			GZ = J0i[0][2]*Gr[i]+J0i[1][2]*Gs[i]+J0i[2][2]*Gt[i];
			
			Fp[0][0] += rp[i].x*GX; Fp[1][0] += rp[i].y*GX; Fp[2][0] += rp[i].z*GX;
			Fp[0][1] += rp[i].x*GY; Fp[1][1] += rp[i].y*GY; Fp[2][1] += rp[i].z*GY;
			Fp[0][2] += rp[i].x*GZ; Fp[1][2] += rp[i].y*GZ; Fp[2][2] += rp[i].z*GZ;
			
			// calculate solid velocity
			vs += vt[i]*H[i];
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
			
			// calculate gradJ
			gradJ += (g1*Grr[i] + g2*Grs[i] + g3*Grt[i])*(rt[i]*g1-r0[i]*G1)
			+ (g1*Gsr[i] + g2*Gss[i] + g3*Gst[i])*(rt[i]*g2-r0[i]*G2)
			+ (g1*Gtr[i] + g2*Gts[i] + g3*Gtt[i])*(rt[i]*g3-r0[i]*G3);
			
			// calculate effective concentration at previous time step
			cprev += cp[i]*H[i];
		}
		
		// next we get the determinant
		double Jp = Fp.det();
		double J = ept.m_J;
		double dJdt = (J-Jp)/dt;
		gradJ *= J;
		
		// and then finally
		double divv = dJdt/J;
		
		// get the solute flux
		vec3d& j = spt.m_j[0];
		// get the effective concentration
		double c = spt.m_c[0];
		
		// evaluate the solubility and its derivatives w.r.t. J and c, and its gradient
		double kappa = m_pMat->GetSolute()->m_pSolub->Solubility(mp);
		double dkdJ = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Strain(mp);
		double dkdc = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Concentration(mp, 0);
		// evaluate the porosity, its derivative w.r.t. J, and its gradient
		double phiw = m_pMat->Porosity(mp);
		double dpdJ = (1. - phiw)/J;
		// evaluate time derivatives of concentration, solubility and porosity
		double dcdt = (c - cprev)/dt;
		double dkdt = dkdJ*dJdt + dkdc*dcdt;
		double dpdt = dpdJ*dJdt;
		// Evaluate solute supply and receptor-ligand kinetics
		double crhat = 0;
		if (m_pMat->GetSolute()->m_pSupp) crhat = m_pMat->GetSolute()->m_pSupp->Supply(mp);
		
		// update force vector
		for (i=0; i<neln; ++i)
		{
			fe[i] -= dt*(B1[i]*j.x+B2[i]*j.y+B3[i]*j.z 
						 - H[i]*(dpdt*kappa*c+phiw*dkdt*c+phiw*kappa*dcdt
								 +phiw*kappa*c*divv-crhat/J)
						 )*detJ*wg[n];
		}
	}
	
	return true;
}


//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces due to the fluid work
//! for steady-state response (zero solid velocity, zero time derivative of
//! solute concentration)
//! Note that we only use the first n entries in fe, where n is the number
//! of nodes

bool FEBiphasicSoluteDomain::ElementInternalSoluteWorkSS(FESolidElement& el, vector<double>& fe, double dt)
{
	int i, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	// jacobian
	double Ji[3][3], detJ;
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	
	// gauss-weights
	double* wg = el.GaussWeights();
		
	zero(fe);
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& pt = *(el.GetMaterialPoint(n)->ExtractData<FEMaterialPoint>());
		FEElasticMaterialPoint& ept = *(pt.ExtractData<FEElasticMaterialPoint>());
		FESolutesMaterialPoint& spt = *(pt.ExtractData<FESolutesMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);
				
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
		}
		
		double J = ept.m_J;

		// get the solute flux
		vec3d& j = spt.m_j[0];
		// Evaluate solute supply and receptor-ligand kinetics
		double crhat = 0;
		if (m_pMat->GetSolute()->m_pSupp)
		{
			// evaluate the solute supply
			crhat = m_pMat->GetSolute()->m_pSupp->SupplySS(pt);
		}
		
		// update force vector
		for (i=0; i<neln; ++i)
		{
			fe[i] -= dt*(B1[i]*j.x+B2[i]*j.y+B3[i]*j.z
						 + H[i]*crhat/J)*detJ*wg[n];
		}
	}
	
	return true;
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::StiffnessMatrix(FESolver* psolver, bool bsymm, const FETimePoint& tp)
{
	// repeat over all solid elements
	const int NE = (int)m_Elem.size();

	#pragma omp parallel for
	for (int iel=0; iel<NE; ++iel)
	{
		// element stiffness matrix
		matrix ke;
		vector<int> elm;
		
		FESolidElement& el = m_Elem[iel];
		
		UnpackLM(el, elm);
		
		// allocate stiffness matrix
		int neln = el.Nodes();
		int ndof = neln*5;
		ke.resize(ndof, ndof);
		
		// calculate the element stiffness matrix
		ElementBiphasicSoluteStiffness(el, ke, bsymm, tp.dt);
		
		// TODO: the problem here is that the LM array that is returned by the UnpackLM
		// function does not give the equation numbers in the right order. For this reason we
		// have to create a new lm array and place the equation numbers in the right order.
		// What we really ought to do is fix the UnpackLM function so that it returns
		// the LM vector in the right order for solute-solid elements.
		#pragma omp critical
		{
			vector<int> lm(ndof);
			int dofc = DOF_C + m_pMat->GetSolute()->GetSoluteID();
			for (int i=0; i<neln; ++i)
			{
				lm[5*i  ] = elm[3*i];
				lm[5*i+1] = elm[3*i+1];
				lm[5*i+2] = elm[3*i+2];
				lm[5*i+3] = elm[3*neln+i];
				lm[5*i+4] = elm[dofc*neln+i];
			}
		
			// assemble element matrix in global stiffness matrix
			psolver->AssembleStiffness(el.m_node, lm, ke);
		}
	}
}


//-----------------------------------------------------------------------------

void FEBiphasicSoluteDomain::StiffnessMatrixSS(FESolver* psolver, bool bsymm, const FETimePoint& tp)
{
	// repeat over all solid elements
	const int NE = (int)m_Elem.size();

	#pragma omp parallel for
	for (int iel=0; iel<NE; ++iel)
	{
		// element stiffness matrix
		matrix ke;
		vector<int> elm;
		
		FESolidElement& el = m_Elem[iel];
		UnpackLM(el, elm);
		
		// allocate stiffness matrix
		int neln = el.Nodes();
		int ndof = neln*5;
		ke.resize(ndof, ndof);
		
		// calculate the element stiffness matrix
		ElementBiphasicSoluteStiffnessSS(el, ke, bsymm, tp.dt);
		
		// TODO: the problem here is that the LM array that is returned by the UnpackLM
		// function does not give the equation numbers in the right order. For this reason we
		// have to create a new lm array and place the equation numbers in the right order.
		// What we really ought to do is fix the UnpackLM function so that it returns
		// the LM vector in the right order for solute-solid elements.
		#pragma omp critical
		{
			vector<int> lm(ndof);
			int dofc = DOF_C + m_pMat->GetSolute()->GetSoluteID();
			for (int i=0; i<neln; ++i)
			{
				lm[5*i  ] = elm[3*i];
				lm[5*i+1] = elm[3*i+1];
				lm[5*i+2] = elm[3*i+2];
				lm[5*i+3] = elm[3*neln+i];
				lm[5*i+4] = elm[dofc*neln+i];
			}
		
			// assemble element matrix in global stiffness matrix
			psolver->AssembleStiffness(el.m_node, lm, ke);
		}
	}
}

//-----------------------------------------------------------------------------
//! calculates element stiffness matrix for element iel
//!
bool FEBiphasicSoluteDomain::ElementBiphasicSoluteStiffness(FESolidElement& el, matrix& ke, bool bsymm, double dt)
{
	int i, j, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz, GX, GY, GZ;
	
	// jacobian
	double Ji[3][3], detJ, J0i[3][3];
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	vector<vec3d> gradN(neln);
	double tmp;
	
	// gauss-weights
	double* gw = el.GaussWeights();
	
	FEMesh& mesh = *GetMesh();

	// get the element's material
	int id0 = m_pMat->GetSolute()->GetSoluteID();
	
	const int NE = FEElement::MAX_NODES;
	vec3d r0[NE], rt[NE], rp[NE], v[NE];
	double cp[NE];
	for (i=0; i<neln; ++i) 
	{
		r0[i] = mesh.Node(el.m_node[i]).m_r0;
		rt[i] = mesh.Node(el.m_node[i]).m_rt;
		rp[i] = mesh.Node(el.m_node[i]).m_rp;
		cp[i] = mesh.Node(el.m_node[i]).m_cp[id0];
		v[i]  = mesh.Node(el.m_node[i]).m_vt;
	}
	
	// zero stiffness matrix
	ke.zero();
	
	// calculate solid stiffness matrix
	int ndof = 3*el.Nodes();
	matrix ks(ndof, ndof); ks.zero();
	SolidElementStiffness(el, ks);
	
	// copy solid stiffness matrix into ke
	for (i=0; i<neln; ++i)
		for (j=0; j<neln; ++j)
		{
			ke[5*i  ][5*j] = ks[3*i  ][3*j  ]; ke[5*i  ][5*j+1] = ks[3*i  ][3*j+1]; ke[5*i  ][5*j+2] = ks[3*i  ][3*j+2];
			ke[5*i+1][5*j] = ks[3*i+1][3*j  ]; ke[5*i+1][5*j+1] = ks[3*i+1][3*j+1]; ke[5*i+1][5*j+2] = ks[3*i+1][3*j+2];
			ke[5*i+2][5*j] = ks[3*i+2][3*j  ]; ke[5*i+2][5*j+1] = ks[3*i+2][3*j+1]; ke[5*i+2][5*j+2] = ks[3*i+2][3*j+2];
		}
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.GetMaterialPoint(n);
		FEElasticMaterialPoint&  ept = *(mp.ExtractData<FEElasticMaterialPoint >());
		FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
		FESolutesMaterialPoint&  spt = *(mp.ExtractData<FESolutesMaterialPoint >());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);

		vec3d g1(Ji[0][0],Ji[0][1],Ji[0][2]);
		vec3d g2(Ji[1][0],Ji[1][1],Ji[1][2]);
		vec3d g3(Ji[2][0],Ji[2][1],Ji[2][2]);
		
		// we need to calculate the divergence of v. To do this we use
		// the formula div(v) = 1/J*dJdt, where J = det(F)
		invjac0(el, J0i, n);
		vec3d G1(J0i[0][0],J0i[0][1],J0i[0][2]);
		vec3d G2(J0i[1][0],J0i[1][1],J0i[1][2]);
		vec3d G3(J0i[2][0],J0i[2][1],J0i[2][2]);
		
		// next we calculate the deformation gradient and the solid velocity
		mat3d Fp, gradv;
		Fp.zero();
		gradv.zero();
		vec3d vs(0,0,0);
		double cprev = 0;
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			GX = J0i[0][0]*Gr[i]+J0i[1][0]*Gs[i]+J0i[2][0]*Gt[i];
			GY = J0i[0][1]*Gr[i]+J0i[1][1]*Gs[i]+J0i[2][1]*Gt[i];
			GZ = J0i[0][2]*Gr[i]+J0i[1][2]*Gs[i]+J0i[2][2]*Gt[i];
			
			Fp[0][0] += rp[i].x*GX; Fp[1][0] += rp[i].y*GX; Fp[2][0] += rp[i].z*GX;
			Fp[0][1] += rp[i].x*GY; Fp[1][1] += rp[i].y*GY; Fp[2][1] += rp[i].z*GY;
			Fp[0][2] += rp[i].x*GZ; Fp[1][2] += rp[i].y*GZ; Fp[2][2] += rp[i].z*GZ;
			
			// calculate solid velocity and its gradient
			vs += v[i]*H[i];
			gradv[0][0] += v[i].x*Gx; gradv[1][0] += v[i].y*Gx; gradv[2][0] += v[i].z*Gx;
			gradv[0][1] += v[i].x*Gy; gradv[1][1] += v[i].y*Gy; gradv[2][1] += v[i].z*Gy;
			gradv[0][2] += v[i].x*Gz; gradv[1][2] += v[i].y*Gz; gradv[2][2] += v[i].z*Gz;
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
			gradN[i] = vec3d(Gx,Gy,Gz);
			
			// calculate effective concentration at previous time step
			cprev += cp[i]*H[i];
		}
		
		// next we get the determinant
		double Jp = Fp.det();
		double J = ept.m_J;
		
		// and then finally
		double dJdt = (J-Jp)/dt;
		double divv = dJdt/J;
		
		// get the fluid flux and pressure gradient
		vec3d w = ppt.m_w;
		vec3d gradp = ppt.m_gradp;
		
		// get the effective concentration, its gradient and its time derivative
		double c = spt.m_c[0];
		vec3d gradc = spt.m_gradc[0];
		double dcdt = (c - cprev)/dt;
		
		// evaluate the permeability and its derivatives
		mat3ds K = m_pMat->GetPermeability()->Permeability(mp);
		tens4ds dKdE = m_pMat->GetPermeability()->Tangent_Permeability_Strain(mp);
		mat3ds dKdc = m_pMat->GetPermeability()->Tangent_Permeability_Concentration(mp, 0); 
		
		// evaluate the porosity and its derivative
		double phiw = m_pMat->Porosity(mp);
		double phis = 1. - phiw;
		double dpdJ = phis/J;
		double dpdJJ = -2*phis/(J*J);
		
		// evaluate the solubility and its derivatives
		double kappa = m_pMat->GetSolute()->m_pSolub->Solubility(mp);
		double dkdJ = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Strain(mp);
		double dkdJJ = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Strain_Strain(mp);
		double dkdc = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Concentration(mp,0);
		double dkdcc = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Concentration_Concentration(mp,0,0);
		double dkdJc = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Strain_Concentration(mp,0);
		double dkdt = dkdJ*dJdt + dkdc*dcdt;
		
		// evaluate the diffusivity tensor and its derivatives
		mat3ds D = m_pMat->GetSolute()->m_pDiff->Diffusivity(mp);
		mat3ds dDdc = m_pMat->GetSolute()->m_pDiff->Tangent_Diffusivity_Concentration(mp, 0);
		tens4ds dDdE = m_pMat->GetSolute()->m_pDiff->Tangent_Diffusivity_Strain(mp);
		
		// evaluate the solute free diffusivity
		double D0 = m_pMat->GetSolute()->m_pDiff->Free_Diffusivity(mp);
		double dD0dc = m_pMat->GetSolute()->m_pDiff->Tangent_Free_Diffusivity_Concentration(mp,0);
		
		// evaluate the osmotic coefficient and its derivatives
		double osmc = m_pMat->GetOsmoticCoefficient()->OsmoticCoefficient(mp);
		double dodc = m_pMat->GetOsmoticCoefficient()->Tangent_OsmoticCoefficient_Concentration(mp, 0);
		
		// evaluate the stress tangent with concentration
//		mat3ds dTdc = pm->GetSolid()->Tangent_Concentration(mp, 0);
		mat3ds dTdc(0,0,0,0,0,0);
		
		// Miscellaneous constants
		mat3dd I(1);
		double R = m_pMat->m_Rgas;
		double T = m_pMat->m_Tabs;
		
		// evaluate the effective permeability and its derivatives
		mat3ds Ki = K.inverse();
		mat3ds ImD = I-D/D0;
		mat3ds Ke = (Ki + ImD*(R*T*kappa*c/phiw/D0)).inverse();
		tens4ds G = dyad1s(Ki,I) - dyad4s(Ki,I)*2 - ddots(dyad2s(Ki),dKdE)*0.5
		+dyad1s(ImD,I)*(R*T*c*J/D0/2/phiw*(dkdJ-kappa/phiw*dpdJ))
		+(dyad1s(I) - dyad4s(I)*2 - dDdE/D0)*(R*T*kappa*c/phiw/D0);
		tens4ds dKedE = dyad1s(Ke,I) - 2*dyad4s(Ke,I) - ddots(dyad2s(Ke),G)*0.5;
		mat3ds Gc = -Ki*dKdc*Ki + ImD*(R*T/phiw/D0*(dkdc*c+kappa-kappa*c/D0*dD0dc))
		+R*T*kappa*c/phiw/D0/D0*(D*dD0dc/D0 - dDdc);
		mat3ds dKedc = -Ke*Gc*Ke;
		
		// evaluate the tangents of solute supply
		double dcrhatdJ = 0;
		double dcrhatdc = 0;
		if (m_pMat->GetSolute()->m_pSupp)
		{
			dcrhatdJ = m_pMat->GetSolute()->m_pSupp->Tangent_Supply_Strain(mp);
			double dcrhatdcr = m_pMat->GetSolute()->m_pSupp->Tangent_Supply_Concentration(mp);
			dcrhatdc = J*phiw*(kappa + c*dkdc)*dcrhatdcr;
		}
		
		// calculate all the matrices
		vec3d vtmp,gp,gc,qpu,qcu,wc,jc;
		mat3d wu,ju;
		double qcc;
		tmp = detJ*gw[n];
		for (i=0; i<neln; ++i)
		{
			for (j=0; j<neln; ++j)
			{
				// calculate the kpu matrix
				gp = gradp+(D*gradc)*R*T*kappa/D0;
				wu = vdotTdotv(-gp, dKedE, gradN[j])
				-(((Ke*(D*gradc)) & gradN[j])*(J*dkdJ - kappa)
				  +Ke*(2*kappa*(gradN[j]*(D*gradc))))*R*T/D0
				- Ke*vdotTdotv(gradc, dDdE, gradN[j])*(kappa*R*T/D0);
//				qpu = -gradN[j]*(divv+1/dt)-gradv.transpose()*gradN[j];
				qpu = -gradN[j]*(divv+1/dt)+gradv.transpose()*gradN[j];
				vtmp = (wu.transpose()*gradN[i] + qpu*H[i])*(tmp*dt);
				ke[5*i+3][5*j  ] += vtmp.x;
				ke[5*i+3][5*j+1] += vtmp.y;
				ke[5*i+3][5*j+2] += vtmp.z;
				
				// calculate the kcu matrix
				gc = -gradc*phiw + w*c/D0;
				ju = ((D*gc) & gradN[j])*(J*dkdJ) 
				+ vdotTdotv(gc, dDdE, gradN[j])*kappa
				+ (((D*gradc) & gradN[j])*(-phis)
				   +(D*((gradN[j]*w)*2) - ((D*w) & gradN[j]))*c/D0
				   )*kappa
				+D*wu*(kappa*c/D0);
				qcu = -gradN[j]*(c*dJdt*(2*(dpdJ*kappa+phiw*dkdJ+J*dpdJ*dkdJ)
										 +J*(dpdJJ*kappa+phiw*dkdJJ))
								 +dcdt*((phiw+J*dpdJ)*(kappa+dkdc*c)
										+J*phiw*(dkdJ+dkdJc*c))-dcrhatdJ)
				+qpu*(c*(phiw*kappa+J*dpdJ*kappa+J*phiw*dkdJ));
				vtmp = (ju.transpose()*gradN[i] + qcu*H[i])*(tmp*dt);
				ke[5*i+4][5*j  ] += vtmp.x;
				ke[5*i+4][5*j+1] += vtmp.y;
				ke[5*i+4][5*j+2] += vtmp.z;
				
				// calculate the kup matrix
				vtmp = -gradN[i]*H[j]*tmp;
				ke[5*i  ][5*j+3] += vtmp.x;
				ke[5*i+1][5*j+3] += vtmp.y;
				ke[5*i+2][5*j+3] += vtmp.z;
				
				// calculate the kpp matrix
				ke[5*i+3][5*j+3] -= gradN[i]*(Ke*gradN[j])*(tmp*dt);
				
				// calculate the kcp matrix
				ke[5*i+4][5*j+3] -= (gradN[i]*((D*Ke)*gradN[j]))*(kappa*c/D0)*(tmp*dt);

				// calculate the kuc matrix
				vtmp = (dTdc*gradN[i] - gradN[i]*(R*T*(dodc*kappa*c+osmc*dkdc*c+osmc*kappa)))*H[j]*tmp;
				ke[5*i  ][5*j+4] += vtmp.x;
				ke[5*i+1][5*j+4] += vtmp.y;
				ke[5*i+2][5*j+4] += vtmp.z;
				
				// calculate the kpc matrix
				wc = (dKedc*gp)*(-H[j])
				-Ke*((((D*(dkdc-kappa*dD0dc/D0)+dDdc*kappa)*gradc)*H[j]
					  +(D*gradN[j])*kappa)*(R*T/D0));
				ke[5*i+3][5*j+4] += (gradN[i]*wc)*(tmp*dt);
				
				// calculate the kcc matrix
				jc = (D*(-gradN[j]*phiw+w*(H[j]/D0)))*kappa
				+((D*dkdc+dDdc*kappa)*gc)*H[j]
				+(D*(w*(-H[j]*dD0dc/D0)+wc))*(kappa*c/D0);
				qcc = -H[j]*(((phiw+J*dpdJ)*divv+phiw/dt)*(kappa+c*dkdc)
							 +phiw*(dkdt+dkdc*dcdt)
							 +phiw*c*(dkdJc*dJdt+dkdcc*dcdt)
							 -dcrhatdc/J);
				ke[5*i+4][5*j+4] += (gradN[i]*jc + H[i]*qcc)*(tmp*dt);
				
			}
		}
	}
	
	// Enforce symmetry by averaging top-right and bottom-left corners of stiffness matrix
	if (bsymm) {
		for (i=0; i<5*neln; ++i)
			for (j=i+1; j<5*neln; ++j) {
				tmp = 0.5*(ke[i][j]+ke[j][i]);
				ke[i][j] = ke[j][i] = tmp;
			}
	}
	
	return true;
}

//-----------------------------------------------------------------------------
//! calculates element stiffness matrix for element iel
//! for steady-state response (zero solid velocity, zero time derivative of
//! solute concentration)
//!
bool FEBiphasicSoluteDomain::ElementBiphasicSoluteStiffnessSS(FESolidElement& el, matrix& ke, bool bsymm, double dt)
{
	int i, j, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz;
	
	// jacobian
	double Ji[3][3], detJ;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	vector<vec3d> gradN(neln);
	double tmp;
	
	// gauss-weights
	double* gw = el.GaussWeights();
		
	// zero stiffness matrix
	ke.zero();
	
	// calculate solid stiffness matrix
	int ndof = 3*el.Nodes();
	matrix ks(ndof, ndof); ks.zero();
	SolidElementStiffness(el, ks);
	
	// copy solid stiffness matrix into ke
	for (i=0; i<neln; ++i)
		for (j=0; j<neln; ++j)
		{
			ke[5*i  ][5*j] = ks[3*i  ][3*j  ]; ke[5*i  ][5*j+1] = ks[3*i  ][3*j+1]; ke[5*i  ][5*j+2] = ks[3*i  ][3*j+2];
			ke[5*i+1][5*j] = ks[3*i+1][3*j  ]; ke[5*i+1][5*j+1] = ks[3*i+1][3*j+1]; ke[5*i+1][5*j+2] = ks[3*i+1][3*j+2];
			ke[5*i+2][5*j] = ks[3*i+2][3*j  ]; ke[5*i+2][5*j+1] = ks[3*i+2][3*j+1]; ke[5*i+2][5*j+2] = ks[3*i+2][3*j+2];
		}
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.GetMaterialPoint(n);
		FEElasticMaterialPoint&  ept = *(mp.ExtractData<FEElasticMaterialPoint >());
		FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
		FESolutesMaterialPoint&  spt = *(mp.ExtractData<FESolutesMaterialPoint >());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
			gradN[i] = vec3d(Gx,Gy,Gz);
		}
		
		// next we get the determinant
		double J = ept.m_J;
		
		// get the fluid flux and pressure gradient
		vec3d w = ppt.m_w;
		vec3d gradp = ppt.m_gradp;
		
		// get the effective concentration, its gradient and its time derivative
		double c = spt.m_c[0];
		vec3d gradc = spt.m_gradc[0];
		
		// evaluate the permeability and its derivatives
		mat3ds K = m_pMat->GetPermeability()->Permeability(mp);
		tens4ds dKdE = m_pMat->GetPermeability()->Tangent_Permeability_Strain(mp);
		mat3ds dKdc = m_pMat->GetPermeability()->Tangent_Permeability_Concentration(mp, 0); 
		
		// evaluate the porosity and its derivative
		double phiw = m_pMat->Porosity(mp);
		double phis = 1. - phiw;
		double dpdJ = phis/J;
		
		// evaluate the solubility and its derivatives
		double kappa = m_pMat->GetSolute()->m_pSolub->Solubility(mp);
		double dkdJ = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Strain(mp);
		double dkdc = m_pMat->GetSolute()->m_pSolub->Tangent_Solubility_Concentration(mp, 0);
		
		// evaluate the diffusivity tensor and its derivatives
		mat3ds D = m_pMat->GetSolute()->m_pDiff->Diffusivity(mp);
		mat3ds dDdc = m_pMat->GetSolute()->m_pDiff->Tangent_Diffusivity_Concentration(mp, 0);
		tens4ds dDdE = m_pMat->GetSolute()->m_pDiff->Tangent_Diffusivity_Strain(mp);
		
		// evaluate the solute free diffusivity
		double D0 = m_pMat->GetSolute()->m_pDiff->Free_Diffusivity(mp);
		double dD0dc = m_pMat->GetSolute()->m_pDiff->Tangent_Free_Diffusivity_Concentration(mp,0);
		
		// evaluate the osmotic coefficient and its derivatives
		double osmc = m_pMat->GetOsmoticCoefficient()->OsmoticCoefficient(mp);
		double dodc = m_pMat->GetOsmoticCoefficient()->Tangent_OsmoticCoefficient_Concentration(mp, 0);
		
		// evaluate the stress tangent with concentration
//		mat3ds dTdc = pm->GetSolid()->Tangent_Concentration(mp, 0);
		mat3ds dTdc(0,0,0,0,0,0);
		
		// Miscellaneous constants
		mat3dd I(1);
		double R = m_pMat->m_Rgas;
		double T = m_pMat->m_Tabs;
		
		// evaluate the effective permeability and its derivatives
		mat3ds Ki = K.inverse();
		mat3ds ImD = I-D/D0;
		mat3ds Ke = (Ki + ImD*(R*T*kappa*c/phiw/D0)).inverse();
		tens4ds G = dyad1s(Ki,I) - dyad4s(Ki,I)*2 - ddots(dyad2s(Ki),dKdE)*0.5
		+dyad1s(ImD,I)*(R*T*c*J/D0/2/phiw*(dkdJ-kappa/phiw*dpdJ))
		+(dyad1s(I) - dyad4s(I)*2 - dDdE/D0)*(R*T*kappa*c/phiw/D0);
		tens4ds dKedE = dyad1s(Ke,I) - 2*dyad4s(Ke,I) - ddots(dyad2s(Ke),G)*0.5;
		mat3ds Gc = -Ki*dKdc*Ki + ImD*(R*T/phiw/D0*(dkdc*c+kappa-kappa*c/D0*dD0dc))
		+R*T*kappa*c/phiw/D0/D0*(D*dD0dc/D0 - dDdc);
		mat3ds dKedc = -Ke*Gc*Ke;
		
		// calculate all the matrices
		vec3d vtmp,gp,gc,wc,jc;
		mat3d wu,ju;
		tmp = detJ*gw[n];
		for (i=0; i<neln; ++i)
		{
			for (j=0; j<neln; ++j)
			{
				// calculate the kpu matrix
				gp = gradp+(D*gradc)*R*T*kappa/D0;
				wu = vdotTdotv(-gp, dKedE, gradN[j])
				-(((Ke*(D*gradc)) & gradN[j])*(J*dkdJ - kappa)
				  +Ke*(2*kappa*(gradN[j]*(D*gradc))))*R*T/D0
				- Ke*vdotTdotv(gradc, dDdE, gradN[j])*(kappa*R*T/D0);
				vtmp = (wu.transpose()*gradN[i])*(tmp*dt);
				ke[5*i+3][5*j  ] += vtmp.x;
				ke[5*i+3][5*j+1] += vtmp.y;
				ke[5*i+3][5*j+2] += vtmp.z;
				
				// calculate the kcu matrix
				gc = -gradc*phiw + w*c/D0;
				ju = ((D*gc) & gradN[j])*(J*dkdJ) 
				+ vdotTdotv(gc, dDdE, gradN[j])*kappa
				+ (((D*gradc) & gradN[j])*(-phis)
				   +(D*((gradN[j]*w)*2) - ((D*w) & gradN[j]))*c/D0
				   )*kappa
				+D*wu*(kappa*c/D0);
				vtmp = (ju.transpose()*gradN[i])*(tmp*dt);
				ke[5*i+4][5*j  ] += vtmp.x;
				ke[5*i+4][5*j+1] += vtmp.y;
				ke[5*i+4][5*j+2] += vtmp.z;
				
				// calculate the kup matrix
				vtmp = -gradN[i]*H[j]*tmp;
				ke[5*i  ][5*j+3] += vtmp.x;
				ke[5*i+1][5*j+3] += vtmp.y;
				ke[5*i+2][5*j+3] += vtmp.z;
				
				// calculate the kpp matrix
				ke[5*i+3][5*j+3] -= gradN[i]*(Ke*gradN[j])*(tmp*dt);
				
				// calculate the kcp matrix
				ke[5*i+4][5*j+3] -= (gradN[i]*((D*Ke)*gradN[j]))*(kappa*c/D0)*(tmp*dt);
				
				// calculate the kuc matrix
				vtmp = (dTdc*gradN[i] - gradN[i]*(R*T*(dodc*kappa*c+osmc*dkdc*c+osmc*kappa)))*H[j]*tmp;
				ke[5*i  ][5*j+4] += vtmp.x;
				ke[5*i+1][5*j+4] += vtmp.y;
				ke[5*i+2][5*j+4] += vtmp.z;
				
				// calculate the kpc matrix
				wc = (dKedc*gp)*(-H[j])
				-Ke*((((D*(dkdc-kappa*dD0dc/D0)+dDdc*(kappa/D0))*gradc)*H[j]
					  +(D*gradN[j])*kappa)*(R*T/D0));
				ke[5*i+3][5*j+4] += (gradN[i]*wc)*(tmp*dt);
				
				// calculate the kcc matrix
				jc = (D*(-gradN[j]*phiw+w*(H[j]/D0)))*kappa
				+((D*dkdc+dDdc*kappa)*gc)*H[j]
				+(D*(w*(-H[j]*dD0dc/D0)+wc))*(kappa*c/D0);
				ke[5*i+4][5*j+4] += (gradN[i]*jc)*(tmp*dt);
			}
		}
	}
	
	// Enforce symmetry by averaging top-right and bottom-left corners of stiffness matrix
	if (bsymm) {
		for (i=0; i<5*neln; ++i)
			for (j=i+1; j<5*neln; ++j) {
				tmp = 0.5*(ke[i][j]+ke[j][i]);
				ke[i][j] = ke[j][i] = tmp;
			}
	}
	
	return true;
}

//-----------------------------------------------------------------------------
//! This function calculates the element stiffness matrix. It calls the material
//! stiffness function, the geometrical stiffness function and, if necessary, the
//! dilatational stiffness function. Note that these three functions only calculate
//! the upper diagonal matrix due to the symmetry of the element stiffness matrix
//! The last section of this function fills the rest of the element stiffness matrix.

void FEBiphasicSoluteDomain::SolidElementStiffness(FESolidElement& el, matrix& ke)
{
	// calculate material stiffness (i.e. constitutive component)
	ElementBiphasicSoluteMaterialStiffness(el, ke);
	
	// calculate geometrical stiffness (inherited from FEElasticSolidDomain)
	ElementGeometricalStiffness(el, ke);
	
	// assign symmetic parts
	// TODO: Can this be omitted by changing the Assemble routine so that it only
	// grabs elements from the upper diagonal matrix?
	int ndof = 3*el.Nodes();
	int i, j;
	for (i=0; i<ndof; ++i)
		for (j=i+1; j<ndof; ++j)
			ke[j][i] = ke[i][j];
}

//-----------------------------------------------------------------------------
//! Calculates element material stiffness element matrix

void FEBiphasicSoluteDomain::ElementBiphasicSoluteMaterialStiffness(FESolidElement &el, matrix &ke)
{
	int i, i3, j, j3, n;

	// see if this is a biphasic-solute material
	int id0 = m_pMat->GetSolute()->GetSoluteID();
	
	// Get the current element's data
	const int nint = el.GaussPoints();
	const int neln = el.Nodes();
	
	// global derivatives of shape functions
	const int NE = FEElement::MAX_NODES;
	double Gx[NE], Gy[NE], Gz[NE];
	
	double Gxi, Gyi, Gzi;
	double Gxj, Gyj, Gzj;
	
	// The 'D' matrix
	double D[6][6] = {0};	// The 'D' matrix
	
	// The 'D*BL' matrix
	double DBL[6][3];
	
	double *Grn, *Gsn, *Gtn;
	double Gr, Gs, Gt;
	
	// jacobian
	double Ji[3][3], detJt;

	// nodal concentrations
	double ct[FEElement::MAX_NODES];
	for (i=0; i<neln; ++i) ct[i] = m_pMesh->Node(el.m_node[i]).m_ct[id0];

	// weights at gauss points
	const double *gw = el.GaussWeights();
	
	// calculate element stiffness matrix
	for (n=0; n<nint; ++n)
	{
		// calculate jacobian
		detJt = invjact(el, Ji, n)*gw[n];
		
		Grn = el.Gr(n);
		Gsn = el.Gs(n);
		Gtn = el.Gt(n);
		
		// setup the material point
		// NOTE: deformation gradient and determinant have already been evaluated in the stress routine
		FEMaterialPoint& mp = *el.GetMaterialPoint(n);
		
		// evaluate concentration at gauss-point
		FESolutesMaterialPoint& spt = *(mp.ExtractData<FESolutesMaterialPoint>());
		spt.m_c[0] = el.Evaluate(ct, n);
		
		// get the 'D' matrix
		tens4ds C = m_pMat->Tangent(mp);
		C.extract(D);
		
		for (i=0; i<neln; ++i)
		{
			Gr = Grn[i];
			Gs = Gsn[i];
			Gt = Gtn[i];
			
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx[i] = Ji[0][0]*Gr+Ji[1][0]*Gs+Ji[2][0]*Gt;
			Gy[i] = Ji[0][1]*Gr+Ji[1][1]*Gs+Ji[2][1]*Gt;
			Gz[i] = Ji[0][2]*Gr+Ji[1][2]*Gs+Ji[2][2]*Gt;
		}
		
		// we only calculate the upper triangular part
		// since ke is symmetric. The other part is
		// determined below using this symmetry.
		for (i=0, i3=0; i<neln; ++i, i3 += 3)
		{
			Gxi = Gx[i];
			Gyi = Gy[i];
			Gzi = Gz[i];
			
			for (j=i, j3 = i3; j<neln; ++j, j3 += 3)
			{
				Gxj = Gx[j];
				Gyj = Gy[j];
				Gzj = Gz[j];
				
				// calculate D*BL matrices
				DBL[0][0] = (D[0][0]*Gxj+D[0][3]*Gyj+D[0][5]*Gzj);
				DBL[0][1] = (D[0][1]*Gyj+D[0][3]*Gxj+D[0][4]*Gzj);
				DBL[0][2] = (D[0][2]*Gzj+D[0][4]*Gyj+D[0][5]*Gxj);
				
				DBL[1][0] = (D[1][0]*Gxj+D[1][3]*Gyj+D[1][5]*Gzj);
				DBL[1][1] = (D[1][1]*Gyj+D[1][3]*Gxj+D[1][4]*Gzj);
				DBL[1][2] = (D[1][2]*Gzj+D[1][4]*Gyj+D[1][5]*Gxj);
				
				DBL[2][0] = (D[2][0]*Gxj+D[2][3]*Gyj+D[2][5]*Gzj);
				DBL[2][1] = (D[2][1]*Gyj+D[2][3]*Gxj+D[2][4]*Gzj);
				DBL[2][2] = (D[2][2]*Gzj+D[2][4]*Gyj+D[2][5]*Gxj);
				
				DBL[3][0] = (D[3][0]*Gxj+D[3][3]*Gyj+D[3][5]*Gzj);
				DBL[3][1] = (D[3][1]*Gyj+D[3][3]*Gxj+D[3][4]*Gzj);
				DBL[3][2] = (D[3][2]*Gzj+D[3][4]*Gyj+D[3][5]*Gxj);
				
				DBL[4][0] = (D[4][0]*Gxj+D[4][3]*Gyj+D[4][5]*Gzj);
				DBL[4][1] = (D[4][1]*Gyj+D[4][3]*Gxj+D[4][4]*Gzj);
				DBL[4][2] = (D[4][2]*Gzj+D[4][4]*Gyj+D[4][5]*Gxj);
				
				DBL[5][0] = (D[5][0]*Gxj+D[5][3]*Gyj+D[5][5]*Gzj);
				DBL[5][1] = (D[5][1]*Gyj+D[5][3]*Gxj+D[5][4]*Gzj);
				DBL[5][2] = (D[5][2]*Gzj+D[5][4]*Gyj+D[5][5]*Gxj);
				
				ke[i3  ][j3  ] += (Gxi*DBL[0][0] + Gyi*DBL[3][0] + Gzi*DBL[5][0] )*detJt;
				ke[i3  ][j3+1] += (Gxi*DBL[0][1] + Gyi*DBL[3][1] + Gzi*DBL[5][1] )*detJt;
				ke[i3  ][j3+2] += (Gxi*DBL[0][2] + Gyi*DBL[3][2] + Gzi*DBL[5][2] )*detJt;
				
				ke[i3+1][j3  ] += (Gyi*DBL[1][0] + Gxi*DBL[3][0] + Gzi*DBL[4][0] )*detJt;
				ke[i3+1][j3+1] += (Gyi*DBL[1][1] + Gxi*DBL[3][1] + Gzi*DBL[4][1] )*detJt;
				ke[i3+1][j3+2] += (Gyi*DBL[1][2] + Gxi*DBL[3][2] + Gzi*DBL[4][2] )*detJt;
				
				ke[i3+2][j3  ] += (Gzi*DBL[2][0] + Gyi*DBL[4][0] + Gxi*DBL[5][0] )*detJt;
				ke[i3+2][j3+1] += (Gzi*DBL[2][1] + Gyi*DBL[4][1] + Gxi*DBL[5][1] )*detJt;
				ke[i3+2][j3+2] += (Gzi*DBL[2][2] + Gyi*DBL[4][2] + Gxi*DBL[5][2] )*detJt;
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! calculates element's geometrical stiffness component for integration point n
void FEBiphasicSoluteDomain::ElementGeometricalStiffness(FESolidElement &el, matrix &ke)
{
	int n, i, j;

	double Gx[FEElement::MAX_NODES];
	double Gy[FEElement::MAX_NODES];
	double Gz[FEElement::MAX_NODES];
	double *Grn, *Gsn, *Gtn;
	double Gr, Gs, Gt;

	// nr of nodes
	int neln = el.Nodes();

	// nr of integration points
	int nint = el.GaussPoints();

	// jacobian
	double Ji[3][3], detJt;

	// weights at gauss points
	const double *gw = el.GaussWeights();

	// stiffness component for the initial stress component of stiffness matrix
	double kab;

	// calculate geometrical element stiffness matrix
	for (n=0; n<nint; ++n)
	{
		// calculate jacobian
		detJt = invjact(el, Ji, n)*gw[n];

		Grn = el.Gr(n);
		Gsn = el.Gs(n);
		Gtn = el.Gt(n);

		for (i=0; i<neln; ++i)
		{
			Gr = Grn[i];
			Gs = Gsn[i];
			Gt = Gtn[i];

			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx[i] = Ji[0][0]*Gr+Ji[1][0]*Gs+Ji[2][0]*Gt;
			Gy[i] = Ji[0][1]*Gr+Ji[1][1]*Gs+Ji[2][1]*Gt;
			Gz[i] = Ji[0][2]*Gr+Ji[1][2]*Gs+Ji[2][2]*Gt;
		}

		// get the material point data
		FEMaterialPoint& mp = *el.GetMaterialPoint(n);
		FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());

		// element's Cauchy-stress tensor at gauss point n
		// s is the voight vector
		mat3ds& s = pt.m_s;

		for (i=0; i<neln; ++i)
			for (j=i; j<neln; ++j)
			{
				kab = (Gx[i]*(s.xx()*Gx[j]+s.xy()*Gy[j]+s.xz()*Gz[j]) +
					   Gy[i]*(s.xy()*Gx[j]+s.yy()*Gy[j]+s.yz()*Gz[j]) + 
					   Gz[i]*(s.xz()*Gx[j]+s.yz()*Gy[j]+s.zz()*Gz[j]))*detJt;

				ke[3*i  ][3*j  ] += kab;
				ke[3*i+1][3*j+1] += kab;
				ke[3*i+2][3*j+2] += kab;
			}
	}
}


//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::UpdateStresses(FEModel &fem)
{
	double dt = fem.GetCurrentStep()->m_dt;
	bool sstate = (fem.GetCurrentStep()->m_nanalysis == FE_STEADY_STATE);
	bool berr = false;

	int NE = (int) m_Elem.size();
	#pragma omp parallel for shared(NE, dt, sstate, berr)
	for (int i=0; i<NE; ++i)
	{
		try
		{
			UpdateElementStress(i, dt, sstate);
		}
		catch (NegativeJacobian e)
		{
			#pragma omp critical
			{
				berr = true;
				if (NegativeJacobian::m_boutput) e.print();
			}
		}
	}

	// if we encountered an error, we request a running restart
	if (berr)
	{
		if (NegativeJacobian::m_boutput == false) felog.printbox("ERROR", "Negative jacobian was detected.");
		throw DoRunningRestart();
	}
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::UpdateElementStress(int iel, double dt, bool sstate)
{
	// get the solid element
	FESolidElement& el = m_Elem[iel];
		
	// get the number of integration points
	int nint = el.GaussPoints();

	// get the number of nodes
	int neln = el.Nodes();
		
	// get the biphasic-solute material
	int id0 = m_pMat->GetSolute()->GetSoluteID();

	// get the nodal data
	FEMesh& mesh = *m_pMesh;
	vec3d r0[FEElement::MAX_NODES];
	vec3d rt[FEElement::MAX_NODES];
	double pn[FEElement::MAX_NODES], ct[FEElement::MAX_NODES];
	for (int j=0; j<neln; ++j)
	{
		r0[j] = mesh.Node(el.m_node[j]).m_r0;
		rt[j] = mesh.Node(el.m_node[j]).m_rt;
		pn[j] = mesh.Node(el.m_node[j]).m_pt;
		ct[j] = mesh.Node(el.m_node[j]).m_ct[id0];
	}
		
	// loop over the integration points and calculate
	// the stress at the integration point
	for (int n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.GetMaterialPoint(n);
		FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());
			
		// material point coordinates
		// TODO: I'm not entirly happy with this solution
		//		 since the material point coordinates are used by most materials.
		pt.m_r0 = el.Evaluate(r0, n);
		pt.m_rt = el.Evaluate(rt, n);
			
		// get the deformation gradient and determinant
		pt.m_J = defgrad(el, pt.m_F, n);
			
		// solute-poroelastic data
		FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
		FESolutesMaterialPoint& spt = *(mp.ExtractData<FESolutesMaterialPoint>());
			
		// evaluate fluid pressure at gauss-point
		ppt.m_p = el.Evaluate(pn, n);
			
		// calculate the gradient of p at gauss-point
		ppt.m_gradp = gradient(el, pn, n);
			
		// evaluate effective solute concentration at gauss-point
		spt.m_c[0] = el.Evaluate(ct, n);
			
		// calculate the gradient of c at gauss-point
		spt.m_gradc[0] = gradient(el, ct, n);
			
		// for biphasic-solute materials also update the porosity, fluid and solute fluxes
		// and evaluate the actual fluid pressure and solute concentration
		ppt.m_w = m_pMat->FluidFlux(mp);
		ppt.m_pa = m_pMat->Pressure(mp);
		spt.m_j[0] = m_pMat->SoluteFlux(mp);
		spt.m_ca[0] = m_pMat->Concentration(mp);
		if (m_pMat->GetSolute()->m_pSupp)
		{
			if (sstate)
				spt.m_sbmr[0] = m_pMat->GetSolute()->m_pSupp->ReceptorLigandConcentrationSS(mp);
			else {
				// update m_crc using backward difference integration
				spt.m_sbmrhat[0] = m_pMat->GetSolute()->m_pSupp->ReceptorLigandSupply(mp);
				spt.m_sbmr[0] = spt.m_sbmrp[0] + spt.m_sbmrhat[0]*dt;
				// update phi0 using backward difference integration

				// NOTE: MolarMass was removed since not used
				ppt.m_phi0hat = 0;
//				ppt.m_phi0hat = pmb->GetSolid()->MolarMass()/pmb->GetSolid()->Density()*pmb->GetSolute()->m_pSupp->SolidSupply(mp);

				ppt.m_phi0 = ppt.m_phi0p + ppt.m_phi0hat*dt;
			}
		}

		// calculate the stress at this material point (must be done after evaluating m_pa)
		pt.m_s = m_pMat->Stress(mp);
	}
}
