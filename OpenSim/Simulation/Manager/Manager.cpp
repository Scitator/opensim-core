/* -------------------------------------------------------------------------- *
 *                           OpenSim:  Manager.cpp                            *
 * -------------------------------------------------------------------------- *
 * The OpenSim API is a toolkit for musculoskeletal modeling and simulation.  *
 * See http://opensim.stanford.edu and the NOTICE file for more information.  *
 * OpenSim is developed at Stanford University and supported by the US        *
 * National Institutes of Health (U54 GM072970, R24 HD065690) and by DARPA    *
 * through the Warrior Web program.                                           *
 *                                                                            *
 * Copyright (c) 2005-2017 Stanford University and the Authors                *
 * Author(s): Frank C. Anderson                                               *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.         *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

/* Note: This code was originally developed by Realistic Dynamics Inc. 
 * Author: Frank C. Anderson 
 */
#include <cstdio>
#include "Manager.h"
#include <OpenSim/Simulation/Model/Model.h>
#include <OpenSim/Simulation/Model/AnalysisSet.h>
#include <OpenSim/Simulation/Model/ControllerSet.h>
#include <OpenSim/Common/Array.h>




using namespace OpenSim;
using namespace std;

#define ASSERT(cond) {if (!(cond)) throw(exception());}
//=============================================================================
// STATICS
//=============================================================================
std::string Manager::_displayName = "Simulator";
//=============================================================================
// DESTRUCTOR
//=============================================================================


//=============================================================================
// CONSTRUCTOR(S)
//=============================================================================
Manager::Manager(Model& model) : Manager(model, true)
{
    //_defaultInteg.reset(
    //        new SimTK::RungeKuttaMersonIntegrator(_model->getMultibodySystem()));
    SimTK::Integrator *vi = new SimTK::SemiExplicitEuler2Integrator(_model->getMultibodySystem());
    vi->setAccuracy(0.001); // reduce accuracy saves time
    _defaultInteg.reset(vi);
    _integ = *_defaultInteg;
}

Manager::Manager(Model& aModel, SimTK::Integrator& integ)
        : Manager(aModel, true) {
    setIntegrator(integ);
}

// Private constructor to handle common tasks of the two constructors above.
Manager::Manager(Model& model, bool dummyVar) :
       _model(&model),
       _performAnalyses(true),
       _writeToStorage(true),
       _controllerSet(&model.updControllerSet())
{
    setNull();

    // STORAGE
    constructStorage();

    // SESSION NAME
    setSessionName(_model->getName());
}

Manager::Manager()
{
    setNull();
}

//=============================================================================
// CONSTRUCTION METHODS
//=============================================================================
//_____________________________________________________________________________
/**
 * Set all member variables to their NULL values.
 */
void Manager::
setNull()
{
    _sessionName = "";
    _ti = 0.0;
    _tf = 1.0;
    _halt = false;
    _specifiedDT = false;
    _constantDT = false;
    _dt = 1.0e-4;
    _performAnalyses=true;
    _writeToStorage=true;
    _tArray.setSize(0);
    _dtArray.setSize(0);
}

//_____________________________________________________________________________
/**
 * Construct the storage utility.
 */
bool Manager::
constructStorage()
{

    Array<string> columnLabels;

    // STATES
    Array<string> stateNames = _model->getStateVariableNames();
    int ny = stateNames.getSize();
    _stateStore.reset(new Storage(512,"states"));
    columnLabels.setSize(0);
    columnLabels.append("time");
    for(int i=0;i<ny;i++) columnLabels.append(stateNames[i]);
    _stateStore->setColumnLabels(columnLabels);

    return(true);
}


//=============================================================================
// GET AND SET
//=============================================================================
//-----------------------------------------------------------------------------
// NAME
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Set the session name of this Manager instance.
 */
void Manager::
setSessionName(const string &aSessionName)
{
    _sessionName = aSessionName;
    if(_integ.get() == nullptr) return;

    // STORAGE NAMES
    string name;
    if(hasStateStorage()) {
        name = _sessionName + "_states";
        getStateStorage().setName(name);
    }
}
//_____________________________________________________________________________
/**
 * Get the session name of this Manager instance.
 */
const string& Manager::
getSessionName() const
{
    return(_sessionName);
}

//_____________________________________________________________________________
/**
 * Get name to be shown for this object in Simtk-model tree

 */
