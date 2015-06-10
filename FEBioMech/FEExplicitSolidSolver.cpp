#include "stdafx.h"
#include "FEExplicitSolidSolver.h"
#include "FEElasticSolidDomain.h"
#include "FERigidMaterial.h"
#include "FEPointBodyForce.h"
#include "FEContactInterface.h"
#include "FECore/FERigidBody.h"
#include "FECore/log.h"
#include "FECore/DOFS.h"

//-----------------------------------------------------------------------------
// define the parameter list
BEGIN_PARAMETER_LIST(FEExplicitSolidSolver, FESolver)
	ADD_PARAMETER(m_dyn_damping, FE_PARAM_DOUBLE, "dyn_damping");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
FEExplicitSolidSolver::FEExplicitSolidSolver(FEModel* pfem) : FESolver(pfem)
{
	m_dyn_damping = 0.99;
	m_niter = 0;
	m_nreq = 0;
}

//-----------------------------------------------------------------------------
void FEExplicitSolidSolver::Clean()
{
}

//-----------------------------------------------------------------------------
bool FEExplicitSolidSolver::Init()
{
	// get nr of equations
	int neq = m_neq;

	// allocate vectors
	m_Fn.assign(neq, 0);
	m_Fd.assign(neq, 0);
	m_Fr.assign(neq, 0);
	m_Ui.assign(neq, 0);
	m_ui.assign(neq, 0);
	m_Ut.assign(neq, 0);
	m_inv_mass.assign(neq, 1);

	int i, n;

	// we need to fill the total displacement vector m_Ut
	// TODO: I need to find an easier way to do this
	FEMesh& mesh = m_fem.GetMesh();
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);

		// displacement dofs
		n = node.m_ID[DOF_X]; if (n >= 0) m_Ut[n] = node.m_rt.x - node.m_r0.x;
		n = node.m_ID[DOF_Y]; if (n >= 0) m_Ut[n] = node.m_rt.y - node.m_r0.y;
		n = node.m_ID[DOF_Z]; if (n >= 0) m_Ut[n] = node.m_rt.z - node.m_r0.z;

		// rotational dofs
		n = node.m_ID[DOF_U]; if (n >= 0) m_Ut[n] = node.m_Dt.x - node.m_D0.x;
		n = node.m_ID[DOF_V]; if (n >= 0) m_Ut[n] = node.m_Dt.y - node.m_D0.y;
		n = node.m_ID[DOF_W]; if (n >= 0) m_Ut[n] = node.m_Dt.z - node.m_D0.z;
	}

	// calculate the inverse mass vector for the explicit analysis
	vector<double> dummy(m_inv_mass);
	FEGlobalVector Mi(GetFEModel(), m_inv_mass, dummy);
	matrix ke;
	int j, iel;
	int nint, neln;
	double *H, kab;
	vector <int> lm;
	vector <double> el_lumped_mass;
	// Data structure to store element mass data for dynamic damping:-
	// Define an overall dynamic array of pointers to the array that points to the element data
	// domain_mass is a list of pointers to the data for each domain
	domain_mass = new double ** [mesh.Domains()];

	for (int nd = 0; nd < mesh.Domains(); ++nd)
	{
		// check whether it is a solid domain
		FEElasticSolidDomain* pbd = dynamic_cast<FEElasticSolidDomain*>(&mesh.Domain(nd));
		if (pbd)  // it is an elastic solid domain
		{
			// for each domain define an array of pointers to the individual element_mass records
			double ** elmasses; 
			elmasses = new double * [pbd->Elements()];
			// and set a pointer in domain_mass to the new element array
			domain_mass[nd] = elmasses;

			for (iel=0; iel<pbd->Elements(); ++iel)
			{
				FESolidElement& el = pbd->Element(iel);
				pbd->UnpackLM(el, lm);

				FESolidMaterial* pme = dynamic_cast<FESolidMaterial*>(m_fem.GetMaterial(el.GetMatID()));

				double d = pme->Density();

				nint = el.GaussPoints();
				neln = el.Nodes();

				ke.resize(3*neln, 3*neln);
				ke.zero();
				el_lumped_mass.resize(3*neln);
				for (i=0; i<3*neln; ++i) el_lumped_mass[i]=0.0;

				// create the element mass matrix
				for (n=0; n<nint; ++n)
				{
					double detJ0 = pbd->detJ0(el, n)*el.GaussWeights()[n];

					H = el.H(n);
					for (i=0; i<neln; ++i)
						for (j=0; j<neln; ++j)
						{
							kab = H[i]*H[j]*detJ0*d;
							ke[3*i  ][3*j  ] += kab;
							ke[3*i+1][3*j+1] += kab;
							ke[3*i+2][3*j+2] += kab;
						}	
				}
				// reduce to a lumped mass vector and add up the total
				double total_mass = 0.0;
				for (i=0; i<3*neln; ++i)
				{
						for (j=0; j<neln; ++j)
						{
							el_lumped_mass[i]+=ke[i][j];
						}
						total_mass += el_lumped_mass[i];
				}	
				total_mass /= 3.0; // because each mass is represented three times for each direction
				// define an element mass record
				double * thiselement;
				thiselement = new double [neln+1];
				// and set a pointer to the element data
				elmasses[iel] = thiselement;
				thiselement[0] = total_mass; // total mass of the element first, followed by the fraction at each node
				// for each node, store the fraction of the element mass associated with it
				for (i=0; i<neln; ++i)
				{
					thiselement[i+1] = (el_lumped_mass[3*i]+el_lumped_mass[3*i+1]+el_lumped_mass[3*i+2])/(3*total_mass);
				} // loop over nodes within element
				// invert and assemble element matrix into inv_mass vector 
				for (i=0; i<3*neln; ++i)
				{
					el_lumped_mass[i] = 1.0/el_lumped_mass[i];
				}
				Mi.Assemble(el.m_node, lm, el_lumped_mass);
				// formerly AssembleResidual(el.m_node, lm, el_lumped_mass, m_inv_mass);
			} // loop over elements
		} // was an elastic solid domain
		else domain_mass[nd] = 0;  // no masses stored for other types of domain
	}

	// Calculate initial residual to be used on the first time step
	if (Residual(m_R1) == false) return false;
	m_R1 += m_Fd;

	return true;
}


