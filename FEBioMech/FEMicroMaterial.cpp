#include "stdafx.h"
#include "FEMicroMaterial.h"
#include "FECore/FEElemElemList.h"
#include "FECore/log.h"
#include "FESolidSolver.h"
#include "FEElasticSolidDomain.h"
#include "FECore/FEAnalysis.h"
#include "FEBioXML/FEBioImport.h"
#include "FEBioPlot/FEBioPlotFile.h"

//-----------------------------------------------------------------------------
FEMicroMaterialPoint::FEMicroMaterialPoint(FEMaterialPoint* mp) : FEMaterialPoint(mp)
{
	m_macro_energy = 0.;
	m_micro_energy = 0.;
	m_energy_diff = 0.;
	
	m_macro_energy_inc = 0.;
	m_micro_energy_inc = 0.;

	m_Ka.zero();
}

//-----------------------------------------------------------------------------
//! Initialize material point data
void FEMicroMaterialPoint::Init(bool bflag)
{
	FEMaterialPoint::Init(bflag);
}

//-----------------------------------------------------------------------------
//! create a shallow copy
FEMaterialPoint* FEMicroMaterialPoint::Copy()
{
	FEMicroMaterialPoint* pt = new FEMicroMaterialPoint(m_pNext?m_pNext->Copy():0);
	pt->m_Ka = m_Ka;
	return pt;
}

//-----------------------------------------------------------------------------
//! serialize material point data
void FEMicroMaterialPoint::Serialize(DumpFile& ar)
{
	if (ar.IsSaving())
	{
		ar << m_Ka;
	}
	else
	{
		ar >> m_Ka;
	}
}

//-----------------------------------------------------------------------------
//! stream material point data
void FEMicroMaterialPoint::ShallowCopy(DumpStream& dmp, bool bsave)
{
	if (bsave)
	{
		dmp << m_Ka;
	}
	else
	{
		dmp >> m_Ka;
	}
}

//=============================================================================

//-----------------------------------------------------------------------------
// define the material parameters
BEGIN_PARAMETER_LIST(FEMicroMaterial, FEElasticMaterial)
	ADD_PARAMETER(m_szrve    , FE_PARAM_STRING, "RVE"     );
	ADD_PARAMETER(m_szbc     , FE_PARAM_STRING, "bc_set"  );
	ADD_PARAMETER(m_bperiodic, FE_PARAM_BOOL  , "periodic");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
FEMicroMaterial::FEMicroMaterial(FEModel* pfem) : FEElasticMaterial(pfem)
{
	m_brve = false;

	// initialize parameters
	m_szrve[0] = 0;
	m_szbc[0] = 0;
	m_bperiodic = false;

	m_bb_x = 0.; m_bb_y = 0.; m_bb_z = 0.;
	m_num_ext_node = 0;
}

//-----------------------------------------------------------------------------
FEMicroMaterial::~FEMicroMaterial(void)
{
}

//-----------------------------------------------------------------------------
FEMaterialPoint* FEMicroMaterial::CreateMaterialPointData()
{
	return new FEMicroMaterialPoint(new FEElasticMaterialPoint);
}

//-----------------------------------------------------------------------------
void FEMicroMaterial::Init()
{
	// try to load the RVE model
	if (m_brve == false)
	{
		// load the RVE model
		FEBioImport fim;
		if (fim.Load(m_rve, m_szrve) == false)
		{
			throw MaterialError("An error occured trying to read the RVE model from file %s.", m_szrve);
		}

		// set the pardiso solver as default
		m_rve.m_nsolver = PARDISO_SOLVER;

		// make sure the RVE problem doesn't output anything to a plot file
		m_rve.GetCurrentStep()->SetPlotLevel(FE_PLOT_NEVER);

		// create the BC's for this RVE
		if (PrepRVE() == false) throw MaterialError("An error occurred preparing RVE model");

		// mark that we read and processed the RVE successfully
		m_brve = true;
	}
}

