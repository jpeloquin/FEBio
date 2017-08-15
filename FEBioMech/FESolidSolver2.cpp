#include "stdafx.h"
#include "FESolidSolver2.h"
#include "FERigidMaterial.h"
#include "FE3FieldElasticSolidDomain.h"
#include "FEBodyForce.h"
#include "FEPressureLoad.h"
#include "FEResidualVector.h"
#include "FECore/FENodeReorder.h"
#include "FECore/log.h"
#include "FECore/DOFS.h"
#include "FEUncoupledMaterial.h"
#include "NumCore/NumCore.h"
#include "FECore/FEGlobalMatrix.h"
#include "FEContactInterface.h"
#include <FECore/sys.h>
#include <FECore/FEModel.h>
#include <FECore/FEAnalysis.h>
#include <FECore/BC.h>
#include <FECore/RigidBC.h>
#include <FECore/FEModelLoad.h>
#include <FECore/FELinearConstraintManager.h>

//-----------------------------------------------------------------------------
// define the parameter list
BEGIN_PARAMETER_LIST(FESolidSolver2, FENewtonSolver)
	ADD_PARAMETER2(m_Dtol        , FE_PARAM_DOUBLE, FE_RANGE_GREATER_OR_EQUAL(0.0), "dtol"        );
	ADD_PARAMETER2(m_Etol        , FE_PARAM_DOUBLE, FE_RANGE_GREATER_OR_EQUAL(0.0), "etol"        );
	ADD_PARAMETER2(m_Rtol        , FE_PARAM_DOUBLE, FE_RANGE_GREATER_OR_EQUAL(0.0), "rtol"        );
	ADD_PARAMETER2(m_Rmin        , FE_PARAM_DOUBLE, FE_RANGE_GREATER_OR_EQUAL(0.0), "min_residual");
    ADD_PARAMETER(m_alpha        , FE_PARAM_DOUBLE, "alpha"       );
	ADD_PARAMETER(m_beta         , FE_PARAM_DOUBLE, "beta"        );
	ADD_PARAMETER(m_gamma        , FE_PARAM_DOUBLE, "gamma"       );
    ADD_PARAMETER(m_bsymm        , FE_PARAM_BOOL  , "symmetric_stiffness");
	ADD_PARAMETER(m_bdivreform   , FE_PARAM_BOOL  , "diverge_reform");
	ADD_PARAMETER(m_bdoreforms   , FE_PARAM_BOOL  , "do_reforms"  );
	ADD_PARAMETER(m_breformtimestep, FE_PARAM_BOOL, "reform_each_time_step");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
//! FESolidSolver2 Construction
//
FESolidSolver2::FESolidSolver2(FEModel* pfem) : FENewtonSolver(pfem), m_rigidSolver(pfem)
{
	// default values
	m_Rtol = 0;	// deactivate residual convergence 
	m_Dtol = 0.001;
	m_Etol = 0.01;
	m_Rmin = 1.0e-20;

	m_niter = 0;
	m_nreq = 0;

	m_bdivreform = true;
	m_bdoreforms = true;
	m_breformtimestep = true;

	// default Newmark parameters for trapezoidal time integration
    m_alpha = 1.0;
	m_beta  = 0.25;
	m_gamma = 0.5;

	m_baugment = false;

	// Allocate degrees of freedom
	DOFS& dofs = pfem->GetDOFS();
	int varD = dofs.AddVariable("displacement", VAR_VEC3);
	dofs.SetDOFName(varD, 0, "x");
	dofs.SetDOFName(varD, 1, "y");
	dofs.SetDOFName(varD, 2, "z");
	int varQ = dofs.AddVariable("rotation", VAR_VEC3);
	dofs.SetDOFName(varQ, 0, "u");
	dofs.SetDOFName(varQ, 1, "v");
	dofs.SetDOFName(varQ, 2, "w");
	int varQR = dofs.AddVariable("rigid rotation", VAR_VEC3);
	dofs.SetDOFName(varQR, 0, "Ru");
	dofs.SetDOFName(varQR, 1, "Rv");
	dofs.SetDOFName(varQR, 2, "Rw");
	int varV = dofs.AddVariable("velocity", VAR_VEC3);
	dofs.SetDOFName(varV, 0, "vx");
	dofs.SetDOFName(varV, 1, "vy");
	dofs.SetDOFName(varV, 2, "vz");
    int varQP = dofs.AddVariable("previous rotation", VAR_VEC3);
    dofs.SetDOFName(varQP, 0, "up");
    dofs.SetDOFName(varQP, 1, "vp");
    dofs.SetDOFName(varQP, 2, "wp");
    int varQV = dofs.AddVariable("shell velocity", VAR_VEC3);
    dofs.SetDOFName(varQV, 0, "vu");
    dofs.SetDOFName(varQV, 1, "vv");
    dofs.SetDOFName(varQV, 2, "vw");
    int varQA = dofs.AddVariable("shell acceleration", VAR_VEC3);
    dofs.SetDOFName(varQA, 0, "au");
    dofs.SetDOFName(varQA, 1, "av");
    dofs.SetDOFName(varQA, 2, "aw");
    int varQVP = dofs.AddVariable("previous shell velocity", VAR_VEC3);
    dofs.SetDOFName(varQVP, 0, "vup");
    dofs.SetDOFName(varQVP, 1, "vvp");
    dofs.SetDOFName(varQVP, 2, "vwp");
    int varQAP = dofs.AddVariable("previous shell acceleration", VAR_VEC3);
    dofs.SetDOFName(varQAP, 0, "aup");
    dofs.SetDOFName(varQAP, 1, "avp");
    dofs.SetDOFName(varQAP, 2, "awp");
    
    // get the DOF indices
	m_dofX  = pfem->GetDOFIndex("x");
	m_dofY  = pfem->GetDOFIndex("y");
	m_dofZ  = pfem->GetDOFIndex("z");
	m_dofVX = pfem->GetDOFIndex("vx");
	m_dofVY = pfem->GetDOFIndex("vy");
	m_dofVZ = pfem->GetDOFIndex("vz");
	m_dofU  = pfem->GetDOFIndex("u");
	m_dofV  = pfem->GetDOFIndex("v");
	m_dofW  = pfem->GetDOFIndex("w");
	m_dofRU = pfem->GetDOFIndex("Ru");
	m_dofRV = pfem->GetDOFIndex("Rv");
	m_dofRW = pfem->GetDOFIndex("Rw");
    
    m_dofVU  = pfem->GetDOFIndex("vu");
    m_dofVV  = pfem->GetDOFIndex("vv");
    m_dofVW  = pfem->GetDOFIndex("vw");
    m_dofAU  = pfem->GetDOFIndex("au");
    m_dofAV  = pfem->GetDOFIndex("av");
    m_dofAW  = pfem->GetDOFIndex("aw");
    
    m_dofUP  = pfem->GetDOFIndex("up");
    m_dofVP  = pfem->GetDOFIndex("vp");
    m_dofWP  = pfem->GetDOFIndex("wp");
    m_dofVUP  = pfem->GetDOFIndex("vup");
    m_dofVVP  = pfem->GetDOFIndex("vvp");
    m_dofVWP  = pfem->GetDOFIndex("vwp");
    m_dofAUP  = pfem->GetDOFIndex("aup");
    m_dofAVP  = pfem->GetDOFIndex("avp");
    m_dofAWP  = pfem->GetDOFIndex("awp");
}

