/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "collocation_integrator_internal.hpp"
#include "symbolic/stl_vector_tools.hpp"
#include "symbolic/matrix/sparsity_tools.hpp"
#include "symbolic/matrix/matrix_tools.hpp"
#include "symbolic/sx/sx_tools.hpp"
#include "symbolic/fx/sx_function.hpp"
#include "symbolic/mx/mx_tools.hpp"

using namespace std;
namespace CasADi{

CollocationIntegratorInternal::CollocationIntegratorInternal(const FX& f, const FX& g) : IntegratorInternal(f,g){
  addOption("number_of_finite_elements",     OT_INTEGER,  20, "Number of finite elements");
  addOption("interpolation_order",           OT_INTEGER,  3,  "Order of the interpolating polynomials");
  addOption("collocation_scheme",            OT_STRING,  "radau",  "Collocation scheme","radau|legendre");
  addOption("implicit_solver",               OT_IMPLICITFUNCTION,  GenericType(), "An implicit function solver");
  addOption("implicit_solver_options",       OT_DICTIONARY, GenericType(), "Options to be passed to the NLP Solver");
  addOption("expand_f",                      OT_BOOLEAN,  false, "Expand the ODE/DAE residual function in an SX graph");
  addOption("expand_q",                      OT_BOOLEAN,  false, "Expand the quadrature function in an SX graph");
  addOption("hotstart",                      OT_BOOLEAN,  true, "Initialize the trajectory at the previous solution");
  addOption("quadrature_solver",             OT_LINEARSOLVER,  GenericType(), "An linear solver to solver the quadrature equations");
  addOption("quadrature_solver_options",     OT_DICTIONARY, GenericType(), "Options to be passed to the quadrature solver");
  addOption("startup_integrator",            OT_INTEGRATOR,  GenericType(), "An ODE/DAE integrator that can be used to generate a startup trajectory");
  addOption("startup_integrator_options",    OT_DICTIONARY, GenericType(), "Options to be passed to the startup integrator");
  setOption("name","unnamed_collocation_integrator");
}

void CollocationIntegratorInternal::deepCopyMembers(std::map<SharedObjectNode*,SharedObject>& already_copied){
}

CollocationIntegratorInternal::~CollocationIntegratorInternal(){
}

void CollocationIntegratorInternal::init(){
  
  // Call the base class init
  IntegratorInternal::init();
  
  // Legendre collocation points
  double legendre_points[][6] = {
    {0},
    {0,0.500000},
    {0,0.211325,0.788675},
    {0,0.112702,0.500000,0.887298},
    {0,0.069432,0.330009,0.669991,0.930568},
    {0,0.046910,0.230765,0.500000,0.769235,0.953090}};
    
  // Radau collocation points
  double radau_points[][6] = {
    {0},
    {0,1.000000},
    {0,0.333333,1.000000},
    {0,0.155051,0.644949,1.000000},
    {0,0.088588,0.409467,0.787659,1.000000},
    {0,0.057104,0.276843,0.583590,0.860240,1.000000}};

  // Read options
  bool use_radau;
  if(getOption("collocation_scheme")=="radau"){
    use_radau = true;
  } else if(getOption("collocation_scheme")=="legendre"){
    use_radau = false;
  }
  
  // Hotstart?
  hotstart_ = getOption("hotstart");
  
  // Number of finite elements
  int nk = getOption("number_of_finite_elements");
  
  // Interpolation order
  int deg = getOption("interpolation_order");

  // Assume explicit ODE
  bool explicit_ode = f_.input(DAE_XDOT).size()==0;
  
  // All collocation time points
  double* tau_root = use_radau ? radau_points[deg] : legendre_points[deg];

  // Collocated times
  times_.clear();
  
  // Size of the finite elements
  double h = (tf_-t0_)/nk;
  
  // MX version of the same
  MX h_mx = h;
    
  // Coefficients of the collocation equation
  vector<vector<MX> > C(deg+1,vector<MX>(deg+1));

  // Coefficients of the continuity equation
  vector<MX> D(deg+1);

  // Collocation point
  SXMatrix tau = ssym("tau");

  // For all collocation points
  for(int j=0; j<deg+1; ++j){
    // Construct Lagrange polynomials to get the polynomial basis at the collocation point
    SXMatrix L = 1;
    for(int j2=0; j2<deg+1; ++j2){
      if(j2 != j){
        L *= (tau-tau_root[j2])/(tau_root[j]-tau_root[j2]);
      }
    }
    
    SXFunction lfcn(tau,L);
    lfcn.init();
  
    // Evaluate the polynomial at the final time to get the coefficients of the continuity equation
    lfcn.setInput(1.0);
    lfcn.evaluate();
    D[j] = lfcn.output();

    // Evaluate the time derivative of the polynomial at all collocation points to get the coefficients of the continuity equation
    for(int j2=0; j2<deg+1; ++j2){
      lfcn.setInput(tau_root[j2]);
      lfcn.setFwdSeed(1.0);
      lfcn.evaluate(1,0);
      C[j][j2] = lfcn.fwdSens();
    }
  }
  
  // Initial state
  MX X0("X0",nx_);
  
  // Parameters
  MX P("P",np_);
  
  // Backward state
  MX RX0("RX0",nrx_);
  
  // Backward parameters
  MX RP("RP",nrp_);
  
  // Collocated states
  int nX = (nk*(deg+1)+1)*(nx_+nrx_);
  
  // Unknowns
  MX V("V",nX-nx_-nrx_);
  int offset = 0;
  
  // Get collocated states
  vector<vector<MX> > X(nk+1);
  vector<vector<MX> > RX(nk+1);
  for(int k=0; k<nk; ++k){
    X[k].resize(deg+1);
    RX[k].resize(deg+1);

    // Get the expression for the state vector
    for(int j=0; j<deg+1; ++j){
      
      // If it's the first interval
      if(k==0 && j==0){
        X[k][j] = X0;
      } else {
        X[k][j] = V[range(offset,offset+nx_)];
        offset += nx_;
      }
      RX[k][j] = V[range(offset,offset+nrx_)];
      offset += nrx_;
    }
  }
  
  // State at end time
  X[nk].resize(1);
  X[nk][0] = V[range(offset,offset+nx_)];
  offset += nx_;
  
  RX[nk].resize(1);
  RX[nk][0] = RX0;
  
  // Check offset for consistency
  casadi_assert(offset==V.size());

  // Constraints
  vector<MX> g;
  g.reserve(nk);
  
  // Quadrature expressions
  MX QF = MX::zeros(nq_);
  MX RQF = MX::zeros(nrq_);
  
  // Counter
  int jk = 0;
  
  // For all finite elements
  for(int k=0; k<nk; ++k, ++jk){
  
    // For all collocation points
    for(int j=1; j<deg+1; ++j, ++jk){
      // Get the time
      times_.push_back(h*(k + tau_root[j]));
      MX tk = times_.back();
      
      // Get an expression for the state derivative at the collocation point
      MX xp_jk = 0;
      for(int j2=0; j2<deg+1; ++j2){
        xp_jk += C[j2][j]*X[k][j2];
      }
      
      // Add collocation equations to the NLP
      vector<MX> f_in(DAE_NUM_IN);
      f_in[DAE_T] = tk;
      f_in[DAE_P] = P;
      f_in[DAE_X] = X[k][j];
      
      vector<MX> f_out;
      if(explicit_ode){
        // Assume equation of the form ydot = f(t,y,p)
        f_out = f_.call(f_in);
        g.push_back(h_mx*f_out[DAE_ODE] - xp_jk);
      } else {
        // Assume equation of the form 0 = f(t,y,ydot,p)
        f_in[DAE_XDOT] = xp_jk/h_mx;
        f_out = f_.call(f_in);
        g.push_back(f_out[DAE_ODE]);
      }
      
      // Add the quadrature
      if(nq_>0){
        QF += D[j]*h_mx*f_out[DAE_QUAD];
      }
      
      // Now for the backward problem
      if(nrx_>0){
        
        // Get an expression for the state derivative at the collocation point
        MX rxp_jk = 0;
        for(int j2=0; j2<deg+1; ++j2){
          rxp_jk += C[j2][j]*RX[k][j2];
        }
        
        // Add collocation equations to the NLP
        vector<MX> g_in(RDAE_NUM_IN);
        g_in[RDAE_T] = tk;
        g_in[RDAE_X] = X[k][j];
        g_in[RDAE_P] = P;
        g_in[RDAE_RP] = RP;
        g_in[RDAE_RX] = X[k][j];
        
        vector<MX> g_out;
        if(explicit_ode){
          // Assume equation of the form xdot = f(t,x,p)
          g_out = g_.call(g_in);
          g.push_back(h_mx*g_out[RDAE_ODE] - rxp_jk);
        } else {
          // Assume equation of the form 0 = f(t,x,xdot,p)
          g_in[RDAE_XDOT] = xp_jk/h_mx;
          g_in[RDAE_RXDOT] = rxp_jk/h_mx;
          g_out = g_.call(g_in);
          g.push_back(g_out[RDAE_ODE]);
        }
        
        if(nrq_>0){
          RQF += D[j]*h_mx*g_out[RDAE_QUAD];
        }
      }
    }
    
    // Add end time
    times_.push_back(h*(k + 1));

    // Get an expression for the state at the end of the finite element
    MX xf_k = 0;
    for(int j=0; j<deg+1; ++j){
      xf_k += D[j]*X[k][j];
    }

    // Add continuity equation to NLP
    g.push_back(X[k+1][0] - xf_k);
    
    if(nrx_>0){
      // Get an expression for the state at the end of the finite element
      MX rxf_k = 0;
      for(int j=0; j<deg+1; ++j){
        rxf_k += D[j]*RX[k][j];
      }

      // Add continuity equation to NLP
      g.push_back(RX[k+1][0] - rxf_k);
    }
  }
  
  // Constraint expression
  MX gv = vertcat(g);
    
  // Make sure that the dimension is consistent with the number of unknowns
  casadi_assert_message(gv.size()==V.size(),"Implicit function unknowns and equations do not match");

  // Nonlinear constraint function input
  vector<MX> gfcn_in(1+INTEGRATOR_NUM_IN);
  gfcn_in[0] = V;
  gfcn_in[1+INTEGRATOR_X0] = X0;
  gfcn_in[1+INTEGRATOR_P] = P;
  gfcn_in[1+INTEGRATOR_RX0] = RX0;
  gfcn_in[1+INTEGRATOR_RP] = RP;

  vector<MX> gfcn_out(3);
  gfcn_out[0] = gv;
  gfcn_out[1] = QF;
  gfcn_out[2] = RQF;
  
  // Nonlinear constraint function
  MXFunction gfcn(gfcn_in,gfcn_out);
  
  // Expand f?
  bool expand_f = getOption("expand_f");
  if(expand_f){
    gfcn.init();
    gfcn_ = SXFunction(gfcn);
  } else {
    gfcn_ = gfcn;
  }

  // Get the NLP creator function
  implicitFunctionCreator implicit_function_creator = getOption("implicit_solver");
  
  // Allocate an NLP solver
  implicit_solver_ = implicit_function_creator(gfcn_);
  
  // Pass options
  if(hasSetOption("implicit_solver_options")){
    const Dictionary& implicit_solver_options = getOption("implicit_solver_options");
    implicit_solver_.setOption(implicit_solver_options);
  }
  
  // Initialize the solver
  implicit_solver_.init();
  
  if(hasSetOption("startup_integrator")){
    
    // Create the linear solver
    integratorCreator startup_integrator_creator = getOption("startup_integrator");
    
    // Allocate an NLP solver
    startup_integrator_ = startup_integrator_creator(f_,g_);
    
    // Pass options
    startup_integrator_.setOption("number_of_fwd_dir",0); // not needed
    startup_integrator_.setOption("number_of_adj_dir",0); // not needed
    startup_integrator_.setOption("t0",times_.front());
    startup_integrator_.setOption("tf",times_.back());
    if(hasSetOption("startup_integrator_options")){
      const Dictionary& startup_integrator_options = getOption("startup_integrator_options");
      startup_integrator_.setOption(startup_integrator_options);
    }
    
    // Initialize the startup integrator
    startup_integrator_.init();
  }

  // Mark the system not yet integrated
  integrated_once_ = false;
}
  
void CollocationIntegratorInternal::initAdj(){
}

void CollocationIntegratorInternal::reset(int nsens, int nsensB, int nsensB_store){
  // Call the base class method
  IntegratorInternal::reset(nsens,nsensB,nsensB_store);
  
  // Pass the inputs
  for(int iind=0; iind<INTEGRATOR_NUM_IN; ++iind){
    implicit_solver_.input(iind).set(input(iind));
  }
  
  // Pass the forward seeds
  for(int dir=0; dir<nsens; ++dir){
    for(int iind=0; iind<INTEGRATOR_NUM_IN; ++iind){
      implicit_solver_.fwdSeed(iind,dir).set(fwdSeed(iind,dir));
    }
  }

  // Pass solution guess (if this is the first integration or if hotstart is disabled)
  if(hotstart_==false || integrated_once_==false){
    vector<double>& v = implicit_solver_.output().data();
      
    // Check if an integrator for the startup trajectory has been supplied
    if(!startup_integrator_.isNull()){
      // Use supplied integrator, if any
      for(int iind=0; iind<INTEGRATOR_NUM_IN; ++iind){
        startup_integrator_.input(iind).set(input(iind));
      }
      
      // Reset the integrator
      startup_integrator_.reset();
      
      // Integrate, stopping at all time points
      int offs=nrx_;
      for(vector<double>::const_iterator it=times_.begin(); it!=times_.end(); ++it){
        // Integrate to the time point
        startup_integrator_.integrate(*it);
        
        // Save the solution
        startup_integrator_.output().getArray(&v[offs],nx_);
        
        // Update the vector offset
        offs+=nx_+nrx_;
      }
      
      // Print
      if(verbose()){
        cout << "startup trajectory generated, statistics:" << endl;
        startup_integrator_.printStats();
      }
    
    } else {
      // Initialize with constant trajectory
      const vector<double>& x0 = input(INTEGRATOR_X0).data();
      const vector<double>& rx0 = input(INTEGRATOR_RX0).data();
      for(int offs=0; offs<v.size(); offs+=nx_+nrx_){
        for(int i=0; i<nrx_; ++i){
          v[i+offs] = rx0[i];
        }
        for(int i=0; i<nx_; ++i){
          v[i+offs+nrx_] = x0[i];
        }
      }
    }
  }
    
  // Solve the system of equations
  implicit_solver_.evaluate(nsens);
  
  // Mark the system integrated at least once
  integrated_once_ = true;
  
  output(INTEGRATOR_QF).set(gfcn_.output(1));
  output(INTEGRATOR_RQF).set(gfcn_.output(2));
  for(int dir=0; dir<nsens_; ++dir){
    fwdSens(INTEGRATOR_QF,dir).set(gfcn_.fwdSens(1,dir));
    fwdSens(INTEGRATOR_RQF,dir).set(gfcn_.fwdSens(2,dir));
  }
}

void CollocationIntegratorInternal::resetB(){
}

void CollocationIntegratorInternal::integrate(double t_out){
  const vector<double>& V = implicit_solver_.output().data();
  vector<double>& xf = output(INTEGRATOR_XF).data();
  vector<double>& rxf = output(INTEGRATOR_RXF).data();
  
  for(int i=0; i<nrx_; ++i){
    rxf[i] = V[i];
  }
  for(int i=0; i<nx_; ++i){
    xf[i] = V[V.size()-nx_+i];
  }
  
  for(int dir=0; dir<nsens_; ++dir){
    // Get the forward sensitivities
    const vector<double>& V = implicit_solver_.fwdSens(0,dir).data();
    vector<double>& xf = fwdSens(INTEGRATOR_XF,dir).data();
    vector<double>& rxf = fwdSens(INTEGRATOR_RXF,dir).data();
    
    for(int i=0; i<nrx_; ++i) rxf[i] = V[i];
    for(int i=0; i<nx_; ++i)  xf[i] = V[V.size()-nx_+i];
  }
}

void CollocationIntegratorInternal::integrateB(double t_out){
}

} // namespace CasADi
