/* Copyright 2014-2018 Rsyn
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#include <queue>
#include <vector>
#include <limits>
#include <iomanip>

#include <Rsyn/Session>
#include "rsyn/sandbox/Sandbox.h"

#include "rsyn/model/timing/SandboxTimer.h"
#include "rsyn/model/scenario/Scenario.h"

#include "rsyn/util/FloatingPoint.h"
#include "rsyn/util/ThreadPool.h"
#include "rsyn/util/MD5.h"

namespace Rsyn {

// -----------------------------------------------------------------------------

const bool SandboxTimer::ENABLE_IITIMER_COMPATIBILITY_MODE = false;
const bool SandboxTimer::ENABLE_UITIMER_COMPATIBILITY_MODE = true;

// -----------------------------------------------------------------------------

// [TODO] Use functors, not lambda.

const std::function<bool(const Number a, const Number b)>
SandboxTimer::TM_MODE_COMPARATORS[NUM_TIMING_MODES] = {
	[](const Number a, const Number b) -> bool { // EARLY
		return a < b;
	},
	[](const Number a, const Number b) -> bool { // LATE
		return a > b;
	}
}; // end array

// -----------------------------------------------------------------------------

// [TODO] Use functors, not lambda.

const std::function<bool(const Number a, const Number b)>
SandboxTimer::TM_MODE_REVERSE_COMPARATORS[NUM_TIMING_MODES] = {
	[](const Number a, const Number b) -> bool { // EARLY
		return a > b;
	},
	[](const Number a, const Number b) -> bool { // LATE
		return a < b;
	}
}; // end array

// -----------------------------------------------------------------------------

// [TODO] Use functors, not lambda.

const std::function<Number(const Number a, const Number b)>
SandboxTimer::TM_MODE_WORST_DELAY_AND_ARRIVAL[NUM_TIMING_MODES] = {
	[](const Number a, const Number b) -> Number { // EARLY
		return std::min(a, b);
	},
	[](const Number a, const Number b) -> Number { // LATE
		return std::max(a, b);
	}
}; // end array

// -----------------------------------------------------------------------------

// [TODO] Use functors, not lambda.

const std::function<Number(const Number a, const Number b)>
SandboxTimer::TM_MODE_WORST_REQUIRED[NUM_TIMING_MODES] = {
	[](const Number a, const Number b) -> Number { // EARLY
		return std::max(a, b);
	},
	[](const Number a, const Number b) -> Number { // LATE
		return std::min(a, b);
	}
}; // end array

// -----------------------------------------------------------------------------

void SandboxTimer::initializeTimingCell(Rsyn::SandboxCell rsynCell) {
	Rsyn::LibraryCell rysnLibraryCell = rsynCell.getLibraryCell();

	TimingLibraryCell &timingLibraryCell = getTimingLibraryCell(rysnLibraryCell);

	for (Rsyn::SandboxPin rsynPin : rsynCell.allPins()) {
		const TimingLibraryPin &timingLibraryPin = getTimingLibraryPin(rsynPin);
		TimingPin &timingPin = getTimingPin(rsynPin);
		timingPin.control = timingLibraryPin.control;
		timingPin.clocked = timingLibraryPin.clocked;
	} // end for

	for (Rsyn::SandboxArc arc : rsynCell.allArcs()) {
		Rsyn::LibraryArc larc = arc.getLibraryArc();

		TimingLibraryArc &timingLibraryArc = getTimingLibraryArc(larc);
		TimingArc &timingArc = getTimingArc(arc);
		timingBuildTimingArcs_SetupBacktrackEdge(timingArc, timingLibraryArc.sense);
	} // end for

	// End points
	if (rysnLibraryCell.isSequential()) {
		sequentialCells.insert(rsynCell.asCell());
		for (Rsyn::SandboxPin pin : rsynCell.allPins(Rsyn::IN)) {
			const TimingPin &timingPin = getTimingPin(pin);
			if (timingPin.isDataPin()) {
				endpoints.insert(pin);
			} // end if
		} // end for
	} // end if

	// Ties
	if (rysnLibraryCell.isTie()) {
		Rsyn::SandboxPin pin = rsynCell.getPinByIndex(0);
		ties.insert(pin);
		floatingStartpoints.insert(pin);
	} // end if

	// Has pin without timing arc
	if (timingLibraryCell.floatingPins.size() > 0 && !rysnLibraryCell.isSequential()){
		for (Rsyn::LibraryPin lpin : timingLibraryCell.floatingPins){
			Rsyn::SandboxPin pin = rsynCell.getPinByLibraryPin(lpin);
			if (pin.getDirection() == Rsyn::IN){
				floatingEndpoints.insert(pin);
			} else if (pin.getDirection() == Rsyn::OUT) {
				floatingStartpoints.insert(pin);
			} else {
				cout << "WARNING: pin "<<pin.getFullName()<<" is INOUT\n";
			}// end if-else
		} // end if

	} // end if
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::uninitializeTimingCell(Rsyn::SandboxCell rsynCell) {
	Rsyn::LibraryCell rysnLibraryCell = rsynCell.getLibraryCell();

	TimingLibraryCell &timingLibraryCell
		= getTimingLibraryCell(rysnLibraryCell);
	if (rysnLibraryCell.isSequential()) {
		sequentialCells.erase(rsynCell.asCell());
		for (Rsyn::SandboxPin pin : rsynCell.allPins(Rsyn::IN)) {
			const TimingPin &timingPin = getTimingPin(pin);
			if (timingPin.isDataPin()) {
				endpoints.erase(pin);
			} // end if
		} // end for
	} // end if

	if (rysnLibraryCell.isTie()) {
		Rsyn::SandboxPin pin = rsynCell.getPinByIndex(0);
		floatingStartpoints.erase(pin);
		ties.erase(pin);
	} // end if

	// Does it have pins without timing arc?
	if (timingLibraryCell.floatingPins.size() > 0){
		for (Rsyn::LibraryPin lpin : timingLibraryCell.floatingPins){
			Rsyn::SandboxPin pin = rsynCell.getPinByLibraryPin(lpin);
			if (pin.getDirection() == Rsyn::IN){
				floatingEndpoints.erase(pin);
			} else if (pin.getDirection() == Rsyn::OUT) {
				floatingStartpoints.erase(pin);
			} else {
				cout << "WARNING: pin "<<pin.getFullName()<<" is INOUT\n";
			}// end if-else
		} // end if

	} // end if
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::init(Rsyn::Session rsynSession, Rsyn::Sandbox rsynSandbox) {
	session = rsynSession;
	design = rsynSandbox.getDesign();
	sandbox = rsynSandbox;

	clsScenario = rsynSession.getService("rsyn.scenario");
	clsTimer = rsynSession.getService("rsyn.timer");

	timingModel = clsTimer->getTimingModel();

	clsMaxCentrality[EARLY] = 0;
	clsMaxCentrality[LATE ] = 0;

	clsSign = 0;
	clsForceFullTimingUpdate = true;

	// Initializing Layers
	clsNetLayer = rsynSandbox.createAttribute();
	clsPinLayer = rsynSandbox.createAttribute();
	clsArcLayer = rsynSandbox.createAttribute();

	// More initializations.
	for (Rsyn::SandboxInstance instance : sandbox.allInstances()) {
		switch (instance.getType()) {
			case Rsyn::CELL:
				initializeTimingCell(instance.asCell());
				break;
			case Rsyn::PORT:
				if (instance.isPort(Rsyn::OUT)) {
					endpoints.insert(instance.asPort().getInnerPin());
				} // end if
				break;
			default:
				assert(false);
		} // end switch
	} // end for

	// Copy timing information for pins with related pins.
	for (Rsyn::SandboxPin pin : sandbox.allPins()) {
		Rsyn::Pin relatedPin = pin.getRelated();
		if (relatedPin) {
			TimingPin &timingPinSandbox = getTimingPin(pin);
			const TimingPin &timingPinDesign = clsTimer->getTimingPin(relatedPin);
			timingPinSandbox.state = timingPinDesign.state;
		} // end if
	} // end for

	// Copy timing information for arcs with related arcs.
	for (Rsyn::SandboxArc arc : sandbox.allArcs()) {
		Rsyn::Arc relatedArc = arc.getRelated();
		if (!relatedArc)
			continue;

		TimingArc &timingArcSandbox = getTimingArc(arc);
		const TimingArc &timingArcDesign = clsTimer->getTimingArc(relatedArc);
		timingArcSandbox.state = timingArcDesign.state;
	} // end for

	// Copy timing information for ports;
	for (Rsyn::SandboxPort port : sandbox.allPorts()) {
		if (port.isVirtual()) {
			Rsyn::Pin relatedPin = port.getAttachedPin().getRelated();

			if (relatedPin) {
				TimingPin &timingPinSandbox = getTimingPin(port.getInnerPin());
				const TimingPin &timingPinDesign = clsTimer->getTimingPin(relatedPin);
				timingPinSandbox.state = timingPinDesign.state;

				switch (port.getDirection()) {
					case Rsyn::IN: {
						setInputDelay(port, EARLY, timingPinSandbox.state[EARLY].a);
						setInputDelay(port, LATE , timingPinSandbox.state[LATE ].a);
						break;
					} // end case
					case Rsyn::OUT: {
						setOutputRequiredTime(port, EARLY, timingPinSandbox.state[EARLY].q);
						setOutputRequiredTime(port, LATE , timingPinSandbox.state[LATE ].q);

						Rsyn::Net net = relatedPin.getNet();
						if (net) {
							setOutputLoad(port, EARLY, clsTimer->getNetLoad(net, EARLY));
							setOutputLoad(port, LATE , clsTimer->getNetLoad(net, LATE ));
						} // end if
						break;
					} // end case
					default:
						std::exit(1);
				} // end switch
			} // end if
		} else {
			// If the port is not virtual, the timing values were already
			// copied when we copied the pin data, but we still need to copy the
			// constraints.
			Rsyn::Port relatedPort = port.getRelated();
			if (relatedPort) {
				switch (port.getDirection()) {
					case Rsyn::IN: {
						const Number a = clsScenario->getInputDelay(relatedPort, 0);
						setInputDelay(port, EARLY, EdgeArray<Number>(a, a));
						setInputDelay(port, LATE , EdgeArray<Number>(a, a));
						break;
					} // end case
					case Rsyn::OUT: {
						const Number c = clsScenario->getOutputLoad(relatedPort, 0);
						setOutputLoad(port, EARLY, EdgeArray<Number>(c, c));
						setOutputLoad(port, LATE , EdgeArray<Number>(c, c));
						setOutputRequiredTime(port, EARLY, clsScenario->getOutputRequiredTime(relatedPort, EARLY, EdgeArray<Number>(0, 0)));
						setOutputRequiredTime(port, LATE , clsScenario->getOutputRequiredTime(relatedPort, LATE , EdgeArray<Number>(0, 0)));
						break;
					} // end case
					default:
						std::exit(1);
				} // end switch
			} // end if
		} // end else
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::timingBuildTimingArcs_SetupBacktrackEdge(TimingArc &arc, const TimingSense sense) {
	for (const TimingMode mode : allTimingModes()) {
		TimingArcState &state = arc.state[mode];
		switch (sense) {
			case POSITIVE_UNATE:
				state.backtrack[RISE] = RISE;
				state.backtrack[FALL] = FALL;
				break;
			case NEGATIVE_UNATE:
				state.backtrack[RISE] = FALL;
				state.backtrack[FALL] = RISE;
				break;
			case NON_UNATE:
				// Backtrack edge depends on timing context.
				break;
			default:
				throw Exception("Invalid timing arc sense.");
		} // end switch
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_Arc_NonUnate(const TimingMode mode, const EdgeArray<Number> islew, const EdgeArray<Number> load, Rsyn::SandboxArc arc) {
	TimingArc &timingArc = getTimingArc(arc);
	TimingPin &timingPinFrom = getTimingPin(arc.getFromPin());

	TimingArcState &state = timingArc.state[mode];
	Rsyn::LibraryArc larc = arc.getLibraryArc();

	if (ENABLE_IITIMER_COMPATIBILITY_MODE) {

		// Compatibility mode enables our timer to match the timing reported by
		// the evaluation script of ICCAD 2014 Contest, which uses IITimer. In
		// this mode timing through non-unate arcs are calculated using the
		// worst input slew. Since some arcs may recovery slew (negative
		// coefficients in the library), this may lead to different results if
		// one compute the four possibilities (e.g. R->R, R->F, F->R, F->F).
		//
		// Note that IITimer also does not use the triggering edge of flip-flops
		// to compute the timing through sequential arcs. IITimer treats them
		// as regular non-unate arcs. However, for setup and hold calculation,
		// which is not done here, IITimer assumes rising triggered flip-flops.

		TimingTransition iedge;
		switch (mode) {
			case LATE: iedge = islew.getMaxEdge();
				break;
			case EARLY: iedge = islew.getMinEdge();
				break;
			default:
				assert(false);
		} // end switch

		for (const TimingTransition oedge : allTimingTransitions()) {
			timingModel->calculateLibraryArcTiming(larc, mode, oedge, islew[iedge], load[oedge], state.delay[oedge], state.oslew[oedge]);
		} // end for

		// Define backtrack.
		const auto &comparator = TM_MODE_COMPARATORS[mode];
		if (comparator(timingPinFrom.state[mode].a[RISE], timingPinFrom.state[mode].a[FALL])) {
			state.backtrack[RISE] = RISE;
			state.backtrack[FALL] = RISE;
		} else {
			state.backtrack[RISE] = FALL;
			state.backtrack[FALL] = FALL;
		} // end else

	} else {

		if (timingPinFrom.clocked /*is sequential*/) {
			// [TODO] For sequential timing arcs we should use the triggering edge
			// to fetch the input slew.

			// [NOTE] Assuming only rising edge-triggered flip-flops.

			const TimingTransition iedge = RISE;
			for (const TimingTransition oedge : allTimingTransitions()) {
				timingModel->calculateLibraryArcTiming(larc, mode, oedge, islew[iedge], load[oedge], state.delay[oedge], state.oslew[oedge]);

				state.backtrack[oedge] = iedge;
			} // end for

		} else {
			// Transition direction cannot be inferred from a single input (take
			// the worst, among rise/fall).

			// Compute the four possibilities: input x output transitions.
			Number delay[2][2]; // delay[transition at output][transition at input]
			Number oslew[2][2]; // oslew[transition at output][transition at input]
			for (const std::tuple<TimingTransition, TimingTransition> transitions : allTimingTransitionPairs()) {
				const TimingTransition iedge = std::get<0>(transitions);
				const TimingTransition oedge = std::get<1>(transitions);
				timingModel->calculateLibraryArcTiming(larc, mode, oedge, islew[iedge], load[oedge], delay[oedge][iedge], oslew[oedge][iedge]);
			} // end for

			// Update delay, output slew and backtrack edge.
			const auto &comparator = TM_MODE_COMPARATORS[mode];

			for (const TimingTransition edge : allTimingTransitions()) {
				// Delay and backtrack.
				if (comparator(delay[edge][RISE], delay[edge][FALL])) {
					state.delay[edge] = delay[edge][RISE];
					state.backtrack[edge] = RISE;
				} else {
					state.delay[edge] = delay[edge][FALL];
					state.backtrack[edge] = FALL;
				} // end else

				// Slew.
				if (comparator(oslew[edge][RISE], oslew[edge][FALL])) {
					state.oslew[edge] = oslew[edge][RISE];
				} else {
					state.oslew[edge] = oslew[edge][FALL];
				} // end else
			} // end for
		} // end else
	} // end else
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_Arc(
		const TimingMode mode,
		const EdgeArray<Number> islew,
		const EdgeArray<Number> load,
		const bool skip,
		Rsyn::SandboxArc arc,
		Rsyn::LibraryArc larc,
		TimingArcState &state
) {
	// [NOTE] By construction, ports do not have a timing arc... So we don't
	// need to deal with ports here.

	const TimingLibraryArc &libarc = getTimingLibraryArc(larc);

	switch (libarc.sense) {
		case POSITIVE_UNATE:
		{
			// Transition direction is maintained from input to output:
			// rise->rise and fall->fall.

			timingModel->calculateLibraryArcTiming(larc, mode, FALL, islew[FALL], load[FALL], state.delay[FALL], state.oslew[FALL]);
			timingModel->calculateLibraryArcTiming(larc, mode, RISE, islew[RISE], load[RISE], state.delay[RISE], state.oslew[RISE]);

			// Backtrack edge are constant for this timing sense.
			break;
		} // end case
		case NEGATIVE_UNATE:
		{
			// Transition direction is reversed from input to output: rise->fall
			// and fall->rise.

			timingModel->calculateLibraryArcTiming(larc, mode, FALL, islew[RISE], load[FALL], state.delay[FALL], state.oslew[FALL]);
			timingModel->calculateLibraryArcTiming(larc, mode, RISE, islew[FALL], load[RISE], state.delay[RISE], state.oslew[RISE]);

			// Backtrack edge are constant for this timing sense.
			break;
		} // end case
		case NON_UNATE:
		{
			updateTiming_Arc_NonUnate(mode, islew, load, arc);
			break;
		} // end case
		default:
			throw Exception("Invalid timing arc sense.");
	} // end switch

	if (skip) {
		state.delay.setBoth(0);
	} // end if

} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_Net_InitDriver(Rsyn::SandboxPin driver, const TimingMode mode, const EdgeArray<Number> load) {
	TimingPin &timingPin = getTimingPin(driver);

	if (driver.isPort()) {
		Rsyn::SandboxPort port  = driver.getInstance().asPort();

		timingPin.state[mode].a = getInputDelay(port, mode, EdgeArray<Number>(0, 0));
		timingPin.state[mode].slew = getInputTransition(port, mode, EdgeArray<Number>(0, 0));

		const InputDriver *inputDriver = getInputDriver(port);
		if (inputDriver) {
			switch (inputDriverDelayMode) {
				case INPUT_DRIVER_DELAY_MODE_UI_TIMER: {
					TimingArcState state;
					updateTiming_Arc(mode, inputDriver->inputSlew, load, false,
							nullptr, inputDriver->libraryArc, state);
					timingPin.state[mode].a = state.delay;
					timingPin.state[mode].slew = state.oslew;
					break;
				} // end case

				case INPUT_DRIVER_DELAY_MODE_PRIME_TIME: {
					// Note. The delay at an input port driven by an input driver is
					// set to the user defined input delay plus the delta between the
					// delay of the input driver driven the actual net load and the
					// delay of the input driver driven no load. The idea is to remove
					// the parasitic delay from the arrival time.
					TimingArcState state0;
					TimingArcState state1;
					updateTiming_Arc(mode, inputDriver->inputSlew, EdgeArray<Number>(0, 0), false,
							nullptr, inputDriver->libraryArc, state0);
					updateTiming_Arc(mode, inputDriver->inputSlew, load, false,
							nullptr, inputDriver->libraryArc, state1);
					timingPin.state[mode].a += state1.delay - state0.delay;
					timingPin.state[mode].slew = state1.oslew;
					break;
				} // end case
			} // end switch
		} // end if
	} else {
		switch (mode) {
			case EARLY: {
				TimingPinState &minState = timingPin.state[EARLY];
				minState.a.setBoth(+UNINITVALUE);
				minState.slew.setBoth(+UNINITVALUE);
				break;
			} // end case
			case LATE: {
				TimingPinState &maxState = timingPin.state[LATE];
				maxState.a.setBoth(-UNINITVALUE);
				maxState.slew.setBoth(-UNINITVALUE);
				break;
			} // end case
			default:
				assert(false);
		} // end switch
	} // end else
} // end method
// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_Net_TimingMode(const TimingMode mode, Rsyn::SandboxNet net, const EdgeArray<Number> load, Rsyn::SandboxArc arc) {
	// Assumes the driver state has been initialized properly (i.e. driver's
	// max arrival is set to -inf and min arrival is set to +inf and so on).

	const auto &comparator = TM_MODE_COMPARATORS[mode];

	TimingNet &timingNet = getTimingNet(net);
	TimingArc &timingArc = getTimingArc(arc);

	TimingPin &timingPinFrom = getTimingPin(arc.getFromPin());
	TimingPin &timingPinTo = getTimingPin(arc.getToPin());

	TimingNetState &netState = timingNet.state[mode];
	TimingArcState &arcState = timingArc.state[mode];
	TimingPinState &toPinState = timingPinTo.state[mode];
	const TimingPinState &fromPinState = timingPinFrom.state[mode];

	updateTiming_Arc(mode, fromPinState.slew, load, timingPinFrom.skip, arc,
			arc.getLibraryArc(), arcState);

	for (const TimingTransition edge : allTimingTransitions()) {
		const Number iarrival = fromPinState.a[arcState.backtrack[edge]];
		const Number oarrival = iarrival + arcState.delay[edge];

		if (comparator(oarrival, toPinState.a[edge])) {
			toPinState.a[edge] = oarrival;
			netState.backtrackArc[edge] = &timingArc;
		} // end if

		if (comparator(arcState.oslew[edge], toPinState.slew[edge])) {
			toPinState.slew[edge] = arcState.oslew[edge];
		} // end if

	} // end for

} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_Net(Rsyn::SandboxNet net) {
	if (net.getNumPins() < 1) {
		std::cout << "[WARNING] Net without pins.\n";
		return;
	} // end if

	Rsyn::SandboxPin driver = net.getDriver();
	TimingNet &timingNet = getTimingNet(net);
	TimingPin &timingPin = getTimingPin(driver);

	// Effective load capacitance..
	EdgeArray<Number> load[NUM_TIMING_MODES];

	for (const TimingMode mode : allTimingModes()) {
		// Compute effective capacitance loaded by the driver.
		timingModel->calculateLoadCapacitance(driver, mode, load[mode]);

		// Initialize the driver timing data with safe values (e.g. +inf, -inf).
		updateTiming_Net_InitDriver(driver, mode, load[mode]);
	} // end for

	// Update  s delay and annotate the max and min timing data (e.g.
	// arrival, slew).

	const bool previousSkip = timingPin.skip;

	int counter = 0;
	timingPin.skip = true;
	for (Rsyn::SandboxArc arc : driver.allIncomingArcs()) {
		updateTiming_Net_TimingMode(LATE , net, load[LATE ], arc);
		updateTiming_Net_TimingMode(EARLY, net, load[EARLY], arc);

		timingPin.skip &= getTimingPin(arc.getFromPin()).skip;
		counter++;
	} // end for

	if (counter == 0)
		timingPin.skip = previousSkip;

	// Update nets and propagate the timing information
	// to the net sinks.

	for (const TimingMode mode : allTimingModes()) {
		const TimingPinState &driverState = timingPin.state[mode];
		timingModel->prepareNet(net, mode, driverState.slew);

		for (Rsyn::SandboxPin sink : net.allPins(Rsyn::SINK)) {
			TimingPin &timingSinkPin = getTimingPin(sink);
			timingSinkPin.skip = timingPin.skip;

			EdgeArray<Number> delay;
			EdgeArray<Number> slew;
			timingModel->calculateNetArcTiming(driver, sink, mode,
					driverState.slew, delay, slew);

			TimingPinState &sinkState = timingSinkPin.state[mode];
			sinkState.a = delay + driverState.a;
			sinkState.slew = slew;
			sinkState.wdelay = delay;
		} // end for
	} // end for

	if (timingPin.skip) {
		for (Rsyn::SandboxPin pin : net.allPins(Rsyn::SINK)) {
			for (const TimingMode mode : allTimingModes()) {
				const TimingPinState &driverState = timingPin.state[mode];

				TimingPin &timingSinkPin = getTimingPin(pin);
				timingSinkPin.skip = timingPin.skip;

				TimingPinState & sinkState = timingSinkPin.state[mode];
				sinkState.a = driverState.a;
				sinkState.wdelay.set(0, 0);

				if (ENABLE_UITIMER_COMPATIBILITY_MODE) {
					for (const TimingTransition edge : allTimingTransitions()) {
						if (std::abs(driverState.slew[edge]) == UNINITVALUE) {
							sinkState.slew[edge] = driverState.slew[edge];
						} // end if
					} // end for
				} else {
					sinkState.slew = driverState.slew;
				} // end else
			} // end for
		} // end for
	} // end else
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_PropagateArrivalTimes() {
	for (Rsyn::SandboxNet net : sandbox.allNetsInTopologicalOrder()) {
		updateTiming_Net(net);
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_HandleFloatingPins() {
	for (Rsyn::SandboxPin pin : floatingStartpoints){
		TimingPin & timingPin = getTimingPin(pin);
		timingPin.state[EARLY].a.setBoth(+UNINITVALUE);
		timingPin.state[LATE ].a.setBoth(-UNINITVALUE);
		timingPin.skip = true;
	} // end for

	for (Rsyn::SandboxPin pin : floatingEndpoints){
		TimingPin & timingPin = getTimingPin(pin);
		timingPin.state[EARLY].q.setBoth(-UNINITVALUE);
		timingPin.state[LATE ].q.setBoth(+UNINITVALUE);
		timingPin.state[EARLY].wsq.setBoth(-UNINITVALUE);
		timingPin.state[LATE ].wsq.setBoth(+UNINITVALUE);
		timingPin.skip = true;
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_UpdateTimingTests_SetupHold_DataPin(Rsyn::SandboxPin pin) {
	const Number T = getClockPeriod();

	TimingPin &timingPin = getTimingPin(pin);
	Rsyn::SandboxCell cell  = pin.getInstance().asCell();

	const TimingLibraryPin &timingLibraryPin = getTimingLibraryPin(pin);
	const TimingPin &clk = getTimingPin(cell.getPinByIndex(timingLibraryPin.control));

	// Setup
	EdgeArray<Number> tsetup = timingModel->getSetupTime(pin);
	timingPin.state[LATE].q = T + clk.state[EARLY].a[RISE] - tsetup;

	// Hold
	EdgeArray<Number> thold = timingModel->getHoldTime(pin);
	timingPin.state[EARLY].q = clk.state[LATE].a[RISE] + thold;
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_UpdateTimingTests() {
	// [NOTE] Assuming only rising edge-triggered pins.

	const Number T = getClockPeriod();

	for (Rsyn::SandboxPin pin : allEndpoints()) {
		TimingPin &timingPin = getTimingPin(pin);

		if (pin.isPort()) {
			Rsyn::SandboxCell port  = pin.getInstance().asCell();
			timingPin.state[EARLY].q = getOutputRequiredTime(port, EARLY, EdgeArray<Number>(0, 0));
			timingPin.state[LATE ].q = getOutputRequiredTime(port, LATE , EdgeArray<Number>(T, T));
		} else if (pin.getInstance().isSequential()) {
			updateTiming_UpdateTimingTests_SetupHold_DataPin(pin);
		} else {
			std::cout << "[WARNING] Endpoint is neither a port or a data pin.\n";
			timingPin.state[EARLY].q.set(0, 0);
			timingPin.state[LATE ].q.set(T, T);
		} // end else

		for (const TimingMode mode : allTimingModes()) {
			timingPin.state[mode].wsq = timingPin.state[mode].q;
		} // end for

	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_PropagateRequiredTimes_Net(Rsyn::SandboxNet net) {
	// [NOTE] 16/Sep/2015 - Guilherme Flach
	// A bug we found was that we were propagating the required time to
	// from pins of arcs driven the driver pin of the net. This is ok for most
	// cases, but breaks down whenever a from pin has more than one arc from it
	// (e.g. multiple output cells). Be aware of that :)

	// [ASSUMPTION] Single driver net.

	// Update driver required time.
	Rsyn::SandboxPin driver = net.getDriver();
	if (!driver)
		return; // [TODO] We should still process the sinks.

	TimingPin &driverTimingPin = getTimingPin(driver);

	// Initialize with safe values.
	driverTimingPin.state[EARLY].q.set(-UNINITVALUE, -UNINITVALUE);
	driverTimingPin.state[LATE ].q.set(+UNINITVALUE, +UNINITVALUE);

	for (Rsyn::SandboxPin sink : net.allPins(Rsyn::SINK)) {
		TimingPin &from = getTimingPin(sink);

		// Initialize worst required times with safe values.
		// We don't reset the from required time directly to avoid
		// overwriting endpoint required times that are set separately.
		EdgeArray<Number> required[NUM_TIMING_MODES];
		for (const TimingTransition transition : allTimingTransitions()) {
			required[EARLY][transition] = -UNINITVALUE;
			required[LATE ][transition] = +UNINITVALUE;
		} // end for

		EdgeArray<Number> worstSpannedRequired[NUM_TIMING_MODES];
		for (const TimingTransition transition : allTimingTransitions()) {
			worstSpannedRequired[EARLY][transition] = -UNINITVALUE;
			worstSpannedRequired[LATE ][transition] = +UNINITVALUE;
		} // end for

		// Get the worst required time if any.
		bool hasArcs = false;
		for (Rsyn::SandboxArc arc : sink.allOutgoingArcs()) {
			TimingArc &timingArc = getTimingArc(arc);
			TimingPin &to = getToTimingPinOfArc(arc);

			if (from.isClockPin() && to.isDataPin()) {
				// Nothing to be done here... We should consider
				// removing the constraint timing arc from the timing graph.
			} else {
				hasArcs = true;

				for (const TimingMode mode : allTimingModes()) {
					for (const TimingTransition oedge : allTimingTransitions()) {
						const TimingTransition iedge = timingArc.state[mode].backtrack[oedge];
						const Number q = to.state[mode].q[oedge] - timingArc.state[mode].delay[oedge];
						if (TM_MODE_REVERSE_COMPARATORS[mode](q, required[mode][iedge])) {
							required[mode][iedge] = q;
							worstSpannedRequired[mode][iedge] = to.state[mode].wsq[oedge];
						} // end if
					} // end for
				} // end for
			} // end else
		} // end for

		// If we found arcs from this pin, update its required time.
		// If no arcs were found, keep its required times untouched as they
		// were set outside this function (e.g. data pins, primary outputs,
		// floating pins).
		if (hasArcs) {
			for (const TimingMode mode : allTimingModes()) {
				from.state[mode].q = required[mode];
				from.state[mode].wsq = worstSpannedRequired[mode];
			} // end for
		} // end if

		// Update required time.
		// Note this depends on the required times just set above.
		if (ENABLE_UITIMER_COMPATIBILITY_MODE && from.isClockPin()) {
			// Note that when the update reaches this point, the data pin must
			// already been processed.

			const TimingPin &data = getTimingPin(sink.getInstance().getPinByIndex(from.getDataPinIndex()));
			TimingPin &ck = from; // just an alias

			ck.state[EARLY].q[RISE] = std::max(ck.state[EARLY].q[RISE],
					ck.state[EARLY].a[RISE] - data.getWorstSlack(LATE));
			ck.state[EARLY].q[FALL] = -UNINITVALUE;

			ck.state[LATE ].q[RISE] = std::min(ck.state[LATE ].q[RISE],
					data.getWorstSlack(EARLY) + ck.state[LATE].a[RISE]);
			ck.state[LATE ].q[FALL] = +UNINITVALUE;
		} // end else

		if (ENABLE_UITIMER_COMPATIBILITY_MODE && from.isDataPin()) {
			// Note that when the update reaches this point, the required time
			//of the clock pin was not yet processed.

			const TimingPin &data = from; // just an alias
			TimingPin &ck =  getTimingPin(sink.getInstance().getPinByIndex(from.getClockPinIndex()));

			ck.state[EARLY].q[RISE] =
					ck.state[EARLY].a[RISE] - data.getWorstSlack(LATE);
			ck.state[EARLY].q[FALL] = -UNINITVALUE;

			ck.state[LATE ].q[RISE] =
					data.getWorstSlack(EARLY) + ck.state[LATE].a[RISE];
			ck.state[LATE ].q[FALL] = +UNINITVALUE;
		} // end else

		// Update required time of the driver.
		for (const TimingMode mode : allTimingModes()) {
			const EdgeArray<Number> wdelay = from.state[mode].wdelay;

			for (const TimingTransition edge : allTimingTransitions()) {
				const Number requred = from.state[mode].q[edge] - wdelay[edge];

				if (TM_MODE_REVERSE_COMPARATORS[mode](requred, driverTimingPin.state[mode].q[edge])) {
					driverTimingPin.state[mode].q[edge] = from.state[mode].q[edge] - wdelay[edge];
					driverTimingPin.state[mode].wsq[edge] = from.state[mode].wsq[edge];
				}  // end if
			} // end for
		} // end for
	} // end for each
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_PropagateRequiredTimes() {
	// Traverse the circuit from outputs to inputs.
	for (Rsyn::SandboxNet net : sandbox.allNetsInReverseTopologicalOrder()) {
		updateTiming_PropagateRequiredTimes_Net(net);
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_PropagateRequiredTimesIncremental(const std::set<Rsyn::SandboxNet> &nets) {
	// A dummy timing pin state to handle nets without driver.
	static const std::array<TimingPinState, 2> dummyState;

	std::array<TimingPinState, NUM_TIMING_MODES> previousState;

	const int arrivalTimePropagationSign = getSign();
	generateNextSign();

	typedef std::pair<Rsyn::TopologicalIndex, Rsyn::SandboxNet> T;
	priority_queue<T, std::deque<T>> queue;

	// Update all timing arcs driving the seed net.
	for (Rsyn::SandboxNet net : nets) {
		if (net) {
			queue.push(std::make_pair(net.getTopologicalIndex(), net));
		} // end if
	} // end for

	// Propagate arrival times.
	while (!queue.empty()) {
		Rsyn::SandboxNet net = queue.top().second;
		queue.pop();

		TimingNet &timingNet = getTimingNet(net);

		// We sign nets for two reasons: avoid processing more than once the
		// logical cone  starting stating at the net and to allow incremental
		// required time propagation. Note that although we are processing in
		// topological order, we may visit a same net twice as logic converges
		// to a single point. Signing nets may save a lot of runtime mainly
		// when the net is the clock net.
		if (timingNet.sign == getSign())
			continue;

		const int previousNetSign = timingNet.sign;
		timingNet.sign = getSign();

		// Get the net driver an its state.
		Rsyn::SandboxPin driver = net.getDriver(); // [ASSUMPTION] Net has a single driver.
		const std::array<TimingPinState, NUM_TIMING_MODES> &driverState = driver?
				getTimingPin(driver).state : dummyState;

		// Annotate the previous timing state to allow early termination.
		previousState = driverState; // must be a copy

		// Debug
		#if TIMER_DEBUG_PRUNING
			sinkStates.resize(net.getNumSinks());
			index = 0;
			for (Rsyn::SandboxPin sink : net.allPins(Rsyn::SINK)) {
				sinkStates[index++] = getTimingPin(sink).state;
			} // end for
		#endif

		// Update timing of the current net.
		updateTiming_PropagateRequiredTimes_Net(net);

		// Debug
		#if TIMER_DEBUG_PRUNING
			index = 0;
			for (Rsyn::SandboxPin from : net.allPins(Rsyn::SINK)) {
				int  counter = 0;
				int  counterPruned = 0;
				for (Rsyn::SandboxArc arc : from.allOutgoingArcs()) {
					Rsyn::SandboxPin to = arc.getToPin();
					if (pruned.count(to))
						counterPruned++;
					counter++;
				} // end for

				const bool changed =
						hasStateChangedSignificantlyForRequiredTimePropagation(sinkStates[index],
						getTimingPin(from).state, pruningPrecision);

				const bool isSeed = nets.count(net);

				if (counter && ((counter - counterPruned) == 0) && changed && !isSeed) {
					std::cout << from.getFullName() << "\n" << net.getName()
							<< " pruned=" << counterPruned << "/" << counter << "\n";
					printState(std::cout, sinkStates[index]);
					printState(std::cout, getTimingPin(from).state);
					exit(1);
				} // end if
				index++;
			} // end for
		#endif

		// Since the required time of the clock pin also depends on the data pin
		// continue propagation through the arc ck->q.
		const bool sequential = driver? driver.getInstance().isSequential() : false;

		// Add previous net to the queue.
		if (driver) {
			for (Rsyn::SandboxArc arc : driver.allIncomingArcs()) {
				Rsyn::SandboxNet previousNet = arc.getFromNet();
				if (previousNet) {
					const TimingNet &previousTimingNet = getTimingNet(previousNet);
					if (previousTimingNet.sign != getSign()) {
						queue.push(std::make_pair(previousNet.getTopologicalIndex(), previousNet));
					} // end if
				} // end if
			} // end for
		} // end if
	} // end while
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_UpdateTimingViolations() {
	// Compute slacks for endpoints.
	for (const TimingMode mode : allTimingModes()) {
		clsTNS[mode] = 0;
		clsWNS[mode] = 0;
		clsWorstSlack[mode] = +std::numeric_limits<Number>::max();
		clsMaxArrivalTime[mode] = -std::numeric_limits<Number>::infinity();
		clsMinArrivalTime[mode] = +std::numeric_limits<Number>::infinity();
		clsNumCriticalEndpoints[mode] = 0;
	} // end for
	clsSlackChecksum = 0;

	for (Rsyn::SandboxPin pin : allEndpoints()) {
		TimingPin &timingPin = getTimingPin(pin);

		for (const TimingMode mode : allTimingModes()) {
			Number wns = 0; // must be zero so that positive slack are ignored
			for (const TimingTransition edge : allTimingTransitions()) {
				const Number slack = timingPin.getSlack(mode, edge);
				if (slack < clsWorstSlack[mode]) {
					clsCriticalPathEndpoint[mode] = std::make_pair(pin, edge);
					clsWorstSlack[mode] = slack;
				} // end method

				wns = std::min(wns, slack);
				clsSlackChecksum += slack;
			} // end for

			if (wns < clsWNS[mode]) { //keep the wns pin for early and late violations;
				clsWNS[mode] = wns;
			} // end if
			clsTNS[mode] += wns;
			if(wns < 0)
				clsNumCriticalEndpoints[mode]++;

			clsMaxArrivalTime[mode] = std::max(clsMaxArrivalTime[mode], timingPin.getMaxArrivalTime(mode));
			clsMinArrivalTime[mode] = std::min(clsMinArrivalTime[mode], timingPin.getMinArrivalTime(mode));
		} // end for
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_CriticalEndpoints() {
	for (const TimingMode mode : allTimingModes()) {
		const int numCriticalEndPoints = getNumCriticalEndpoints( mode );
		queryTopCriticalEndpoints(mode, numCriticalEndPoints, clsCriticalEndpoints[mode] );
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTimingFull() {
	timingModel->beforeTimingUpdate(); // don't count this in the runtime

	clsStopwatchUpdateTiming.start();

	updateTiming_HandleFloatingPins();
	updateTiming_PropagateArrivalTimes();
	updateTiming_UpdateTimingTests();
	updateTiming_UpdateTimingViolations();
	updateTiming_PropagateRequiredTimes();
	updateTiming_CriticalEndpoints();

	dirtyNets.clear();
	clsDirtyTimingCells.clear();
	clsForceFullTimingUpdate = false;

	clsStopwatchUpdateTiming.start();
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTiming_PropagateArrivalTimesIncremental(std::set<Rsyn::SandboxNet> &endpoints) {
	endpoints.clear();

	generateNextSign();

	std::vector<std::array<TimingPinState, 2>> sinkStates;
	int index;
	int enqueued;

	// Priority queue.
	typedef std::pair<Rsyn::TopologicalIndex, Rsyn::SandboxNet> T;
	std::priority_queue< T, std::deque<T>, std::greater<T> > queue;

	// Update all timing arcs driving the seed net.
	for (Rsyn::SandboxNet net : dirtyNets) {
		if (net != getClockNet()) {
			queue.push(std::make_pair(net.getTopologicalIndex(), net));

			TimingNet &timingNet = getTimingNet(net);
			timingNet.dirty = true;
		} // end if
	} // end for

	// Propagate arrival times.
	while (!queue.empty()) {
		Rsyn::SandboxNet net = queue.top().second;
		queue.pop();

		TimingNet &timingNet = getTimingNet(net);

		// We sign nets for two reasons: avoid processing more than once the
		// logical cone  starting stating at the net and to allow incremental
		// required time propagation. Note that although we are processing in
		// topological order, we may visit a same net twice as logic converges
		// to a single point. Signing nets may save a lot of runtime mainly
		// when the net is the clock net.
		if (timingNet.sign == getSign())
			continue;
		timingNet.sign = getSign();

		// Indicates that the timing of this net was update outside a complete
		// timing update. For instance, when timing is update only locally.
		// If the timing was updated locally, it may happen that the current timing
		// matches the propagated timing, which lead to the propagation to be
		// pruned. However, as the timing was updated only locally, that means that
		// it was not propagated, therefore we should not prune. So we need a flag
		// to avoid pruning in such cases.
		const bool wasDirty = timingNet.dirty;
		timingNet.dirty = false;

		// Copy the previous timing state of sinks to allow early termination.
		sinkStates.resize(net.getNumSinks());
		index = 0;
		for (Rsyn::SandboxPin sink : net.allPins(Rsyn::SINK)) {
			const TimingPin &timingPin = getTimingPin(sink);
			sinkStates[index] = timingPin.state;
			index++;
		} // end for

		// Update timing of the current net.
		updateTiming_Net(net);

		// Add next nets to the queue.
		enqueued = 0;
		index = 0;
		for (Rsyn::SandboxPin from : net.allPins(Rsyn::SINK)) {
			const TimingPin &timingPin = getTimingPin(from);

			if (wasDirty) {
				for (Rsyn::SandboxArc arc : from.allOutgoingArcs()) {
					Rsyn::SandboxNet nextNet = arc.getToNet();
					if (nextNet) {
						const TimingNet &nextTimingNet = getTimingNet(nextNet);
						if (nextTimingNet.sign != getSign()) {
							queue.push(std::make_pair(nextNet.getTopologicalIndex(), nextNet));
							enqueued++;
						} // end if
					} // end if
				} // end for
			} // end if

			// If this is a register and the arrival time is being
			// propagated to the clock pin, this may affect the required
			// time at the data pin, so the net driving the data pin should
			// be considered a seed for required time propagation.
			if (timingPin.isClockPin()) {
				Rsyn::SandboxPin data = from.getInstance().getPinByIndex(timingPin.getDataPinIndex());
				if (data && data.getNet()) {
					endpoints.insert(data.getNet());
				} // end if
			} // end if

			index++;
		} // end for

		// If this net did not expanded any neighbors, add it as a final net.
		if (enqueued == 0) {
			endpoints.insert(net);
		} // end if
	} // end while
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updateTimingIncremental() {
	if (clsForceFullTimingUpdate) {
		std::cout << "[INFO] Forcing full timing update.\n";
		updateTimingFull();
	} else {
		timingModel->beforeTimingUpdate(); // don't count this in the runtime

		clsStopwatchUpdateTiming.start();
		for (Rsyn::SandboxInstance cell : clsDirtyTimingCells) {
			for (Rsyn::SandboxPin pin : cell.allPins()) {
				Rsyn::SandboxNet net = pin.getNet();
				if (net) {
					dirtyNets.insert(net);
				} // end if
			} // end for
		} // end for

		// Store nets were the required time need to be propagated back.
		std::set<Rsyn::SandboxNet> endpoints;

		updateTiming_HandleFloatingPins();
		updateTiming_PropagateArrivalTimesIncremental(endpoints);
		updateTiming_UpdateTimingTests();
		updateTiming_UpdateTimingViolations();
		updateTiming_PropagateRequiredTimesIncremental(endpoints);
		updateTiming_CriticalEndpoints();

		// Clear dirty cells and nets.
		dirtyNets.clear();
		clsDirtyTimingCells.clear();

		clsStopwatchUpdateTiming.stop();
	} // end else
} // end method

////////////////////////////////////////////////////////////////////////////////
// Steiner Tree
////////////////////////////////////////////////////////////////////////////////

void SandboxTimer::updateTimingOfNet(Rsyn::SandboxNet net) {
	updateTiming_Net(net);
	dirtyNets.insert(net);
} // end method

// -----------------------------------------------------------------------------

Number SandboxTimer::getClockPeriod() const {
	return clsScenario->getClockPeriod();
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::dumpTiming(const std::string &filename) {
	std::ofstream out(filename.c_str());

	out << std::fixed << std::setprecision(1);

	out << "pin" << " "
			<< "at_fall" << " "
			<< "at_rise" << " "
			<< "rat_fall" << " "
			<< "rat_rise" << " "
			<< "slew_fall" << " "
			<< "slew_rise" << " "
			<< "load_fall" << " "
			<< "load_rise" << "\n";

	for (Rsyn::SandboxNet net : sandbox.allNetsInTopologicalOrder()) {
		for (Rsyn::SandboxPin pin : net.allPins(Rsyn::OUT)) { // OUT just for now...
			if (!pin.isConnected())
				continue;

			Rsyn::SandboxNet net = pin.getNet();

			out << (pin.isPort()? pin.getInstanceName() : pin.getFullName()) << " ";
			out << getPinArrivalTime(pin, LATE, FALL) << " "
				<< getPinArrivalTime(pin, LATE, RISE) << " ";

			out << getPinRequiredTime(pin, LATE, FALL) << " "
				<< getPinRequiredTime(pin, LATE, RISE) << " ";

			out << getPinSlew(pin, LATE, FALL) << " "
				<< getPinSlew(pin, LATE, RISE) << " ";

			if (getClockNet() == net) {
				out << 0.0 << " " << 0.0; // used to precision purpose when writing file
			} else {
				EdgeArray<Number> load(0, 0);
				timingModel->calculateLoadCapacitance(pin, LATE, load);
				out << load[FALL] << " " << load[RISE];
			} // end else
			out << "\n";
		} // end for
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::printTimingPropagation( ostream &out, bool newLine  ){
	const bool opt_show_caps = !true;

	const bool opt_show_time = true;
	const bool opt_show_arrival_times = true;
	const bool opt_show_required_times = !true;
	const bool opt_show_slews = !true;
	const bool opt_show_slacks = !true;

	const bool opt_show_wdelay = !true;

	const bool opt_show_pin_offset = !true;
	const bool opt_show_cell_core = !true;

	const bool opt_show_stwl = !true;

	const bool opt_show_eary = false;
	const bool opt_show_late = true;

	const bool opt_show_only_endpoints = !true;

	out << std::setprecision(2);
	out << std::fixed;

	// this afects the log of tool.
	//std::cout << std::setprecision(12);

	if(!opt_show_stwl)
	for(Rsyn::SandboxInstance instance : sandbox.allInstances()){
		for(Rsyn::SandboxPin pin : instance.allPins()){
			if(!pin.isConnected())
				continue;

			if (opt_show_only_endpoints && !endpoints.count(pin)) {
				continue;
			} // end if

			for (Rsyn::SandboxPin predecessor : pin.allPredecessorPins()) {
				if (predecessor.getTopologicalIndex() >= pin.getTopologicalIndex()) {
					std::cout << "BUG2:\n";
					std::cout << "Predecessor: " << predecessor.getFullName() << " " << predecessor.getTopologicalIndex() << "\n";
					std::cout << "Current    : " << pin.getFullName() << " " << pin.getTopologicalIndex() << "\n";
					//std::exit(1);
				} // end if
			} // end for

			for (Rsyn::SandboxPin successor : pin.allSucessorPins()) {
				if (successor.getTopologicalIndex() <= pin.getTopologicalIndex()) {
					std::cout << "BUG2:\n";
					std::cout << "Successor  : " << successor.getFullName() << " " << successor.getTopologicalIndex() << "\n";
					std::cout << "Current    : " << pin.getFullName() << " " << pin.getTopologicalIndex() << "\n";
				} // end if
			} // end for

			Rsyn::SandboxNet net = pin.getNet();
			//Rsyn::SandboxCell cell = pin.getCell(); unused variable
			//TimingPin &timingPin = getTimingPin(pin); unused variable
			//TimingLibraryPin & tlPin = getTimingLibraryPin(pin); unused variable

			//if(!opt_show_only_endpoints || !tlPin.clocked)
			//	continue;

			//		TimingNet & timingNet = getTimingNet(net);
			//		RCTree & rc = timingNet.rctree;

			if (pin.isPort())
				out << pin.getInstanceName();
			else
				out << pin.getFullName();


			//out << (timingPin.skip ? " TRUE " : " FALSE ");

			if (net)
				out << " - " << net.getName();
			else
				out << " - NULL";

			if (newLine)
				out << "\n";

			if(opt_show_time){
				if (opt_show_arrival_times) {
					out << " at: ";
					if (opt_show_eary) {
						out << getPinArrivalTime(pin, EARLY, FALL) << " "
							<< getPinArrivalTime(pin, EARLY, RISE) << (opt_show_late?" ": "");
					}
					if (opt_show_late) {
						out << getPinArrivalTime(pin, LATE, FALL) << " "
							<< getPinArrivalTime(pin, LATE, RISE);
					}
				}

				if (opt_show_required_times) {
					out << " rat: ";
					if (opt_show_eary) {
						out << getPinRequiredTime(pin, EARLY, FALL) << " "
							<< getPinRequiredTime(pin, EARLY, RISE) << (opt_show_late?" ": "");
					}
					if (opt_show_late) {
						out << getPinRequiredTime(pin, LATE, FALL) << " "
							<< getPinRequiredTime(pin, LATE, RISE);
					}
				} // end if

				if(opt_show_slacks) {
					out	<< " slack: ";
					if (opt_show_eary) {
						out << getPinSlack(pin, EARLY, FALL) << " "
							<< getPinSlack(pin, EARLY, RISE) << (opt_show_late?" ": "");
					}
					if (opt_show_late) {
						out << getPinSlack(pin, LATE, FALL) << " "
							<< getPinSlack(pin, LATE, RISE);
					}
				} // end if

				if (opt_show_slews) {
					out << " slew: ";
					if (opt_show_eary) {
						out << getPinSlew(pin, EARLY, FALL) << " "
							<< getPinSlew(pin, EARLY, RISE) << (opt_show_late?" ": "");
					}
					if (opt_show_late) {
						out << getPinSlew(pin, LATE, FALL) << " "
							<< getPinSlew(pin, LATE, RISE);
					}
				} // end if

			}

			if (net){
				TimingNet & timingNet = getTimingNet(net);

				if (opt_show_caps){
					out << " dwcap: ";

					if (getClockNet() == net) {
						out << 0.0 << " " << 0.0 << " "<< 0.0 << " " << 0.0 ; // used to precision purpose when writing file
					} else {
						EdgeArray<Number> cap = EdgeArray<Number>(timingModel->getLibraryPinInputCapacitance(pin.getLibraryPin()));
						if (opt_show_eary) {
								out	<< cap.getFall() << " "
									<< cap.getRise() << (opt_show_late?" ": "");
						}
						if (opt_show_late) {
							out << cap.getFall() << " "
								<< cap.getRise();
						}
					} // end else
				} // end if
			} else {
				if (opt_show_caps) {
					out << " dwcap: ";

					// Print 0.0 (double) instead of integer or string zero to
					// allow it to be formated accordingly to setprecision().

					if (opt_show_eary) {
						out << 0.0 << " " << 0.0 << (opt_show_late?" ": "");
					}

					if (opt_show_late) {
						out << 0.0 << " " << 0.0 ;
					}
				}
			}

			if(!newLine)
				out << "\n";

		}
	}
}

// -----------------------------------------------------------------------------

void SandboxTimer::printPinDebug(ostream& out, Rsyn::SandboxPin pin, const bool early, const bool late) {
	if (!early && !late)
		return;

	if (pin.isPort())
		out << pin.getInstanceName();
	else
		out << pin.getFullName();

		out << " - " << pin.getNetName();

		out << " at: ";
		if (early) {
			out << getPinArrivalTime(pin, EARLY, FALL) << " ";
			out << getPinArrivalTime(pin, EARLY, RISE) << " ";
		} // end if
		if (late) {
			out << getPinArrivalTime(pin, LATE, FALL) << " ";
			out << getPinArrivalTime(pin, LATE, RISE);
		} // end if

		out << " rat: ";
		if (early) {
			out << getPinRequiredTime(pin, EARLY, FALL) << " ";
			out << getPinRequiredTime(pin, EARLY, RISE) << " ";
		} // end if
		if (late) {
			out << getPinRequiredTime(pin, LATE, FALL) << " ";
			out << getPinRequiredTime(pin, LATE, RISE);
		} // end if

		out << " slack: ";
		if (early) {
			out << getPinSlack(pin, EARLY, FALL) << " ";
			out << getPinSlack(pin, EARLY, RISE) << " ";
		} // end if
		if (late) {
			out << getPinSlack(pin, LATE, FALL) << " ";
			out << getPinSlack(pin, LATE, RISE);
		} // end if

		out << " slew: ";
		if (early) {
			out	<< getPinSlew(pin, EARLY, FALL) << " ";
			out << getPinSlew(pin, EARLY, RISE) << " ";
		} // end if
		if (late) {
			out << getPinSlew(pin, LATE, FALL) << " ";
			out << getPinSlew(pin, LATE, RISE);
		} // end if

		out << " wdelay: ";
		if (early) {
			out << getPinWireDelay(pin, EARLY, FALL) << " ";
			out << getPinWireDelay(pin, EARLY, RISE) << " ";
		} // end if
		if (late) {
			out << getPinWireDelay(pin, LATE, FALL) << " ";
			out << getPinWireDelay(pin, LATE, RISE);
		} // end if

		out << "\n";
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::printArcDebug(ostream &out, Rsyn::SandboxArc arc){
	const TimingArc &timingArc = getTimingArc(arc);

	out
		<< (arc.getInstance().getName()) <<  " "
		<< (arc.getFromPin().getName()) << " -> "
		<< (arc.getToPin().getName()) << " "

		<< "delay: "
		<< timingArc.state[EARLY].delay[FALL] << " "
		<< timingArc.state[EARLY].delay[RISE] << " "
		<< timingArc.state[LATE].delay[FALL] << " "
		<< timingArc.state[LATE].delay[RISE] << " "

		<< "slew: "
		<< timingArc.state[EARLY].oslew[FALL] << " "
		<< timingArc.state[EARLY].oslew[RISE] << " "
		<< timingArc.state[LATE].oslew[FALL] << " "
		<< timingArc.state[LATE].oslew[RISE]
		<< "\n";
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::printNetDebug(ostream &out, Rsyn::SandboxNet net) {
	const TimingNet &timingNet = getTimingNet(net);

	EdgeArray<Number> load(0, 0);
	Rsyn::SandboxPin driver = net.getDriver();
	if (driver)
		timingModel->calculateLoadCapacitance(driver, LATE, load);

	std::cout << "net: " << net.getName() << "\n";
	std::cout << "  " << "#sinks     : " << net.getNumSinks() << "\n";
	std::cout << "  " << "#drivers   : " << net.getNumDrivers() << "\n";
	std::cout << "  " << "lumped cap : " << load << "\n";
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::printState(ostream &out, const std::array<TimingPinState, NUM_TIMING_MODES> &state) {
	out
			<< "arrival  : " << state[EARLY].a << " " << state[LATE].a << "\n"
			<< "slew     : " << state[EARLY].slew << " " << state[LATE].slew << "\n"
			<< "wdelay   : " << state[EARLY].wdelay << " " << state[LATE].wdelay << "\n"
			<< "required : " << state[EARLY].q << " " << state[LATE].q << "\n";
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::writeTimingFile(ostream &out) {
	out << "clock "<<getClockPeriod() << " 50\n";

	for (Rsyn::SandboxPort cell : sandbox.allPorts(Rsyn::IN)) {
		for(Rsyn::SandboxPin pin : cell.allPins(Rsyn::OUT)){
			const TimingPin &timingPin = getTimingPin(pin);
		out << "at " << pin.getInstanceName() << " " << timingPin.state[EARLY].a[RISE] <<
			" " << timingPin.state[EARLY].a[FALL] <<
			" " << timingPin.state[LATE].a[RISE] <<
			" " << timingPin.state[LATE].a[RISE] << "\n";
		}
	}

	for (Rsyn::SandboxPort cell : sandbox.allPorts(Rsyn::IN)) {
		for(Rsyn::SandboxPin pin : cell.allPins(Rsyn::OUT)){
			const TimingPin &timingPin = getTimingPin(pin);
		out << "slew " << pin.getInstanceName() << " " << timingPin.state[EARLY].slew[RISE] <<
			" " << timingPin.state[EARLY].slew[FALL] <<
			" " << timingPin.state[LATE].slew[RISE] <<
			" " << timingPin.state[LATE].slew[RISE] << "\n";
		}
	}

	for (Rsyn::SandboxPort cell : sandbox.allPorts(Rsyn::OUT)) {
		for(Rsyn::SandboxPin pin : cell.allPins(Rsyn::IN)){
			const TimingPin &timingPin = getTimingPin(pin);
		out << "rat " << pin.getInstanceName() << " " << timingPin.state[EARLY].q[RISE] <<
			" " << timingPin.state[EARLY].q[FALL] <<
			" " << timingPin.state[LATE].q[RISE] <<
			" " << timingPin.state[LATE].q[RISE] << "\n";
		}
	}

	for (Rsyn::SandboxPort cell : sandbox.allPorts(Rsyn::OUT)) {
		const EdgeArray<Number> earlyLoadCap = getOutputLoad(cell, EARLY, EdgeArray<Number>(0, 0));
		const EdgeArray<Number> lateLoadCap = getOutputLoad(cell, EARLY, EdgeArray<Number>(0, 0));
		out << "load " << cell.getName() <<
			" " << earlyLoadCap[FALL] <<
			" " << earlyLoadCap[RISE] <<
			" " << lateLoadCap[FALL] <<
			" " << lateLoadCap[RISE] << "\n";

	}
} // end method

////////////////////////////////////////////////////////////////////////////////
// Top Critical Paths
////////////////////////////////////////////////////////////////////////////////

void SandboxTimer::queryTopCriticalPaths_Queue_AddCriticalEndpoint(CriticalPathQueue &queue,
		Rsyn::SandboxPin endpoint,
		const TimingMode mode,
		const Number slackThreshold,
		const bool checkSign,
		const int sign,
		const bool debug
) {
	Rsyn::SandboxNet net = endpoint.getNet();

	TimingPin &timingPin = getTimingPin(endpoint);
	std::tuple<Number, TimingTransition> slackTransitionPair
			= getPinWorstSlackWithTransition(timingPin, mode);

	const Number slack = std::get<0>(slackTransitionPair);
	const TimingTransition transition = std::get<1>(slackTransitionPair);
	const Number required = getPinRequiredTime(timingPin, mode, transition);

	if (slack < slackThreshold && (!checkSign || (net && getTimingNet(net).sign == sign))) {
		Reference reference(endpoint, nullptr, nullptr, required, slack, transition, -1, transition);
		queue.push(reference);
		if (debug) {
			reference.print("queuing endpoint", std::cout, design);
		} // end if
	} // end if
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::queryTopCriticalPaths_Queue_AddAllCriticalEndpoints(CriticalPathQueue &queue,
		const TimingMode mode,
		const Number slackThreshold,
		const bool checkSign,
		const int sign,
		const bool debug
) {
	// Insert the all critical endpoints in the queue. Note that even if we
	// want just a limited number of paths (e.g. 100) we still need to add all
	// critical endpoints as they need to be sorted.

	for (Rsyn::SandboxPin endpoint : allEndpoints()) {
		queryTopCriticalPaths_Queue_AddCriticalEndpoint(queue, endpoint, mode, slackThreshold, checkSign, sign, debug);
	} // end for
} // end method

// -----------------------------------------------------------------------------

bool SandboxTimer::queryTopCriticalPaths_Internal(CriticalPathQueue &queue,
		const TimingMode mode,
		const int maxNumPaths,
		std::vector<std::vector<SandboxTimer::PathHop>> &paths,
		const Number slackThreshold,
		const bool checkSign,
		const int sign,
		const bool debug
) {

	// Clear old paths.
	paths.clear();

	// Nothing to be done if the worst slack is zero or positive.
	if (getWns(mode) >= slackThreshold)
		return false; // no critical paths

	// Used to store partial paths (backtrack).
	std::deque<Reference> partialPaths;

	// Used to store path startpoint reference in partial paths vector.
	std::vector<int> startpoints;

	// Process path by worst slack.
	int pathCounter = 0;
	while (!queue.empty()) {
		if (debug) {
			queue.top().print("adding pin", std::cout, design);
		} // end if

		Reference currentReference = queue.top(); // must be copy
		Rsyn::SandboxPin currentPin = queue.top().propPin;
		Rsyn::SandboxNet currentNet = currentPin.getNet();
		const TimingTransition currentTransition = queue.top().propTransition;
		const Number currentRequired = queue.top().propRequired;
		const TimingPin &currentTimingPin = getTimingPin(currentPin);
		const int current = partialPaths.size();

		queue.pop();

		if (getPinSlack(currentPin, mode, currentTransition) >= slackThreshold)
			continue;

		if (checkSign && (currentNet && getTimingNet(currentNet).sign != sign))
			continue;

		partialPaths.push_back(currentReference);

		if (currentPin.isPort(Rsyn::IN)) {
			// Startpoint.
			startpoints.push_back(current);
			pathCounter++;
			if (pathCounter == maxNumPaths) {
				break;
			} // end if
		} else {
			switch (currentPin.getDirection()) {
				case Rsyn::IN: {
					// Put the net's driver into the queue.
					// [ASSUMPTION] Assuming only a single driver per net.
					if (currentNet) {
						Rsyn::SandboxPin driver = currentNet.getDriver();
						if (driver) {
							const Number required = currentRequired -
									currentTimingPin.state[mode].wdelay[currentTransition];
							const Number arrival = getPinArrivalTime(driver, mode, currentTransition);
							const Number slack = computeSlack(mode, arrival, required);

							Reference reference(driver, nullptr, nullptr, required, slack,
									currentTransition, current, currentTransition);
							queue.push(reference);

							if (debug) {
								reference.print("queuing driver", std::cout, design);
							} // end if
						} // end if
					} // end if

					break;
				} // end case

				case Rsyn::OUT: {
					// Put all from pins driven this pin in the queue.
					for (Rsyn::SandboxArc arc : currentPin.allIncomingArcs()) {
						TimingArc &timingArc = getTimingArc(arc);

						Rsyn::SandboxPin from = arc.getFromPin();

						const TimingTransition transition =
								timingArc.state[mode].backtrack[currentTransition];
						const Number required = currentRequired -
								timingArc.state[mode].delay[currentTransition];
						const Number arrival = getPinArrivalTime(from, mode, transition);
						const Number slack = computeSlack(mode, arrival, required);

						Reference reference(from, &timingArc, arc, required, slack,
								transition, current, currentTransition);
						queue.push(reference);

						if (debug) {
							reference.print("queuing sink", std::cout, design);
						} // end if
					} // end for

					break;
				} // end case

				default:
					assert(false);
			} // end switch

		} // end if

	} // end while

	// Construct paths from partial results.
	const int numPaths = startpoints.size();

	paths.resize(numPaths);
	int countPaths = 0;
	for (int i = 0; i < numPaths; i++) {
		int index = startpoints[i];

		const Reference &startpoint = partialPaths[index];

		Number previousArrival = 0;
		Number arrival = getPinArrivalTime(startpoint.propPin, mode, startpoint.propTransition);
		Rsyn::SandboxArc arc = nullptr;
		Rsyn::SandboxPin previousPin = nullptr;

		if (computeSlack(mode, arrival, startpoint.propRequired) >= slackThreshold)
			break;

		countPaths++;

		while (index >= 0) {
			const Reference &reference = partialPaths[index];
			const TimingPin &timingPin = getTimingPin(reference.propPin);

			arrival += timingPin.state[mode].wdelay[reference.propTransition];

			PathHop hop;
			hop.arrival = arrival;
			hop.delay = arrival - previousArrival;
			hop.required = reference.propRequired;
			hop.pin = reference.propPin;
			hop.transition = reference.propTransition;
			hop.mode = mode;
			hop.rsynArcFromThisPin = reference.propRsynArcFromThisPin;

			hop.rsynArcToThisPin = arc;
			hop.previousPin = previousPin;
			if (reference.propParentPartialPath != -1) {
				hop.nextPin = partialPaths[reference.propParentPartialPath].propPin;
			} else {
				hop.nextPin = nullptr;
			} // end else

			paths[i].push_back(hop);

			previousArrival = arrival;
			if (reference.propArcPointerFromThisPin) {
				// arc a->o: a is this pin, o is the parent pin
				arrival += reference.propArcPointerFromThisPin->state[mode].delay[
						reference.propTransitionAtParent];
			} // end if

			arc = hop.getArcFromThisPin();
			previousPin = hop.getPin();

			index = reference.propParentPartialPath;
		} // end while
	} // end method
	paths.resize(countPaths);

	return paths.size() > 0;
} // end method

// -----------------------------------------------------------------------------

bool SandboxTimer::queryTopCriticalPaths(
		const TimingMode mode,
		const int maxNumPaths,
		std::vector<std::vector<PathHop>> &paths,
		const Number slackThreshold
) {
	const bool debug = false;

	CriticalPathQueue queue;
	queryTopCriticalPaths_Queue_AddAllCriticalEndpoints(queue, mode, slackThreshold, false, -1, debug);
	return queryTopCriticalPaths_Internal(queue, mode, maxNumPaths, paths, slackThreshold, false, -1, debug);
} // end method

// -----------------------------------------------------------------------------

int SandboxTimer::queryTopCriticalPathFromTopCriticalEndpoints(
    const TimingMode mode,
    const int maxNumEndpoints,
    std::vector<std::vector<PathHop>> &paths,
    const Number slackThreshold
) {

	const bool debug = false;

	// Sort endpoints.
	std::deque<std::tuple<Number, Rsyn::SandboxPin>> sortedEndpoint;

	for (Rsyn::SandboxPin pin : allEndpoints()) {
		TimingPin &timingPin = getTimingPin(pin);
		std::tuple<Number, TimingTransition> slackTransitionPair
				= getPinWorstSlackWithTransition(timingPin, mode);

		const Number slack = std::get<0>(slackTransitionPair);
		if (slack < slackThreshold) {
			sortedEndpoint.push_back(std::make_tuple(slack, pin));
		} // end if
	} // end for

	std::sort(sortedEndpoint.begin(), sortedEndpoint.end());

	// Generate paths.
	const int numEndpoints = std::min((size_t) maxNumEndpoints,
			sortedEndpoint.size());

	paths.clear();
	paths.resize(numEndpoints);

	for (int i = 0; i < numEndpoints; i++) {
		CriticalPathQueue queue;
		std::vector<std::vector<PathHop>> endpointPaths;

		queryTopCriticalPaths_Queue_AddCriticalEndpoint(queue,
				std::get<1>(sortedEndpoint[i]),
				mode,
				slackThreshold,
				false,
				-1,
				debug);

		if (queryTopCriticalPaths_Internal(queue, mode, 1, endpointPaths, slackThreshold, false, -1, debug)) {
			paths[i].swap(endpointPaths[0]);
		} // end if
	} // end for

	return numEndpoints;
} // end method

// -----------------------------------------------------------------------------

int SandboxTimer::queryTopCriticalPathsFromTopCriticalEndpoints(
		const TimingMode mode,
		const int maxNumEndpoints,
		const int maxNumPathsPerEndpoint,
		std::vector<std::vector<std::vector<PathHop>>> &paths,
		const Number slackThreshold
) {

	const bool debug = false;

	// Sort endpoints.
	std::deque<std::tuple<Number, Rsyn::SandboxPin>> sortedEndpoint;

	for (Rsyn::SandboxPin pin : allEndpoints()) {
		TimingPin &timingPin = getTimingPin(pin);
		std::tuple<Number, TimingTransition> slackTransitionPair
				= getPinWorstSlackWithTransition(timingPin, mode);

		const Number slack = std::get<0>(slackTransitionPair);
		if (slack < slackThreshold) {
			sortedEndpoint.push_back(std::make_tuple(slack, pin));
		} // end if
	} // end for

	std::sort(sortedEndpoint.begin(), sortedEndpoint.end());

	// Generate paths.
	const int numEndpoints = std::min((size_t) maxNumEndpoints,
			sortedEndpoint.size());

	paths.clear();
	paths.resize(numEndpoints);

	int pathCounter = 0;
	for (int i = 0; i < numEndpoints; i++) {
		CriticalPathQueue queue;
		std::vector<std::vector<PathHop>> endpointPaths;

		queryTopCriticalPaths_Queue_AddCriticalEndpoint(queue,
				std::get<1>(sortedEndpoint[i]),
				mode,
				slackThreshold,
				false,
				-1,
				debug);

		if (queryTopCriticalPaths_Internal(queue, mode, maxNumPathsPerEndpoint, endpointPaths, slackThreshold, false, -1, debug)) {
			const int numEndpointPaths = endpointPaths.size();
			paths[i].resize(numEndpointPaths);
			for (int k = 0; k < numEndpointPaths; k++) {
				paths[i][k].swap(endpointPaths[k]);
			} // end for

			pathCounter += numEndpointPaths;
		} // end if
	} // end for

	return pathCounter;
} // end method

// -----------------------------------------------------------------------------

int SandboxTimer::queryTopCriticalPathsFromEndpoint(
		const TimingMode mode,
		const Rsyn::SandboxPin endpoint,
		const int maxNumPaths,
		std::vector<std::vector<PathHop>> &paths,
		const Number slackThreshold
) {

	const bool debug = false;

	CriticalPathQueue queue;

	queryTopCriticalPaths_Queue_AddCriticalEndpoint(queue, endpoint, mode,
			slackThreshold, false, -1, debug);

	queryTopCriticalPaths_Internal(queue, mode, maxNumPaths, paths,
			slackThreshold, false, -1, debug);

	return paths.size();
} // end method

// -----------------------------------------------------------------------------

int SandboxTimer::queryTopCriticalPathsPassingThruPin(
		const TimingMode mode,
		const Rsyn::SandboxPin referencePin,
		const int maxNumPaths,
		std::vector<std::vector<PathHop>> &paths,
		const Number slackThreshold
) {
	const bool debug = false;
	const int sign = generateNextSign();

	// Mark nets in the fan-in of the reference pin.
	for (Rsyn::SandboxNet net : sandbox.getFaninConeNetsInBreadthFirstOrder(referencePin)) {
		getTimingNet(net).sign = sign;
	} // end for

	// Mark nets in the fan-out of the reference pin.
	for (Rsyn::SandboxNet net : sandbox.getFanoutConeNetsInBreadthFirstOrder(referencePin)) {
		getTimingNet(net).sign = sign;
	} // end for

	// Generate paths.
	CriticalPathQueue queue;

	queryTopCriticalPaths_Queue_AddAllCriticalEndpoints(queue, mode,
			slackThreshold, true, sign, debug);

	queryTopCriticalPaths_Internal(queue, mode, maxNumPaths, paths,
			slackThreshold, true, sign, debug);

	return paths.size();
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::updatePath(std::vector<PathHop> &path) {
	const int numHops = path.size();
	if (numHops == 0)
		return;

	const PathHop &startpoint = path.front();
	const PathHop &endpoint = path.back();

	const TimingMode mode = startpoint.getTimingMode();

	// Update arrival times.
	Number arrival = getPinArrivalTime(startpoint.getPin(), mode,
			startpoint.getTransition());

	for (int i = 0; i < numHops; i++) {
		PathHop &hop = path[i];

		hop.delay = getPathHopDelay(hop);
		arrival += hop.delay;
		hop.arrival = arrival;
	} // end for

	// Update required times.
	Number required = getPinRequiredTime(endpoint.getPin(), mode,
			endpoint.getTransition());

	for (int i = numHops - 1; i >= 0; i--) {
		PathHop &hop = path[i];

		hop.required = required;
		required -= getPathHopDelay(hop);
	} // end for
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::reportPath(const std::vector<PathHop> &path, std::ostream &out) {
	if (path.empty())
		return;

    std::ios streamState(nullptr);
    streamState.copyfmt(out);

	const TimingMode mode = path.front().getTimingMode();

	out << "================================================================================\n";
	out << "Path Report\n";
	out << "Mode: " << ((mode == EARLY)? "Early" : "Late") << "\n";
	out << "================================================================================\n";

	out << std::right;

	out << std::setw(3) << " " << " ";
	out << std::setw(11) << "Wire/Arc" << " ";
	out << std::setw(9) << "Arrival" << " ";
	out << std::setw(9) << "Required" << " ";
	out << std::setw(9) << "Slack" << " ";
	out << std::setw(7) << "Net" << " ";
	out << std::setw(7) << "Net" << " ";
	out << std::setw(9) << "" << " ";

	out << " ";
	out << "\n";

	out << std::setw(3) << "#" << " ";
	out << std::setw(11) << "Delay" << " ";
	out << std::setw(9) << "Time" << " ";
	out << std::setw(9) << "Time" << " ";
	out << std::setw(9) << " " << " ";
	out << std::setw(7) << "Load" << " ";
	out << std::setw(7) << "Fanout" << " ";
	out << std::setw(9) << "Slew" << " ";

	out << "Pin";
	out << "\n";

	out << "--------------------------------------------------------------------------------\n";

	out << std::fixed << std::setprecision(2);

	const int numHops = path.size();
	for (int i = 0; i < numHops; i++) {
		const PathHop &hop = path[i];

		const TimingPin &timingPin = getTimingPin(hop.getPin());
		Rsyn::SandboxNet net = hop.getNet();

		out << std::setw(3) << (i+1) << " ";

		out << std::setw(9) << getPathHopDelay(hop) << " ";
		out << std::setw(1) << (hop.getArcToThisPin()? "a" : "w") << " ";

		out << std::setw(9) << hop.getArrival() << " ";
		out << std::setw(9) << hop.getRequired() << " ";
		out << std::setw(9) << hop.getSlack() << " ";

		if (net) {
			out << std::setw(7);

			EdgeArray<Number> load;
			timingModel->calculateLoadCapacitance(hop.getPin(), mode, load);
			out << load[hop.getTransition()];

			out << std::setw(7);
			out << net.getNumSinks();
		} else {
			// This should not happen, but...
			out << std::setw(7);
			out << "-";

			out << std::setw(7);
			out << "-";
		} // end method
		out << " ";

		out << std::setw(9) << timingPin.state[mode].slew[hop.getTransition()] << " ";

		out << (hop.getTransition() == RISE? "r" : "f") << " ";

		out << hop.getPin().getFullName();
		switch (hop.getInstance().getType()) {
			case Rsyn::CELL:
				out << " (" << hop.getInstance().asCell().getLibraryCellName() << ")";
				break;
			case Rsyn::PORT:
				out << " (" << "<PORT>" << ")";
				break;
			default:
				out << " (" << "<BUG>" << ")";
		} // end switch

		out << "\n";
	} // end method

	out << "--------------------------------------------------------------------------------\n";

	// Restore formatting state.
	out.copyfmt(streamState);
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::reportPaths(const std::vector<std::vector<PathHop>> &paths, std::ostream &out) {
	const int numPaths = paths.size();
	for (int i = 0; i < numPaths; i++) {
		reportPath(paths[i]);
		out << "\n";
	} // end method
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::reportCriticalPath(const TimingMode mode, std::ostream &out) {
	std::vector<PathHop> path;
	queryTopCriticalPath(mode, path);
	reportPath(path);
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::reportEndpoints(const TimingMode mode, const int top, const Number slackThreshold, std::ostream &out) {
	std::vector<Rsyn::SandboxPin> endpoints;
	queryTopCriticalEndpoints(mode, top, endpoints, slackThreshold);

    std::ios streamState(nullptr);
    streamState.copyfmt(out);

	out << "================================================================================\n";
	out << "Endpoint Report\n";
	out << "Mode: " << ((mode == EARLY)? "Early" : "Late") << "\n";
	out << "================================================================================\n";

	out << std::fixed << std::setprecision(2);
	out << std::right;

	out << std::setw(9) << "Slack" << " ";
	out << std::setw(9) << "Arrival" << " ";
	out << std::setw(9) << "Required" << " ";
	out << std::setw(4) << "Edge" << " ";
	out << "Pin" << "\n";

	for (Rsyn::SandboxPin endpoint : endpoints) {
		const std::tuple<Number, TimingTransition> t =
				getPinWorstSlackWithTransition(endpoint, mode);

		const Number slack = std::get<0>(t);
		const TimingTransition edge = std::get<1>(t);

		out << std::setw(9) << slack << " ";
		out << std::setw(9) << getPinArrivalTime(endpoint, mode, edge) << " ";
		out << std::setw(9) << getPinRequiredTime(endpoint, mode, edge) << " ";
		out << std::setw(4) << (edge == FALL? "f" : "r") << " ";
		out << endpoint.getFullName() << "\n";
	} // end for


	// Restore formatting state.
	out.copyfmt(streamState);
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::getPathDelayDueToCellsAndWires(const std::vector<PathHop> &path,
		Number &delayDueToCell,
		Number &delayDueToWire
) {
	delayDueToCell = 0;
	delayDueToWire = 0;

	const int numHops = path.size();
	for (int i = 0; i < numHops; i++) {
		const PathHop &hop = path[i];

		Rsyn::SandboxPin from = hop.getPreviousPin();
		if (from && from.isInput()) {
			delayDueToCell += hop.getDelay();
		} else {
			if (from) {
				delayDueToWire += hop.getDelay();
			} // end if
		} // end if
	} // end for
} // end method

// -----------------------------------------------------------------------------

std::string SandboxTimer::getPathHash(const std::vector<PathHop> &path, const bool ignoreTransitions) const {
	std::string str;

	const int numHops = path.size();
	for (int i = 0; i < numHops; i++) {
		const PathHop &hop = path[i];

		str += hop.getPin().getFullName();
		if (!ignoreTransitions)
			str += hop.getTransition() == RISE? "RISE" : "FALL";
	} // end for

	return md5(str);
} // end method

////////////////////////////////////////////////////////////////////////////////
// Endpoints
////////////////////////////////////////////////////////////////////////////////

bool SandboxTimer::queryTopCriticalEndpoints(
		const TimingMode mode,
		const int maxNumEndpoints,
		std::vector<Rsyn::SandboxPin> &endpoints,
		const Number slackThreshold
) {
	const bool debug = false;

	CriticalPathQueue queue;
	queryTopCriticalPaths_Queue_AddAllCriticalEndpoints(queue, mode, slackThreshold, false, -1, debug);

	endpoints.clear();
	endpoints.reserve(maxNumEndpoints);
	while (!queue.empty() && endpoints.size() < maxNumEndpoints) {
		endpoints.push_back(queue.top().propPin);
		queue.pop();
	} // end method

	return !endpoints.empty();
} // end method

////////////////////////////////////////////////////////////////////////////////
// Constraints
////////////////////////////////////////////////////////////////////////////////

void SandboxTimer::setInputDriver(Rsyn::SandboxInstance port, InputDriver driver) {
	clsInputDrivers[port] = driver;
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::setInputDelay(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &value) {
	clsInputDelays[mode][port] = value;
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::setInputTransition(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &value) {
	clsInputTransitions[mode][port] = value;
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::setOutputRequiredTime(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &value) {
	clsOutputRequiredTimes[mode][port] = value;
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::setOutputLoad(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &value) {
	clsOutputLoads[mode][port] = value;
} // end method

// -----------------------------------------------------------------------------

void SandboxTimer::setOutputDelay(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &value) {
	clsOutputDelays[mode][port] = value;
} // end method

// -----------------------------------------------------------------------------

EdgeArray<Number> SandboxTimer::getInputDelay(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &defaultValue) const {
	auto it = clsInputDelays[mode].find(port);
	return it == clsInputDelays[mode].end() ? defaultValue : it->second;
} // end method

// -----------------------------------------------------------------------------

EdgeArray<Number> SandboxTimer::getInputTransition(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &defaultValue) const {
	auto it = clsInputTransitions[mode].find(port);
	return it == clsInputTransitions[mode].end() ? defaultValue : it->second;
} // end method

// -----------------------------------------------------------------------------

EdgeArray<Number> SandboxTimer::getOutputRequiredTime(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &defaultValue) const {
	auto it = clsOutputRequiredTimes[mode].find(port);
	return it == clsOutputRequiredTimes[mode].end()? defaultValue : it->second;
} // end method

// -----------------------------------------------------------------------------

EdgeArray<Number> SandboxTimer::getOutputLoad(Rsyn::SandboxInstance port, const TimingMode &mode, const EdgeArray<Number> &defaultValue) const {
	auto it = clsOutputLoads[mode].find(port);
	return it == clsOutputLoads[mode].end() ? defaultValue : it->second;
} // end method

// -----------------------------------------------------------------------------

const SandboxTimer::InputDriver *SandboxTimer::getInputDriver(Rsyn::SandboxPort port) const {
	auto it = clsInputDrivers.find(port);
	return (it == clsInputDrivers.end())? nullptr : &it->second;
} // end method


} // end namespace
