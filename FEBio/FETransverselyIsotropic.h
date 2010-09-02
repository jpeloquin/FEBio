#pragma once

#include "FEUncoupledMaterial.h"

//-----------------------------------------------------------------------------
//! Base class for transversely isotropic materials.

//! This class was created to simplify the implementation of the TransIso Mooney-
//! Rivlin and Veronda-Westmann models.
//! This class only stores some data

// TODO: don't derive it from FEIncompressibleMaterial. Materials that are both
// incompressible and have a fiber distribution should derive from 
// FEIncompressibleMaterial and from a fiber material class. Or perhaps they should
// have a fiber (or material axis) class as a member.

class FETransverselyIsotropic : public FEUncoupledMaterial
{
public:
	//! constructor
	FETransverselyIsotropic() {}

public:
	FEFiberMaterial	m_fib;

	// declare parameter list
	DECLARE_PARAMETER_LIST();
};