//-----------------------------------------------------------------------------
//! Determine the number of linear equations and assign equation numbers
//!

//-----------------------------------------------------------------------------
//!	This function initializes the equation system.
//! It is assumed that all free dofs up until now have been given an ID >= 0
//! and the fixed or rigid dofs an ID < 0.
//! After this operation the nodal ID array will contain the equation
//! number assigned to the corresponding degree of freedom. To distinguish
//! between free or unconstrained dofs and constrained ones the following rules
//! apply to the ID array:
//!
//!           /
//!          |  >=  0 --> dof j of node i is a free dof
//! ID[i][j] <  == -1 --> dof j of node i is a fixed (no equation assigned too)
//!          |  <  -1 --> dof j of node i is constrained and has equation nr = -ID[i][j]-2
//!           \
//!
bool FEExplicitSolidSolver::InitEquations()
{
	int i, j, n;

    // get nodal DOFS
    DOFS& fedofs = *DOFS::GetInstance();
    int MAX_NDOFS = fedofs.GetNDOFS();

	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

	// initialize nr of equations
	int neq = 0;

	// give all free dofs an equation number
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		for (j=0; j<MAX_NDOFS; ++j)
			if (node.m_ID[j] >= 0) node.m_ID[j] = neq++;
	}

	// Next, we assign equation numbers to the rigid body degrees of freedom
	m_nreq = neq;
	int nrb = m_fem.Objects();
	for (i=0; i<nrb; ++i)
	{
		FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(i));
		for (j=0; j<6; ++j)
			if (RB.m_BC[j] >= 0)
			{
				RB.m_LM[j] = neq++;
			}
			else 
			{
				RB.m_LM[j] = -1;
			}
	}

	// store the number of equations
	m_neq = neq;

	// we assign the rigid body equation number to
	// Also make sure that the nodes are NOT constrained!
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		if (node.m_rid >= 0)
		{
			FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(node.m_rid));
			node.m_ID[DOF_X] = -RB.m_LM[0]-2;
			node.m_ID[DOF_Y] = -RB.m_LM[1]-2;
			node.m_ID[DOF_Z] = -RB.m_LM[2]-2;
			node.m_ID[DOF_RU] = -RB.m_LM[3]-2;
			node.m_ID[DOF_RV] = -RB.m_LM[4]-2;
			node.m_ID[DOF_RW] = -RB.m_LM[5]-2;
		}
	}

	// adjust the rigid dofs that are prescribed
	for (i=0; i<nrb; ++i)
	{
		FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(i));
		FERigidMaterial* pm = dynamic_cast<FERigidMaterial*>(m_fem.GetMaterial(RB.m_mat));
		for (j=0; j<6; ++j)
		{
			n = RB.m_LM[j];
			if (RB.m_BC[j] > 0) RB.m_LM[j] = -n-2;
		}
	}

	// All initialization is done
	return true;
}

//-----------------------------------------------------------------------------
//!  Assembles the element into the global residual. This function
//!  also checks for rigid dofs and assembles the residual using a condensing
//!  procedure in the case of rigid dofs.
/*
void FEExplicitSolidSolver::AssembleResidual(vector<int>& en, vector<int>& elm, vector<double>& fe, vector<double>& R)
{
	int i, j, I, n, l;
	vec3d a, d;

	// assemble the element residual into the global residual
	int ndof = fe.size();
	for (i=0; i<ndof; ++i)
	{
		I = elm[i];
		if ( I >= 0) R[I] += fe[i];
		else if (-I-2 >= 0) m_Fr[-I-2] -= fe[i];
	}

	int ndn = ndof / en.size();

	// if there are linear constraints we need to apply them
	if (m_fem.m_LinC.size() > 0)
	{
		// loop over all degrees of freedom of this element
		for (i=0; i<ndof; ++i)
		{
			// see if this dof belongs to a linear constraint
			n = MAX_NDOFS*(en[i/ndn]) + i%ndn;
			l = m_fem.m_LCT[n];
			if (l >= 0)
			{
				// if so, get the linear constraint
				FELinearConstraint& lc = *m_fem.m_LCA[l];
				assert(elm[i] == -1);
	
				// now loop over all "slave" nodes and
				// add the contribution to the residual
				int ns = lc.slave.size();
				list<FELinearConstraint::SlaveDOF>::iterator is = lc.slave.begin();
				for (j=0; j<ns; ++j, ++is)
				{
					I = is->neq;
					if (I >= 0)
					{
						double A = is->val;
						R[I] += A*fe[i];
					}
				}
			}
		}
	}

	// If there are rigid bodies we need to look for rigid dofs
	if (m_fem.Objects())
	{
		int *lm;

		for (i=0; i<ndof; i+=ndn)
		{
			FENode& node = m_fem.GetMesh().Node(en[i/ndn]);
			if (node.m_rid >= 0)
			{
				vec3d F(fe[i], fe[i+1], fe[i+2]);

				// this is an interface dof
				// get the rigid body this node is connected to
				FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(node.m_rid));
				lm = RB.m_LM;

				// add to total torque of this body
				a = node.m_rt - RB.m_rt;

				n = lm[3]; if (n >= 0) R[n] += a.y*F.z-a.z*F.y; RB.m_Mr.x -= a.y*F.z-a.z*F.y;
				n = lm[4]; if (n >= 0) R[n] += a.z*F.x-a.x*F.z; RB.m_Mr.y -= a.z*F.x-a.x*F.z;
				n = lm[5]; if (n >= 0) R[n] += a.x*F.y-a.y*F.x; RB.m_Mr.z -= a.x*F.y-a.y*F.x;

				// if the rotational degrees of freedom are constrained for a rigid node
				// then we need to add an additional component to the residual
				//if (node.m_ID[DOF_RU] == lm[3])
				//{
				//	d = node.m_Dt;
				//	n = lm[3]; if (n >= 0) R[n] += d.y*F.z-d.z*F.y; RB.m_Mr.x -= d.y*F.z-d.z*F.y;
				//	n = lm[4]; if (n >= 0) R[n] += d.z*F.x-d.x*F.z; RB.m_Mr.y -= d.z*F.x-d.x*F.z;
				//	n = lm[5]; if (n >= 0) R[n] += d.x*F.y-d.y*F.x; RB.m_Mr.z -= d.x*F.y-d.y*F.x;
				//}

				// add to global force vector
				n = lm[0]; if (n >= 0) R[n] += F.x; RB.m_Fr.x -= F.x;
				n = lm[1]; if (n >= 0) R[n] += F.y; RB.m_Fr.y -= F.y;
				n = lm[2]; if (n >= 0) R[n] += F.z; RB.m_Fr.z -= F.z;
			}
		}
	}
}
*/