const std::string& Manager::
toString() const
{
    return(_displayName);
}
//-----------------------------------------------------------------------------
// SPECIFIED DT
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Set whether or not to take a specified sequence of deltas during an
 * integration. The time deltas are obtained from what's stored in the
 * vector dt vector (@see setDTVector()).  In order to execute an
 * integration in this manner, the sum of the deltas must cover any
 * requested integration interval.  If not, an exception will be thrown
 * at the beginning of an integration.
 *
 * @param aTrueFalse If true, a specified dt's will be used.
 * If set to false, a variable-step integration or a constant step integration
 * will be used.
 * When set to true, the flag used to indicate whether or not a constant
 * time step is used is set to false.
 *
 * @see setDTVector()
 * @see getUseConstantDT()
 */
void Manager::
setUseSpecifiedDT(bool aTrueFalse)
{
    _specifiedDT = aTrueFalse;
    if(_specifiedDT==true) _constantDT = false;
}
//_____________________________________________________________________________
/**
 * Get whether or not to take a specified sequence of deltas during an
 * integration.
 * The time deltas are obtained from what's stored in the dt vector
 * (@see setDTVector()).  In order to execute an
 * integration in this manner, the sum of the deltas must cover any
 * requested integration interval.  If not, an exception will be thrown
 * at the beginning of an integration.
 *
 * @return If true, a specified time step will be used if possible.
 * If false, a variable-step integration will be performed or a constant
 * time step will be taken.
 *
 * @see getUseConstantDT()
 * @see getDT()
 * @see getTimeVector()
 */
bool Manager::
getUseSpecifiedDT() const
{
    return(_specifiedDT);
}

//-----------------------------------------------------------------------------
// CONSTANT DT
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Set whether or not to take a constant integration time step. The size of
 * the constant integration time step can be set using setDT().
 *
 * @param aTrueFalse If true, constant time steps are used.
 * When set to true, the flag used to indicate whether or not to take
 * specified time steps is set to false.
 * If set to false, a variable-step integration or a constant integration
 * time step will be used.
 *
 * @see setDT()
 * @see setUseSpecifiedDT()
 * @see getUseSpecifiedDT();
 */
void Manager::
setUseConstantDT(bool aTrueFalse)
{
    _constantDT = aTrueFalse;
    if(_constantDT==true) _specifiedDT = false;
}
//_____________________________________________________________________________
/**
 * Get whether or not to use a constant integration time step. The
 * constant integration time step can be set using setDT().
 *
 * @return If true, constant time steps are used.  If false, either specified
 * or variable time steps are used.
 *
 * @see setDT()
 * @see getUseSpecifiedDTs();
 */
bool Manager::
getUseConstantDT() const
{
    return(_constantDT);
}
//-----------------------------------------------------------------------------
// DT ARRAY
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Get the time deltas used in the last integration.
 *
 * @return Constant reference to the dt array.
 */
const OpenSim::Array<double>& Manager::
getDTArray()
{
    return(_dtArray);
}
//_____________________________________________________________________________
/**
 * Set the deltas held in the dt array.  These deltas will be used
 * if the integrator is set to take a specified set of deltas.  In order to
 * integrate using a specified set of deltas, the sum of deltas must cover
 * the requested integration time interval, otherwise an exception will be
 * thrown at the beginning of an integration.
 *
 * Note that the time vector is reconstructed in order to check that the
 * sum of the deltas covers a requested integration interval.
 *
 * @param aN Number of deltas.
 * @param aDT Array of deltas.
 * @param aTI Initial time.  If not specified, 0.0 is assumed.
 * @see getUseSpecifiedDT()
 */
void Manager::setDTArray(const SimTK::Vector_<double>& aDT,double aTI) {
    if(aDT.size() == 0)
        return;

    _dtArray.setSize(0);
    _dtArray.ensureCapacity(aDT.size());
    _tArray.setSize(0);
    _tArray.ensureCapacity(aDT.size() + 1);
    int i;
    for(_tArray.append(aTI), i = 0; i < aDT.size(); ++i) {
        _dtArray.append(aDT[i]);
        _tArray.append(_tArray.getLast() + aDT[i]);
    }
}
//_____________________________________________________________________________
/**
 * Get the delta used for a specified integration step.
 * For step aStep, the delta returned is the delta used to go from
 * step aStep to step aStep+1.
 *
 * @param aStep Index of the desired step.
 * @return Delta.  SimTK::Nan is returned on error.
 */
double Manager::
getDTArrayDT(int aStep)
{
    if((aStep<0) || (aStep>=_dtArray.getSize())) {
        printf("Manager.getDTArrayDT: ERR- invalid step.\n");
        return(SimTK::NaN);
    }

    return(_dtArray[aStep]);
}
//_____________________________________________________________________________
/**
 * Print the dt array.
 */