//-----------------------------------------------------------------------------
FESolidSolver2::~FESolidSolver2()
{

}

//-----------------------------------------------------------------------------
//! Allocates and initializes the data structures used by the FESolidSolver2
//
bool FESolidSolver2::Init()
{
	// initialize base class
	if (FENewtonSolver::Init() == false) return false;

	// allocate vectors
	int neq = m_neq;
	m_Fn.assign(neq, 0);
	m_Fd.assign(neq, 0);
	m_Fr.assign(neq, 0);
	m_Ui.assign(neq, 0);
	m_Ut.assign(neq, 0);

	// we need to fill the total displacement vector m_Ut
	FEMesh& mesh = m_fem.GetMesh();
	gather(m_Ut, mesh, m_dofX);
	gather(m_Ut, mesh, m_dofY);
	gather(m_Ut, mesh, m_dofZ);
	gather(m_Ut, mesh, m_dofU);
	gather(m_Ut, mesh, m_dofV);
	gather(m_Ut, mesh, m_dofW);

	return true;
}

//-----------------------------------------------------------------------------
//! Save data to dump file

void FESolidSolver2::Serialize(DumpStream& ar)
{
	// Serialize parameters
	FENewtonSolver::Serialize(ar);
	
	if (ar.IsSaving())
	{
		ar << m_nrhs;
		ar << m_niter;
		ar << m_nref << m_ntotref;
		ar << m_naug;
		ar << m_nreq;
	}
	else
	{
		ar >> m_nrhs;
		ar >> m_niter;
		ar >> m_nref >> m_ntotref;
		ar >> m_naug;
		ar >> m_nreq;

		// initialize data structures
		// (only when number of equations is non-zero.
		// This can be zero in a multi-step analysis for steps that have not yet been initialized.)
		if (m_neq > 0) Init();
	}

	// serialize rigid solver
	m_rigidSolver.Serialize(ar);
}

//-----------------------------------------------------------------------------
bool FESolidSolver2::InitEquations()
{
	// First call the base class.
	// This will initialize all equation numbers, except the rigid body equation numbers
	if (FENewtonSolver::InitEquations() == false) return false;

	// store the number of equations we currently have
	m_nreq = m_neq;

	// Next, we assign equation numbers to the rigid body degrees of freedom
	int neq = m_rigidSolver.InitEquations(m_neq);
	if (neq == -1) return false; 
	else m_neq = neq;

	// All initialization is done
	return true;
}

//-----------------------------------------------------------------------------
//!  This functions performs the Lagrange augmentations
//!  It returns true if all the augmentation have converged, 
//!	otherwise it returns false
//
//! \todo There is an inherent problem with this approach. Since
//!	      Lagrangian multipliers are inherited from previous timesteps
//!       they might not be zero in case a node-surface contact breaks. 
//!       The node's gap value needs to become negative to a certain value
//!       before the Lagr. multipliers dissapears. 
//
bool FESolidSolver2::Augment()
{
	FETimeInfo tp = m_fem.GetTime();
	tp.alpha = m_alpha;
	tp.beta  = m_beta;
	tp.gamma = m_gamma;

	// Assume we will pass (can't hurt to be optimistic)
	bool bconv = true;

	// Do contact augmentations
	if (m_fem.SurfacePairInteractions() > 0)
	{
		// loop over all contact interfaces
		for (int i=0; i<m_fem.SurfacePairInteractions(); ++i)
		{
			FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));
			if (pci->IsActive()) bconv = (pci->Augment(m_naug) && bconv);
		}
	}

	// do nonlinear constraint augmentations
	int n = m_fem.NonlinearConstraints();
	for (int i=0; i<n; ++i) 
	{
		FENLConstraint* plc = m_fem.NonlinearConstraint(i);
		if (plc->IsActive()) bconv = plc->Augment(m_naug, tp) && bconv;
	}

	// do incompressibility multipliers for 3Field domains
	FEMesh& mesh = m_fem.GetMesh();
	int ND = mesh.Domains();
	for (int i=0; i<ND; ++i)
	{
		FE3FieldElasticSolidDomain* pd = dynamic_cast<FE3FieldElasticSolidDomain*>(&mesh.Domain(i));
		if (pd) bconv = (pd->Augment(m_naug) && bconv);
	}

	return bconv;
}

