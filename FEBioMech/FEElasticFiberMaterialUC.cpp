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
#include "FEElasticFiberMaterialUC.h"

//-----------------------------------------------------------------------------
BEGIN_FECORE_CLASS(FEElasticFiberMaterialUC, FEUncoupledMaterial)
	ADD_PARAMETER(m_fiber, "fiber");
END_FECORE_CLASS();


//-----------------------------------------------------------------------------
FEElasticFiberMaterialUC::FEElasticFiberMaterialUC(FEModel* pfem) : FEUncoupledMaterial(pfem)
{
	// initialize the fiber vector
	m_fiber = vec3d(1, 0, 0);
}

// Get the fiber direction (in global coordinates) at a material point
vec3d FEElasticFiberMaterialUC::FiberVector(FEMaterialPoint& mp)
{
	// get the material coordinate system
	mat3d Q = GetLocalCS(mp);

	// get the fiber vector in local coordinates
	vec3d fiber = m_fiber.unitVector(mp);

	// convert to global coordinates
	vec3d a0 = Q*fiber;

	return a0;
}
