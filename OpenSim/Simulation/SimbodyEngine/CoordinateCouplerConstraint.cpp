// CoordinateCouplerConstraint.cpp
// Author: Ajay Seth, Frank C. Anderson
/*
* Copyright (c)  2007, Stanford University. All rights reserved. 
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

//=============================================================================
// INCLUDES
//=============================================================================
#include <iostream>
#include <math.h>
#include <OpenSim/Common/Function.h>
#include <OpenSim/Simulation/Model/Model.h>
#include "CoordinateCouplerConstraint.h"


// Helper class to construct functions when user's specify a dependency as qd = f(qi)
// this function casts as C(q) = 0 = f(qi) - qd;

// Excluding this from Doxygen until it has better documentation! -Sam Hamner
    /// @cond
class CompoundFunction : public SimTK::Function {
// returns f1(x[0]) - x[1];
private:
	const SimTK::Function *f1;
    const double scale;

public:
	
	CompoundFunction(const SimTK::Function *cf, double scale) : f1(cf), scale(scale) {
	}

    double calcValue(const SimTK::Vector& x) const {
		SimTK::Vector xf(1);
		xf[0] = x[0];
		return scale*f1->calcValue(xf)-x[1];
    }

	double calcDerivative(const std::vector<int>& derivComponents, const SimTK::Vector& x) const {
		return calcDerivative(SimTK::ArrayViewConst_<int>(derivComponents),x); 
	}

	double calcDerivative(const SimTK::Array_<int>& derivComponents, const SimTK::Vector& x) const {
		if (derivComponents.size() == 1){
			if (derivComponents[0]==0){
				SimTK::Vector x1(1);
				x1[0] = x[0];
				return scale*f1->calcDerivative(derivComponents, x1);
			}
			else if (derivComponents[0]==1)
				return -1;
		}
		else if(derivComponents.size() == 2){
			if (derivComponents[0]==0 && derivComponents[1] == 0){
				SimTK::Vector x1(1);
				x1[0] = x[0];
				return scale*f1->calcDerivative(derivComponents, x1);
			}
		}
        return 0;
    }

    int getArgumentSize() const {
        return 2;
    }
    int getMaxDerivativeOrder() const {
        return 2;
    }

	void setFunction(const SimTK::Function *cf) {
		f1 = cf;
	}
};


//=============================================================================
// STATICS
//=============================================================================
using namespace std;
using namespace SimTK;
using namespace OpenSim;

//=============================================================================
// CONSTRUCTOR(S) AND DESTRUCTOR
//=============================================================================
//_____________________________________________________________________________
/**
 * Destructor.
 */
CoordinateCouplerConstraint::~CoordinateCouplerConstraint()
{
}
//_____________________________________________________________________________
/**
 * Default constructor.
 */
CoordinateCouplerConstraint::CoordinateCouplerConstraint() :
	Constraint(),
	_function(_functionProp.getValueObjPtrRef()),
	_independentCoordNames(_independentCoordNamesProp.getValueStrArray()),
	_dependentCoordName(_dependentCoordNameProp.getValueStr()),
    _scaleFactorProp(PropertyDbl("", 1.0)),
    _scaleFactor(_scaleFactorProp.getValueDbl())
{
	setNull();
	setupProperties();
}
//_____________________________________________________________________________
/**
 * Copy constructor.
 *
 * @param aConstraint CoordinateCouplerConstraint to be copied.
 */
CoordinateCouplerConstraint::CoordinateCouplerConstraint(const CoordinateCouplerConstraint &aConstraint) :
   Constraint(aConstraint),
	_function(_functionProp.getValueObjPtrRef()),
	_independentCoordNames(_independentCoordNamesProp.getValueStrArray()),
	_dependentCoordName(_dependentCoordNameProp.getValueStr()),
    _scaleFactorProp(PropertyDbl("", 1.0)),
    _scaleFactor(_scaleFactorProp.getValueDbl())
{
	setNull();
	setupProperties();
	copyData(aConstraint);
}