//-----------------------------------------------------------------------------
bool FEMicroMaterial::PrepRVE()
{
	// find all boundar nodes
	FindBoundaryNodes();

	if (m_bperiodic == false)
	{
		// prep displacement BC's
		if (PrepDisplacementBC() == false) return false;
	}
	else
	{
		// prep periodic BC's
		if (PrepPeriodicBC() == false) return false;
	}

	// the logfile is a shared resource between the master FEM and the RVE
	// in order not to corrupt the logfile we don't print anything for
	// the RVE problem.
	Logfile::MODE nmode = felog.GetMode();
	felog.SetMode(Logfile::NEVER);

	// initialize RVE
	if (m_rve.Init() == false) return false;

	// calculate intial RVE volume
	m_V0 = 0;
	double ve;
	int nint;
	double* w, J;
	FEMesh& m = m_rve.GetMesh();
	for (int k=0; k<m.Domains(); ++k)
	{
		FESolidDomain& dom = static_cast<FESolidDomain&>(m.Domain(k));
		for (int i=0; i<dom.Elements(); ++i)
		{
			FESolidElement& el = dom.Element(i);
			nint = el.GaussPoints();
			w = el.GaussWeights();
			ve = 0;
			for (int n=0; n<nint; ++n)
			{
				FEElasticMaterialPoint& pt = *el.GetMaterialPoint(n)->ExtractData<FEElasticMaterialPoint>();
				J = dom.detJt(el, n);

				ve += J*w[n];
			}
			m_V0 += ve;
		}
	}

	// reset the logfile mode
	felog.SetMode(nmode);

	return true;
}

//-----------------------------------------------------------------------------
void FEMicroMaterial::FindBoundaryNodes()
{
	// first we need to find all the boundary nodes
	FEMesh& m = m_rve.GetMesh();
	int N = m.Nodes();
	m_BN.assign(N, 0);

	// create the element-element list
	FEElemElemList EEL;
	EEL.Create(&m);

	// LTE - Find the initial bounding box and center of the RVE
	double xmin = 0.; double ymin = 0.; double zmin = 0.;
	double xmax = 0.; double ymax = 0.; double zmax = 0.;
	double centx = 0.; double centy = 0.; double centz = 0.;

	FENode& node = m.Node(0);
	xmin = node.m_r0.x; xmax = node.m_r0.x; 
	ymin = node.m_r0.y; ymax = node.m_r0.y; 
	zmin = node.m_r0.z; zmax = node.m_r0.z; 

	for (int n = 1; n < N; ++n){
		FENode& node = m.Node(n);

		if (node.m_r0.x >= xmax) xmax = node.m_r0.x;
		if (node.m_r0.x <= xmin) xmin = node.m_r0.x;
		if (node.m_r0.y >= ymax) ymax = node.m_r0.y;
		if (node.m_r0.y <= ymin) ymin = node.m_r0.y;
		if (node.m_r0.z >= zmax) zmax = node.m_r0.z;
		if (node.m_r0.z <= zmin) zmin = node.m_r0.z;
	}

	centx = xmin + (xmax - xmin)/2; centy = ymin + (ymax - ymin)/2; centz = zmin + (zmax - zmin)/2;
	
	// LTE - Recenter the RVE about the origin
	for (int n = 0; n < N; ++n){
		FENode& node = m.Node(n);
		node.m_r0.x -= centx; node.m_r0.y -= centy; node.m_r0.z -= centz;
		node.m_rt.x = node.m_r0.x; node.m_rt.y = node.m_r0.y; node.m_rt.z = node.m_r0.z;
	}
	
	// LTE - Find the bounding box for the RVE (+/- Lx, Ly, Lz)
	m_bb_x = xmax - centx; m_bb_y = ymax - centy; m_bb_z = zmax - centz;
	
	// use the E-E list to tag all exterior nodes
	int fn[FEElement::MAX_NODES], nf, M = 0;
	for (int k=0; k<m.Domains(); ++k)
	{
		FEDomain& dom = m.Domain(k);
		for (int i=0; i<dom.Elements(); ++i, ++M)
		{
			FEElement& el = dom.ElementRef(i);
			nf = m.Faces(el);
			for (int j=0; j<nf; ++j)
			{
				if (EEL.Neighbor(M, j) == 0)
				{
					// mark all nodes
					int nn = m.GetFace(el, j, fn);
					for (int k=0; k<nn; ++k)
					{
					FENode& node = m.Node(fn[k]);
						
					if (fabs(node.m_r0.x) >= 0.999*m_bb_x) m_BN[fn[k]] = 1;
					if (fabs(node.m_r0.y) >= 0.999*m_bb_y) m_BN[fn[k]] = 1;
					if (fabs(node.m_r0.z) >= 0.999*m_bb_z) m_BN[fn[k]] = 1;
					}
				}
			}
		}
	}

}

