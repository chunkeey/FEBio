#pragma once
#include "FEContactInterface.h"
#include "FEContactSurface.h"

// Elastic sliding contact, reducing the algorithm of biphasic sliding contact
// (FESlidingInterface2) to elastic case.  The algorithm derives from Bonet
// & Wood's treatment of surface pressures

//-----------------------------------------------------------------------------
class FESlidingSurfaceBW : public FEContactSurface
{
public:
	// data for each integration point
	class Data
	{
	public:
		Data();

	public:
		double	m_gap;		//!< gap function
		double	m_Lmd;		//!< Lagrange multipliers for displacements
		double	m_Ln;		//!< net contact pressure
		double	m_epsn;		//!< penalty factor
		vec3d	m_nu;		//!< local normal
		vec2d	m_rs;		//!< natural coordinates of this integration point
		FESurfaceElement*	m_pme;	//!< projected master element
	};

public:
	//! constructor
	FESlidingSurfaceBW(FEModel* pfem);
	
	//! initialization
	bool Init();
	
	void Serialize(DumpStream& ar);

	//! evaluate net contact force
	vec3d GetContactForce();

	//! evaluate net contact area
	double GetContactArea();
    
public:
    void GetContactGap     (int nface, double& pg);
    void GetContactPressure(int nface, double& pg);
    void GetContactTraction(int nface, vec3d& pt);
	void GetNodalContactGap     (int nface, double* pg);
	void GetNodalContactPressure(int nface, double* pg);
	void GetNodalContactTraction(int nface, vec3d* pt);
	
protected:
	FEModel*	m_pfem;
	
public:
	vector< vector<Data> >	m_Data;		//!< integration point data for all elements
    
    vec3d	m_Ft;	//!< total contact force (from equivalent nodal forces)
};

//-----------------------------------------------------------------------------
class FESlidingInterfaceBW : public FEContactInterface
{
public:
	//! constructor
	FESlidingInterfaceBW(FEModel* pfem);
	
	//! destructor
	~FESlidingInterfaceBW();
	
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
	
	//! calculate contact pressures for file output
	void UpdateContactPressures();
	
	//! calculate Lagrangian augmentations
	bool Augment(int naug);
	
	//! serialize data to archive
	void Serialize(DumpStream& ar);

	//! return the master and slave surface
	FESurface* GetMasterSurface() { return &m_ms; }
	FESurface* GetSlaveSurface () { return &m_ss; }

	//! return integration rule class
	bool UseNodalIntegration() { return false; }

	//! build the matrix profile for use in the stiffness matrix
	void BuildMatrixProfile(FEGlobalMatrix& K);

protected:
	void ProjectSurface(FESlidingSurfaceBW& ss, FESlidingSurfaceBW& ms, bool bupseg, bool bmove = false);
	
	//! calculate penalty factor
	void CalcAutoPenalty(FESlidingSurfaceBW& s);
	
public:
	FESlidingSurfaceBW	m_ms;	//!< master surface
	FESlidingSurfaceBW	m_ss;	//!< slave surface
	
	int				m_knmult;		//!< higher order stiffness multiplier
	bool			m_btwo_pass;	//!< two-pass flag
	double			m_atol;			//!< augmentation tolerance
	double			m_gtol;			//!< gap tolerance
	double			m_stol;			//!< search tolerance
	bool			m_bsymm;		//!< use symmetric stiffness components only
	double			m_srad;			//!< contact search radius
	int				m_naugmax;		//!< maximum nr of augmentations
	int				m_naugmin;		//!< minimum nr of augmentations
	int				m_nsegup;		//!< segment update parameter
	bool			m_breloc;		//!< node relocation on activation
    bool            m_bsmaug;       //!< smooth augmentation
	
	double			m_epsn;			//!< normal penalty factor
	bool			m_bautopen;		//!< use autopenalty factor
	
	bool			m_btension;		//!< allow tension across interface
	
	DECLARE_PARAMETER_LIST();
};