//=============================================================================
// CONSTRUCTION
//=============================================================================
//_____________________________________________________________________________
/**
 * Copy this body and return a pointer to the copy.
 * The copy constructor for this class is used.
 *
 * @return Pointer to a copy of this OpenSim::Body.
 */
Object* CoordinateCouplerConstraint::copy() const
{
	CoordinateCouplerConstraint *constraint = new CoordinateCouplerConstraint(*this);
	return(constraint);
}
//_____________________________________________________________________________
/**
 * Copy data members from one CoordinateCouplerConstraint to another.
 *
 * @param aConstraint CoordinateCouplerConstraint to be copied.
 */
void CoordinateCouplerConstraint::copyData(const CoordinateCouplerConstraint &aConstraint)
{
	Constraint::copyData(aConstraint);
	_function = (Function*)Object::SafeCopy(aConstraint._function);
	_independentCoordNames = aConstraint._independentCoordNames;
	_dependentCoordName = aConstraint._dependentCoordName;
    _scaleFactor = aConstraint._scaleFactor;
}

//_____________________________________________________________________________
/**
 * Set the data members of this CoordinateCouplerConstraint to their null values.
 */
void CoordinateCouplerConstraint::setNull()
{
	setType("CoordinateCouplerConstraint");
}

//_____________________________________________________________________________
/**
 * Connect properties to local pointers.
 */
void CoordinateCouplerConstraint::setupProperties()
{
	// Coordinate Coupler Function
	_functionProp.setName("coupled_coordinates_function");
	_propertySet.append(&_functionProp);

	// coordinates that are coupled (by name)
	_independentCoordNamesProp.setName("independent_coordinate_names");
	_propertySet.append(&_independentCoordNamesProp);

		// coordinates that are coupled (by name)
	_dependentCoordNameProp.setName("dependent_coordinate_name");
	_propertySet.append(&_dependentCoordNameProp);

    // scale factor
    _scaleFactorProp.setName("scale_factor");
    _propertySet.append(&_scaleFactorProp);
}

//_____________________________________________________________________________
/**
 * Perform some set up functions that happen after the
 * object has been deserialized or copied.
 *
 * @param aModel OpenSim Model containing this CoordinateCouplerConstraint.
 */
void CoordinateCouplerConstraint::setup(Model& aModel)
{
	// Base class
	Constraint::setup(aModel);

	string errorMessage;

	// Look up the bodies and coordinates being coupled by name in the
	// model and keep lists of their indices
	for(int i=0; i<_independentCoordNames.getSize(); i++){
		if (!_model->updCoordinateSet().contains(_independentCoordNames[i])) {
			errorMessage = "Coordinate coupler: unknown independent coordinate " ;
			errorMessage += _independentCoordNames[i];
			throw (Exception(errorMessage));
		}
	}

	// Last coordinate in the coupler is the dependent coordinate
	if (!_model->updCoordinateSet().contains(_dependentCoordName)) {
		errorMessage = "Coordinate coupler: unknown dependent coordinate " ;
		errorMessage += _dependentCoordName;
		throw (Exception(errorMessage));
	}
}