//-----------------------------------------------------------------------------
bool FEMicroMaterial::PrepDisplacementBC()
{
	FEMesh& m = m_rve.GetMesh();
	int N = m.Nodes();

	// count the nr of exterior nodes
	int NN = 0, i;
	for (i=0; i<N; ++i) if (m_BN[i] == 1) ++NN;
	m_num_ext_node = NN;

	assert(NN > 0);

	// create a load curve
	FELoadCurve* plc = new FELoadCurve;
	plc->SetInterpolation(FELoadCurve::LINEAR);
	plc->Add(0.0, 0.0);
	plc->Add(1.0, 1.0);
	m_rve.AddLoadCurve(plc);
	int NLC = m_rve.LoadCurves() - 1;

	// create the DC's
	NN = 0;
	m_rve.ClearBCs();
	for (i=0; i<N; ++i)
		if (m_BN[i] == 1)
		{
			for (int j=0; j<3; ++j, ++NN)
			{
				FEPrescribedBC* pdc = new FEPrescribedBC(&m_rve);
				pdc->bc = j;
				pdc->lc = NLC;
				pdc->node = i;
				pdc->s = 0;
				m_rve.AddPrescribedBC(pdc);
			}
		}

	return true;
}

//-----------------------------------------------------------------------------
bool FEMicroMaterial::PrepPeriodicBC()
{
	// get the RVE mesh
	FEMesh& m = m_rve.GetMesh();

	// create a load curve
	FELoadCurve* plc = new FELoadCurve;
	plc->SetInterpolation(FELoadCurve::LINEAR);
	plc->Add(0.0, 0.0);
	plc->Add(1.0, 1.0);
	m_rve.AddLoadCurve(plc);
	int NLC = m_rve.LoadCurves() - 1;

	// find the node set that defines the corner nodes
	FENodeSet* pset = m.FindNodeSet(m_szbc);
	if (pset == 0) return false;

	// create the DC's
	m_rve.ClearBCs();
	int N = pset->size();
	for (int i=0; i<N; ++i)
		for (int j=0; j<3; ++j)
		{
			FEPrescribedBC* pdc = new FEPrescribedBC(&m_rve);
			pdc->bc = j;
			pdc->lc = NLC;
			pdc->node = (*pset)[i];
			pdc->s = 0;
			m_rve.AddPrescribedBC(pdc);
		}

	return true;
}

//-----------------------------------------------------------------------------
//! Assign the prescribed displacement to the boundary nodes.
void FEMicroMaterial::UpdateBC(FEModel& rve, mat3d& F)
{
	// get the mesh
	FEMesh& m = rve.GetMesh();

	// assign new DC's for the boundary nodes
	int N = rve.PrescribedBCs()/3, i;
	for (i=0; i<N; ++i)
	{
		FEPrescribedBC& dx = *rve.PrescribedBC(3*i  );
		FEPrescribedBC& dy = *rve.PrescribedBC(3*i+1);
		FEPrescribedBC& dz = *rve.PrescribedBC(3*i+2);

		FENode& node = m.Node(dx.node);

		vec3d r0 = node.m_r0;
		vec3d r1 = F*r0;

		dx.s = r1.x - r0.x;
		dy.s = r1.y - r0.y;
		dz.s = r1.z - r0.z;
	}

	if (m_bperiodic)
	{
		// get the "displacement" component of the deformation gradient
		mat3d U = F - mat3dd(1);

		// set the offset for the periodic BC's
		vec3d r[FEElement::MAX_NODES];

		// loop over periodic boundaries
		for (int i=0; i<3; ++i)
		{
			FEPeriodicBoundary1O* pc = dynamic_cast<FEPeriodicBoundary1O*>(rve.SurfacePairInteraction(i));
			assert(pc);

			pc->m_Fmacro = F;
		}
	}
}

