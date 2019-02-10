#include "SchurSolver.h"
#include "FGMRES_ILU0_Solver.h"
#include <FECore/SchurComplement.h>
#include "ILU0_Preconditioner.h"
#include "IncompleteCholesky.h"
#include "HypreGMRESsolver.h"
#include "RCICGSolver.h"
#include <FECore/FEModel.h>
#include <FECore/FESolidDomain.h>
#include <FECore/FEGlobalMatrix.h>
#include "PardisoSolver.h"

//-----------------------------------------------------------------------------
//! constructor
SchurSolver::SchurSolver(FEModel* fem) : LinearSolver(fem)
{
	// default parameters
	m_reltol = 1e-8;
	m_abstol = 0.0;
	m_maxiter = 0;
	m_printLevel = 0;
	m_bfailMaxIters = true;
	m_bzeroDBlock = false;
	m_Bk = 1.0;

	// default solution strategy
	m_nAsolver = 0;			// LU factorization (i.e. pardiso)
	m_nSchurSolver = 0;		// FGMRES, no precond.
	m_nSchurPreC = 0;		// no preconditioner
	m_iter = 0;

	// initialize pointers
	m_pK = nullptr;
	m_Asolver = nullptr;
	m_PS = nullptr;
	m_schurSolver = nullptr;
}

//-----------------------------------------------------------------------------
//! constructor
SchurSolver::~SchurSolver()
{
}

//-----------------------------------------------------------------------------
// get the iteration count
int SchurSolver::GetIterations() const
{
	return m_iter;
}

//-----------------------------------------------------------------------------
// set the print level
void SchurSolver::SetPrintLevel(int n)
{
	m_printLevel = n;
}

//-----------------------------------------------------------------------------
// set max nr of iterations
void SchurSolver::SetMaxIterations(int n)
{
	m_maxiter = n;
}

//-----------------------------------------------------------------------------
// set convergence tolerance
void SchurSolver::SetRelativeResidualTolerance(double tol)
{
	m_reltol = tol;
}

//-----------------------------------------------------------------------------
void SchurSolver::SetAbsoluteResidualTolerance(double tol)
{
	m_abstol = tol;
}

//-----------------------------------------------------------------------------
//! Set the partition
void SchurSolver::SetPartitions(const vector<int>& part)
{
	m_npart = part;
}

//-----------------------------------------------------------------------------
void SchurSolver::SetLinearSolver(int n)
{
	m_nAsolver = n;
}

//-----------------------------------------------------------------------------
void SchurSolver::SetSchurSolver(int n)
{
	m_nSchurSolver = n;
}

//-----------------------------------------------------------------------------
void SchurSolver::SetSchurPreconditioner(int n)
{
	m_nSchurPreC = n;
}

//-----------------------------------------------------------------------------
void SchurSolver::FailOnMaxIterations(bool b)
{
	m_bfailMaxIters = b;
}

//-----------------------------------------------------------------------------
void SchurSolver::ZeroDBlock(bool b)
{
	m_bzeroDBlock = b;
}

//-----------------------------------------------------------------------------
void SchurSolver::SetScaleFactor(double k)
{ 
	m_Bk = k; 
}

//-----------------------------------------------------------------------------
//! Create a sparse matrix
SparseMatrix* SchurSolver::CreateSparseMatrix(Matrix_Type ntype)
{
	if (m_npart.size() != 2) return 0;
	m_pK = new BlockMatrix();
	m_pK->Partition(m_npart, ntype, (m_nAsolver == Diagonal_Solver_HYPRE ? 0 : 1));
	return m_pK;
}

//-----------------------------------------------------------------------------
//! set the sparse matrix
bool SchurSolver::SetSparseMatrix(SparseMatrix* A)
{
	m_pK = dynamic_cast<BlockMatrix*>(A);
	if (m_pK == 0) return false;
	return true;
}