void Manager::
printDTArray(const char *aFileName)
{
    // OPEN FILE
    FILE *fp;
    if(aFileName==NULL) {
        fp = stdout;
    } else {
        fp = fopen(aFileName,"w");
        if(fp==NULL) {
            printf("Manager.printDTArray: unable to print to file %s.\n",
                aFileName);
            fp = stdout;
        }
    }

    // PRINT
    int i;
    fprintf(fp,"\n\ndt vector =\n");
    for(i=0;i<_dtArray.getSize();i++) {
        fprintf(fp,"%.16lf",_dtArray[i]);
        if(fp!=stdout) fprintf(fp,"\n");
        else fprintf(fp," ");
    }
    fprintf(fp,"\n");

    // CLOSE
    if(fp!=stdout) fclose(fp);
}

//-----------------------------------------------------------------------------
// TIME ARRAY
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Get the sequence of time steps taken in the last integration.
 */
const OpenSim::Array<double>& Manager::
getTimeArray()
{
    return(_tArray);
}
//_____________________________________________________________________________
/**
 * Get the integration step (index) that occurred prior to or at 
 * a specified time.
 *
 * @param aTime Time of the integration step.
 * @return Step that occurred prior to or at aTime.  0 is returned if there
 * is no such time stored.
 */
int Manager::
getTimeArrayStep(double aTime)
{
    int step = _tArray.searchBinary(aTime);
    return(step);
}
//_____________________________________________________________________________
/**
 * Get the next time in the time array
 *
 * @param aTime Time of the integration step.
 * @return next time
 */
double Manager::
getNextTimeArrayTime(double aTime)
{
    return(getTimeArrayTime( _tArray.searchBinary(aTime)+1));
}
//_____________________________________________________________________________
//
/**
 * Get the time of a specified integration step.
 *
 * @param aStep Index of the desired step.
 * @return Time of integration step aStep.  SimTK::NaN is returned on error.
 */
double Manager::
getTimeArrayTime(int aStep)
{
    if((aStep<0) || (aStep>=_tArray.getSize())) {
        printf("Manager.getTimeArrayTime: ERR- invalid step.\n");
        return(SimTK::NaN);
    }

    return(_tArray[aStep]);
}
//_____________________________________________________________________________
/**
 * Print the time array.
 *
 * @param aFileName Name of the file to which to print.  If the time array
 * cannot be written to a file of the specified name, the time array is
 * written to standard out.
 */
void Manager::
printTimeArray(const char *aFileName)
{
    // OPEN FILE
    FILE *fp;
    if(aFileName==NULL) {
        fp = stdout;
    } else {
        fp = fopen(aFileName,"w");
        if(fp==NULL) {
            printf("Manager.printTimeArray: unable to print to file %s.\n",
                aFileName);
            fp = stdout;
        }
    }

    // PRINT
    int i;
    fprintf(fp,"\n\ntime vector =\n");
    for(i=0;i<_tArray.getSize();i++) {
        fprintf(fp,"%.16lf",_tArray[i]);
        if(fp!=stdout) fprintf(fp,"\n");
        else fprintf(fp," ");
    }
    fprintf(fp,"\n");

    // CLOSE
    if(fp!=stdout) fclose(fp);
}
//_____________________________________________________________________________
/**
 * Reset the time and dt arrays so that all times after the specified time
 * and the corresponding deltas are erased.
 *
 * @param aTime Time after which to erase the entries in the time and dt
 * vectors.
 */
void Manager::
resetTimeAndDTArrays(double aTime)
{
    int size = getTimeArrayStep(aTime);
    _tArray.setSize(size+1);
    _dtArray.setSize(size);
}

//-----------------------------------------------------------------------------
// INTEGRAND
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Sets the model  and initializes other entities that depend on it
 */
void Manager::
setModel(Model& aModel)
{
    if(_model!=NULL){
        // May need to issue a warning here that model was already set to avoid a leak.
    }

    if (_timeStepper) {
        std::string msg = "Cannot set a new Model on this Manager";
        msg += "after Manager::integrate() has been called at least once.";
        OPENSIM_THROW(Exception, msg);
    }

    _model = &aModel;

    // STORAGE
    constructStorage();

    // SESSION NAME
    setSessionName(_model->getName());
}

//-----------------------------------------------------------------------------
// INTEGRATOR
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Get the integrator.
 */
SimTK::Integrator& Manager::
getIntegrator() const
{
    return *_integ;
}
/**
 * Set the integrator.
 */