//-----------------------------------------------------------------------------
mat3ds FEMicroMaterial::Stress(FEMaterialPoint &mp)
{
	// get the deformation gradient
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	FEMicroMaterialPoint& mmpt = *mp.ExtractData<FEMicroMaterialPoint>();
	mat3d F = pt.m_F;
	
	// Create a local copy of the rve
	FEModel rve;
	rve.CopyFrom(m_rve);
	rve.GetStep(0)->SetPrintLevel(FE_PRINT_NEVER);

	// initialize
	if (rve.Init() == false) throw FEMultiScaleException();

	// apply the BC's
	UpdateBC(rve, F);

	// solve the RVE
	bool bret = rve.Solve();

	// make sure it converged
	if (bret == false) throw FEMultiScaleException();

	// calculate the averaged stress
	mat3ds sa = AveragedStress(rve, mp);
	
	mmpt.m_PK1_prev = mmpt.m_PK1;
	mmpt.m_PK1 = AveragedStressPK1(rve, mp);
	mmpt.m_S = AveragedStressPK2(rve, mp);

	// calculate the averaged stiffness
	mmpt.m_Ka = AveragedStiffness(rve, mp);

	calc_energy_diff(rve, mp);	
	
	// set the plot file
	FEBioPlotFile* pplt = new FEBioPlotFile(rve);
	vector<int> item;
	pplt->AddVariable("displacement", item);
	pplt->AddVariable("stress", item);

	if (m_bperiodic)
	{
		pplt->AddVariable("contact gap", item);
		pplt->AddVariable("contact traction", item);
		pplt->AddVariable("contact pressure", item);
	}

	pplt->Open(rve, "rve.xplt");
	pplt->Write(rve);
	pplt->Close();

	return sa;
}

//-----------------------------------------------------------------------------
// The stiffness is evaluated at the same time the stress is evaluated so we 
// can just return it here. Note that this assumes that the stress function 
// is always called prior to the tangent function.
tens4ds FEMicroMaterial::Tangent(FEMaterialPoint &mp)
{
	FEMicroMaterialPoint& mmpt = *mp.ExtractData<FEMicroMaterialPoint>();
	return mmpt.m_Ka;
}

//-----------------------------------------------------------------------------
//! Calculate the average stress from the RVE solution.
mat3ds FEMicroMaterial::AveragedStress(FEModel& rve, FEMaterialPoint &mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	mat3d F = pt.m_F;
	double J = pt.m_J;

	// get the RVE mesh
	FEMesh& m = rve.GetMesh();

	mat3d T; T.zero();

	// for periodic BC's we take the reaction forces directly from the periodic constraints
	if (m_bperiodic)
	{
		// get the reaction for from the periodic constraints
		for (int i=0; i<3; ++i)
		{
			FEPeriodicBoundary1O* pbc = dynamic_cast<FEPeriodicBoundary1O*>(rve.SurfacePairInteraction(i));
			assert(pbc);
			FEPeriodicSurface& ss = pbc->m_ss;
			int N = ss.Nodes();
			for (int i=0; i<N; ++i)
			{
				FENode& node = ss.Node(i);
				vec3d f = ss.m_Fr[i];

				// We multiply by two since the reaction forces are only stored at the slave surface 
				// and we also need to sum over the master nodes (NOTE: should I figure out a way to 
				// store the reaction forces on the master nodes as well?)
				T += (f & node.m_rt)*2.0;
			}
		}
	}

	// get the reaction force vector from the solid solver
	// (We also need to do this for the periodic BC, since at the prescribed nodes,
	// the contact forces will be zero). 
	FEAnalysis* pstep = rve.GetCurrentStep();
	FESolidSolver* ps = dynamic_cast<FESolidSolver*>(pstep->m_psolver);
	assert(ps);
	vector<double>& R = ps->m_Fr;
	int nbc = rve.PrescribedBCs();
	for (int i=0; i<nbc/3; ++i)
	{
		FEPrescribedBC& dc = *rve.PrescribedBC(3*i);
		FENode& n = m.Node(dc.node);
		vec3d f;
		f.x = R[-n.m_ID[DOF_X]-2];
		f.y = R[-n.m_ID[DOF_Y]-2];
		f.z = R[-n.m_ID[DOF_Z]-2];
		T += f & n.m_rt;
	}

	return T.sym() / (J*m_V0);
}

