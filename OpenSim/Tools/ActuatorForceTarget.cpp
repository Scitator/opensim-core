// ActuatorForceTarget.cpp
/*
* Copyright (c)  2005, Stanford University. All rights reserved. 
* Use of the OpenSim software in source form is permitted provided that the following
* conditions are met:
* 	1. The software is used only for non-commercial research and education. It may not
*     be used in relation to any commercial activity.
* 	2. The software is not distributed or redistributed.  Software distribution is allowed 
*     only through https://simtk.org/home/opensim.
* 	3. Use of the OpenSim software or derivatives must be acknowledged in all publications,
*      presentations, or documents describing work in which OpenSim or derivatives are used.
* 	4. Credits to developers may not be removed from executables
*     created from modifications of the source.
* 	5. Modifications of source code must retain the above copyright notice, this list of
*     conditions and the following disclaimer. 
* 
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
*  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
*  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
*  SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
*  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
*  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
*  OR BUSINESS INTERRUPTION) OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
*  WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
//
// This software, originally developed by Realistic Dynamics, Inc., was
// transferred to Stanford University on November 1, 2006.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


//==============================================================================
// INCLUDES
//==============================================================================
#include <iostream>
#include <OpenSim/Common/Exception.h>
#include <OpenSim/Simulation/Model/Actuator.h>
#include <OpenSim/Simulation/Model/ForceSet.h>
#include "CMC.h"
#include "ActuatorForceTarget.h"
#include "CMC_TaskSet.h"
#include <SimTKmath.h>
#include <SimTKlapack.h>
#include "StateTrackingTask.h"

using namespace std;
using namespace OpenSim;
using SimTK::Real;
using SimTK::Vector;
using SimTK::Matrix;

#define USE_PRECOMPUTED_PERFORMANCE_MATRICES
#define USE_LAPACK_DIRECT_SOLVE

//==============================================================================
// DESTRUCTOR & CONSTRUCTIOR(S)
//==============================================================================
//______________________________________________________________________________
/**
 * Destructor.
 */
ActuatorForceTarget::~ActuatorForceTarget()
{
	delete[] _lapackA;
	delete[] _lapackB;
	delete[] _lapackSingularValues;
	delete[] _lapackWork;
}
//______________________________________________________________________________
/**
 * Constructor.
 *
 * @param aNX Number of controls.
 * @param aController Parent controller.
 */
ActuatorForceTarget::ActuatorForceTarget(int aNX,CMC *aController) :
	OptimizationTarget(aNX), _controller(aController), _stressTermWeight(1.0)
{
	// NUMBER OF CONTROLS
	if(getNumParameters()<=0) {
		throw(Exception("ActuatorForceTarget: ERROR- no controls.\n"));
	}

	// ALLOCATE STATE ARRAYS
	int ny = _controller->getModel().getNumStates();
	int nq = _controller->getModel().getNumCoordinates();
	int nu = _controller->getModel().getNumSpeeds();
	_y.setSize(ny);
	_dydt.setSize(ny);
	_dqdt.setSize(nq);
	_dudt.setSize(nu);

	// DERIVATIVE PERTURBATION SIZES;
	setDX(1.0e-6);

	_lapackA = 0;
	_lapackB = 0;
	_lapackSingularValues = 0;
	_lapackLWork = 0;
	_lapackWork = 0;
}

//==============================================================================
// SET AND GET
//==============================================================================
//______________________________________________________________________________
/**
 * Set the weight of the actuator force stress term in the performance
 * criterion.
 *
 * @param aWeight Weight premultiplying the sum squared stresses.
 */
void ActuatorForceTarget::
setStressTermWeight(double aWeight)
{
	_stressTermWeight = aWeight;
}


//==============================================================================
// PERFORMANCE AND CONSTRAINTS
//==============================================================================
//------------------------------------------------------------------------------
// PERFORMANCE
//------------------------------------------------------------------------------