//-----------------------------------------------------------------------------
// allocate solver for A block
LinearSolver* SchurSolver::BuildASolver(int nsolver)
{
	switch (nsolver)
	{
	case Diagonal_Solver_LU:
	{
		return new PardisoSolver(GetFEModel());
	}
	break;
	case Diagonal_Solver_FGMRES:
	{
		FGMRES_ILU0_Solver* fgmres = new FGMRES_ILU0_Solver(GetFEModel());
		fgmres->SetMaxIterations(m_maxiter);
		fgmres->SetPrintLevel(m_printLevel == 3 ? 0 : m_printLevel);
		fgmres->SetRelativeResidualTolerance(m_reltol);
		fgmres->FailOnMaxIterations(false);

		// Get the A block
		BlockMatrix::BLOCK& A = m_pK->Block(0, 0);
		fgmres->GetPreconditioner()->SetSparseMatrix(A.pA);

		return fgmres;
	}
	break;
	case Diagonal_Solver_HYPRE:
	{
		HypreGMRESsolver* fgmres = new HypreGMRESsolver(GetFEModel());
		fgmres->SetMaxIterations(m_maxiter);
		fgmres->SetPrintLevel(m_printLevel == 3 ? 0 : m_printLevel);
		fgmres->SetConvergencTolerance(m_reltol);

		return fgmres;
	}
	break;
	case Diagonal_Solver_ILU0:
	{
		return new ILU0_Solver(GetFEModel());
	}
	break;
	case Diagonal_Solver_DIAGONAL:
	{
		PCSolver* solver = new PCSolver(GetFEModel());
		solver->SetPreconditioner(new DiagonalPreconditioner(GetFEModel()));
		return solver;
	}
	break;
	};

	return nullptr;
}

//-----------------------------------------------------------------------------
// allocate Schur complement solver
IterativeLinearSolver* SchurSolver::BuildSchurSolver(int nsolver)
{
	// Get the blocks
	BlockMatrix::BLOCK& A = m_pK->Block(0, 0);
	BlockMatrix::BLOCK& B = m_pK->Block(0, 1);
	BlockMatrix::BLOCK& C = m_pK->Block(1, 0);
	BlockMatrix::BLOCK& D = m_pK->Block(1, 1);

	switch (nsolver)
	{
	case Schur_Solver_FGMRES:
	{
		// build solver for Schur complement
		FGMRESSolver* fgmres = new FGMRESSolver(GetFEModel());
		fgmres->SetPrintLevel(m_printLevel == 3 ? 2 : m_printLevel);
		if (m_maxiter > 0) fgmres->SetMaxIterations(m_maxiter);
		fgmres->SetRelativeResidualTolerance(m_reltol);
		fgmres->SetAbsoluteResidualTolerance(m_abstol);
		fgmres->FailOnMaxIterations(m_bfailMaxIters);
		return fgmres;
	}
	break;
	case Schur_Solver_CG:
	{
		RCICG_ICHOL_Solver* cg = new RCICG_ICHOL_Solver(GetFEModel());
		cg->SetPrintLevel(m_printLevel == 3 ? 2 : m_printLevel);
		if (m_maxiter > 0) cg->SetMaxIterations(m_maxiter);
		cg->SetTolerance(m_reltol);
	}
	break;
	case Schur_Solver_PC:
	{
		return new PCSolver(GetFEModel());
	}
	break;
	default:
		assert(false);
	};

	return nullptr;
}