//-----------------------------------------------------------------------------
//! Update the kinematics of the model, such as nodal positions, velocities,
//! accelerations, etc.
void FESolidSolver2::UpdateKinematics(vector<double>& ui)
{
	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

	// update rigid bodies
	m_rigidSolver.UpdateRigidBodies(m_Ui, ui);

	// total displacements
	vector<double> U(m_Ut.size());
	for (size_t i=0; i<m_Ut.size(); ++i) U[i] = ui[i] + m_Ui[i] + m_Ut[i];

	// update flexible nodes
	// translational dofs
	scatter(U, mesh, m_dofX);
	scatter(U, mesh, m_dofY);
	scatter(U, mesh, m_dofZ);
	// rotational dofs
	scatter(U, mesh, m_dofU);
	scatter(U, mesh, m_dofV);
	scatter(U, mesh, m_dofW);

	// make sure the prescribed displacements are fullfilled
	int ndis = m_fem.PrescribedBCs();
	for (int i = 0; i<ndis; ++i)
	{
		FEPrescribedBC& dc = *m_fem.PrescribedBC(i);
		if (dc.IsActive()) dc.Update();
	}

	// enforce the linear constraints
	// TODO: do we really have to do this? Shouldn't the algorithm
	// already guarantee that the linear constraints are satisfied?
	FELinearConstraintManager& LCM = m_fem.GetLinearConstraintManager();
	if (LCM.LinearConstraints() > 0)
	{
		LCM.Update();
	}

	// Update the spatial nodal positions
	// Don't update rigid nodes since they are already updated
	for (int i = 0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		if (node.m_rid == -1)
			node.m_rt = node.m_r0 + node.get_vec3d(m_dofX, m_dofY, m_dofZ);
	}

	// update velocity and accelerations
	// for dynamic simulations
	FEAnalysis* pstep = m_fem.GetCurrentStep();
	if (pstep->m_nanalysis == FE_DYNAMIC)
	{
		int N = mesh.Nodes();
		double dt = pstep->m_dt;
		double a = 1.0 / (m_beta*dt);
		double b = a / dt;
		double c = 1.0 - 0.5/m_beta;
		for (int i=0; i<N; ++i)
		{
			FENode& n = mesh.Node(i);
			n.m_at = (n.m_rt - n.m_rp)*b - n.m_vp*a + n.m_ap*c;
			vec3d vt = n.m_vp + (n.m_ap*(1.0 - m_gamma) + n.m_at*m_gamma)*dt;
			n.set_vec3d(m_dofVX, m_dofVY, m_dofVZ, vt);
            
            // shell kinematics
            vec3d qt = n.get_vec3d(m_dofU, m_dofV, m_dofW);
            vec3d qp = n.get_vec3d(m_dofUP, m_dofVP, m_dofWP);
            vec3d vqp = n.get_vec3d(m_dofVUP, m_dofVVP, m_dofVWP);
            vec3d aqp = n.get_vec3d(m_dofAUP, m_dofAVP, m_dofAWP);
            vec3d aqt = (qt - qp)*b - vqp*a + aqp*c;
            vec3d vqt = vqp + (aqp*(1.0 - m_gamma) + aqt*m_gamma)*dt;
            n.set_vec3d(m_dofAU, m_dofAV, m_dofAW, aqt);
            n.set_vec3d(m_dofVU, m_dofVV, m_dofVW, vqt);
        }
    }
}

//-----------------------------------------------------------------------------
//! Update DOF increments
void FESolidSolver2::UpdateIncrements(vector<double>& Ui, vector<double>& ui, bool emap)
{
	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();
    
	// update rigid bodies
	m_rigidSolver.UpdateIncrements(Ui, ui, emap);
        
	// update flexible nodes
	int n;
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
        
		// displacement dofs
		// current position = initial + total at prev conv step + total increment so far + current increment
		if ((n = node.m_ID[m_dofX]) >= 0) Ui[n] += ui[n];
		if ((n = node.m_ID[m_dofY]) >= 0) Ui[n] += ui[n];
		if ((n = node.m_ID[m_dofZ]) >= 0) Ui[n] += ui[n];
        
        // rotational dofs
        if ((n = node.m_ID[m_dofU]) >= 0) Ui[n] += ui[n];
        if ((n = node.m_ID[m_dofV]) >= 0) Ui[n] += ui[n];
        if ((n = node.m_ID[m_dofW]) >= 0) Ui[n] += ui[n];
	}    
}

//-----------------------------------------------------------------------------
//! Updates the current state of the model
void FESolidSolver2::Update(vector<double>& ui)
{
	TimerTracker t(m_UpdateTime);

	// update kinematics
	UpdateKinematics(ui);

	// update contact
	if (m_fem.SurfacePairInteractions() > 0) UpdateContact();

	// update constraints
	if (m_fem.NonlinearConstraints() > 0) UpdateConstraints();

	// update element stresses
	UpdateStresses();

	// update other stuff that may depend on the deformation
	int NBL = m_fem.BodyLoads();
	for (int i=0; i<NBL; ++i)
	{
		FEBodyForce* pbf = dynamic_cast<FEBodyForce*>(m_fem.GetBodyLoad(i));
		if (pbf) pbf->Update();
	}
}

//-----------------------------------------------------------------------------
//!  Updates the element stresses
void FESolidSolver2::UpdateStresses()
{
	FEMesh& mesh = m_fem.GetMesh();
	FETimeInfo tp = m_fem.GetTime();
    tp.alpha = m_alpha;
    tp.beta  = m_beta;
    tp.gamma = m_gamma;
    
    // update the stresses on all domains
	for (int i=0; i<mesh.Domains(); ++i) mesh.Domain(i).Update(tp);
}

//-----------------------------------------------------------------------------
//! Update contact interfaces.
void FESolidSolver2::UpdateContact()
{
	// Update all contact interfaces
	for (int i=0; i<m_fem.SurfacePairInteractions(); ++i) 
	{
		FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));
		if (pci->IsActive()) pci->Update(m_niter);
	}
}

//-----------------------------------------------------------------------------
//! Update nonlinear constraints
void FESolidSolver2::UpdateConstraints()
{
	FETimeInfo tp = m_fem.GetTime();
	tp.alpha = m_alpha;
	tp.beta  = m_beta;
	tp.gamma = m_gamma;
	tp.currentIteration = m_niter;

	// Update all nonlinear constraints
	for (int i=0; i<m_fem.NonlinearConstraints(); ++i) 
	{
		FENLConstraint* pci = m_fem.NonlinearConstraint(i);
		if (pci->IsActive()) pci->Update(tp);
	}
}