void Manager::
setIntegrator(SimTK::Integrator& integrator) 
{   
    if (_integ.get() == &integrator) return;
    if (_timeStepper) {
        std::string msg = "Cannot set a new integrator on this Manager";
        msg += "after Manager::integrate() has been called at least once.";
        OPENSIM_THROW(Exception, msg);
    }

    _integ = &integrator;
    // If we had been using the _defaultInteg, we no longer need it.
    _defaultInteg.reset();
}

//-----------------------------------------------------------------------------
// INITIAL AND FINAL TIME
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Set the initial time of the simulation.
 *
 * @param aTI Initial time.
 */
void Manager::
setInitialTime(double aTI)
{
    _ti = aTI;
}
//_____________________________________________________________________________
/**
 * Get the initial time of the simulation.
 *
 * @return Initial time.
 */
double Manager::
getInitialTime() const
{
    return(_ti);
}
//_____________________________________________________________________________
/**
 * Set the final time of the simulation.
 *
 * @param aTF Final time.
 */
void Manager::
setFinalTime(double aTF)
{
    _tf = aTF;
}
//_____________________________________________________________________________
/**
 * Get the final time of the simulation.
 *
 * @return Final time.
 */
double Manager::
getFinalTime() const
{
    return(_tf);
}


//=============================================================================
// EXECUTION
//=============================================================================

//-----------------------------------------------------------------------------
// STATE STORAGE
//-----------------------------------------------------------------------------
//_____________________________________________________________________________
/**
 * Set the storage buffer for the integration states.
 */
void Manager::
setStateStorage(Storage& aStorage)
{
    _stateStore.reset(&aStorage);
}
//_____________________________________________________________________________
/**
 * Get the storage buffer for the integration states.
 */
Storage& Manager::
getStateStorage() const 
{
    if(!_stateStore)
        throw Exception("Manager::getStateStorage(): Storage is not set");
    return *_stateStore;
}

TimeSeriesTable Manager::getStatesTable() const {
    return getStateStorage().getAsTimeSeriesTable();
}

//_____________________________________________________________________________
/**
 * Get whether there is a storage buffer for the integration states.
 */
bool Manager::
hasStateStorage() const
{
    return _stateStore != nullptr;
}

//-----------------------------------------------------------------------------
// INTEGRATION
//-----------------------------------------------------------------------------

bool Manager::
integrate(SimTK::State& s, double finalTime)
{
    int step = 0; // for AnalysisSet::step()

    if (!_integ) {
        throw Exception("Manager::integrate(): "
            "Integrator has not been set. Construct the Manager "
            "with an integrator, or call Manager::setIntegrator().");
    }

    // CLEAR ANY INTERRUPT
    // Halts must arrive during an integration.
    clearHalt();

    // CHECK SPECIFIED DT STEPPING
    double initialTime = s.getTime();
    if (_specifiedDT) {
        if (_tArray.getSize() <= 0) {
            string msg = "IntegRKF.integrate: ERR- specified dt stepping not";
            msg += "possible-- empty time array.";
            throw(Exception(msg));
        }
        double first = _tArray[0];
        double last = _tArray.getLast();
        if ((getTimeArrayStep(initialTime)<0) || (initialTime<first) || (finalTime>last)) {
            string msg = "IntegRKF.integrate: ERR- specified dt stepping not";
            msg += "possible-- time array does not cover the requested";
            msg += " integration interval.";
            throw(Exception(msg));
        }
    }

    // RECORD FIRST TIME STEP
    if (!_specifiedDT) {
        resetTimeAndDTArrays(initialTime);
        if (_tArray.getSize() <= 0) {
            _tArray.append(initialTime);
        }
    }
    bool fixedStep = false;
    if (_constantDT || _specifiedDT) fixedStep = true;

    // Only initialize a TimeStepper if it hasn't been done yet
    if (_timeStepper == NULL) initializeTimeStepper(s);

    SimTK::Integrator::SuccessfulStepStatus status;

    if (!fixedStep) {
        _integ->setReturnEveryInternalStep(true);
    }

    _model->realizeVelocity(s);
    initializeStorageAndAnalyses(s);

    if (fixedStep) {
        _model->realizeAcceleration(s);

        if (_performAnalyses) _model->updAnalysisSet().step(s, step);
        if (_writeToStorage) {
            SimTK::Vector stateValues = _model->getStateVariableValues(s);
            StateVector vec;
            vec.setStates(s.getTime(), stateValues);
            getStateStorage().append(vec);
            if (_model->isControlled())
                _controllerSet->storeControls(s, step);
        }
    }

    double time = initialTime;
    double stepToTime = finalTime;

    // LOOP
    while (time < finalTime) {
        double fixedStepSize;
        if (fixedStep) {
            fixedStepSize = getNextTimeArrayTime(time) - time;
            if (fixedStepSize + time >= finalTime)  fixedStepSize = finalTime - time;
            _integ->setFixedStepSize(fixedStepSize);
            stepToTime = time + fixedStepSize;
        }

        // stepTo() does not return if it fails. However, the final step
        // is returned once as an ordinary return; by the time we get
        // EndOfSimulation status we have already seen the state and don't
        // need to record it again.
        status = _timeStepper->stepTo(stepToTime);

        if (status != SimTK::Integrator::EndOfSimulation) {
            const SimTK::State& s = _integ->getState();
            if (_performAnalyses) _model->updAnalysisSet().step(s, step);
            if (_writeToStorage) {
                SimTK::Vector stateValues = _model->getStateVariableValues(s);
                StateVector vec;
                vec.setStates(s.getTime(), stateValues);
                getStateStorage().append(vec);
                if (_model->isControlled())
                    _controllerSet->storeControls(s, step);
            }
            step++;
        }
        else
            halt();

        time = _integ->getState().getTime();
        // CHECK FOR INTERRUPT
        if (checkHalt()) break;
    }
    finalize(_integ->updAdvancedState());
    s = _integ->getState();

    // CLEAR ANY INTERRUPT
    clearHalt();

    return true;

}

