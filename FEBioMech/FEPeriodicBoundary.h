#pragma once

#include "FEContactInterface.h"
#include "FEContactSurface.h"

//-----------------------------------------------------------------------------
class FEPeriodicSurface : public FEContactSurface
{
public:
	//! constructor
	FEPeriodicSurface(FEModel* pfem) : FEContactSurface(pfem) {}

	//! initializes data
	bool Init();

	//! copy data
	void CopyFrom(FEPeriodicSurface& s);

	//! calculates the center of mass of the surface
	vec3d CenterOfMass();

	void Serialize(DumpStream& ar);

public:
    void GetContactGap     (int nface, double& pg);
    void GetContactPressure(int nface, double& pg);
    void GetContactTraction(int nface, vec3d& pt);
	void GetNodalContactGap     (int nface, double* pg);
	void GetNodalContactPressure(int nface, double* pg);
	void GetNodalContactTraction(int nface, vec3d* pt);

public:
	vector<vec3d>				m_gap;	//!< gap function at nodes
	vector<FESurfaceElement*>	m_pme;	//!< master element a slave node penetrates
	vector<vec2d>				m_rs;	//!< natural coordinates of slave projection on master element
	vector<vec3d>				m_Lm;	//!< Lagrange multipliers
	vector<vec3d>				m_Tn;	//!< nodal traction forces
	vector<vec3d>				m_Fr;	//!< reaction forces
};

//-----------------------------------------------------------------------------

class FEPeriodicBoundary : public FEContactInterface
{
public:
	//! constructor
	FEPeriodicBoundary(FEModel* pfem);

	//! destructor
	virtual ~FEPeriodicBoundary(void) {}

	//! initialization
	bool Init();

	//! interface activation
	void Activate();

	//! update
	void Update(int niter);

	//! calculate contact forces
	void ContactForces(FEGlobalVector& R);

	//! calculate contact stiffness
	void ContactStiffness(FESolver* psolver);

	//! calculate Lagrangian augmentations
	bool Augment(int naug);

	//! serialize data to archive
	void Serialize(DumpStream& ar);

	//! return the master and slave surface
	FESurface* GetMasterSurface() { return &m_ms; }
	FESurface* GetSlaveSurface () { return &m_ss; }

	//! return integration rule class
	bool UseNodalIntegration() { return true; }

	//! build the matrix profile for use in the stiffness matrix
	void BuildMatrixProfile(FEGlobalMatrix& K);

	//! create a copy of this interface
	void CopyFrom(FESurfacePairInteraction* pci);

protected:
	void ProjectSurface(FEPeriodicSurface& ss, FEPeriodicSurface& ms, bool bmove);

public:
	FEPeriodicSurface	m_ss;	//!< slave surface
	FEPeriodicSurface	m_ms;	//!< master surface

	double	m_atol;			//!< augmentation tolerance
	double	m_eps;			//!< penalty scale factor
	double	m_stol;			//!< search tolerance
	double  m_srad;			//!< search radius (%)
	bool	m_btwo_pass;	//!< two-pass flag
	int		m_naugmin;		//!< minimum number of augmentations
	vec3d	m_off;			//!< relative displacement offset

	DECLARE_PARAMETER_LIST();
};