//-----------------------------------------------------------------------------
//! Calculate the average stiffness from the RVE solution.
tens4ds FEMicroMaterial::AveragedStiffness(FEModel& rve, FEMaterialPoint &mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	// get the mesh
	FEMesh& m = rve.GetMesh();

	// get the solver
	FEAnalysis* pstep = rve.GetCurrentStep();
	FESolidSolver* ps = dynamic_cast<FESolidSolver*>(pstep->m_psolver);

	// the element's stiffness matrix
	matrix ke;

	// element's residual
	vector<double> fe;

	// get deformation gradient and its inverse
	mat3d F = pt.m_F;
	mat3d Fi = F.inverse();

	// get the stress
	mat3ds s = pt.m_s;

	// calculate the center point
	vec3d rc(0,0,0);
	for (int k=0; k<m.Nodes(); ++k) rc += m.Node(k).m_rt;
	rc /= (double) m.Nodes();

	// LTE - Calculate the initial center point
	vec3d rc0(0,0,0);
	for (int k=0; k<m.Nodes(); ++k) rc0 += m.Node(k).m_r0;
	rc0 /= (double) m.Nodes();

	tens4ds c(0.);

	
	// LTE - elasticity tensor
	double D[6][6] = {0};
		
	// calculate the stiffness matrix and residual
	for (int k=0; k<m.Domains(); ++k)
	{
		FEElasticSolidDomain& bd = dynamic_cast<FEElasticSolidDomain&>(m.Domain(k));
		int NS = bd.Elements();
		for (int n=0; n<NS; ++n)
		{
			FESolidElement& e = bd.Element(n);

			// create the element's stiffness matrix
			int ne = e.Nodes();
			int ndof = 3*ne;
			ke.resize(ndof, ndof);
			ke.zero();

			// calculate the element's stiffness matrix
			bd.ElementStiffness(rve, n, ke);

			// create the element's residual
			fe.assign(ndof, 0);

			// calculate the element's residual
			bd.ElementInternalForce(e, fe);

			// loop over the element's nodes
			for (int i=0; i<ne; ++i)
			{
				FENode& ni = m.Node(e.m_node[i]);
				for (int j=0; j<ne; ++j)
				{
					FENode& nj = m.Node(e.m_node[j]);
					if ((m_BN[e.m_node[i]] == 1) && (m_BN[e.m_node[j]] == 1))
					{
						// both nodes are boundary nodes
						// so grab the element's submatrix
						double K[3][3];
						K[0][0] = ke[3*i  ][3*j  ]; K[0][1] = ke[3*i  ][3*j+1]; K[0][2] = ke[3*i  ][3*j+2];
						K[1][0] = ke[3*i+1][3*j  ]; K[1][1] = ke[3*i+1][3*j+1]; K[1][2] = ke[3*i+1][3*j+2];
						K[2][0] = ke[3*i+2][3*j  ]; K[2][1] = ke[3*i+2][3*j+1]; K[2][2] = ke[3*i+2][3*j+2];

						// get the nodal positions
						vec3d ri = ni.m_rt - rc;
						vec3d rj = nj.m_rt - rc;

						double Ri[3] = { ri.x, ri.y, ri.z };
						double Rj[3] = { rj.x, rj.y, rj.z };

						// create the elasticity tensor
						D[0][0] += Ri[0]*K[0][0]*Rj[0]; 
						D[1][1] += Ri[1]*K[1][1]*Rj[1]; 
						D[2][2] += Ri[2]*K[2][2]*Rj[2]; 

						D[0][1] += Ri[0]*K[0][1]*Rj[1];
						D[0][2] += Ri[0]*K[0][2]*Rj[2];
						D[1][2] += Ri[1]*K[1][2]*Rj[2];

						D[0][3] += 0.5*(Ri[0]*K[0][0]*Rj[1] + Ri[0]*K[0][1]*Rj[0]);
						D[0][4] += 0.5*(Ri[0]*K[0][1]*Rj[2] + Ri[0]*K[0][2]*Rj[1]);
						D[0][5] += 0.5*(Ri[0]*K[0][0]*Rj[2] + Ri[0]*K[0][2]*Rj[0]);

						D[1][3] += 0.5*(Ri[1]*K[1][0]*Rj[1] + Ri[1]*K[1][1]*Rj[0]);
						D[1][4] += 0.5*(Ri[1]*K[1][1]*Rj[2] + Ri[1]*K[1][2]*Rj[1]);
						D[1][5] += 0.5*(Ri[1]*K[1][0]*Rj[2] + Ri[1]*K[1][2]*Rj[0]);

						D[2][3] += 0.5*(Ri[2]*K[2][0]*Rj[1] + Ri[2]*K[2][1]*Rj[0]);
						D[2][4] += 0.5*(Ri[2]*K[2][1]*Rj[2] + Ri[2]*K[2][2]*Rj[1]);
						D[2][5] += 0.5*(Ri[2]*K[2][0]*Rj[2] + Ri[2]*K[2][2]*Rj[0]);

						D[3][3] += 0.25*(Ri[0]*K[1][0]*Rj[1] + Ri[1]*K[0][0]*Rj[1] + Ri[0]*K[1][1]*Rj[0] + Ri[1]*K[0][1]*Rj[0]);
						D[3][4] += 0.25*(Ri[0]*K[1][1]*Rj[2] + Ri[1]*K[0][1]*Rj[2] + Ri[0]*K[1][2]*Rj[1] + Ri[1]*K[0][2]*Rj[1]);
						D[3][5] += 0.25*(Ri[0]*K[1][0]*Rj[2] + Ri[1]*K[0][0]*Rj[2] + Ri[0]*K[1][2]*Rj[0] + Ri[1]*K[0][2]*Rj[0]);

						D[4][4] += 0.25*(Ri[1]*K[2][1]*Rj[2] + Ri[2]*K[1][1]*Rj[2] + Ri[1]*K[2][2]*Rj[1] + Ri[2]*K[1][2]*Rj[1]);
						D[4][5] += 0.25*(Ri[1]*K[2][0]*Rj[2] + Ri[2]*K[1][0]*Rj[2] + Ri[1]*K[2][2]*Rj[0] + Ri[2]*K[1][2]*Rj[0]);

						D[5][5] += 0.25*(Ri[0]*K[2][0]*Rj[2] + Ri[2]*K[0][0]*Rj[2] + Ri[0]*K[2][2]*Rj[0] + Ri[2]*K[0][2]*Rj[0]);
					}
				}
			}
		}
	}

	// divide by volume
	double Vi = 1.0/(pt.m_J * m_V0);
	D[0][0] *= Vi; D[0][1] *= Vi; D[0][2] *= Vi; D[0][3] *= Vi; D[0][4] *= Vi; D[0][5] *= Vi;
	D[1][1] *= Vi; D[1][2] *= Vi; D[1][3] *= Vi; D[1][4] *= Vi; D[1][5] *= Vi;
	D[2][2] *= Vi; D[2][3] *= Vi; D[2][4] *= Vi; D[2][5] *= Vi;
	D[3][3] *= Vi; D[3][4] *= Vi; D[3][5] *= Vi;
	D[4][4] *= Vi; D[4][5] *= Vi;
	D[5][5] *= Vi;
																							
	c = tens4ds(D);
	return c;
}