//-----------------------------------------------------------------------------
bool FESolidSolver2::InitStep(double time)
{
	FEModel& fem = GetFEModel();

	// get the time information
	FETimeInfo tp = fem.GetTime();
    tp.alpha = m_alpha;
    tp.beta  = m_beta;
    tp.gamma = m_gamma;
    
    // evaluate load curve values at current (or intermediate) time
	double t = tp.currentTime;
	double dt = tp.timeIncrement;
	double ta = (t > 0) ? t - (1-m_alpha)*dt : m_alpha*dt;

	return FESolver::InitStep(ta);
}

//-----------------------------------------------------------------------------
//! Prepares the data for the first BFGS-iteration. 
void FESolidSolver2::PrepStep(const FETimeInfo& timeInfo)
{
	TimerTracker t(m_UpdateTime);

	// initialize counters
	m_niter = 0;	// nr of iterations
	m_nrhs  = 0;	// nr of RHS evaluations
	m_nref  = 0;	// nr of stiffness reformations
	m_ntotref = 0;
	m_naug  = 0;	// nr of augmentations

	// zero total displacements
	zero(m_Ui);

	// store previous mesh state
	// we need them for velocity and acceleration calculations
	FEMesh& mesh = m_fem.GetMesh();
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& ni = mesh.Node(i);
		ni.m_rp = ni.m_rt;
		ni.m_vp = ni.get_vec3d(m_dofVX, m_dofVY, m_dofVZ);
		ni.m_ap = ni.m_at;
        ni.set_vec3d(m_dofUP, m_dofVP, m_dofWP, ni.get_vec3d(m_dofU, m_dofV, m_dofW));
        ni.set_vec3d(m_dofVUP, m_dofVVP, m_dofVWP, ni.get_vec3d(m_dofVU, m_dofVV, m_dofVW));
        ni.set_vec3d(m_dofAUP, m_dofAVP, m_dofAWP, ni.get_vec3d(m_dofAU, m_dofAV, m_dofAW));
    }

	FETimeInfo tp = m_fem.GetTime();
    tp.alpha = m_alpha;
    tp.beta  = m_beta;
    tp.gamma = m_gamma;
    
    // apply concentrated nodal forces
	// since these forces do not depend on the geometry
	// we can do this once outside the NR loop.
	NodalForces(m_Fn, tp);

	// apply prescribed displacements
	// we save the prescribed displacements increments in the ui vector
	vector<double>& ui = m_ui;
	zero(ui);
	int neq = m_neq;
	int nbc = m_fem.PrescribedBCs();
	for (int i=0; i<nbc; ++i)
	{
		FEPrescribedBC& dc = *m_fem.PrescribedBC(i);
		if (dc.IsActive()) dc.PrepStep(ui);
	}

	// do the linear constraints
	m_fem.GetLinearConstraintManager().PrepStep();

	// initialize rigid bodies
	m_rigidSolver.PrepStep(timeInfo, ui);

	// initialize contact
	if (m_fem.SurfacePairInteractions() > 0) UpdateContact();

	// initialize nonlinear constraints
	if (m_fem.NonlinearConstraints() > 0) UpdateConstraints();

	// intialize material point data
	// NOTE: do this before the stresses are updated
	// TODO: does it matter if the stresses are updated before
	//       the material point data is initialized
	for (int i=0; i<mesh.Domains(); ++i) mesh.Domain(i).PreSolveUpdate(timeInfo);

	// update stresses
	UpdateStresses();

	// see if we need to do contact augmentations
	m_baugment = false;
	for (int i = 0; i<m_fem.SurfacePairInteractions(); ++i)
	{
		FEContactInterface& ci = dynamic_cast<FEContactInterface&>(*m_fem.SurfacePairInteraction(i));
		if (ci.IsActive() && ci.m_blaugon) m_baugment = true;
	}

	// see if we need to do incompressible augmentations
	int nmat = m_fem.Materials();
	for (int i = 0; i<nmat; ++i)
	{
		FEUncoupledMaterial* pmi = dynamic_cast<FEUncoupledMaterial*>(m_fem.GetMaterial(i));
		if (pmi && pmi->m_blaugon) m_baugment = true;
	}

	// see if we have to do nonlinear constraint augmentations
	if (m_fem.NonlinearConstraints() != 0) m_baugment = true;
}