//-----------------------------------------------------------------------------
// allocate Schur complement solver
Preconditioner* SchurSolver::BuildSchurPreconditioner(int nopt)
{
	switch (nopt)
	{
	case Schur_PC_NONE:
		// no preconditioner selected
		return nullptr;
		break;
	case Schur_PC_DIAGONAL_MASS:
	{
		// diagonal mass matrix
		CompactSymmMatrix* M = new CompactSymmMatrix(1);
		if (BuildDiagonalMassMatrix(M) == false) return false;

		DiagonalPreconditioner* PS = new DiagonalPreconditioner(GetFEModel());
		PS->SetSparseMatrix(M);
		if (PS->Create() == false) return false;

		return PS;
	}
	break;
	case Schur_PC_ICHOL_MASS:
	{
		// mass matrix
		CompactSymmMatrix* M = new CompactSymmMatrix(1);
		if (BuildMassMatrix(M) == false) return false;

		// We do an incomplete cholesky factorization
		IncompleteCholesky* PS = new IncompleteCholesky(GetFEModel());
		PS->SetSparseMatrix(M);
		if (PS->Create() == false) return false;

		return PS;
	}
	break;
	default:
		assert(false);
	};

	return nullptr;
}

//-----------------------------------------------------------------------------
//! Preprocess 
// TODO: What if we get here again? Wouldn't that cause some problem like a memory leak?
bool SchurSolver::PreProcess()
{
	// make sure we have a matrix
	if (m_pK == 0) return false;

	// Get the blocks
	BlockMatrix::BLOCK& A = m_pK->Block(0, 0);
	BlockMatrix::BLOCK& B = m_pK->Block(0, 1);
	BlockMatrix::BLOCK& C = m_pK->Block(1, 0);
	BlockMatrix::BLOCK& D = m_pK->Block(1, 1);

	// get the number of partitions
	// and make sure we have two
	int NP = m_pK->Partitions();
	if (NP != 2) return false;

	// build solver for A block
	m_Asolver = BuildASolver(m_nAsolver);
	if (m_Asolver == nullptr) return false;

	m_Asolver->SetSparseMatrix(A.pA);
	if (m_Asolver->PreProcess() == false) return false;

	// build the solver for the schur complement
	m_schurSolver = BuildSchurSolver(m_nSchurSolver);
	if (m_schurSolver == nullptr) return false;

	if (m_nSchurSolver != Schur_Solver_PC)
	{
		SchurComplement* S = new SchurComplement(m_Asolver, B.pA, C.pA, (m_bzeroDBlock ? nullptr : D.pA));
		if (m_schurSolver->SetSparseMatrix(S) == false) { delete S;  return false; }
	}

	// build a preconditioner for the schur complement solver
	m_PS = BuildSchurPreconditioner(m_nSchurPreC);
	if (m_PS) m_schurSolver->SetPreconditioner(m_PS);

	if (m_schurSolver->PreProcess() == false) return false;

	// reset iteration counter
	m_iter = 0;

	return true;
}

//-----------------------------------------------------------------------------
//! Factor matrix
bool SchurSolver::Factor()
{
	// Get the blocks
	BlockMatrix::BLOCK& A = m_pK->Block(0, 0);
	BlockMatrix::BLOCK& B = m_pK->Block(0, 1);
	BlockMatrix::BLOCK& C = m_pK->Block(1, 0);
	BlockMatrix::BLOCK& D = m_pK->Block(1, 1);

	// Get the blocks
	CRSSparseMatrix* MA = dynamic_cast<CRSSparseMatrix*>(A.pA);
	CRSSparseMatrix* MB = dynamic_cast<CRSSparseMatrix*>(B.pA);
	CRSSparseMatrix* MC = dynamic_cast<CRSSparseMatrix*>(C.pA);
	CRSSparseMatrix* MD = dynamic_cast<CRSSparseMatrix*>(D.pA);

	// Scale B and D
	MB->scale(1.0 / m_Bk);
	if (MD) MD->scale(1.0 / m_Bk);

	// factor the A block solver
	if (m_Asolver->Factor() == false) return false;

	// factor the schur complement solver
	if (m_schurSolver->Factor() == false) return false;

	return true;
}