//-----------------------------------------------------------------------------
//! Calculate the energy difference between the RVE problem and the macro material point
void FEMicroMaterial::calc_energy_diff(FEModel& rve, FEMaterialPoint& mp)
{
	double energy_diff = 0.;
	
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	FEMicroMaterialPoint& mmpt = *mp.ExtractData<FEMicroMaterialPoint>();
	mat3d F = pt.m_F;
	mat3d Finv = F.inverse();
	mat3d Finvtrans = Finv.transpose();
	mat3d Ftrans = F.transpose();
	
	vec3d x = pt.m_rt;
	vec3d X = pt.m_r0;

	// calculate infinitesimal strain
	mmpt.m_inf_str = ((F.transpose() + F)*0.5 - mat3dd(1)).sym();
	
	// calculate Green-Lagrange strain
	mmpt.m_E = ((Ftrans*F - mat3dd(1))*0.5).sym();
	
	// calculate Euler-Almansi strain
	mmpt.m_e = ((mat3dd(1) - Finvtrans*Finv)*0.5).sym();
	
	// calculate the energy difference between macro point and RVE
	// to verify that we have satisfied the Hill-Mandel condition

	// calculate the macroscopic strain energy increment according to PK1 stress
	mmpt.m_macro_energy_inc = mmpt.m_PK1.dotdot(pt.m_F - pt.m_F_prev);

	// calculate the macroscopic strain energy increment according to PK2 stress
	//mat3ds E_prev = ((pt.m_F_prev.transpose()*pt.m_F_prev - mat3dd(1))*0.5).sym();
	//mmpt.m_macro_energy_inc = mmpt.m_S.dotdot(mmpt.m_E - E_prev);

	// calculate the macroscopic strain energy increment according to Cauchy stress
	//mat3ds e_prev = ((mat3dd(1) - pt.m_F_prev.transinv()*pt.m_F_prev.inverse())*0.5).sym();
	//mmpt.m_macro_energy_inc = pt.m_s.dotdot(mmpt.m_e - e_prev);

	// calculate the microscopic strain energy increment
	double rve_energy_avg = 0.;
	int nint; 
	double* w;
	
	mat3d rve_F;		
	mat3d rve_F_prev;
	double J0 = 0.;
	double V0 = 0.;
	mat3d rve_PK1;
	
	mat3ds rve_S;
	mat3ds rve_E;
	mat3ds rve_E_prev;
		
	mat3ds rve_s;
	mat3ds rve_e;
	mat3ds rve_e_prev;
	double J = 0.;
	double v = 0.;

	FEMesh& m = rve.GetMesh();
	for (int k=0; k<m.Domains(); ++k)
	{
		FESolidDomain& dom = static_cast<FESolidDomain&>(m.Domain(k));
		for (int i=0; i<dom.Elements(); ++i)
		{
			FESolidElement& el = dom.Element(i);
			nint = el.GaussPoints();
			w = el.GaussWeights();
			
			for (int n=0; n<nint; ++n)
			{
				FEElasticMaterialPoint& rve_pt = *el.GetMaterialPoint(n)->ExtractData<FEElasticMaterialPoint>();
				
				rve_F = rve_pt.m_F;
				rve_F_prev = rve_pt.m_F_prev;
				rve_s = rve_pt.m_s;

				// calculate microscopic strain energy according to PK1 stress
				rve_PK1 = rve_F.det()*rve_s*rve_F.transinv();
				J0 = dom.detJ0(el, n);		
				V0 += J0*w[n];
				rve_energy_avg += rve_PK1.dotdot(rve_F - rve_F_prev)*J0*w[n];
								
				// calculate microscopic strain energy according to PK2 stress
				/*rve_E = ((rve_F.transpose()*rve_F - mat3dd(1))*0.5).sym();
				rve_E_prev = ((rve_F_prev.transpose()*rve_F_prev - mat3dd(1))*0.5).sym();
				rve_PK1 = rve_F.det()*rve_s*rve_F.transinv();
				rve_S = (rve_F.inverse()*rve_PK1).sym();
				J0 = dom.detJ0(el, n);		
				V0 += J0*w[n];
				rve_energy_avg += rve_S.dotdot(rve_E - rve_E_prev)*J0*w[n];*/
				
				//// calculate microscopic strain energy according to Cauchy stress
				/*rve_e = ((mat3dd(1) - rve_F.transinv()*rve_F.inverse())*0.5).sym();
				rve_e_prev = ((mat3dd(1) - rve_F_prev.transinv()*rve_F_prev.inverse())*0.5).sym();
				J = dom.detJt(el, n);		
				v += J*w[n];		
				rve_energy_avg += rve_s.dotdot(rve_e - rve_e_prev)*J*w[n];*/
			}
		}
	}

	mmpt.m_micro_energy_inc = rve_energy_avg/V0;
	//mmpt.m_micro_energy_inc = rve_energy_avg/v;
}