//-----------------------------------------------------------------------------
//! Implements the BFGS algorithm to solve the nonlinear FE equations.
//! The details of this implementation of the BFGS method can be found in:
//!   "Finite Element Procedures", K.J. Bathe, p759 and following
bool FESolidSolver2::Quasin(double time)
{
	int i;

	vector<double> u0(m_neq);
	vector<double> Rold(m_neq);

	// convergence norms
	double	normR1;		// residual norm
	double	normE1;		// energy norm
	double	normU;		// displacement norm
	double	normu;		// displacement increment norm
	double	normRi;		// initial residual norm
	double	normEi;		// initial energy norm
	double	normEm;		// max energy norm
	double	normUi;		// initial displacement norm

	// initialize flags
	bool bconv = false;		// convergence flag
	bool sdflag = true;		// flag for steepest descent iterations in NLCG

	// Get the current step
	FEAnalysis* pstep = m_fem.GetCurrentStep();

	// get the time information
	FETimeInfo tp = m_fem.GetTime();
	tp.alpha = m_alpha;
	tp.beta  = m_beta;
	tp.gamma = m_gamma;

	// prepare for the first iteration
	PrepStep(tp);

	// calculate initial stiffness matrix
	bool breform = m_breformtimestep;
	if (pstep->m_ntotiter == 0) breform = true;
	if (breform)
	{
		// reset the bfgs updates
		if (ReformStiffness(tp) == false) return false;
	}

	// reset reformation flag to false so that we won't reform until necessary
	breform = false;

	// calculate initial residual
	if (Residual(m_R0) == false) return false;

	m_R0 += m_Fd;

	// TODO: I can check here if the residual is zero.
	// If it is than there is probably no force acting on the system
	// if (m_R0*m_R0 < eps) bconv = true;

//	double r0 = m_R0*m_R0;

	// set the initial step length estimates to 1.0
	double s = 1.0;

	// loop until converged or when max nr of reformations reached
	do
	{
		Logfile::MODE oldmode = felog.GetMode();
		if ((pstep->GetPrintLevel() <= FE_PRINT_MAJOR_ITRS) &&
			(pstep->GetPrintLevel() != FE_PRINT_NEVER)) felog.SetMode(Logfile::LOG_FILE);

		felog.printf(" %d\n", m_niter+1);
		felog.SetMode(oldmode);

		// assume we'll converge. 
		bconv = true;
		// solve the equations
		m_SolverTime.start();
		{
			m_pbfgs->SolveEquations(m_ui, m_R0);
		}
		m_SolverTime.stop();

		// set initial convergence norms
		if (m_niter == 0)
		{
			normRi = fabs(m_R0*m_R0);
			normEi = fabs(m_ui*m_R0);
			normUi = fabs(m_ui*m_ui);
			normEm = normEi;
		}

		// perform a linesearch
		// the geometry is also updated in the line search
		if (m_LStol > 0) s = LineSearch(1.0);
		else
		{
			s = 1;

			// Update geometry
			Update(m_ui);

			// calculate residual at this point
			Residual(m_R1);
		}

		// update total displacements
		m_UpdateTime.start();
		{
			int neq = (int)m_Ui.size();
			vector<double> ui(m_ui);
			for (i=0; i<neq; ++i) ui[i] *= s;
			UpdateIncrements(m_Ui, ui, false);

			// calculate norms
			normR1 = m_R1*m_R1;
			normu  = (m_ui*m_ui)*(s*s);
			normU  = m_Ui*m_Ui;
			normE1 = s*fabs(m_ui*m_R1);
		}
		m_UpdateTime.stop();

		// check for nans
		if (ISNAN(normR1) || ISNAN(normu)) throw NANDetected();

		// check residual norm
		if ((m_Rtol > 0) && (normR1 > m_Rtol*normRi)) bconv = false;	

		// check displacement norm
		if ((m_Dtol > 0) && (normu  > (m_Dtol*m_Dtol)*normU )) bconv = false;

		// check energy norm
		if ((m_Etol > 0) && (normE1 > m_Etol*normEi)) bconv = false;

		// check linestep size
		if ((m_LStol > 0) && (s < m_LSmin)) bconv = false;

		// check energy divergence
		if (m_bdivreform)
		{
			if (normE1 > normEm) bconv = false;
		}

		// print convergence summary
		oldmode = felog.GetMode();
		if ((pstep->GetPrintLevel() <= FE_PRINT_MAJOR_ITRS) &&
			(pstep->GetPrintLevel() != FE_PRINT_NEVER)) felog.SetMode(Logfile::LOG_FILE);

		felog.printf(" Nonlinear solution status: time= %lg\n", time); 
		felog.printf("\tstiffness updates             = %d\n", m_pbfgs->m_nups);
		felog.printf("\tright hand side evaluations   = %d\n", m_nrhs);
		felog.printf("\tstiffness matrix reformations = %d\n", m_nref);
		if (m_LStol > 0) felog.printf("\tstep from line search         = %lf\n", s);
		felog.printf("\tconvergence norms :     INITIAL         CURRENT         REQUIRED\n");
		felog.printf("\t   residual         %15le %15le %15le \n", normRi, normR1, m_Rtol*normRi);
		felog.printf("\t   energy           %15le %15le %15le \n", normEi, normE1, m_Etol*normEi);
		felog.printf("\t   displacement     %15le %15le %15le \n", normUi, normu ,(m_Dtol*m_Dtol)*normU );

		felog.SetMode(oldmode);

		// see if we may have a small residual
		if ((bconv == false) && (normR1 < m_Rmin))
		{
			// check for almost zero-residual on the first iteration
			// this might be an indication that there is no force on the system
			felog.printbox("WARNING", "No force acting on the system.");
			bconv = true;
		}

		// check if we have converged. 
		// If not, calculate the BFGS update vectors
		if (bconv == false)
		{
			if (s < m_LSmin)
			{
				// check for zero linestep size
				felog.printbox("WARNING", "Zero linestep size. Stiffness matrix will now be reformed");
				breform = true;
			}
			else if ((normE1 > normEm) && m_bdivreform)
			{
				// check for diverging
				felog.printbox("WARNING", "Problem is diverging. Stiffness matrix will now be reformed");
				normEm = normE1;
				normEi = normE1;
				normRi = normR1;
				breform = true;
			}
			else
			{
				// If we havn't reached max nr of BFGS updates
				// do an update
				if (!breform)
				{
					if (m_pbfgs->m_nups < m_pbfgs->m_maxups-1)
					{
						m_QNTime.start();
						if (m_pbfgs->Update(s, m_ui, m_R0, m_R1) == false)
						{
							// Stiffness update has failed.
							// this might be due a too large condition number
							// or the update was no longer positive definite.
							felog.printbox("WARNING", "The BFGS update has failed.\nStiffness matrix will now be reformed.");
							breform = true;
						}
						m_QNTime.stop();
					}
					else
					{
						// we've reached the max nr of BFGS updates, so
						// we need to do a stiffness reformation
						breform = true;

						// print a warning only if the user did not intent full-Newton
						if (m_pbfgs->m_maxups > 0)
							felog.printbox("WARNING", "Max nr of iterations reached.\nStiffness matrix will now be reformed.");

					}
				}
			}	

			// zero displacement increments
			// we must set this to zero before the reformation
			// because we assume that the prescribed displacements are stored 
			// in the m_ui vector.
			zero(m_ui);

			// reform stiffness matrices if necessary
			if (breform && m_bdoreforms)
			{
				felog.printf("Reforming stiffness matrix: reformation #%d\n\n", m_nref);

				// reform the matrix
				if (ReformStiffness(tp) == false) break;
	
				// reset reformation flag
				breform = false;
			}

			// copy last calculated residual
			m_R0 = m_R1;
		}
		else if (m_baugment)
		{
			// we have converged, so let's see if the augmentations have converged as well
			felog.printf("\n........................ augmentation # %d\n", m_naug+1);

			// plot states before augmentations.
			// The reason we store the state prior to the augmentations
			// is because the augmentations are going to change things such that
			// the system no longer in equilibrium. Since the model has to be converged
			// before we do augmentations, storing the model now will store an actual converged state.
			pstep->GetFEModel().DoCallback(CB_AUGMENT);

			// do the augmentations
			bconv = Augment();

			// update counter
			++m_naug;

			// we reset the reformations counter
			m_nref = 0;
	
			// If we havn't converged we prepare for the next iteration
			if (!bconv) 
			{
				// Since the Lagrange multipliers have changed, we can't just copy 
				// the last residual but have to recalculate the residual
				// we also recalculate the stresses in case we are doing augmentations
				// for incompressible materials
				UpdateStresses();
				Residual(m_R0);

				// reform the matrix if we are using full-Newton
				if (m_pbfgs->m_maxups == 0)
				{
					felog.printf("Reforming stiffness matrix: reformation #%d\n\n", m_nref);
					if (ReformStiffness(tp) == false) break;
				}
			}
		}
	
		// increase iteration number
		m_niter++;

		// let's flush the logfile to make sure the last output will not get lost
		felog.flush();

		// do minor iterations callbacks
		m_fem.DoCallback(CB_MINOR_ITERS);
	}
	while (bconv == false);

	// when converged, 
	// print a convergence summary to the felog file
	if (bconv)
	{
		Logfile::MODE mode = felog.GetMode();
		if (mode != Logfile::LOG_NEVER)
		{
			felog.SetMode(Logfile::LOG_FILE);
			felog.printf("\nconvergence summary\n");
			felog.printf("    number of iterations   : %d\n", m_niter);
			felog.printf("    number of reformations : %d\n", m_nref);
			felog.SetMode(mode);
		}
	}

	// if converged we update the total displacements
	if (bconv)
	{
        UpdateIncrements(m_Ut, m_Ui, true);
	}

	return bconv;
}

