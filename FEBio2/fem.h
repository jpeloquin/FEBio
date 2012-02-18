// fem.h: interface for the FEM class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _FEM_H_07012006_
#define _FEM_H_07012006_

#include "FEBioLib/FEBioModel.h"
#include "FECore/DumpFile.h"
#include "FECore/FEContactInterface.h"
#include "FEBioLib/DataStore.h"
#include "FEBioLib/Timer.h"
#include "FEBioPlot/PlotFile.h"
#include "FEBioLib/FEElasticMixture.h"
#include "FEBioLib/FEUncoupledElasticMixture.h"
#include "FEBioLib/FESolute.h"

#include <list>
using namespace std;

//-----------------------------------------------------------------------------
//! The Finite Element Model class. 

//! This class stores solver parameters, geometry data, material data, and 
//! other data that is needed to solve the FE problem.
//! FEBio is designed to solve finite element problems. All the finite element
//! data is collected here in this class. This class also provides
//! routines to initalize, input, output and update the FE data. Although this
//! class provides the main solve routine it does not really solve anything.
//! The actual solving is done by one of the classes derived from the FESolver class.

class FEM : public FEBioModel
{
public:
	//! constructor - sets default variables
	FEM();

	//! destructor
	virtual ~FEM();

	//! read the configuration file
	bool Configure(const char* szfile);

	//! Restart from restart point
	bool Restart(const char* szfile);

	//! Initializes data structures
	bool Init();

	//! Resets data structures
	bool Reset();

	//! Solves the problem
	bool Solve();

	//! Serialize the current state to/from restart file
	bool Serialize(DumpFile& ar);

	//! input data from file
	bool Input(const char* szfile);

	//! write to plot file
	void Write();

	//! write data to log file
	void WriteData();

	//! dump data to archive for restart
	void DumpData();

	//! Add data record
	void AddDataRecord(DataRecord* pd) { m_Data.AddRecord(pd); }

	//! Set the plot file
	void SetPlotFile(PlotFile* pplt) { m_plot = pplt; }

	// set the i/o files
	void SetInputFilename(const char* szfile);
	void SetLogFilename  (const char* szfile) { strcpy(m_szlog , szfile); }
	void SetPlotFilename (const char* szfile) { strcpy(m_szplot, szfile); }
	void SetDumpFilename (const char* szfile) { strcpy(m_szdump, szfile); }

	void SetPlotFileNameExtension(const char* szext);

	// Get the I/O files
	const char* GetInputFileName() { return m_szfile; }
	const char* GetLogfileName  () { return m_szlog;  }
	const char* GetPlotFileName () { return m_szplot; }

	//! get the file title
	const char* GetFileTitle() { return m_szfile_title; }

	//! return a pointer to the named variable
	double* FindParameter(const char* szname);

	//! return a pointer to the parameter variable
	double* ReturnParameter(FEParam* pp, const int index);
	
	//! return a pointer to the named variable in a solid mixture
	double* FindSolidMixtureParameter(const char* szvar, const int index, FEElasticMixture* pme);
	double* FindUncoupledSolidMixtureParameter(const char* szvar, const int index, FEUncoupledElasticMixture* pme);
	
	//! find a boundary condition from the ID
	FEBoundaryCondition* FindBC(int nid);

	//! Evaluate parameter list
	void EvaluateParameterList(FEParameterList& pl);
	void EvaluateMaterialParameters(FEMaterial* pm);

	//! check for user-interruption
	virtual void CheckInterruption();

public:
	virtual void PushState();
	virtual void PopState ();

protected:
	void ShallowCopy(FEM& fem);

protected:
	void SerializeMaterials   (DumpFile& ar);
	void SerializeAnalysisData(DumpFile& ar);
	void SerializeGeometry    (DumpFile& ar);
	void SerializeMesh        (DumpFile& ar);
	void SerializeContactData (DumpFile& ar);
	void SerializeBoundaryData(DumpFile& ar);
	void SerializeIOData      (DumpFile& ar);
	void SerializeLoadData    (DumpFile& ar);
	void SerializeConstants   (DumpFile& ar);
	void SerializeDataStore   (DumpFile& ar);

public:
	//{ --- Initialization routines ---

		//! Initialize rigid bodies
		bool CreateRigidBodies();

		//! Initialize poroelastic/biphasic and solute data
		bool InitPoroSolute();

		//! Initializes contact data
		bool InitContact();

		//! Initialize material data
		bool InitMaterials();

		//! Initialize mesh data
		bool InitMesh();
	//}

public:
	Timer	m_TotalTime;		//!< Create timer to track total running time
	bool	m_bInterruptable;	//!< true if this model can be interrupted with ctrl+c

public:
	// --- I/O-Data --- 
	//{
		PlotFile*	m_plot;		//!< the plot file
		DataStore	m_Data;		//!< the data store used for data logging
		
		bool		m_becho;			//!< echo input to logfile (TODO: Make this a command line option)

protected:
		// file names
		char*	m_szfile_title;			//!< master input file title 
		char	m_szfile[MAX_STRING];	//!< master input file name (= path + title)
		char	m_szplot[MAX_STRING];	//!< plot output file name
		char	m_szlog [MAX_STRING];	//!< log output file name
		char	m_szdump[MAX_STRING];	//!< dump file name
	//}
};

//-----------------------------------------------------------------------------
class FEBioProgress : public Progress
{
public:
	FEBioProgress(FEM& fem) : m_fem(fem) {}
	void SetProgress(double f);

protected:
	FEM&	m_fem;
};

#endif // _FEM_H_07012006_