void CoordinateCouplerConstraint::createSystem(SimTK::MultibodySystem& system) const
{
	/** List of mobilized body indices established when constraint is setup */
	std::vector<SimTK::MobilizedBodyIndex> mob_bodies;
	/** List of coordinate (Q) indices corresponding to the respective body */
	std::vector<SimTK::MobilizerQIndex> mob_qs;

	string errorMessage;

	// Look up the bodies and coordinates being coupled by name in the
	// model and keep lists of their indices
	for(int i=0; i<_independentCoordNames.getSize(); i++){
		// Error checking was handled in setup()
	    Coordinate& aCoordinate = _model->updCoordinateSet().get(_independentCoordNames[i]);
		mob_bodies.push_back(aCoordinate._bodyIndex);
		mob_qs.push_back(SimTK::MobilizerQIndex(aCoordinate._mobilityIndex));
	}

	// Last coordinate in the coupler is the dependent coordinate
	Coordinate& aCoordinate = _model->updCoordinateSet().get(_dependentCoordName);
	mob_bodies.push_back(aCoordinate._bodyIndex);
	mob_qs.push_back(SimTK::MobilizerQIndex(aCoordinate._mobilityIndex));

	if (!mob_qs.size() & (mob_qs.size() != mob_bodies.size())) {
		errorMessage = "CoordinateCouplerConstraint:: requires at least one body and coordinate." ;
		throw (Exception(errorMessage));
	}

	// Create and set the underlying coupler constraint function;
	SimTK::Function *simtkCouplerFunction = new CompoundFunction(_function->createSimTKFunction(), _scaleFactor);


	// Now create a Simbody Constraint::CoordinateCoupler
	SimTK::Constraint::CoordinateCoupler simtkCoordinateCoupler( _model->updMatterSubsystem() ,
																 simtkCouplerFunction, 
																 mob_bodies, mob_qs);

	// Beyond the const Component get the index so we can access the SimTK::Constraint later
	CoordinateCouplerConstraint* mutableThis = const_cast<CoordinateCouplerConstraint *>(this);
	mutableThis->_index = simtkCoordinateCoupler.getConstraintIndex();
}

double CoordinateCouplerConstraint::getValue(const Vector& aIndValues) const
{
	return getFunction().calcValue(aIndValues) * _scaleFactor;
}

//=============================================================================
// SCALE
//=============================================================================
/**
 * Scale the coordinate coupler constraint according to the mobilized body that
 * the dependent coordinate belongs too. The scale factor is determined by 
 * dotting the coordinate axis with that of the translation. Rotations are NOT
 * scaled.
 *
 * @param Scaleset
 */
void CoordinateCouplerConstraint::scale(const ScaleSet& aScaleSet)
{
	Coordinate& depCoordinate = _model->updCoordinateSet().get(_dependentCoordName);
	
	// Only scale if the dependent coordinate is a translation
	if (depCoordinate.getMotionType() == Coordinate::Translational){
		// Constraint scale factor
		double scaleFactor = 1.0;
		// Get appropriate scale factors from parent body
		Vec3 bodyScaleFactors(1.0); 
		const string& parentName = depCoordinate._joint->getParentName();

		// Cycle through the scale set to get the appropriate factors
		for (int i=0; i<aScaleSet.getSize(); i++) {
			Scale& scale = aScaleSet.get(i);
			if (scale.getSegmentName()==parentName) {
				scale.getScaleFactors(bodyScaleFactors);
				break;
			}
		}

		// Assume unifrom scaling unless proven otherwise
		scaleFactor = bodyScaleFactors[0];

		// We can handle non-unifrom scaling along transform axes of custom joints ONLY at this time
		const Joint *joint =  dynamic_cast<const Joint*>(depCoordinate._joint);
		// Simplifies things if we have uniform scaling so check first
		// TODO: Non-uniform scaling below has not been exercised! - ASeth
		if(joint && bodyScaleFactors[0] != bodyScaleFactors[1] ||  bodyScaleFactors[0] != bodyScaleFactors[2] )
		{
			// Get the coordinate axis defined on the parent body
			Vec3 xyzEuler;
			joint->getOrientationInParent(xyzEuler);
			Rotation orientInParent(BodyRotationSequence,xyzEuler[0],XAxis,xyzEuler[1],YAxis,xyzEuler[2],ZAxis);
			
			Vec3 axis;

			throw(Exception("Non-uniform scaling of CoordinateCoupler constraints not implemented."));
		}

		// scale the user-defined OpenSim 
        _scaleFactor *= scaleFactor;
	}
}

//=============================================================================
// OPERATORS
//=============================================================================
//_____________________________________________________________________________
/**
 * Assignment operator.
 *
 * @return Reference to this object.
 */
CoordinateCouplerConstraint& CoordinateCouplerConstraint::operator=(const CoordinateCouplerConstraint &aConstraint)
{
	Constraint::operator=(aConstraint);
	copyData(aConstraint);
	return(*this);
}