bool ActuatorForceTarget::
prepareToOptimize(SimTK::State& s, double *x)
{
	// Keep around a "copy" of the state so we can use it in objective function 
	// in cases where we're tracking states
	_saveState = s;
#ifdef USE_PRECOMPUTED_PERFORMANCE_MATRICES
	int nu = _controller->getModel().getNumSpeeds();
	int nf = _controller->getModel().getActuators().getSize();
	int ny = _controller->getModel().getNumStates();
	int nacc = _controller->updTaskSet().getDesiredAccelerations().getSize();

	_accelPerformanceMatrix.resize(nacc,nf);
	_accelPerformanceVector.resize(nacc);

	_forcePerformanceMatrix.resize(nf,nf);
	_forcePerformanceVector.resize(nf);

	Vector f(nf), accelVec(nacc), forceVec(nf);

	// Build matrices and vectors assuming performance is a linear least squares problem.
	// i.e. assume we're solving
	//   min || _accelPerformanceMatrix * x + _accelPerformanceVector ||^2 + || _forcePerformanceMatrix * x + _forcePerformanceVector || ^2
	f = 0;
	computePerformanceVectors(s, f, _accelPerformanceVector, _forcePerformanceVector);


	for(int j=0; j<nf; j++) {
		f[j] = 1;
		computePerformanceVectors(s, f, accelVec, forceVec);
		for(int i=0; i<nacc; i++) _accelPerformanceMatrix(i,j) = (accelVec[i] - _accelPerformanceVector[i]);
		for(int i=0; i<nf; i++) _forcePerformanceMatrix(i,j) = (forceVec[i] - _forcePerformanceVector[i]);
		f[j] = 0;
	}

#ifdef USE_LAPACK_DIRECT_SOLVE
	// 
	// Try to solve using lapack
	//
	int Am = nacc+nf;

	if(!_lapackA) {
		_lapackA = new double[Am*nf];
		_lapackB = new double[Am];
		_lapackSingularValues = new double[Am];
		// based on lapack documentation but multiplied by 10 to make it bigger yet (they recommended it be bigger than the minimum)
		_lapackLWork = 10*(3*Am + max(2*nf,Am)); 
		_lapackWork = new double[_lapackLWork];
	}

	for(int i=0; i<nacc; i++) for(int j=0; j<nf; j++) _lapackA[j*Am+i] = _accelPerformanceMatrix(i,j);
	for(int i=nacc; i<Am; i++) for(int j=0; j<nf; j++) _lapackA[j*Am+i] = _forcePerformanceMatrix(i-nacc,j);
	for(int i=0; i<nacc; i++) _lapackB[i] = -_accelPerformanceVector[i];
	for(int i=nacc; i<Am; i++) _lapackB[i] = -_forcePerformanceVector[i-nacc];

	int info;
	int nrhs = 1;
	double rcond = 1e-10;
	int rank;

	dgelss_(Am, nf, nrhs, _lapackA, Am, _lapackB, Am, _lapackSingularValues, rcond, rank, _lapackWork, _lapackLWork, info);

	// Assume it's valid to begin with
	bool gotValidSolution = true;

	// Check if it satisfies parameter bounds (if they exist)
	if(getHasLimits()) {
		double *lowerBounds, *upperBounds;
		getParameterLimits(&lowerBounds,&upperBounds);
		for(int i=0; i<nf; i++) {
			if(_lapackB[i] < lowerBounds[i] || _lapackB[i] > upperBounds[i]) {
				gotValidSolution = false;
				break;
			}
		}
	}

	// If it's still valid, we return this as the solution and return true since the iterative optimizer doesn't need to be run
	if(gotValidSolution) {
		for(int i=0; i<nf; i++)
			x[i] = _lapackB[i];
		return true;
	}

#if 0
	//
	// Test lapack solution
	//
	SimTK::Vector answer(nf, b);
	std::cout << "Result from dgglse: " << info << ", rank " << rank << ": " << std::endl << answer << std::endl;
	double p = (_accelPerformanceMatrix * answer + _accelPerformanceVector).normSqr() + (_forcePerformanceMatrix * answer + _forcePerformanceVector).normSqr();
	std::cout << "Performance: " << p << std::endl;
	std::cout << "Violated bounds:\n";
	ForceSet& Force = model->getForceSet();
	for(int i=0; i<nf; i++) if(answer[i]<_lowerBounds[i] || answer[i]>_upperBounds[i])
		std::cout << i << " (" << fSet.get(i).getName() << ") got " << answer[i] << ", bounds are (" << _lowerBounds[i] << "," << _upperBounds[i] << ")" << std::endl;
#endif

#endif

	// Compute the performance gradients (again assuming we have the above linear least squares problem)
	_performanceGradientMatrix = ~_accelPerformanceMatrix * _accelPerformanceMatrix + ~_forcePerformanceMatrix * _forcePerformanceMatrix;
	_performanceGradientMatrix *= 2;
	_performanceGradientVector = ~_accelPerformanceMatrix * _accelPerformanceVector + ~_forcePerformanceMatrix * _forcePerformanceVector;
	_performanceGradientVector *= 2;

	return false;
#endif
}

