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
#include "MixedLinearSolver.h"
#include "PardisoSolver.h"
#include "FGMRES_ILU0_Solver.h"
#include <FECore/FEModel.h>
#include <FECore/FEAnalysis.h>

MixedLinearSolver::MixedLinearSolver(FEModel* fem) : LinearSolver(fem)
{
	m_strategy = (int)DIRECT_SOLVER; // start with direct solver

	m_solver[0] = new PardisoSolver(fem);
	m_solver[1] = m_fgmres = new FGMRES_ILU0_Solver(fem);
}

MixedLinearSolver::~MixedLinearSolver()
{
	delete m_solver[0];
	delete m_solver[1];
}

void MixedLinearSolver::SetSolverStrategy(MixedLinearSolver::Strategy n)
{
	if (m_strategy != n)
	{
		fprintf(stderr, "Mixed solver: switching to strategy %s\n", (n == 0 ? "direct" : "iterative"));
		m_strategy = (int)n;
	}
}

void MixedLinearSolver::SetMaxIterations(int nmax)
{
	m_fgmres->SetMaxIterations(nmax);
}

void MixedLinearSolver::SetPrintLevel(int n)
{
	m_fgmres->SetPrintLevel(n);
}

void MixedLinearSolver::SetRelativeConvergence(double tol)
{
	m_fgmres->SetRelativeResidualTolerance(tol);
}

void MixedLinearSolver::SetAbsoluteConvergence(double tol)
{
	m_fgmres->SetAbsoluteResidualTolerance(tol);
}

SparseMatrix* MixedLinearSolver::CreateSparseMatrix(Matrix_Type ntype)
{
	if (ntype != REAL_UNSYMMETRIC) return nullptr;
	m_A = new CRSSparseMatrix(1);

	if (m_solver[0]->SetSparseMatrix(m_A) == false) return nullptr;
	if (m_solver[1]->SetSparseMatrix(m_A) == false) return nullptr;

	return m_A;
}

bool MixedLinearSolver::SetSparseMatrix(SparseMatrix* pA)
{
	m_A = dynamic_cast<CRSSparseMatrix*>(pA);
	if (m_A == nullptr) return false;
	if (m_A->Offset() != 1) return false;

	if (m_solver[0]->SetSparseMatrix(m_A) == false) return false;
	if (m_solver[1]->SetSparseMatrix(m_A) == false) return false;

	return true;
}

bool MixedLinearSolver::PreProcess()
{
	// pre-process the solver
	if (m_solver[0]->PreProcess() == false) return false;
	if (m_solver[1]->PreProcess() == false) return false;
	return true;
}

bool MixedLinearSolver::Factor()
{
	// Currently, we only use the direct solver for the first time step.
	FEModel* fem = GetFEModel();
	FEAnalysis* step = fem->GetCurrentStep();
	if (step->m_ntimesteps == 0) SetSolverStrategy(DIRECT_SOLVER);
//	if (step->GetFESolver()->m_niter == 0) SetSolverStrategy(DIRECT_SOLVER);
	else SetSolverStrategy(ITERATIVE_SOLVER);

	return currentSolver()->Factor();
}

bool MixedLinearSolver::BackSolve(double* x, double* y)
{
	// Currently, we only use the direct solver for the first time step.
	FEModel* fem = GetFEModel();
	FEAnalysis* step = fem->GetCurrentStep();
	if ((step->m_ntimesteps >  0) && (m_strategy == DIRECT_SOLVER))
//	if ((step->GetFESolver()->m_niter >  0) && (m_strategy == DIRECT_SOLVER))
	{
		SetSolverStrategy(ITERATIVE_SOLVER);
		if (currentSolver()->Factor() == false) return false;
	}

	return currentSolver()->BackSolve(x, y);
}

void MixedLinearSolver::Destroy()
{
	return currentSolver()->Destroy();
}