//-----------------------------------------------------------------------------
//! Backsolve the linear system
bool SchurSolver::BackSolve(double* x, double* b)
{
	// get the partition sizes
	int n0 = m_pK->PartitionEquations(0);
	int n1 = m_pK->PartitionEquations(1);

	// Get the blocks
	BlockMatrix::BLOCK& A = m_pK->Block(0, 0);
	BlockMatrix::BLOCK& B = m_pK->Block(0, 1);
	BlockMatrix::BLOCK& C = m_pK->Block(1, 0);
	BlockMatrix::BLOCK& D = m_pK->Block(1, 1);

	// split right hand side in two
	vector<double> F(n0), G(n1);
	for (int i = 0; i<n0; ++i) F[i] = b[i];
	for (int i = 0; i<n1; ++i) G[i] = b[i + n0];

	// solution vectors
	vector<double> u(n0, 0.0);
	vector<double> v(n1, 0.0);

	// step 1: solve Ay = F
	vector<double> y(n0);
	if (m_printLevel != 0) fprintf(stderr, "----------------------\nstep 1:\n");
	if (m_Asolver->BackSolve(y, F) == false) return false;

	// step 2: Solve Sv = H, where H = Cy - G
	if (m_printLevel != 0) fprintf(stderr, "step 2:\n");
	vector<double> H(n1);
	C.vmult(y, H);
	H -= G;
	if (m_schurSolver->BackSolve(v, H) == false) return false;

	// step 3: solve Au = L , where L = F - Bv
	if (m_printLevel != 0) fprintf(stderr, "step 3:\n");
	vector<double> tmp(n0);
	B.vmult(v, tmp);
	vector<double> L = F - tmp;
	if (m_Asolver->BackSolve(u, L) == false) return false;

	// put it back together
	for (int i = 0; i<n0; ++i) x[i] = u[i];
	for (int i = 0; i<n1; ++i) x[i + n0] = v[i];

	// scale degrees of second partition
	for (size_t i = n0; i < n0 + n1; ++i) x[i] /= m_Bk;

	return true;
}

//-----------------------------------------------------------------------------
//! Clean up
void SchurSolver::Destroy()
{
	if (m_Asolver) m_Asolver->Destroy();
	if (m_schurSolver) m_schurSolver->Destroy();
}

