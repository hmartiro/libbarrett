/*
	Copyright 2009, 2010 Barrett Technology <support@barrett.com>

	This file is part of libbarrett.

	This version of libbarrett is free software: you can redistribute it
	and/or modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This version of libbarrett is distributed in the hope that it will be
	useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this version of libbarrett.  If not, see
	<http://www.gnu.org/licenses/>.

	Further, non-binding information about licensing is available at:
	<http://wiki.barrett.com/libbarrett/wiki/LicenseNotes>
*/

/*
 * first_order_filter-inl.h
 *
 *  Created on: Nov 18, 2009
 *      Author: dc
 */


#include <barrett/systems/abstract/single_io.h>


namespace barrett {
namespace systems {


template<typename T>
FirstOrderFilter<T>::FirstOrderFilter(bool updateEveryExecutionCycle) :
	SingleIO<T, T>(updateEveryExecutionCycle)
{
	getSamplePeriodFromEM();
}

template<typename T>
FirstOrderFilter<T>::FirstOrderFilter(const libconfig::Setting& setting, bool updateEveryExecutionCycle) :
	SingleIO<T, T>(updateEveryExecutionCycle), math::FirstOrderFilter<T>(setting)
{
	getSamplePeriodFromEM();
}

// TODO(dc): anyway to remove the code duplication with PIDController?
template<typename T>
void FirstOrderFilter<T>::setExecutionManager(ExecutionManager* newEm)
{
	SingleIO<T, T>::setExecutionManager(newEm);  // call super
	getSamplePeriodFromEM();
}

template<typename T>
void FirstOrderFilter<T>::operate()
{
	this->outputValue->setValue(eval(this->input.getValue()));
}


// TODO(dc): anyway to remove the code duplication with PIDController?
template<typename T>
inline void FirstOrderFilter<T>::getSamplePeriodFromEM()
{
	if (this->isExecutionManaged()) {
		this->setSamplePeriod(this->getExecutionManager()->getPeriod());
	} else {
		this->setSamplePeriod(0.0);
	}
}


}
}