//-----------------------------------------------------------------------------
//! Calculates global stiffness matrix.

bool FESolidSolver2::StiffnessMatrix(const FETimeInfo& tp)
{
	// get the stiffness matrix
	SparseMatrix& K = *m_pK;

	// zero stiffness matrix
	K.zero();

	// zero the residual adjustment vector
	zero(m_Fd);

	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

	// calculate the stiffness matrix for each domain
	for (int i=0; i<mesh.Domains(); ++i) 
	{
		FEElasticDomain& dom = dynamic_cast<FEElasticDomain&>(mesh.Domain(i));
		dom.StiffnessMatrix(this);
	}

	// calculate the body force stiffness matrix for each non-rigid domain
	for (int i = 0; i<mesh.Domains(); ++i)
	{
        FEDomain& dom = mesh.Domain(i);
        if (dom.GetMaterial()->IsRigid() == false)
        {
            FEElasticDomain& edom = dynamic_cast<FEElasticDomain&>(dom);
            for (int j=0; j<m_fem.BodyLoads(); ++j)
            {
                FEBodyForce* pbf = dynamic_cast<FEBodyForce*>(m_fem.GetBodyLoad(j));
                if (pbf) edom.BodyForceStiffness(this, *pbf);
            }
        }
	}
    
    // TODO: add body force stiffness for rigid bodies

	// Add mass matrix for dynamic problems
	FEAnalysis* pstep = m_fem.GetCurrentStep();
	if (pstep->m_nanalysis == FE_DYNAMIC)
	{
		// scale factor
		double dt = tp.timeIncrement;
		double a = 1.0 / (m_beta*dt*dt);

		// loop over all domains (except rigid)
		for (int i = 0; i<mesh.Domains(); ++i)
		{
			FEDomain& dom = mesh.Domain(i);
			if (dom.GetMaterial()->IsRigid() == false)
			{
				FEElasticDomain& edom = dynamic_cast<FEElasticDomain&>(dom);
				edom.MassMatrix(this, a);
			}
		}

		m_rigidSolver.RigidMassMatrix(this, tp);
	}

	// calculate contact stiffness
	if (m_fem.SurfacePairInteractions() > 0) 
	{
		ContactStiffness();
	}

	// calculate stiffness matrices for surface loads
	int nsl = m_fem.SurfaceLoads();
	for (int i = 0; i<nsl; ++i)
	{
		FESurfaceLoad* psl = m_fem.SurfaceLoad(i);
		if (psl->IsActive())
		{
			psl->StiffnessMatrix(tp, this); 
		}
	}

	// calculate nonlinear constraint stiffness
	// note that this is the contribution of the 
	// constrainst enforced with augmented lagrangian
	NonLinearConstraintStiffness(tp);

	// calculate the stiffness contributions for the rigid forces
	for (int i = 0; i<m_fem.ModelLoads(); ++i) m_fem.ModelLoad(i)->StiffnessMatrix(this, tp);

	// add contributions from rigid bodies
	m_rigidSolver.StiffnessMatrix(*m_pK, tp);

	return true;
}

//-----------------------------------------------------------------------------
//! Calculate the stiffness contribution due to nonlinear constraints
void FESolidSolver2::NonLinearConstraintStiffness(const FETimeInfo& tp)
{
	int N = m_fem.NonlinearConstraints();
	for (int i=0; i<N; ++i) 
	{
		FENLConstraint* plc = m_fem.NonlinearConstraint(i);
		if (plc->IsActive()) plc->StiffnessMatrix(this, tp);
	}
}

//-----------------------------------------------------------------------------
//! This function calculates the contact stiffness matrix

void FESolidSolver2::ContactStiffness()
{
	for (int i=0; i<m_fem.SurfacePairInteractions(); ++i)
	{
		FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));
		if (pci->IsActive()) pci->ContactStiffness(this);
	}
}