//-----------------------------------------------------------------------------
//! Updates the current state of the model
void FEExplicitSolidSolver::Update(vector<double>& ui)
{
	FETimePoint tp = m_fem.GetTime();

	// update kinematics
	UpdateKinematics(ui);

	// Update all contact interfaces
	for (int i=0; i<m_fem.SurfacePairInteractions(); ++i)
	{
		FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));
		pci->Update(m_niter);
	}

	// update rigid joints
	int NC = m_fem.NonlinearConstraints();
	for (int i=0; i<NC; ++i)
	{
		FENLConstraint* plc = m_fem.NonlinearConstraint(i);
		if (plc->IsActive()) plc->Update(tp);
	}

	// update element stresses
	UpdateStresses();

	// update other stuff that may depend on the deformation
	int NBL = m_fem.BodyLoads();
	for (int i=0; i<NBL; ++i)
	{
		FEBodyForce* pbf = dynamic_cast<FEBodyForce*>(m_fem.GetBodyLoad(i));
		if (pbf) pbf->Update();
	}

	// dump all states to the plot file when requested
	if (m_fem.GetCurrentStep()->GetPlotLevel() == FE_PLOT_MINOR_ITRS) m_fem.Write();
}


//-----------------------------------------------------------------------------
//! Update the kinematics of the model, such as nodal positions, velocities,
//! accelerations, etc.
void FEExplicitSolidSolver::UpdateKinematics(vector<double>& ui)
{
	int i, n;

	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

	// update rigid bodies
	UpdateRigidBodies(ui);

	// update flexible nodes
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);

		// displacement dofs
		// current position = initial + total at prev conv step + total increment so far + current increment  
		if ((n = node.m_ID[DOF_X]) >= 0) node.m_rt.x = node.m_r0.x + m_Ut[n] + m_Ui[n] + ui[n];
		if ((n = node.m_ID[DOF_Y]) >= 0) node.m_rt.y = node.m_r0.y + m_Ut[n] + m_Ui[n] + ui[n];
		if ((n = node.m_ID[DOF_Z]) >= 0) node.m_rt.z = node.m_r0.z + m_Ut[n] + m_Ui[n] + ui[n];

		// rotational dofs
		if ((n = node.m_ID[DOF_U]) >= 0) node.m_Dt.x = node.m_D0.x + m_Ut[n] + m_Ui[n] + ui[n];
		if ((n = node.m_ID[DOF_V]) >= 0) node.m_Dt.y = node.m_D0.y + m_Ut[n] + m_Ui[n] + ui[n];
		if ((n = node.m_ID[DOF_W]) >= 0) node.m_Dt.z = node.m_D0.z + m_Ut[n] + m_Ui[n] + ui[n];
	}

	// make sure the prescribed displacements are fullfilled
	int ndis = m_fem.PrescribedBCs();
	for (i=0; i<ndis; ++i)
	{
		FEPrescribedBC& dc = *m_fem.PrescribedBC(i);
		if (dc.IsActive())
		{
			int n    = dc.node;
			int lc   = dc.lc;
			int bc   = dc.bc;
			double s = dc.s;
			double r = dc.r;

			FENode& node = mesh.Node(n);

			double g = r + s*m_fem.GetLoadCurve(lc)->Value();

			switch (bc)
			{
			case 0:
				node.m_rt.x = node.m_r0.x + g;
				break;
			case 1:
				node.m_rt.y = node.m_r0.y + g;
				break;
			case 2:
				node.m_rt.z = node.m_r0.z + g;
				break;
			case 20:
				{
					vec3d dr = node.m_r0;
					dr.x = 0; dr.unit(); dr *= g;

					node.m_rt.y = node.m_r0.y + dr.y;
					node.m_rt.z = node.m_r0.z + dr.z;
				}
				break;
			}
		}
	}

	// enforce the linear constraints
	// TODO: do we really have to do this? Shouldn't the algorithm
	// already guarantee that the linear constraints are satisfied?
	if (m_fem.m_LinC.size() > 0)
	{
		int nlin = m_fem.m_LinC.size();
		list<FELinearConstraint>::iterator it = m_fem.m_LinC.begin();
		double d;
		for (int n=0; n<nlin; ++n, ++it)
		{
			FELinearConstraint& lc = *it;
			FENode& node = mesh.Node(lc.master.node);

			d = 0;
			int ns = lc.slave.size();
			list<FELinearConstraint::SlaveDOF>::iterator si = lc.slave.begin();
			for (int i=0; i<ns; ++i, ++si)
			{
				FENode& node = mesh.Node(si->node);
				switch (si->bc)
				{
				case 0: d += si->val*(node.m_rt.x - node.m_r0.x); break;
				case 1: d += si->val*(node.m_rt.y - node.m_r0.y); break;
				case 2: d += si->val*(node.m_rt.z - node.m_r0.z); break;
				}
			}

			switch (lc.master.bc)
			{
			case 0: node.m_rt.x = node.m_r0.x + d; break;
			case 1: node.m_rt.y = node.m_r0.y + d; break;
			case 2: node.m_rt.z = node.m_r0.z + d; break;
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! Updates the rigid body data
void FEExplicitSolidSolver::UpdateRigidBodies(vector<double>& ui)
{
	FEMesh& mesh = m_fem.GetMesh();

	// update rigid bodies
	int nrb = m_fem.Objects();
	for (int i=0; i<nrb; ++i)
	{
		// get the rigid body
		FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(i));
		int *lm = RB.m_LM;
		double* du = RB.m_du;

		// first do the displacements
		if (RB.m_prb == 0)
		{
			FERigidBodyDisplacement* pdc;
			for (int j=0; j<3; ++j)
			{
				pdc = RB.m_pDC[j];
				if (pdc)
				{
					int lc = pdc->lc;
					// TODO: do I need to take the line search step into account here?
					du[j] = (lc < 0? 0 : pdc->sf*m_fem.GetLoadCurve(lc)->Value() - RB.m_Up[j] + pdc->ref);
				}
				else 
				{
					du[j] = (lm[j] >=0 ? m_Ui[lm[j]] + ui[lm[j]] : 0);
				}
			}
		}

		RB.m_rt.x = RB.m_rp.x + du[0];
		RB.m_rt.y = RB.m_rp.y + du[1];
		RB.m_rt.z = RB.m_rp.z + du[2];

		// next, we do the rotations. We do this seperatly since
		// they need to be interpreted differently than displacements
		if (RB.m_prb == 0)
		{
			FERigidBodyDisplacement* pdc;
			for (int j=3; j<6; ++j)
			{
				pdc = RB.m_pDC[j];
				if (pdc)
				{
					int lc = pdc->lc;
					// TODO: do I need to take the line search step into account here?
					du[j] = (lc < 0? 0 : pdc->sf*m_fem.GetLoadCurve(lc)->Value() - RB.m_Up[j]);
				}
				else du[j] = (lm[j] >=0 ? m_Ui[lm[j]] + ui[lm[j]] : 0);
			}
		}

		vec3d r = vec3d(du[3], du[4], du[5]);
		double w = sqrt(r.x*r.x + r.y*r.y + r.z*r.z);
		quatd dq = quatd(w, r);

		RB.m_qt = dq*RB.m_qp;
		RB.m_qt.MakeUnit();

		if (RB.m_prb) du = RB.m_dul;
		RB.m_Ut[0] = RB.m_Up[0] + du[0];
		RB.m_Ut[1] = RB.m_Up[1] + du[1];
		RB.m_Ut[2] = RB.m_Up[2] + du[2];
		RB.m_Ut[3] = RB.m_Up[3] + du[3];
		RB.m_Ut[4] = RB.m_Up[4] + du[4];
		RB.m_Ut[5] = RB.m_Up[5] + du[5];

		// update the mesh' nodes
		FEMesh& mesh = m_fem.GetMesh();
		int N = mesh.Nodes();
		for (int i=0; i<N; ++i)
		{
			FENode& node = mesh.Node(i);
			if (node.m_rid == RB.m_nID)
			{
				vec3d a0 = node.m_r0 - RB.m_r0;
				vec3d at = RB.m_qt*a0;
				node.m_rt = RB.m_rt + at;
			}
		}
	}
}

//-----------------------------------------------------------------------------
//!  Updates the element stresses
void FEExplicitSolidSolver::UpdateStresses()
{
	FEMesh& mesh = m_fem.GetMesh();

	// update the stresses on all domains
	for (int i=0; i<mesh.Domains(); ++i)
	{
		FEElasticDomain& dom = dynamic_cast<FEElasticDomain&>(mesh.Domain(i));
		dom.UpdateStresses(m_fem);
	}
}

//-----------------------------------------------------------------------------
//! Save data to dump file

void FEExplicitSolidSolver::Serialize(DumpFile& ar)
{
	if (ar.IsSaving())
	{
		ar << m_nrhs;
		ar << m_niter;
		ar << m_nref << m_ntotref;
		ar << m_naug;
		ar << m_neq << m_nreq;
	}
	else
	{
		ar >> m_nrhs;
		ar >> m_niter;
		ar >> m_nref >> m_ntotref;
		ar >> m_naug;
		ar >> m_neq >> m_nreq;
	}
}

//-----------------------------------------------------------------------------
//!  This function mainly calls the DoSolve routine 
//!  and deals with exceptions that require the immediate termination of
//!	 the solution eg negative Jacobians.
bool FEExplicitSolidSolver::SolveStep(double time)
{
	bool bret;

	try
	{
		// let's try to solve the step
		bret = DoSolve(time);
	}
	catch (NegativeJacobian e)
	{
		// A negative jacobian was detected
		felog.printbox("ERROR","Negative jacobian was detected at element %d at gauss point %d\njacobian = %lg\n", e.m_iel, e.m_ng+1, e.m_vol);
		if (m_fem.GetDebugFlag()) m_fem.Write();
		return false;
	}
	catch (MaxStiffnessReformations) // shouldn't happen for an explicit analysis!
	{
		// max nr of reformations is reached
		felog.printbox("ERROR", "Max nr of reformations reached.");
		return false;
	}
	catch (ForceConversion)
	{
		// user forced conversion of problem
		felog.printbox("WARNING", "User forced conversion.\nSolution might not be stable.");
		return true;
	}
	catch (IterationFailure)
	{
		// user caused a forced iteration failure
		felog.printbox("WARNING", "User forced iteration failure.");
		return false;
	}
	catch (ZeroLinestepSize) // shouldn't happen for an explicit analysis!
	{
		// a zero line step size was detected
		felog.printbox("ERROR", "Zero line step size.");
		return false;
	}
	catch (EnergyDiverging) // shouldn't happen for an explicit analysis!
	{
		// problem was diverging after stiffness reformation
		felog.printbox("ERROR", "Problem diverging uncontrollably.");
		return false;
	}
	catch (FEMultiScaleException)
	{
		// the RVE problem didn't solve
		felog.printbox("ERROR", "The RVE problem has failed. Aborting macro run.");
		return false;
	}

	return bret;
}


//-----------------------------------------------------------------------------
//! Prepares the data for the time step. 
void FEExplicitSolidSolver::PrepStep(double time)
{
	int i, j;

	// initialize counters
	m_niter = 0;	// nr of iterations
	m_nrhs  = 0;	// nr of RHS evaluations
	m_nref  = 0;	// nr of stiffness reformations
	m_ntotref = 0;
	m_naug  = 0;	// nr of augmentations

	// zero total displacements
	zero(m_Ui);

	// store previous mesh state
	// we need them for velocity and acceleration calculations
	FEMesh& mesh = m_fem.GetMesh();
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& ni = mesh.Node(i);
		ni.m_rp = ni.m_rt;
		ni.m_vp = ni.m_vt;
		ni.m_ap = ni.m_at;
		// ---> TODO: move to the FEPoroSoluteSolver
		for (int k=0; k<(int)ni.m_cp.size(); ++k) ni.m_cp[k] = ni.m_ct[k];
	}

	// apply concentrated nodal forces
	// since these forces do not depend on the geometry
	// we can do this once outside the NR loop.
	NodalForces(m_Fn);

	// apply prescribed displacements
	// we save the prescribed displacements increments in the ui vector
	vector<double>& ui = m_ui;
	zero(ui);
	int neq = m_neq;
	int nbc = m_fem.PrescribedBCs();
	for (i=0; i<nbc; ++i)
	{
		FEPrescribedBC& dc = *m_fem.PrescribedBC(i);
		if (dc.IsActive())
		{
			int n    = dc.node;
			int lc   = dc.lc;
			int bc   = dc.bc;
			double s = dc.s;
			double r = dc.r;

			double dq = r + s*m_fem.GetLoadCurve(lc)->Value();

			int I;

			FENode& node = m_fem.GetMesh().Node(n);

			switch (bc)
			{
				case DOF_X: 
					I = -node.m_ID[bc]-2;
					if (I>=0 && I<neq) 
						ui[I] = dq - (node.m_rt.x - node.m_r0.x);
					break;
				case DOF_Y: 
					I = -node.m_ID[bc]-2;
					if (I>=0 && I<neq) 
						ui[I] = dq - (node.m_rt.y - node.m_r0.y); 
					break;
				case DOF_Z: 
					I = -node.m_ID[bc]-2;
					if (I>=0 && I<neq) 
						ui[I] = dq - (node.m_rt.z - node.m_r0.z); 
					break;
					// ---> TODO: move to the FEPoroSolidSolver
				case DOF_P: 
					I = -node.m_ID[bc]-2;
					if (I>=0 && I<neq) 
						ui[I] = dq - node.m_pt; 
					break;
/*				case DOF_C: 
					I = -node.m_ID[bc]-2;
					if (I>=0 && I<neq) 
						ui[I] = dq - node.m_ct[0]; 
					break;
				case DOF_C+1: 
					I = -node.m_ID[bc]-2;
					if (I>=0 && I<neq) 
						ui[I] = dq - node.m_ct[1]; 
					break;*/
					// ---> TODO: change bc=20 to something else
				case 20:
				{
					vec3d dr = node.m_r0;
					dr.x = 0; dr.unit(); dr *= dq;
					
					I = -node.m_ID[1]-2;
					if (I>=0 && I<neq) 
						ui[I] = dr.y - (node.m_rt.y - node.m_r0.y); 
					I = -node.m_ID[2]-2;
					if (I>=0 && I<neq) 
						ui[I] = dr.z - (node.m_rt.z - node.m_r0.z); 
				}
					break;
				default:
					if ((bc >= DOF_C) && (bc < (int)node.m_ID.size())) {
						I = -node.m_ID[bc]-2;
						if (I>=0 && I<neq) 
							ui[I] = dq - node.m_ct[bc - DOF_C]; 
					}
			}
		}
	}

	// initialize rigid bodies
	int NO = m_fem.Objects();
	for (i=0; i<NO; ++i) m_fem.Object(i)->Init();

	// calculate local rigid displacements
	for (i=0; i<(int) m_fem.m_RDC.size(); ++i)
	{
		FERigidBodyDisplacement& DC = *m_fem.m_RDC[i];
		FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(DC.id));
		if (DC.IsActive())
		{
			int I = DC.bc;
			int lc = DC.lc;
			if (lc >= 0)
			{
				RB.m_dul[I] = DC.sf*m_fem.GetLoadCurve(lc)->Value() - RB.m_Ut[DC.bc];
			}
		}
	}

	// calculate global rigid displacements
	for (i=0; i<NO; ++i)
	{
		FERigidBody* prb = dynamic_cast<FERigidBody*>(m_fem.Object(i));
		if (prb)
		{
			FERigidBody& RB = *prb;
			if (RB.m_prb == 0)
			{
				for (j=0; j<6; ++j) RB.m_du[j] = RB.m_dul[j];
			}
			else
			{
				double* dul = RB.m_dul;
				vec3d dr = vec3d(dul[0], dul[1], dul[2]);
				
				vec3d v = vec3d(dul[3], dul[4], dul[5]);
				double w = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
				quatd dq = quatd(w, v);

				FERigidBody* pprb = RB.m_prb;

				vec3d r0 = RB.m_rt;
				quatd Q0 = RB.m_qt;

				dr = Q0*dr;
				dq = Q0*dq*Q0.Inverse();

				while (pprb)
				{
					vec3d r1 = pprb->m_rt;
					dul = pprb->m_dul;

					quatd Q1 = pprb->m_qt;
					
					dr = r0 + dr - r1;

					// grab the parent's local displacements
					vec3d dR = vec3d(dul[0], dul[1], dul[2]);
					v = vec3d(dul[3], dul[4], dul[5]);
					w = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
					quatd dQ = quatd(w, v);

					dQ = Q1*dQ*Q1.Inverse();

					// update global displacements
					quatd Qi = Q1.Inverse();
					dr = dR + r1 + dQ*dr - r0;
					dq = dQ*dq;

					// move up in the chain
					pprb = pprb->m_prb;
					Q0 = Q1;
				}

				// set global displacements
				double* du = RB.m_du;

				du[0] = dr.x;
				du[1] = dr.y;
				du[2] = dr.z;

				v = dq.GetVector();
				w = dq.GetAngle();
				du[3] = w*v.x;
				du[4] = w*v.y;
				du[5] = w*v.z;
			}
		}
	}

	// store rigid displacements in Ui vector
	for (i=0; i<NO; ++i)
	{
		FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(i));
		for (j=0; j<6; ++j)
		{
			int I = -RB.m_LM[j]-2;
			if (I >= 0) ui[I] = RB.m_du[j];
		}
	}

	// apply prescribed rigid body forces
	// TODO: I don't think this does anything since
	//       the reaction forces are zeroed in the FESolidSolver::Residual function
	for (i=0; i<(int) m_fem.m_RFC.size(); ++i)
	{
		FERigidBodyForce& FC = *m_fem.m_RFC[i];
		FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(FC.id));
		if (FC.IsActive())
		{
			int lc = FC.lc;
			int I  = RB.m_LM[FC.bc];
			if ((I>=0) && (lc >= 0))
			{
				double f = m_fem.GetLoadCurve(lc)->Value()*FC.sf;
				m_Fn[I] += f;

				switch (FC.bc)
				{
				case 0: RB.m_Fr.x += f; break;
				case 1: RB.m_Fr.y += f; break;
				case 2: RB.m_Fr.z += f; break;
				case 3: RB.m_Mr.x += f; break;
				case 4: RB.m_Mr.y += f; break;
				case 5: RB.m_Mr.z += f; break;
				}
			}
		}
	}

	// initialize contact
	for (int i=0; i<m_fem.SurfacePairInteractions(); ++i)
	{
		FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));
		pci->Update(m_niter);
	}

	// intialize material point data
	// NOTE: do this before the stresses are updated
	// TODO: does it matter if the stresses are updated before
	//       the material point data is initialized
	FEMaterialPoint::dt = m_fem.GetCurrentStep()->m_dt;
	FEMaterialPoint::time = m_fem.m_ftime;

	for (i=0; i<mesh.Domains(); ++i) mesh.Domain(i).InitElements();

	UpdateStresses();
}