/**
 * Integrate the equations of motion for the specified model.
 *
 * This method starts the integration at the initial default states of
 * the model.
 */
bool Manager::
integrate(SimTK::State& s)
{
    s.setTime( _ti );
    return integrate(s, _tf);
}

//_____________________________________________________________________________
/**
 * return the step size when the integrator is taking fixed
 * step sizes
 * 
 * @param tArrayStep Step number
 */
double Manager::getFixedStepSize(int tArrayStep) const {
    if( _constantDT ) 
        return( _dt );
    else { 
        if( tArrayStep >= _dtArray.getSize() ) 
             return( _dtArray[ _dtArray.getSize()-1 ] );
        else 
            return(_dtArray[tArrayStep]);
    }
}
//_____________________________________________________________________________
/**
 * initialize storages and analyses 
 * 
 * @param s system state before integration
 */
void Manager::initializeStorageAndAnalyses(SimTK::State& s)
{
    if( _writeToStorage && _performAnalyses ) { 

        double tReal = s.getTime();
    
        // STORE STARTING CONTROLS
        if (_model->isControlled()){
            _controllerSet->setModel(*_model);
            _controllerSet->storeControls(s, 0);
        }

        // STORE STARTING STATES
        if(hasStateStorage()) {
            // ONLY IF NO STATES WERE PREVIOUSLY STORED
            if(getStateStorage().getSize()==0) {
                SimTK::Vector stateValues = _model->getStateVariableValues(s);
                getStateStorage().store(0,tReal,stateValues.size(), &stateValues[0]);
            }
        }

        // ANALYSES 
        AnalysisSet& analysisSet = _model->updAnalysisSet();
        analysisSet.begin(s);
    }

    return;
}
//_____________________________________________________________________________
/**
* set and initialize a SimTK::TimeStepper
*/
void Manager::initializeTimeStepper(const SimTK::State& s)
{
    _timeStepper.reset(
        new SimTK::TimeStepper(_model->getMultibodySystem(), *_integ));
    _timeStepper->initialize(s);
    _timeStepper->setReportAllSignificantStates(true);
}

//_____________________________________________________________________________
/**
 * finalize storages and analyses
 * 
 * @param s system state before integration
 */
void Manager::finalize(SimTK::State& s )
{
        // ANALYSES 
    if(  _performAnalyses ) { 
        AnalysisSet& analysisSet = _model->updAnalysisSet();
        analysisSet.end(s);
    }

    return;
}
//=============================================================================
// INTERRUPT
//=============================================================================
//_____________________________________________________________________________
/**
 * Halt an integration.
 *
 * If an integration is pending or executing, the value of the interrupt
 * flag is set to true.
 */
void Manager::halt()
{
        _halt = true;
}
//_____________________________________________________________________________
/**
 * Clear the halt flag.
 *
 * The value of the interrupt flag is set to false.
 */
void Manager::clearHalt()
{
    _halt = false;
}
//_____________________________________________________________________________
/**
 * Check for a halt request.
 *
 * The value of the halt flag is simply returned.
 */
bool Manager::checkHalt()
{
    return(_halt);
}



