/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2019 University of Utah, Columbia University, and others.

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
#include "FEBioMech/FEElasticDomain.h"
#include "FEBiphasicSoluteSolver.h"
#include "FEBiphasicSoluteDomain.h"
#include "FEBiphasicDomain.h"
#include "FETriphasicDomain.h"
#include "FEBioMech/FEPressureLoad.h"
#include "FEBioMech/FEResidualVector.h"
#include "FECore/log.h"
#include <FECore/FEModel.h>
#include <FECore/FEModelLoad.h>
#include <FECore/FEAnalysis.h>
#include <FECore/FEPrescribedBC.h>
#include <FECore/FENodalLoad.h>
#include "FECore/sys.h"

//-----------------------------------------------------------------------------
// define the parameter list
BEGIN_FECORE_CLASS(FEBiphasicSoluteSolver, FEBiphasicSolver)
	ADD_PARAMETER(m_Ctol, "ctol"        );
END_FECORE_CLASS();

//-----------------------------------------------------------------------------
FEBiphasicSoluteSolver::FEBiphasicSoluteSolver(FEModel* pfem) : FEBiphasicSolver(pfem)
{
	m_Ctol = 0.01;
    
	m_msymm = REAL_UNSYMMETRIC; // assume non-symmetric stiffness matrix by default

	// Allocate degrees of freedom
	// (We start with zero concentration degrees of freedom)
	DOFS& dofs = pfem->GetDOFS();
	int varC = dofs.AddVariable("concentration", VAR_ARRAY);
    int varD = dofs.AddVariable("shell concentration", VAR_ARRAY);

	m_dofC = m_dofD = -1;
}

//-----------------------------------------------------------------------------
//! Allocates and initializes the data structures.
//
bool FEBiphasicSoluteSolver::Init()
{
	// initialize base class
	if (FEBiphasicSolver::Init() == false) return false;

	int i;
    
    // get number of DOFS
	FEModel& fem = *GetFEModel();
	DOFS& fedofs = fem.GetDOFS();
    int MAX_CDOFS = fedofs.GetVariableSize("concentration");
    int MAX_DDOFS = fedofs.GetVariableSize("shell concentration");
    
	// allocate concentration-vectors
	m_ci.assign(MAX_CDOFS,vector<double>(0,0));
	m_Ci.assign(MAX_CDOFS,vector<double>(0,0));
	for (i=0; i<MAX_CDOFS; ++i) {
		m_ci[i].assign(m_nceq[i], 0);
		m_Ci[i].assign(m_nceq[i], 0);
	}
	
	// we need to fill the total displacement vector m_Ut
	vector<int> dofs;
	for (int j=0; j<MAX_CDOFS; ++j) 
	{
		if (m_nceq[j])
			dofs.push_back(m_dofC + j);
	}
    for (int j=0; j<MAX_DDOFS; ++j)
    {
        if (m_nceq[j])
            dofs.push_back(m_dofD + j);
    }

	FEMesh& mesh = fem.GetMesh();
	gather(m_Ut, mesh, dofs);

	return true;
}

//-----------------------------------------------------------------------------
//! Initialize equations
bool FEBiphasicSoluteSolver::InitEquations()
{
	// base class does most of the work
	FEBiphasicSolver::InitEquations();
	
	// determined the nr of concentration equations
	FEModel& fem = *GetFEModel();
	FEMesh& mesh = fem.GetMesh();
	for (int j=0; j<(int)m_nceq.size(); ++j) m_nceq[j] = 0;

    // get number of DOFS
    DOFS& fedofs = fem.GetDOFS();
    int MAX_CDOFS = fedofs.GetVariableSize("concentration");
	m_dofC = fem.GetDOFIndex("concentration", 0);
    m_dofD = fem.GetDOFIndex("shell concentration", 0);

    m_nceq.assign(MAX_CDOFS, 0);
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& n = mesh.Node(i);
        for (int j=0; j<MAX_CDOFS; ++j) {
			if (n.m_ID[m_dofC+j] != -1) m_nceq[j]++;
            if (n.m_ID[m_dofD+j] != -1) m_nceq[j]++;
        }
	}
	
	return true;
}

