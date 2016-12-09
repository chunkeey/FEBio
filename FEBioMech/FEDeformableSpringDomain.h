#pragma once
#include <FECore/FEDiscreteDomain.h>
#include "FEElasticDomain.h"
#include "FESpringMaterial.h"

//-----------------------------------------------------------------------------
//! domain for deformable springs
class FEDeformableSpringDomain : public FEDiscreteDomain, public FEElasticDomain
{
public:
	//! constructor
	FEDeformableSpringDomain(FEModel* pfem);

	//! Unpack LM data
	void UnpackLM(FEElement& el, vector<int>& lm);

	//! get the material (overridden from FEDomain)
	FEMaterial* GetMaterial() { return m_pMat; }

	//! set the material
	void SetMaterial(FEMaterial* pmat);

	void Activate();

public: // overridden from FEElasticDomain
	//! build the matrix profile
	void BuildMatrixProfile(FEGlobalMatrix& K);

	//! calculate stiffness matrix
	void StiffnessMatrix(FESolver* psolver);
	void MassMatrix(FESolver* psolver, double scale) {}
	void BodyForceStiffness(FESolver* psolver, FEBodyForce& bf) {}

	//! Calculates inertial forces for dynamic problems | todo implement (removed assert DSR)
	void InertialForces(FEGlobalVector& R, vector<double>& F) { }

	//! update domain data
	void Update(const FETimeInfo& tp){}

	//! internal stress forces
	void InternalForces(FEGlobalVector& R);

	//! calculate bodyforces (not used since springs are considered mass-less)
	void BodyForce(FEGlobalVector& R, FEBodyForce& bf) {}

protected:
	double InitialLength();
	double CurrentLength();

protected:
	FESpringMaterial*	m_pMat;
	double				m_kbend;	// bending stiffness
	double				m_kstab;	// stabilization penalty
	double				m_L0;	//!< initial spring length

	DECLARE_PARAMETER_LIST();
};

//-----------------------------------------------------------------------------
//! domain for deformable springs
//! This approach assumes that the nodes are distributed evenly between anchor 
//! points. An anchor is a point that is constrained (e.g. prescribed, or in contact).
class FEDeformableSpringDomain2 : public FEDiscreteDomain, public FEElasticDomain
{
	struct NodeData
	{
		bool	banchor;
	};

	struct Wire
	{
		int	node[2];
	};

public:
	//! constructor
	FEDeformableSpringDomain2(FEModel* pfem);

	//! Unpack LM data
	void UnpackLM(FEElement& el, vector<int>& lm);

	//! get the material (overridden from FEDomain)
	FEMaterial* GetMaterial() { return m_pMat; }

	//! set the material
	void SetMaterial(FEMaterial* pmat);

	//! initialization
	bool Initialize();

	//! activation
	void Activate();

public:
	//! Set the position of a node
	void SetNodePosition(int node, const vec3d& r);

	//! Anchor (or release) a node
	void AnchorNode(int node, bool banchor);

	//! see if a node is anchored
	bool IsAnchored(int node) { return m_nodeData[node].banchor; }

	//! Update the position of all the nodes
	void UpdateNodes();

	//! Get the net force on this node
	vec3d NodalForce(int node);

	//! get net spring force
	double SpringForce();

	//! tangent
	vec3d Tangent(int node);

public: // overridden from FEElasticDomain

	//! calculate stiffness matrix
	void StiffnessMatrix(FESolver* psolver);
	void MassMatrix(FESolver* psolver, double scale) {}
	void BodyForceStiffness(FESolver* psolver, FEBodyForce& bf) {}

	//! Calculates inertial forces for dynamic problems | todo implement (removed assert DSR)
	void InertialForces(FEGlobalVector& R, vector<double>& F) { }

	//! update domain data
	void Update(const FETimeInfo& tp);

	//! internal stress forces
	void InternalForces(FEGlobalVector& R);

	//! calculate bodyforces (not used since springs are considered mass-less)
	void BodyForce(FEGlobalVector& R, FEBodyForce& bf) {}

protected:
	double InitialLength();
	double CurrentLength();

protected:
	FESpringMaterial*	m_pMat;
	double				m_L0;	//!< initial wire length
	double				m_Lt;	//!< current wire length
	vector<NodeData>	m_nodeData;
	vector<Wire>		m_wire;
};