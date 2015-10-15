#pragma once
#include "FECore/LinearSolver.h"
#include "CompactMatrix.h"

//-----------------------------------------------------------------------------
//! This class implements an interface to the MKL FGMRES iterative solver for 
//! nonsymmetric indefinite matrices (without pre-conditioning).
class FGMRESSolver : public LinearSolver
{
public:
	//! constructor
	FGMRESSolver();

	//! do any pre-processing (does nothing for iterative solvers)
	bool PreProcess() { return true; }

	//! Factor the matrix (does nothing for iterative solvers)
	bool Factor() { return true; }

	//! Calculate the solution of RHS b and store solution in x
	bool BackSolve(vector<double>& x, vector<double>& b);

	//! Clean up
	void Destroy() {}

	//! Return a sparse matrix compatible with this solver
	SparseMatrix* CreateSparseMatrix(Matrix_Type ntype);

private:
	CompactUnSymmMatrix*	m_pA;		//!< the sparse matrix format
};

//-----------------------------------------------------------------------------
//! This class implements an interface to the MKL FGMRES iterative solver with
//! LUT pre-conditioner for nonsymmetric indefinite matrices.
class FGMRES_LUT_Solver : public LinearSolver
{
public:
	//! constructor
	FGMRES_LUT_Solver();

	//! do any pre-processing (does nothing for iterative solvers)
	bool PreProcess() { return true; }

	//! Factor the matrix (does nothing for iterative solvers)
	bool Factor() { return true; }

	//! Calculate the solution of RHS b and store solution in x
	bool BackSolve(vector<double>& x, vector<double>& b);

	//! Clean up
	void Destroy() {}

	//! Return a sparse matrix compatible with this solver
	SparseMatrix* CreateSparseMatrix(Matrix_Type ntype);

private:
	CompactUnSymmMatrix*	m_pA;		//!< the sparse matrix format
};