//-----------------------------------------------------------------------------
//! Calculate the average stress from the RVE solution.
mat3d FEMicroMaterial::AveragedStressPK1(FEModel& rve, FEMaterialPoint &mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	mat3d F = pt.m_F;
	double J = pt.m_J;
	
	// get the RVE mesh
	FEMesh& m = rve.GetMesh();

	mat3d PK1; PK1.zero();

	// for periodic BC's we take the reaction forces directly from the periodic constraints
	if (m_bperiodic)
	{
		// get the reaction for from the periodic constraints
		for (int i=0; i<3; ++i)
		{
			FEPeriodicBoundary1O* pbc = dynamic_cast<FEPeriodicBoundary1O*>(rve.SurfacePairInteraction(i));
			assert(pbc);
			FEPeriodicSurface& ss = pbc->m_ss;
			int N = ss.Nodes();
			for (int i=0; i<N; ++i)
			{
				FENode& node = ss.Node(i);
				vec3d f = ss.m_Fr[i];

				// We multiply by two since the reaction forces are only stored at the slave surface 
				// and we also need to sum over the master nodes (NOTE: should I figure out a way to 
				// store the reaction forces on the master nodes as well?)
				PK1 += (f & node.m_r0)*2.0;
			}
		}
	}

	// get the reaction force vector from the solid solver
	// (We also need to do this for the periodic BC, since at the prescribed nodes,
	// the contact forces will be zero). 
	FEAnalysis* pstep = rve.GetCurrentStep();
	FESolidSolver* ps = dynamic_cast<FESolidSolver*>(pstep->m_psolver);
	assert(ps);
	vector<double>& R = ps->m_Fr;
	int nbc = rve.PrescribedBCs();
	for (int i=0; i<nbc/3; ++i)
	{
		FEPrescribedBC& dc = *rve.PrescribedBC(3*i);
		FENode& n = m.Node(dc.node);
		vec3d f;
		f.x = R[-n.m_ID[DOF_X]-2];
		f.y = R[-n.m_ID[DOF_Y]-2];
		f.z = R[-n.m_ID[DOF_Z]-2];
		PK1 += f & n.m_r0;
	}

	return PK1 / m_V0;
}