//-----------------------------------------------------------------------------
//! calculates the concentrated nodal forces

void FEExplicitSolidSolver::NodalForces(vector<double>& F)
{
	int i, id, bc, lc, n;
	double s, f;
	vec3d a;
	int* lm;

	// zero nodal force vector
	zero(F);

	FEMesh& mesh = m_fem.GetMesh();

	// loop over nodal force cards
	int ncnf = m_fem.NodalLoads();
	for (i=0; i<ncnf; ++i)
	{
		FENodalForce& fc = *m_fem.NodalLoad(i);
		if (fc.IsActive())
		{
			id	 = fc.node;	// node ID
			bc   = fc.bc;	// direction of force
			lc   = fc.lc;	// loadcurve number
			s    = fc.s;		// force scale factor

			FENode& node = mesh.Node(id);

			n = node.m_ID[bc];
		
			f = s*m_fem.GetLoadCurve(lc)->Value();
			
			// For pressure and concentration loads, multiply by dt
			// for consistency with evaluation of residual and stiffness matrix
			if ((bc == DOF_P) || (bc >= DOF_C))
				f *= m_fem.GetCurrentStep()->m_dt;

			if (n >= 0) F[n] = f;
			else if (node.m_rid >=0)
			{
				// this is a rigid body node
				FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(node.m_rid));

				// get the relative position
				a = node.m_rt - RB.m_rt;

				lm = RB.m_LM;
				switch (bc)
				{
				case 0:
					if (lm[0] >= 0) F[lm[0]] +=  f;
					if (lm[4] >= 0) F[lm[4]] +=  a.z*f;
					if (lm[5] >= 0) F[lm[5]] += -a.y*f;
					break;
				case 1:
					if (lm[1] >= 0) F[lm[1]] +=  f;
					if (lm[3] >= 0) F[lm[3]] += -a.z*f;
					if (lm[5] >= 0) F[lm[5]] +=  a.x*f;
					break;
				case 2:
					if (lm[2] >= 0) F[lm[2]] +=  f;
					if (lm[3] >= 0) F[lm[3]] +=  a.y*f;
					if (lm[4] >= 0) F[lm[4]] += -a.x*f;
					break;
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
bool FEExplicitSolidSolver::DoSolve(double time)
{
	int i, n;

	vector<double> u0(m_neq);
	vector<double> Rold(m_neq);

	// Get the current step
	FEAnalysis* pstep = m_fem.GetCurrentStep();

	// prepare for the first iteration
	PrepStep(time);

	// do minor iterations callbacks
	m_fem.DoCallback(CB_MINOR_ITERS);

	// print starting message
	felog.printf("\n===== beginning time step %d : %lg =====\n", pstep->m_ntimesteps+1, m_fem.m_ftime);

	Logfile::MODE oldmode = felog.GetMode();
	if ((pstep->GetPrintLevel() <= FE_PRINT_MAJOR_ITRS) &&
		(pstep->GetPrintLevel() != FE_PRINT_NEVER)) felog.SetMode(Logfile::FILE_ONLY);

	felog.printf(" %d\n", m_niter+1);
	felog.SetMode(oldmode);

	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();
	int N = mesh.Nodes(); // this is the total number of nodes in the mesh
	int j,iel;
	double dt=m_fem.GetCurrentStep()->m_dt;
	double avx = 0.0;  // average element velocity in each direction
	double avy = 0.0;
	double avz = 0.0;
	double mass_at_node;

	for (i=0; i<N; ++i) // zero the new acceleration vector ready to add in the damping components
	{
		FENode& node = mesh.Node(i);
		node.m_at.x = 0.0;
		node.m_at.y = 0.0;
		node.m_at.z = 0.0;
	}

	for (int nd = 0; nd < mesh.Domains(); ++nd)
	{
		FEElasticSolidDomain* pbd = dynamic_cast<FEElasticSolidDomain*>(&mesh.Domain(nd));
		if (pbd)  // it is an elastic solid domain
		{
			double ** emass = domain_mass[nd]; // array of pointers to the element mass records for this domain
			for (iel=0; iel<pbd->Elements(); ++iel)
			{
				FESolidElement& el = pbd->Element(iel);

				// zero the three average velocity components
				avx = 0.0;
				avy = 0.0;
				avz = 0.0;

				// will use previously calculated element mass data for weighted averaging of velocities

				// loop over each element to find the average velocity
				// then calculate the weighted velocity change for each node
				// add each velocity change into node.m_vt
				double * this_element = emass[iel]; // pointer to the array of fractional nodal masses for this element
				for (j=0; j<el.Nodes(); j++) // loop over each node in the element
				{
					FENode& node = mesh.Node(el.m_node[j]);  // get the node 
					avx += node.m_vp.x*this_element[j+1];  // add each of the three components to the averages
					avy += node.m_vp.y*this_element[j+1];  // weighted by the fractional mass of the node
					avz += node.m_vp.z*this_element[j+1];  // remembering that this_element[0] is the total mass
				}
				for (j=0; j<el.Nodes(); j++) // loop over each node in the element again
				// and calculate and add in the velocity change contribution to each dof
				{
					FENode& node = mesh.Node(el.m_node[j]);  // get the node 
					//	need to find node.m_vt.x += (avx-node.m_vp.x)*dt*m_dyn_damping*element_mass_at_node/total_mass at node;
					// should be t* = dt/(h/c) not dt
					// put this into the accelerations as (avx-node.m_vp.x)*m_dyn_damping*element_mass_at_node
					// then it will be multiplied by dt and divided by m_inv_mass later 
					mass_at_node = this_element[j+1]*this_element[0];
					node.m_at.x += (avx-node.m_vp.x)*mass_at_node*m_dyn_damping;
					node.m_at.y += (avy-node.m_vp.y)*mass_at_node*m_dyn_damping;
					node.m_at.z += (avz-node.m_vp.z)*mass_at_node*m_dyn_damping;
				}
			}  // loop over elements
		}  // if (pbd)
	}  // loop over domains

	for (i=0; i<N; ++i)
	{
		FENode& node = mesh.Node(i);
		//  calculate acceleration using F=ma and update - note m_inv_mass is 1/m so multiply not divide
		n=m_R1[0];
		if ((n = node.m_ID[DOF_X]) >= 0) node.m_at.x = (node.m_at.x+m_R1[n])*m_inv_mass[n];
		if ((n = node.m_ID[DOF_Y]) >= 0) node.m_at.y = (node.m_at.y+m_R1[n])*m_inv_mass[n];
		if ((n = node.m_ID[DOF_Z]) >= 0) node.m_at.z = (node.m_at.z+m_R1[n])*m_inv_mass[n];
		// and update the velocities using the accelerations
		// which are added to the previously calculated velocity changes from damping
		node.m_vt = node.m_vp + node.m_at*dt;	//  update velocity using acceleration m_at
		//	calculate incremental displacement using the velocity
		if ((n = node.m_ID[DOF_X]) >= 0) m_ui[n] = node.m_vt.x*dt;
		if ((n = node.m_ID[DOF_Y]) >= 0) m_ui[n] = node.m_vt.y*dt;
		if ((n = node.m_ID[DOF_Z]) >= 0) m_ui[n] = node.m_vt.z*dt;
	}

	// need to update everything for the explicit solver
	// Update geometry
	Update(m_ui);

	// calculate new residual at this point - which will be used on the next step to find the acceleration
	Residual(m_R1);

	// update total displacements
	int neq = m_Ui.size();
	for (i=0; i<neq; ++i) m_Ui[i] += m_ui[i];

	// increase iteration number
	m_niter++;

	// let's flush the logfile to make sure the last output will not get lost
	felog.flush();

	// do minor iterations callbacks
	m_fem.DoCallback(CB_MINOR_ITERS);

	// when converged, 
	// print a convergence summary to the felog file
	Logfile::MODE mode = felog.SetMode(Logfile::FILE_ONLY);
	if (mode != Logfile::NEVER)
	{
		felog.printf("\nconvergence summary\n");
		felog.printf("    number of iterations   : %d\n", m_niter);
		felog.printf("    number of reformations : %d\n", m_nref);
	}
	felog.SetMode(mode);

	// if converged we update the total displacements
	m_Ut += m_Ui;

	return true;
}

//-----------------------------------------------------------------------------
//! calculates the residual vector
//! Note that the concentrated nodal forces are not calculated here.
//! This is because they do not depend on the geometry 
//! so we only calculate them once (in Quasin) and then add them here.

bool FEExplicitSolidSolver::Residual(vector<double>& R)
{
	int i;
	// initialize residual with concentrated nodal loads
	R = m_Fn;

	// zero nodal reaction forces
	zero(m_Fr);

	// setup the global vector
	FEGlobalVector RHS(GetFEModel(), R, m_Fr);

	// zero rigid body reaction forces
	int NRB = m_fem.Objects();
	for (i=0; i<NRB; ++i)
	{
		FERigidBody& RB = static_cast<FERigidBody&>(*m_fem.Object(i));
		RB.m_Fr = RB.m_Mr = vec3d(0,0,0);
	}

	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

	// calculate the internal (stress) forces
	for (i=0; i<mesh.Domains(); ++i)
	{
		FEElasticDomain& dom = dynamic_cast<FEElasticDomain&>(mesh.Domain(i));
		dom.InternalForces(RHS);
	}

	// update body forces
	for (i=0; i<m_fem.BodyLoads(); ++i)
	{
		// TODO: I don't like this but for now I'll hard-code the modification of the
		//       force center position
		FEPointBodyForce* pbf = dynamic_cast<FEPointBodyForce*>(m_fem.GetBodyLoad(i));
		if (pbf)
		{
			if (pbf->m_rlc[0] >= 0) pbf->m_rc.x = m_fem.GetLoadCurve(pbf->m_rlc[0])->Value();
			if (pbf->m_rlc[1] >= 0) pbf->m_rc.y = m_fem.GetLoadCurve(pbf->m_rlc[1])->Value();
			if (pbf->m_rlc[2] >= 0) pbf->m_rc.z = m_fem.GetLoadCurve(pbf->m_rlc[2])->Value();
		}
	}

	// calculate the body forces
	for (i=0; i<mesh.Domains(); ++i)
	{
		FEElasticDomain& dom = dynamic_cast<FEElasticDomain&>(mesh.Domain(i));
		for (int j=0; j<m_fem.BodyLoads(); ++j)
		{
			FEBodyForce* pbf = dynamic_cast<FEBodyForce*>(m_fem.GetBodyLoad(j));
			if (pbf) dom.BodyForce(RHS, *pbf);
		}
	}

	// calculate inertial forces for dynamic problems
	if (m_fem.GetCurrentStep()->m_nanalysis == FE_DYNAMIC) InertialForces(RHS);

	// calculate forces due to surface loads
	int nsl = m_fem.SurfaceLoads();
	for (i=0; i<nsl; ++i)
	{
		FESurfaceLoad* psl = m_fem.SurfaceLoad(i);
		if (psl->IsActive()) psl->Residual(RHS);
	}

	// calculate contact forces
	if (m_fem.SurfacePairInteractions() > 0)
	{
		ContactForces(RHS);
	}

	// get the time information
	FETimePoint tp = m_fem.GetTime();

	// calculate nonlinear constraint forces
	// note that these are the linear constraints
	// enforced using the augmented lagrangian
	NonLinearConstraintForces(RHS, tp);

	// forces due to point constraints
//	for (i=0; i<(int) fem.m_PC.size(); ++i) fem.m_PC[i]->Residual(this, R);

	// set the nodal reaction forces
	// TODO: Is this a good place to do this?
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		node.m_Fr = vec3d(0,0,0);

		int n;
		if ((n = -node.m_ID[DOF_X]-2) >= 0) node.m_Fr.x = -m_Fr[n];
		if ((n = -node.m_ID[DOF_Y]-2) >= 0) node.m_Fr.y = -m_Fr[n];
		if ((n = -node.m_ID[DOF_Z]-2) >= 0) node.m_Fr.z = -m_Fr[n];
	}

	// increase RHS counter
	m_nrhs++;

	return true;
}

//-----------------------------------------------------------------------------
//! Calculates the contact forces
void FEExplicitSolidSolver::ContactForces(FEGlobalVector& R)
{
	for (int i=0; i<m_fem.SurfacePairInteractions(); ++i)
	{
		FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));
		pci->ContactForces(R);
	}
}


//-----------------------------------------------------------------------------
//! calculate the nonlinear constraint forces 
void FEExplicitSolidSolver::NonLinearConstraintForces(FEGlobalVector& R, const FETimePoint& tp)
{
	int N = m_fem.NonlinearConstraints();
	for (int i=0; i<N; ++i) 
	{
		FENLConstraint* plc = m_fem.NonlinearConstraint(i);
		if (plc->IsActive()) plc->Residual(R, tp);
	}
}

//-----------------------------------------------------------------------------
//! This function calculates the inertial forces for dynamic problems

void FEExplicitSolidSolver::InertialForces(FEGlobalVector& R)
{
	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();
    FEModel& fem = GetFEModel();

	// allocate F
	vector<double> F(3*mesh.Nodes());
	zero(F);

	// calculate F
	double dt = m_fem.GetCurrentStep()->m_dt;
	double a = 4.0 / dt;
	double b = a / dt;
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		vec3d& rt = node.m_rt;
		vec3d& rp = node.m_rp;
		vec3d& vp = node.m_vp;
		vec3d& ap = node.m_ap;

		F[3*i  ] = b*(rt.x - rp.x) - a*vp.x - ap.x;
		F[3*i+1] = b*(rt.y - rp.y) - a*vp.y - ap.y;
		F[3*i+2] = b*(rt.z - rp.z) - a*vp.z - ap.z;
	}

	// now multiply F with the mass matrix
	matrix ke;
	for (int nd = 0; nd < mesh.Domains(); ++nd)
	{
		FEElasticDomain& dom = dynamic_cast<FEElasticDomain&>(mesh.Domain(nd));
		dom.InertialForces(R, F);
	}
}