//-----------------------------------------------------------------------------
bool SchurSolver::BuildMassMatrix(CompactSymmMatrix* M, double scale)
{
	FEModel* fem = GetFEModel();
	FEMesh& mesh = fem->GetMesh();

	// get number of equations
	int N0 = m_pK->Block(0, 0).Rows();
	int N = m_pK->Block(1, 1).Rows();

	// this is the degree of freedom in the LM arrays that we need
	// TODO: This is hard coded for fluid problems
	const int edofs = 4; // nr of degrees per element node
	const int dof = 3;	// degree of freedom we need

	// build the global matrix
	vector<vector<int> > LM;
	vector<int> lm, lme;
	SparseMatrixProfile MP(N, N);
	MP.CreateDiagonal();
	int ND = mesh.Domains();
	for (int i = 0; i < ND; ++i)
	{
		FESolidDomain& dom = dynamic_cast<FESolidDomain&>(mesh.Domain(i));
		int NE = dom.Elements();
		for (int j = 0; j < NE; ++j)
		{
			FESolidElement& el = dom.Element(j);
			int neln = el.Nodes();
			// get the equation numbers
			dom.UnpackLM(el, lme);

			// we don't want equation numbers below N0
			lm.resize(neln);
			for (int i = 0; i < neln; ++i)
			{
				lm[i] = lme[i*edofs + dof];
				if (lm[i] >= 0) lm[i] -= N0;
				else if (lm[i] < -1) lm[i] = -lm[i]-2 - N0;
			}

			LM.push_back(lm);
			MP.UpdateProfile(LM, 1);
			LM.clear();
		}
	}
	M->Create(MP);
	M->Zero();

	// build the mass matrix
	matrix me;
	double density = 1.0;
	int n = 0;
	for (int i = 0; i < ND; ++i)
	{
		FESolidDomain& dom = dynamic_cast<FESolidDomain&>(mesh.Domain(i));
		int NE = dom.Elements();
		for (int j = 0; j < NE; ++j, ++n)
		{
			FESolidElement& el = dom.Element(j);

			// Get the current element's data
			const int nint = el.GaussPoints();
			const int neln = el.Nodes();
			const int ndof = neln;

			// weights at gauss points
			const double *gw = el.GaussWeights();

			matrix me(ndof, ndof);
			me.zero();

			// calculate element stiffness matrix
			for (int n = 0; n<nint; ++n)
			{
				FEMaterialPoint& mp = *el.GetMaterialPoint(n);
				double Dn = density;

				// shape functions
				double* H = el.H(n);

				// Jacobian
				double J0 = dom.detJ0(el, n)*gw[n];

				for (int i = 0; i<neln; ++i)
					for (int j = 0; j<neln; ++j)
					{
						double mab = Dn*H[i] * H[j] * J0;
						me[i][j] += mab*scale;
					}
			}

			// get the equation numbers
			dom.UnpackLM(el, lme);

			// we don't want equation numbers below N0
			lm.resize(neln);
			for (int i = 0; i < neln; ++i)
			{
				lm[i] = lme[i*edofs + dof];
				if (lm[i] >= 0) lm[i] -= N0;
				else if (lm[i] < -1) lm[i] = -lm[i]-2 - N0;
			}

			M->Assemble(me, lm);
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
bool SchurSolver::BuildDiagonalMassMatrix(CompactSymmMatrix* M, double scale)
{
	FEModel* fem = GetFEModel();
	FEMesh& mesh = fem->GetMesh();

	// get number of equations
	int N0 = m_pK->Block(0, 0).Rows();
	int N = m_pK->Block(1, 1).Rows();

	// build the global matrix
	SparseMatrixProfile MP(N, N);
	MP.CreateDiagonal();
	M->Create(MP);
	M->Zero();

	// build the mass matrix
	matrix me;
	double density = 1.0;
	int n = 0;
	for (int i = 0; i < mesh.Domains(); ++i)
	{
		FESolidDomain& dom = dynamic_cast<FESolidDomain&>(mesh.Domain(i));
		int NE = dom.Elements();
		for (int j = 0; j < NE; ++j, ++n)
		{
			FESolidElement& el = dom.Element(j);

			// Get the current element's data
			const int nint = el.GaussPoints();
			const int neln = el.Nodes();
			const int ndof = neln;

			// weights at gauss points
			const double *gw = el.GaussWeights();

			matrix me(ndof, ndof);
			me.zero();

			// calculate element stiffness matrix
			double Me = 0.0;
			for (int n = 0; n<nint; ++n)
			{
				FEMaterialPoint& mp = *el.GetMaterialPoint(n);
				double Dn = density;

				// Jacobian
				double Jw = dom.detJ0(el, n)*gw[n];

				Me += Dn*Jw;
			}

			int lm = el.m_lm - N0;
			M->set(lm, lm, Me);
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
PCSolver::PCSolver(FEModel* fem) : IterativeLinearSolver(fem)
{
	m_PC = nullptr;
}

void PCSolver::SetPreconditioner(Preconditioner* pc)
{
	m_PC = pc;
}

bool PCSolver::HasPreconditioner() const
{
	return (m_PC == nullptr ? false : true);
}

SparseMatrix* PCSolver::CreateSparseMatrix(Matrix_Type ntype)
{
	// This solver does not manage a matrix
	return nullptr;
}

bool PCSolver::SetSparseMatrix(SparseMatrix* A)
{
	if (m_PC == nullptr) return false;
	m_PC->SetSparseMatrix(A);
	return true;
}

bool PCSolver::PreProcess()
{
	if (m_PC == nullptr) return false;
	return true;
}

bool PCSolver::Factor()
{
	if (m_PC == nullptr) return false;
	return m_PC->Create();
}

bool PCSolver::BackSolve(double* x, double* b)
{
	if (m_PC == nullptr) return false;
	return m_PC->mult_vector(b, x);
}