//-----------------------------------------------------------------------------
//! Calculate the average stress from the RVE solution.
mat3ds FEMicroMaterial::AveragedStressPK2(FEModel& rve, FEMaterialPoint &mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	mat3d F = pt.m_F;
	double J = pt.m_J;
	mat3d Finv = F.inverse();

	// get the RVE mesh
	FEMesh& m = rve.GetMesh();

	mat3d S; S.zero();

	// for periodic BC's we take the reaction forces directly from the periodic constraints
	if (m_bperiodic)
	{
		// get the reaction for from the periodic constraints
		for (int i=0; i<3; ++i)
		{
			FEPeriodicBoundary1O* pbc = dynamic_cast<FEPeriodicBoundary1O*>(rve.SurfacePairInteraction(i));
			assert(pbc);
			FEPeriodicSurface& ss = pbc->m_ss;
			int N = ss.Nodes();
			for (int i=0; i<N; ++i)
			{
				FENode& node = ss.Node(i);
				vec3d f = ss.m_Fr[i];
				vec3d f0 = Finv*f;

				// We multiply by two since the reaction forces are only stored at the slave surface 
				// and we also need to sum over the master nodes (NOTE: should I figure out a way to 
				// store the reaction forces on the master nodes as well?)
				S += (f0 & node.m_r0)*2.0;
			}
		}
	}

	// get the reaction force vector from the solid solver
	// (We also need to do this for the periodic BC, since at the prescribed nodes,
	// the contact forces will be zero). 
	FEAnalysis* pstep = rve.GetCurrentStep();
	FESolidSolver* ps = dynamic_cast<FESolidSolver*>(pstep->m_psolver);
	assert(ps);
	vector<double>& R = ps->m_Fr;
	int nbc = rve.PrescribedBCs();
	for (int i=0; i<nbc/3; ++i)
	{
		FEPrescribedBC& dc = *rve.PrescribedBC(3*i);
		FENode& n = m.Node(dc.node);
		vec3d f;
		f.x = R[-n.m_ID[DOF_X]-2];
		f.y = R[-n.m_ID[DOF_Y]-2];
		f.z = R[-n.m_ID[DOF_Z]-2];
		vec3d f0 = Finv*f;
		S += f0 & n.m_r0;
	}

	return S.sym() / m_V0;
}
