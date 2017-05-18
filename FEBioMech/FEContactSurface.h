#pragma once

#include "FECore/FESurface.h"
#include "FECore/vec2d.h"

//-----------------------------------------------------------------------------
//! This class describes a contact slave or master surface

//!	this class is used in contact analyses to describe a contacting
//! surface in a contact interface.

class FEContactSurface : public FESurface
{
public:
	//! constructor
	FEContactSurface(FEModel* pfem);

	//! destructor
	~FEContactSurface();

	// initialization
	bool Init();

	//! Set the sibling of this contact surface
	void SetSibling(FEContactSurface* ps);

	//! Unpack surface element data
	void UnpackLM(FEElement& el, vector<int>& lm);

public:
    virtual void GetContactGap     (int nface, double& pg);
    virtual void GetVectorGap      (int nface, vec3d& pg);
    virtual void GetContactPressure(int nface, double& pg);
    virtual void GetContactTraction(int nface, vec3d& pt);
    
	virtual void GetNodalContactGap     (int nface, double* pg);
    virtual void GetNodalVectorGap      (int nface, vec3d* pg);
	virtual void GetNodalContactPressure(int nface, double* pg);
	virtual void GetNodalContactTraction(int nface, vec3d* pt);

    void GetSurfaceTraction(int nface, vec3d& pt);
    void GetNodalSurfaceTraction(int nface, vec3d* pt);
    void GetGPSurfaceTraction(int nface, vec3d* pt);

	virtual vec3d GetContactForce();
    virtual double GetContactArea();

	FEModel* GetFEModel() { return m_pfem; }

protected:
	FEContactSurface* m_pSibling;
	FEModel*	m_pfem;

	int	m_dofX;
	int	m_dofY;
	int	m_dofZ;
};
