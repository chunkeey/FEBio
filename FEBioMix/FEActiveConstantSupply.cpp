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
#include "FEActiveConstantSupply.h"
#include "FEBioMech/FEElasticMaterial.h"

// define the material parameters
BEGIN_FECORE_CLASS(FEActiveConstantSupply, FEActiveMomentumSupply)
    ADD_PARAMETER(m_asupp, "supply");
END_FECORE_CLASS();

//-----------------------------------------------------------------------------
//! Constructor.
FEActiveConstantSupply::FEActiveConstantSupply(FEModel* pfem) : FEActiveMomentumSupply(pfem)
{
    m_asupp = 0;
}

//-----------------------------------------------------------------------------
//! Active momentum supply vector.
//! The momentum supply is oriented along the first material axis
vec3d FEActiveConstantSupply::ActiveSupply(FEMaterialPoint& mp)
{
    FEElasticMaterialPoint& et = *mp.ExtractData<FEElasticMaterialPoint>();

	// get the local coordinate systems
	mat3d Q = GetLocalCS(mp);

    // active momentum supply vector direction
    vec3d V(Q[0][0], Q[1][0], Q[2][0]);
    
    mat3d F = et.m_F;
    vec3d pw = (F*V)*m_asupp;

    return pw;
}

//-----------------------------------------------------------------------------
//! Tangent of permeability
vec3d FEActiveConstantSupply::Tangent_ActiveSupply_Strain(FEMaterialPoint &mp)
{
    return vec3d(0,0,0);
}