//-----------------------------------------------------------------------------
// \todo I'd like to do something different with this. Right now, if a nodal load
//       it applied to a rigid body, the load has to be translated to a force and 
//       torque applied to the rigid body. Perhaps we should really define two types
//       of nodal loads, one for the deformable body and for the rigid body. This can
//       be done in a pre-processor phase. That way, standard assembly routines can be
//       used to assemble to loads into the global vector.
void FESolidSolver2::AssembleResidual(int node_id, int dof, double f, vector<double>& R)
{
	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

	// get the equation number
	FENode& node = mesh.Node(node_id);
	int n = node.m_ID[dof];

	// assemble into global vector
	if (n >= 0) R[n] += f;
	else m_rigidSolver.AssembleResidual(node_id, dof, f, R);
}

//-----------------------------------------------------------------------------
//! \todo This function is only used for rigid joints. I need to figure out if
//!       I can use the other assembly function.
void FESolidSolver2::AssembleStiffness(std::vector<int>& lm, matrix& ke)
{
	m_pK->Assemble(ke, lm);
}

//-----------------------------------------------------------------------------
// \todo adjust for rigid bodies
void FESolidSolver2::AssembleStiffness2(vector<int>& lmi, vector<int>& lmj, matrix& ke)
{
	m_pK->Assemble(ke, lmi, lmj);

	// adjust for prescribed dofs
	vector<double>& ui = m_ui;

	SparseMatrix& K = *m_pK;
	// loop over columns
	int cols = ke.columns();
	int rows = ke.rows();
	for (int j=0; j<cols; ++j)
	{
		int J = -lmj[j] - 2;
		if ((J >= 0) && (J<m_nreq))
		{
			// dof j is a prescribed degree of freedom

			// loop over rows
			for (int i=0; i<rows; ++i)
				{
					int I = lmi[i];
					if (I >= 0)
					{
						// dof i is not a prescribed degree of freedom
						m_Fd[I] -= ke[i][j]*ui[J];
					}
				}

			// set the diagonal element of K to 1
			K.set(J,J, 1);			
		}
	}

	// TODO: implement this in the FERigidSolver
}

//-----------------------------------------------------------------------------
//!  Assembles the element stiffness matrix into the global stiffness matrix.
//!  Also adjusts the global stiffness matrix and residual to take the 
//!  prescribed displacements into account.

//! \todo In stead of changing the global stiffness matrix to accomodate for 
//!       the rigid bodies and linear constraints, can I modify the element stiffness
//!       matrix prior to assembly? I might have to change the elm vector as well as 
//!       the element matrix size.

void FESolidSolver2::AssembleStiffness(vector<int>& en, vector<int>& elm, matrix& ke)
{
    // get nodal DOFS
    DOFS& fedofs = m_fem.GetDOFS();
    int MAX_NDOFS = fedofs.GetTotalDOFS();

	// assemble into global stiffness matrix
	m_pK->Assemble(ke, elm);

	vector<double>& ui = m_ui;

	// adjust for linear constraints
	FELinearConstraintManager& LCM = m_fem.GetLinearConstraintManager();
	if (LCM.LinearConstraints() > 0)
	{
		LCM.AssembleStiffness(*m_pK, m_Fd, m_ui, en, elm, ke);
	}

	// adjust stiffness matrix for prescribed degrees of freedom
	// NOTE: I had to comment this if statement out since otherwise
	//       poroelastic DOF's that are set as free-draining in the
	//       sliding2 contact code are skipt and zeroes will appear
	//       on the diagonal of the stiffness matrix.
//	if (m_fem.m_DC.size() > 0)
	{
		int i, j;
		int I, J;

		SparseMatrix& K = *m_pK;

		int N = ke.rows();

		// loop over columns
		for (j=0; j<N; ++j)
		{
			J = -elm[j]-2;
			if ((J >= 0) && (J<m_nreq))
			{
				// dof j is a prescribed degree of freedom

				// loop over rows
				for (i=0; i<N; ++i)
				{
					I = elm[i];
					if (I >= 0)
					{
						// dof i is not a prescribed degree of freedom
						m_Fd[I] -= ke[i][j]*ui[J];
					}
				}

				// set the diagonal element of K to 1
				K.set(J,J, 1);			
			}
		}
	}

	// see if there are any rigid body dofs here
	m_rigidSolver.RigidStiffness(*m_pK, m_ui, m_Fd, en, elm, ke, m_alpha);
}

//-----------------------------------------------------------------------------
//! Calculates the contact forces
void FESolidSolver2::ContactForces(FEGlobalVector& R)
{
	for (int i=0; i<m_fem.SurfacePairInteractions(); ++i) 
	{
		FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));
		if (pci->IsActive()) pci->ContactForces(R);
	}
}

//-----------------------------------------------------------------------------
//! calculates the residual vector
//! Note that the concentrated nodal forces are not calculated here.
//! This is because they do not depend on the geometry 
//! so we only calculate them once (in Quasin) and then add them here.

