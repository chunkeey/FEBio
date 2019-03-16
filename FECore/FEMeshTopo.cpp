#include "stdafx.h"
#include "FEMeshTopo.h"
#include "FEElementList.h"
#include "FEMesh.h"
#include "FEDomain.h"
#include "FEElemElemList.h"

class FEMeshTopo::MeshTopoImp
{
public:
	FEMesh*				m_mesh;			// the mesh
	FEEdgeList			m_edgeList;		// the edge list
	FEElementEdgeList	m_EEL;			// the element-edge list
	FEFaceList			m_faceList;		// the face list (all faces)
	FEElementFaceList	m_EFL;			// the element-face list
	FEElemElemList		m_ENL;			// the element neighbor list
	FEFaceList			m_surface;		// only surface facets
	FEElementFaceList	m_ESL;			// element-surface facet list
	FEFaceEdgeList		m_FEL;			// face-edge list
	std::vector<FEElement*>	m_elem;		// element list
};

FEMeshTopo::FEMeshTopo() : imp(new FEMeshTopo::MeshTopoImp)
{

}

FEMeshTopo::~FEMeshTopo()
{
	delete imp;
}

bool FEMeshTopo::Create(FEMesh* mesh)
{
	imp->m_mesh = mesh;
	FEElementList elemList(*mesh);

	// create a vector of all elements
	int NEL = mesh->Elements();
	imp->m_elem.resize(NEL);
	NEL = 0;
	for (int i = 0; i < mesh->Domains(); ++i)
	{
		FEDomain& dom = mesh->Domain(i);
		int nel = dom.Elements();
		for (int j = 0; j < nel; ++j)
		{
			imp->m_elem[NEL++] = &dom.ElementRef(j);
		}
	}

	// create the element neighbor list
	if (imp->m_ENL.Create(mesh) == false) return false;

	// create a face list
	if (imp->m_faceList.Create(*mesh, imp->m_ENL) == false) return false;

	// extract the surface facets
	imp->m_surface = imp->m_faceList.GetSurface();

	// create the element-face list
	if (imp->m_EFL.Create(elemList, imp->m_faceList) == false) return false;

	// create the element-surface facet list
	if (imp->m_ESL.Create(elemList, imp->m_surface) == false) return false;
	imp->m_surface.BuildNeighbors();

	// create the edge list (from the face list)
	if (imp->m_edgeList.Create(mesh) == false) return false;

	// create the element-edge list
	if (imp->m_EEL.Create(elemList, imp->m_edgeList) == false) return false;

	// create the face-edge list
	if (imp->m_FEL.Create(imp->m_faceList, imp->m_edgeList) == false) return false;

	return true;
}

// return elements
int FEMeshTopo::Elements()
{
	return (int)imp->m_elem.size();
}

// return an element
FEElement* FEMeshTopo::Element(int i)
{
	return imp->m_elem[i];
}

int FEMeshTopo::Faces()
{
	return imp->m_faceList.Faces();
}

// return a face
const FEFaceList::FACE& FEMeshTopo::Face(int i)
{
	return imp->m_faceList[i];
}

// return the element-face list
const std::vector<int>& FEMeshTopo::ElementFaceList(int nelem)
{
	return imp->m_EFL.FaceList(nelem);
}

// return the number of edges in the mesh
int FEMeshTopo::Edges()
{
	return imp->m_edgeList.Edges();
}

// return a edge
const FEEdgeList::EDGE& FEMeshTopo::Edge(int i)
{
	return imp->m_edgeList[i];
}

// return the face-edge list
const std::vector<int>& FEMeshTopo::FaceEdgeList(int nface)
{
	return imp->m_FEL.EdgeList(nface);
}

// return the element-edge list
const std::vector<int>& FEMeshTopo::ElementEdgeList(int nelem)
{
	return imp->m_EEL.EdgeList(nelem);
}