//-----------------------------------------------------------------------------
//! calculates the concentrated nodal forces
void FEBiphasicSoluteSolver::NodalForces(vector<double>& F, const FETimeInfo& tp)
{
	// zero nodal force vector
	zero(F);

	// loop over nodal loads
	FEModel& fem = *GetFEModel();
	int NNL = fem.NodalLoads();
	for (int i=0; i<NNL; ++i)
	{
		const FENodalLoad& fc = *fem.NodalLoad(i);
		if (fc.IsActive())
		{
			int dof = fc.GetDOF();

			int N = fc.Nodes();
			for (int j=0; j<N; ++j)
			{
				int nid	= fc.NodeID(j);

				// get the nodal load value
				double f = fc.NodeValue(j);
			
				// For pressure and concentration loads, multiply by dt
				// for consistency with evaluation of residual and stiffness matrix
				if ((dof == m_dofP) || (dof == m_dofQ) || (dof >= m_dofC)) f *= tp.timeIncrement;

				// assemble into residual
				AssembleResidual(nid, dof, f, F);
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! Prepares the data for the first QN iteration. 
//!
void FEBiphasicSoluteSolver::PrepStep()
{
	for (int j=0; j<(int)m_nceq.size(); ++j) if (m_nceq[j]) zero(m_Ci[j]);
	FEBiphasicSolver::PrepStep();
}

//-----------------------------------------------------------------------------
//! Implements the BFGS algorithm to solve the nonlinear FE equations.
bool FEBiphasicSoluteSolver::Quasin()
{
	// convergence norms
	double	normR1;		// residual norm
	double	normE1;		// energy norm
	double	normD;		// displacement norm
	double	normd;		// displacement increment norm
	double	normRi;		// initial residual norm
	double	normEi;		// initial energy norm
	double	normEm;		// max energy norm
	double	normDi;		// initial displacement norm

	// poro convergence norms data
	double	normPi;		// initial pressure norm
	double	normP;		// current pressure norm
	double	normp;		// incremement pressure norm

    // get number of DOFS
	FEModel& fem = *GetFEModel();
	DOFS& fedofs = fem.GetDOFS();
    int MAX_CDOFS = fedofs.GetVariableSize("concentration");
    
	// solute convergence data
	vector<double>	normCi(MAX_CDOFS);	// initial concentration norm
	vector<double>	normC(MAX_CDOFS);	// current concentration norm
	vector<double>	normc(MAX_CDOFS);	// incremement concentration norm

	// prepare for the first iteration
	const FETimeInfo& tp = fem.GetTime();
	PrepStep();

	// init QN method
	if (QNInit() == false) return false;

	// loop until converged or when max nr of reformations reached
	bool bconv = false;		// convergence flag
	do
	{
		feLog(" %d\n", m_niter+1);

		// assume we'll converge. 
		bconv = true;

		// solve the equations (returns line search; solution stored in m_ui)
		double s = QNSolve();

		// extract the pressure increments
		GetDisplacementData(m_di, m_ui);

		// set initial convergence norms
		if (m_niter == 0)
		{
			normRi = fabs(m_R0*m_R0);
			normEi = fabs(m_ui*m_R0);
			normDi = fabs(m_di*m_di);
			normEm = normEi;
		}

		// update all degrees of freedom
		for (int i=0; i<m_neq; ++i) m_Ui[i] += s*m_ui[i];

		// update displacements
		for (int i = 0; i<m_ndeq; ++i) m_Di[i] += s*m_di[i];

		// calculate norms
		normR1 = m_R1*m_R1;
		normd  = (m_di*m_di)*(s*s);
		normD  = m_Di*m_Di;
		normE1 = s*fabs(m_ui*m_R1);

		// check residual norm
		if ((m_Rtol > 0) && (normR1 > m_Rtol*normRi)) bconv = false;	

		// check displacement norm
		if ((m_Dtol > 0) && (normd  > (m_Dtol*m_Dtol)*normD )) bconv = false;

		// check energy norm
		if ((m_Etol > 0) && (normE1 > m_Etol*normEi)) bconv = false;

		// check linestep size
		if ((m_lineSearch->m_LStol > 0) && (s < m_lineSearch->m_LSmin)) bconv = false;

		// check energy divergence
		if (normE1 > normEm) bconv = false;

		// check poroelastic convergence
		// extract the pressure increments
		GetPressureData(m_pi, m_ui);

		// set initial norm
		if (m_niter == 0) normPi = fabs(m_pi*m_pi);

		// update total pressure
		for (int i = 0; i<m_npeq; ++i) m_Pi[i] += s*m_pi[i];

		// calculate norms
		normP = m_Pi*m_Pi;
		normp = (m_pi*m_pi)*(s*s);

		// check convergence
		if ((m_Ptol > 0) && (normp > (m_Ptol*m_Ptol)*normP)) bconv = false;

		// check solute convergence
		{
			// extract the concentration increments
			for (int j = 0; j<(int)m_nceq.size(); ++j) {
				if (m_nceq[j]) {
					GetConcentrationData(m_ci[j], m_ui,j);
					
					// set initial norm
					if (m_niter == 0)
						normCi[j] = fabs(m_ci[j]*m_ci[j]);
					
					// update total concentration
					for (int i = 0; i<m_nceq[j]; ++i) m_Ci[j][i] += s*m_ci[j][i];
					
					// calculate norms
					normC[j] = m_Ci[j]*m_Ci[j];
					normc[j] = (m_ci[j]*m_ci[j])*(s*s);
					
				}
			}
			
			// check convergence
			if (m_Ctol > 0) {
				for (int j = 0; j<(int)m_nceq.size(); ++j)
					if (m_nceq[j]) bconv = bconv && (normc[j] <= (m_Ctol*m_Ctol)*normC[j]);
			}
		}

		// print convergence summary
		feLog(" Nonlinear solution status: time= %lg\n", tp.currentTime);
		feLog("\tstiffness updates             = %d\n", m_strategy->m_nups);
		feLog("\tright hand side evaluations   = %d\n", m_nrhs);
		feLog("\tstiffness matrix reformations = %d\n", m_nref);
		if (m_lineSearch->m_LStol > 0) feLog("\tstep from line search         = %lf\n", s);
		feLog("\tconvergence norms :        INITIAL         CURRENT         REQUIRED\n");
		feLog("\t residual               %15le %15le %15le\n", normRi, normR1, m_Rtol*normRi);
		feLog("\t energy                 %15le %15le %15le\n", normEi, normE1, m_Etol*normEi);
		feLog("\t displacement           %15le %15le %15le\n", normDi, normd ,(m_Dtol*m_Dtol)*normD );
		feLog("\t fluid pressure         %15le %15le %15le\n", normPi, normp ,(m_Ptol*m_Ptol)*normP );
		for (int j = 0; j<(int)m_nceq.size(); ++j) {
			if (m_nceq[j])
				feLog("\t solute %d concentration %15le %15le %15le\n", j+1, normCi[j], normc[j] ,(m_Ctol*m_Ctol)*normC[j] );
		}

		if ((bconv == false) && (normR1 < m_Rmin))
		{
			// check for almost zero-residual on the first iteration
			// this might be an indication that there is no force on the system
			feLogWarning("No force acting on the system.");
			bconv = true;
		}

		// check if we have converged. 
		// If not, calculate the BFGS update vectors
		if (bconv == false)
		{
			if (s < m_lineSearch->m_LSmin)
			{
				// check for zero linestep size
				feLogWarning("Zero linestep size. Stiffness matrix will now be reformed");
				QNForceReform(true);
			}
			else if (normE1 > normEm)
			{
				// check for diverging
				feLogWarning("Problem is diverging. Stiffness matrix will now be reformed");
				normEm = normE1;
				normEi = normE1;
				normRi = normR1;
				normDi = normd;
				normPi = normp;
				for (int j = 0; j<(int)m_nceq.size(); ++j)
					if (m_nceq[j]) normCi[j] = normc[j];
				QNForceReform(true);
			}

			// Do the QN update (This may also do a stiffness reformation if necessary)
			bool bret = QNUpdate();

			// something went wrong with the update, so we'll need to break
			if (bret == false) break;
		}
		else if (m_baugment)
		{
			// Do augmentations
			bconv = DoAugmentations();
		}
	
		// increase iteration number
		m_niter++;

		// do minor iterations callbacks
		fem.DoCallback(CB_MINOR_ITERS);
	}
	while (bconv == false);

	// if converged we update the total displacements
	if (bconv)
	{
		m_Ut += m_Ui;
	}

	return bconv;
}

//-----------------------------------------------------------------------------
//! calculates the residual vector
//! Note that the concentrated nodal forces are not calculated here.
//! This is because they do not depend on the geometry 
//! so we only calculate them once (in Quasin) and then add them here.

bool FEBiphasicSoluteSolver::Residual(vector<double>& R)
{
	int i;

	// get the time information
	FEModel& fem = *GetFEModel();
	const FETimeInfo& tp = fem.GetTime();

	// initialize residual with concentrated nodal loads
	R = m_Fn;

	// zero nodal reaction forces
	zero(m_Fr);

	// setup global RHS vector
	FEResidualVector RHS(fem, R, m_Fr);

	// zero rigid body reaction forces
	m_rigidSolver.Residual();

	// get the mesh
	FEMesh& mesh = fem.GetMesh();

	// internal stress work
	for (i=0; i<mesh.Domains(); ++i)
	{
        FEDomain& dom = mesh.Domain(i);
        FEElasticDomain* ped = dynamic_cast<FEElasticDomain*>(&dom);
        FEBiphasicDomain*  pbd = dynamic_cast<FEBiphasicDomain* >(&dom);
        FEBiphasicSoluteDomain* psd = dynamic_cast<FEBiphasicSoluteDomain*>(&dom);
        FETriphasicDomain*      ptd = dynamic_cast<FETriphasicDomain*     >(&dom);
        if (psd) {
            if (fem.GetCurrentStep()->m_nanalysis == FE_STEADY_STATE)
                psd->InternalForcesSS(RHS);
            else
                psd->InternalForces(RHS);
        }
        else if (ptd) {
            if (fem.GetCurrentStep()->m_nanalysis == FE_STEADY_STATE)
                ptd->InternalForcesSS(RHS);
            else
                ptd->InternalForces(RHS);
        }
        else if (pbd) {
            if (fem.GetCurrentStep()->m_nanalysis == FE_STEADY_STATE)
                pbd->InternalForcesSS(RHS);
            else
                pbd->InternalForces(RHS);
        }
        else if (ped)
            ped->InternalForces(RHS);
    }
    
	// calculate forces due to surface loads
	int nsl = fem.SurfaceLoads();
	for (i=0; i<nsl; ++i)
	{
		FESurfaceLoad* psl = fem.SurfaceLoad(i);
		if (psl->IsActive()) psl->Residual(tp, RHS);
	}

	// calculate contact forces
	if (fem.SurfacePairConstraints() > 0)
	{
		ContactForces(RHS);
	}

	// calculate linear constraint forces
	// note that these are the linear constraints
	// enforced using the augmented lagrangian
	NonLinearConstraintForces(RHS, tp);

	// add model loads
	int NML = fem.ModelLoads();
	for (i=0; i<NML; ++i)
	{
		FEModelLoad& mli = *fem.ModelLoad(i);
		if (mli.IsActive())
		{
			mli.Residual(RHS, tp);
		}
	}

	// set the nodal reaction forces
	// TODO: Is this a good place to do this?
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		node.m_Fr = vec3d(0,0,0);

		int n;
		if ((n = -node.m_ID[m_dofX]-2) >= 0) node.m_Fr.x = -m_Fr[n];
		if ((n = -node.m_ID[m_dofY]-2) >= 0) node.m_Fr.y = -m_Fr[n];
		if ((n = -node.m_ID[m_dofZ]-2) >= 0) node.m_Fr.z = -m_Fr[n];
	}

	// increase RHS counter
	m_nrhs++;

	return true;
}

//-----------------------------------------------------------------------------
//! Calculates global stiffness matrix.

bool FEBiphasicSoluteSolver::StiffnessMatrix()
{
	FEModel& fem = *GetFEModel();
	const FETimeInfo& tp = fem.GetTime();

	// get the mesh
	FEMesh& mesh = fem.GetMesh();

	// calculate the stiffness matrix for each domain
	FEAnalysis* pstep = fem.GetCurrentStep();
	bool bsymm = (m_msymm == REAL_SYMMETRIC);
	if (pstep->m_nanalysis == FE_STEADY_STATE)
	{
		for (int i=0; i<mesh.Domains(); ++i) 
		{
            // Biphasic-solute analyses may also include biphasic and elastic domains
			FETriphasicDomain*      ptdom = dynamic_cast<FETriphasicDomain*>(&mesh.Domain(i));
			FEBiphasicSoluteDomain* psdom = dynamic_cast<FEBiphasicSoluteDomain*>(&mesh.Domain(i));
			FEBiphasicDomain*  pbdom = dynamic_cast<FEBiphasicDomain*>(&mesh.Domain(i));
			FEElasticDomain*   pedom = dynamic_cast<FEElasticDomain*>(&mesh.Domain(i));
			if (psdom) psdom->StiffnessMatrixSS(this, bsymm);
			else if (ptdom) ptdom->StiffnessMatrixSS(this, bsymm);
			else if (pbdom) pbdom->StiffnessMatrixSS(this, bsymm);
            else if (pedom) pedom->StiffnessMatrix(this);
		}
	}
	else
	{
		for (int i = 0; i<mesh.Domains(); ++i)
		{
            // Biphasic-solute analyses may also include biphasic and elastic domains
			FETriphasicDomain*      ptdom = dynamic_cast<FETriphasicDomain*>(&mesh.Domain(i));
			FEBiphasicSoluteDomain* psdom = dynamic_cast<FEBiphasicSoluteDomain*>(&mesh.Domain(i));
			FEBiphasicDomain* pbdom = dynamic_cast<FEBiphasicDomain*>(&mesh.Domain(i));
			FEElasticDomain* pedom = dynamic_cast<FEElasticDomain*>(&mesh.Domain(i));
			if (psdom) psdom->StiffnessMatrix(this, bsymm);
			else if (ptdom) ptdom->StiffnessMatrix(this, bsymm);
			else if (pbdom) pbdom->StiffnessMatrix(this, bsymm);
            else if (pedom) pedom->StiffnessMatrix(this);
		}
	}

	// calculate contact stiffness
	if (fem.SurfacePairConstraints() > 0) 
	{
		ContactStiffness();
	}

	// calculate stiffness matrices for surface loads
	int nsl = fem.SurfaceLoads();
	for (int i = 0; i<nsl; ++i)
	{
		FESurfaceLoad* psl = fem.SurfaceLoad(i);

		if (psl->IsActive())
		{
			psl->StiffnessMatrix(tp, this); 
		}
	}

	// calculate nonlinear constraint stiffness
	// note that this is the contribution of the 
	// constrainst enforced with augmented lagrangian
	NonLinearConstraintStiffness(tp);

	// add contributions from rigid bodies
	m_rigidSolver.StiffnessMatrix(*m_pK, tp);

	return true;
}

//-----------------------------------------------------------------------------
void FEBiphasicSoluteSolver::GetConcentrationData(vector<double> &ci, vector<double> &ui, const int sol)
{
	FEModel& fem = *GetFEModel();
	int N = fem.GetMesh().Nodes(), nid, m = 0;
	zero(ci);
	for (int i=0; i<N; ++i)
	{
		FENode& n = fem.GetMesh().Node(i);
		nid = n.m_ID[m_dofC+sol];
		if (nid != -1)
		{
			nid = (nid < -1 ? -nid-2 : nid);
			ci[m++] = ui[nid];
			assert(m <= (int) ci.size());
		}
        nid = n.m_ID[m_dofD+sol];
        if (nid != -1)
        {
            nid = (nid < -1 ? -nid-2 : nid);
            ci[m++] = ui[nid];
            assert(m <= (int) ci.size());
        }
	}
}


//-----------------------------------------------------------------------------
//! Update the model's kinematic data. This is overriden from FEBiphasicSolver so
//! that solute data is updated
void FEBiphasicSoluteSolver::UpdateKinematics(vector<double>& ui)
{
	// first update all solid-mechanics kinematics
	FEBiphasicSolver::UpdateKinematics(ui);

	// update solute-poroelastic data
	UpdateSolute(ui);
}

//-----------------------------------------------------------------------------
//! Updates the solute data
void FEBiphasicSoluteSolver::UpdateSolute(vector<double>& ui)
{
	FEModel& fem = *GetFEModel();
	FEMesh& mesh = fem.GetMesh();
	double dt = fem.GetTime().timeIncrement;
	
    // get number of DOFS
    DOFS& fedofs = fem.GetDOFS();
    int MAX_CDOFS = fedofs.GetVariableSize("concentration");
    int MAX_DDOFS = fedofs.GetVariableSize("shell concentration");
    
	// update solute data
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		
		// update nodal concentration
		for (int j=0; j<MAX_CDOFS; ++j) {
			int n = node.m_ID[m_dofC+j];
			// Force the concentrations to remain positive
			if (n >= 0) {
				double ct = 0 + m_Ut[n] + m_Ui[n] + ui[n];
				if (ct < 0) ct = 0.0;
				node.set(m_dofC + j, ct);
			}
		}
        for (int j=0; j<MAX_DDOFS; ++j) {
            int n = node.m_ID[m_dofD+j];
            // Force the concentrations to remain positive
            if (n >= 0) {
                double ct = 0 + m_Ut[n] + m_Ui[n] + ui[n];
                if (ct < 0) ct = 0.0;
                node.set(m_dofD + j, ct);
            }
        }
	}
	
	// update solute data
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		
		// update velocities
		vec3d vt = (node.m_rt - node.m_rp) / dt;
		node.set_vec3d(m_dofVX, m_dofVY, m_dofVZ, vt);
	}
}

//-----------------------------------------------------------------------------
//! Save data to dump file

void FEBiphasicSoluteSolver::Serialize(DumpStream& ar)
{
	FEBiphasicSolver::Serialize(ar);
	if (ar.IsShallow()) return;
	ar & m_Ctol & m_nceq & m_dofC & m_dofD;
	ar & m_ci;
	ar & m_Ci;
}