bool FESolidSolver2::Residual(vector<double>& R)
{
	TimerTracker t(m_RHSTime);

	// get the time information
	FETimeInfo tp = m_fem.GetTime();
	tp.alpha = m_alpha;
	tp.beta  = m_beta;
	tp.gamma = m_gamma;

	int i;
	// initialize residual with concentrated nodal loads
	R = m_Fn;

	// zero nodal reaction forces
	zero(m_Fr);

	// setup the global vector
	FEResidualVector RHS(GetFEModel(), R, m_Fr);

	// zero rigid body reaction forces
	m_rigidSolver.Residual();

	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

	// calculate the internal (stress) forces
	for (i=0; i<mesh.Domains(); ++i)
	{
        FEDomain& dom = mesh.Domain(i);
        if (dom.GetMaterial()->IsRigid() == false)
        {
            FEElasticDomain& edom = dynamic_cast<FEElasticDomain&>(dom);
            edom.InternalForces(RHS);
        }
	}

	// calculate the body forces
	for (i=0; i<mesh.Domains(); ++i)
	{
        FEDomain& dom = mesh.Domain(i);
        if (dom.GetMaterial()->IsRigid() == false)
        {
            FEElasticDomain& edom = dynamic_cast<FEElasticDomain&>(dom);
            for (int j=0; j<m_fem.BodyLoads(); ++j)
            {
                FEBodyForce* pbf = dynamic_cast<FEBodyForce*>(m_fem.GetBodyLoad(j));
                edom.BodyForce(RHS, *pbf);
            }
        }
	}
    
    // calculate body forces for rigid bodies
    for (int j=0; j<m_fem.BodyLoads(); ++j)
    {
        FEBodyForce* pbf = dynamic_cast<FEBodyForce*>(m_fem.GetBodyLoad(j));
        m_rigidSolver.BodyForces(RHS, tp, *pbf);
    }
    
    
    // calculate inertial forces for dynamic problems
	if (m_fem.GetCurrentStep()->m_nanalysis == FE_DYNAMIC) InertialForces(RHS);

	// calculate forces due to surface loads
	int nsl = m_fem.SurfaceLoads();
	for (i=0; i<nsl; ++i)
	{
		FESurfaceLoad* psl = m_fem.SurfaceLoad(i);
		if (psl->IsActive()) psl->Residual(tp, RHS);
	}

	// calculate contact forces
	if (m_fem.SurfacePairInteractions() > 0)
	{
		ContactForces(RHS);
	}

	// calculate nonlinear constraint forces
	// note that these are the linear constraints
	// enforced using the augmented lagrangian
	NonLinearConstraintForces(RHS, tp);

	// forces due to point constraints
//	for (i=0; i<(int) fem.m_PC.size(); ++i) fem.m_PC[i]->Residual(this, R);

	// add model loads
	int NML = m_fem.ModelLoads();
	for (i=0; i<NML; ++i)
	{
		FEModelLoad& mli = *m_fem.ModelLoad(i);
		if (mli.IsActive())
		{
			mli.Residual(RHS, tp);
		}
	}

	// set the nodal reaction forces
	// TODO: Is this a good place to do this?
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		node.m_Fr = vec3d(0,0,0);

		int n;
		if ((n = -node.m_ID[m_dofX]-2) >= 0) node.m_Fr.x = -m_Fr[n];
		if ((n = -node.m_ID[m_dofY]-2) >= 0) node.m_Fr.y = -m_Fr[n];
		if ((n = -node.m_ID[m_dofZ]-2) >= 0) node.m_Fr.z = -m_Fr[n];
	}

	// increase RHS counter
	m_nrhs++;

	return true;
}

//-----------------------------------------------------------------------------
//! calculate the nonlinear constraint forces 
void FESolidSolver2::NonLinearConstraintForces(FEGlobalVector& R, const FETimeInfo& tp)
{
	int N = m_fem.NonlinearConstraints();
	for (int i=0; i<N; ++i) 
	{
		FENLConstraint* plc = m_fem.NonlinearConstraint(i);
		if (plc->IsActive()) plc->Residual(R, tp);
	}
}

//-----------------------------------------------------------------------------
//! calculates the concentrated nodal forces
void FESolidSolver2::NodalForces(vector<double>& F, const FETimeInfo& tp)
{
	// zero nodal force vector
	zero(F);

	// loop over nodal loads
	int NNL = m_fem.NodalLoads();
	for (int i=0; i<NNL; ++i)
	{
		const FENodalLoad& fc = *m_fem.NodalLoad(i);
		if (fc.IsActive())
		{
			int dof = fc.GetDOF();
			int N = fc.Nodes();
			for (int j=0; j<N; ++j)
			{
				int nid = fc.NodeID(j);

				// get the nodal load value
				double f = fc.NodeValue(j);
			
				// assemble into residual
				AssembleResidual(nid, dof, f, F);
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! This function calculates the inertial forces for dynamic problems

void FESolidSolver2::InertialForces(FEGlobalVector& R)
{
	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();
    
	// allocate F
	vector<double> F(3*mesh.Nodes());
	zero(F);
    
	// calculate F
	FETimeInfo tp = m_fem.GetTime();
    tp.alpha = m_alpha;
    tp.beta  = m_beta;
    tp.gamma = m_gamma;
    double dt = tp.timeIncrement;

    // Newmark rule
    double a = 1.0 / (m_beta*dt);
    double b = a / dt;
    double c = 1.0 - 0.5/m_beta;

    // update kinematics since acceleration is needed for inertial forces
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
        node.m_at = (node.m_rt - node.m_rp)*b - node.m_vp*a + node.m_ap*c;
        vec3d vt = node.m_vp + (node.m_ap*(1.0 - m_gamma) + node.m_at*m_gamma)*dt;
		node.set_vec3d(m_dofVX, m_dofVY, m_dofVZ, vt);
        
        F[3*i  ] = node.m_at.x;
        F[3*i+1] = node.m_at.y;
        F[3*i+2] = node.m_at.z;
        
        // shell kinematics
        vec3d qt = node.get_vec3d(m_dofU, m_dofV, m_dofW);
        vec3d qp = node.get_vec3d(m_dofUP, m_dofVP, m_dofWP);
        vec3d vqp = node.get_vec3d(m_dofVUP, m_dofVVP, m_dofVWP);
        vec3d aqp = node.get_vec3d(m_dofAUP, m_dofAVP, m_dofAWP);
        vec3d aqt = (qt - qp)*b - vqp*a + aqp*c;
        vec3d vqt = vqp + (aqp*(1.0 - m_gamma) + aqt*m_gamma)*dt;
        node.set_vec3d(m_dofAU, m_dofAV, m_dofAW, aqt);
        node.set_vec3d(m_dofVU, m_dofVV, m_dofVW, vqt);
    }
    
	// calculate the inertial forces for all elastic domains (except rigid domains)
	for (int nd = 0; nd < mesh.Domains(); ++nd)
	{
		FEDomain& dom = mesh.Domain(nd);
		if (dom.GetMaterial()->IsRigid() == false)
		{
			FEElasticDomain& edom = dynamic_cast<FEElasticDomain&>(dom);
			edom.InertialForces(R, F);
		}
	}

	// update rigid bodies
	m_rigidSolver.InertialForces(R, tp);
}
