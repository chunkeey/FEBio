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

#pragma once
#include "fecore_api.h"
#include "FEMeshAdaptor.h"
class FEModel;

class FEMeshTopo;
class FEFixedBC;
class FEPrescribedDOF;
class FESurfaceLoad;
class FESurfacePairConstraint;
class FESurface;

class FECORE_API FERefineMesh : public FEMeshAdaptor
{
public:
	FERefineMesh(FEModel* fem);

protected:
	bool BuildMeshTopo();

	void UpdateBCs();

private:
	void UpdateFixedBC(FEFixedBC& bc);
	void UpdatePrescribedBC(FEPrescribedDOF& bc);
	void UpdateSurfaceLoad(FESurfaceLoad& surfLoad);
	void UpdateContactInterface(FESurfacePairConstraint& ci);

	bool UpdateSurface(FESurface& surf);

protected:
	FEMeshTopo*	m_topo;
	int			m_N0;
	int			m_NC;
	int			m_NN;
	vector<int>	m_edgeList;	// list of edge flags to see whether the edge was split
	vector<int>	m_faceList;	// list of face flags to see whether the face was split
};
