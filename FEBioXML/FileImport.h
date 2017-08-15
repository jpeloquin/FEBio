// FileImport.h: interface for the FileImport class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_FILEIMPORT_H__AC15F5F8_E069_4640_B3FD_077984EEA78F__INCLUDED_)
#define AFX_FILEIMPORT_H__AC15F5F8_E069_4640_B3FD_077984EEA78F__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <stdio.h>
#include <FECore/fecore_export.h>
#include "XMLReader.h"
#include <FECore/vec3d.h>
#include <FECore/mat3d.h>
#include <FECore/tens3d.h>
#include <FECore/FECoreBase.h>
#include "FEModelBuilder.h"
#include <map>
using namespace std;

//-----------------------------------------------------------------------------
// Forward declarations
class FEModel;
class FEFileImport;

//-----------------------------------------------------------------------------
// Base class for FEBio import exceptions
// Derived classes should set the error string in their constructor
class FEFileException
{
public:
	enum { MAX_ERR_STRING = 1024 };

	FEFileException();

	FEFileException(const char* sz, ...);

public:
	// retrieve the error string
	const char* GetErrorString() const { return m_szerr; }

protected:
	// set the error string (used by derived classes)
	void SetErrorString(const char* sz, ...);

protected:
	char	m_szerr[MAX_ERR_STRING];
};

//-------------------------------------------------------------------------
class FEFileParam
{
	enum { MAX_TAG = 128 };

public:
	FEFileParam() { m_szname[0] = 0; m_szval[0] = 0; }

public:
	char	m_szname[MAX_TAG];	// parameter name
	char	m_szval[MAX_TAG];	// parameter value
};

//-----------------------------------------------------------------------------
// Base class for XML sections parsers
class FECOREDLL_EXPORT FEFileSection
{
public:
	FEFileSection(FEFileImport* pim) { m_pim = pim; }

	virtual void Parse(XMLTag& tag) = 0;

	FEFileImport* GetFileReader() { return m_pim; }

	FEModel* GetFEModel();

	FEModelBuilder* GetBuilder();

public:
	//! read a nodal ID
	//! This assumes the node ID is defined via the "id" attribute
	int ReadNodeID(XMLTag& tag);

public:
	bool ReadParameter(XMLTag& tag, FEParameterList& pl, const char* szparam = 0);
	bool ReadParameter(XMLTag& tag, FECoreBase* pc, const char* szparam = 0);
	void ReadParameterList(XMLTag& tag, FEParameterList& pl);
	void ReadParameterList(XMLTag& tag, FECoreBase* pc);

public:
	const char* get_value_string(XMLTag& tag);
	void value(XMLTag& tag, int&    n);
	void value(XMLTag& tag, double& g);
	void value(XMLTag& tag, bool&   b);
	void value(XMLTag& tag, vec3d&  v);
	void value(XMLTag& tag, mat3d&  m);
	void value(XMLTag& tag, mat3ds& m);
	void value(XMLTag& tag, tens3drs& m);
	void value(XMLTag& tag, char* szstr);
	int value(XMLTag& tag, int* pi, int n);
	int value(XMLTag& tag, double* pf, int n);

private:
	FEFileImport*	m_pim;
};

//-----------------------------------------------------------------------------
// class that manages file section parsers
class FEFileSectionMap : public map<string, FEFileSection*>
{
public:
	~FEFileSectionMap();
	void Clear();

	void Parse(XMLTag& tag);
};

//-----------------------------------------------------------------------------
//! Base class for file import classes. 
//! FEBio import files are XML formatted files, where each major section (children of root) is represented
//! by an FEFileSection. 
//! This class also offers a simple error reporting mechanism and manages the FILE* pointer. 
//! This class also manages "xml parameters". This is a feature of FEBio files that allow users to use parameters
//! as values for xml tag. A parameter is defined by a name-value pair and referenced in the input file using the $(parameter_name) syntax.

class FECOREDLL_EXPORT FEFileImport
{
public:
	//! constructor
	FEFileImport();

	//! destructor
	virtual ~FEFileImport();

	//! open the file
	bool Load(FEModel& fem, const char* szfile);

	//! get the error message
	void GetErrorMessage(char* szerr);

	//! Get the current FE model that is being processed
	FEModel* GetFEModel();

	//! Get the model builder
	FEModelBuilder* GetBuilder();

	// return the file path
	const char* GetFilePath();

	// set file version
	void SetFileVerion(int nversion);

	// get file version
	int GetFileVersion() const;

public:
	// Add a file parameter
	void AddFileParameter(const char* szname, const char* szval);

	// find a file parameter
	FEFileParam* FindFileParameter(const char* sz);

	// clear all file parameters
	void ClearFileParams();

protected:
	//! open a file
	bool Open(const char* szfile, const char* szmode);

	//! This function will be overloaded in the derived classes
	virtual bool Parse(const char* szfile) = 0;

	//! close the file
	void Close();

	//! helper function for reporting errors
	bool errf(const char* szerr, ...);

protected:
	FILE*	m_fp;			//!< file pointer
	char	m_szfile[256];	//!< file name
	char	m_szerr[256];	//!< error message
	char	m_szpath[512];	//!< file path

protected:
	FEFileSectionMap	m_map;
	FEModel*			m_fem;
	FEModelBuilder*		m_builder;
	vector<FEFileParam>	m_Param;	// parameter list

private:
	int	m_nversion;	// version of file
};

#endif // !defined(AFX_FILEIMPORT_H__AC15F5F8_E069_4640_B3FD_077984EEA78F__INCLUDED_)
