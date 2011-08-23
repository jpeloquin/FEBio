#pragma once
#include "FECore/FEMaterial.h"

//-----------------------------------------------------------------------------
//! Material point data for mixtures
//!
class FEElasticMixtureMaterialPoint : public FEMaterialPoint
{
public:
	FEElasticMixtureMaterialPoint() { m_pt = new FEElasticMaterialPoint; }
	FEMaterialPoint* Copy()
	{
		FEElasticMixtureMaterialPoint* pt = new FEElasticMixtureMaterialPoint;
		pt->m_w = m_w;
		if (m_pt) pt->m_pt = m_pt->Copy();
		return pt;
	}

	void Init(bool bflag)
	{
		if (bflag)
		{
			for (int i=0; i<(int) m_w.size(); ++i) m_w[i] = 1.0;
		}
	}

	void Serialize(DumpFile& ar)
	{
		if (ar.IsSaving())
		{
			ar << m_w;
		}
		else
		{
			ar >> m_w;
		}
	}

public:
	vector<double>	m_w;	//!< material weights
};

//-----------------------------------------------------------------------------
//! Elastic mixtures

//! This class describes a mixture of elastic solids.  The user must declare
//! elastic solids that can be combined within this class.  The stress and
//! tangent tensors evaluated in this class represent the sum of the respective
//! tensors of all the solids forming the mixture.

class FEElasticMixture : public FEElasticMaterial
{
public:
	FEElasticMixture() {}

	// returns a pointer to a new material point object
	virtual FEMaterialPoint* CreateMaterialPointData() 
	{ 
		FEElasticMixtureMaterialPoint* pt = new FEElasticMixtureMaterialPoint();
		pt->m_w.resize(m_pMat.size());
		return pt;
	}
	
public:
	vector <FEElasticMaterial*>	m_pMat;	//!< pointers to elastic materials
		
public:
	//! calculate stress at material point
	virtual mat3ds Stress(FEMaterialPoint& pt);
		
	//! calculate tangent stiffness at material point
	virtual tens4ds Tangent(FEMaterialPoint& pt);
		
	//! data initialization and checking
	void Init();
		
	//! return bulk modulus
	double BulkModulus();
		
	// declare as registered
	DECLARE_REGISTERED(FEElasticMixture);
};