void ActuatorForceTarget::
computePerformanceVectors(SimTK::State& s, const Vector &aF, Vector &rAccelPerformanceVector, Vector &rForcePerformanceVector)
{
	const Set<Actuator> &fSet = _controller->getModel().getActuators();

	for(int i=0;i<fSet.getSize();i++) {
        Actuator& act = fSet.get(i);
        act.setOverrideForce(s, aF[i]);
        act.overrideForce(s,true);

        
	}
    _controller->getModel().getSystem().realize(s, SimTK::Stage::Acceleration );

	CMC_TaskSet& taskSet = _controller->updTaskSet();
	taskSet.computeAccelerations(s);
	Array<double> &w = taskSet.getWeights();
	Array<double> &aDes = taskSet.getDesiredAccelerations();
	Array<double> &a = taskSet.getAccelerations();

	// PERFORMANCE
	double sqrtStressTermWeight = sqrt(_stressTermWeight);
	for(int i=0;i<fSet.getSize();i++) {
        Actuator& act = fSet.get(i);
        rForcePerformanceVector[i] = sqrtStressTermWeight * act.getStress(s);
     }

	int nacc = aDes.getSize();
	for(int i=0;i<nacc;i++) rAccelPerformanceVector[i] = sqrt(w[i]) * (a[i] - aDes[i]);

	// reset the actuator control
	for(int i=0;i<fSet.getSize();i++) {
        Actuator& act = fSet.get(i);
        act.overrideForce(s,false);
	}


}

//______________________________________________________________________________
/**
 * Compute performance given x.
 *
 * @param aF Vector of controls.
 * @param rP Value of the performance criterion.
 * @return Status (normal termination = 0, error < 0).
 */
int ActuatorForceTarget::
objectiveFunc(const Vector &aF, const bool new_coefficients, Real& rP) const
{
	const CMC_TaskSet& tset=_controller->getTaskSet();
#ifndef USE_PRECOMPUTED_PERFORMANCE_MATRICES

	// Explicit computation of performance (use this if it's not actually linear)
	int nf = _controller->getModel()->getActuators().getSize();
	int nacc = _controller->getTaskSet()->getDesiredAccelerations().getSize();
	Vector pacc(nacc), pf(nf);
	computePerformanceVectors(aF,pacc,pf);
	rP = pacc.normSqr() + pf.normSqr();

#else

	// Use precomputed matrices/vectors to simplify computing performance (works if it's really linear)
	rP = (_accelPerformanceMatrix * aF + _accelPerformanceVector).normSqr() + (_forcePerformanceMatrix * aF + _forcePerformanceVector).normSqr();
#endif
	// If tracking states, add in errors from them squared
	for(int t=0; t<tset.getSize(); t++){
		TrackingTask& ttask = tset.get(t);
		StateTrackingTask* stateTask=NULL;
		if ((stateTask=dynamic_cast<StateTrackingTask*>(&ttask))!= NULL){
			double err = stateTask->getTaskError(_saveState);
			rP+= (err * err);

		}
	}

	return(0);
}
//______________________________________________________________________________
/**
 * Compute the gradient of performance given x.
 *
 * @param x Array of controls.
 * @param dpdx Derivatives of performance with respect to the controls.
 * @return Status (normal termination = 0, error < 0).
 */
int ActuatorForceTarget::
gradientFunc(const Vector &x, const bool new_coefficients, Vector &gradient) const
{
	int status = 0;

#ifndef USE_PRECOMPUTED_PERFORMANCE_MATRICES

	// Explicit computation of derivative
	status = OptimizationTarget::CentralDifferences(this,&_dx[0],x,gradient);

#else

	// Use precomputed matrices/vectors
	gradient = _performanceGradientMatrix * x + _performanceGradientVector;

#endif

	return status;
}
