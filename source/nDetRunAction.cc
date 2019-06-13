//
// $Id: nDetRunAction.cc,v1.0 Sept., 2015 $
// Written by Dr. Xiaodong Zhang
// 
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#include "time.h"

#include "G4Timer.hh"
#include "G4Run.hh"

#include "nDetDataPack.hh"
#include "nDetThreadContainer.hh"
#include "nDetRunAction.hh"
#include "nDetEventAction.hh"
#include "nDetStackingAction.hh"
#include "nDetTrackingAction.hh"
#include "nDetSteppingAction.hh"
#include "nDetParticleSource.hh"
#include "nDetParticleSourceMessenger.hh"
#include "termColors.hh"

#include "nDetConstructionMessenger.hh"
#include "nDetRunActionMessenger.hh"
#include "nDetMasterOutputFile.hh"

const double KINETIC_ENERGY_THRESHOLD = 0.001; // MeV

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

/// Returns the dot product of two vectors i.e. v1 * v2
double dotProduct(const G4ThreeVector &v1, const G4ThreeVector &v2){
	return (v1.getX()*v2.getX() + v1.getY()*v2.getY() + v1.getZ()*v2.getZ());
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

primaryTrackInfo::primaryTrackInfo(const G4Step *step){
	this->setValues(step->GetTrack());
	if(step->GetPreStepPoint()->GetPhysicalVolume()->GetName().find("Scint") != std::string::npos) // Scatter event occured inside a scintillator.
		inScint = true;
	dkE = -1*(step->GetPostStepPoint()->GetKineticEnergy()-step->GetPreStepPoint()->GetKineticEnergy());
}

primaryTrackInfo::primaryTrackInfo(const G4Track *track){
	this->setValues(track);
}

bool primaryTrackInfo::compare(const G4Track *rhs){
	return (this->pos == rhs->GetPosition());
}

double primaryTrackInfo::getAngle(const G4Track *rhs){
	G4ThreeVector dir2 = rhs->GetMomentumDirection();
	if(this->pos == rhs->GetPosition())
		angle = getAngle(dir2);
	else
		angle = -1;
	return angle;
}

double primaryTrackInfo::getAngle(const G4ThreeVector &dir2){
	angle = std::acos(dotProduct(dir, dir2)/(dir.mag()*dir2.mag())) * 180/3.14159;
	return angle;
}

double primaryTrackInfo::getPathLength(const G4ThreeVector &rhs){
	return (plength = (pos-rhs).mag());
}

void primaryTrackInfo::print(){
	std::cout << "name=" << part->GetParticleName() << ", kE=" << kE << ", dkE=" << dkE << ", pos=(" << pos.getX() << ", " << pos.getY() << ", " << pos.getZ() << "), dir=(" << dir.getX() << ", " << dir.getY() << ", " << dir.getZ() << ")\n";
}

void primaryTrackInfo::setValues(const G4Track *track){
	pos = track->GetPosition();
	dir = track->GetMomentumDirection();
	kE = track->GetKineticEnergy();
	dkE = -1;
	gtime = track->GetGlobalTime();
	plength = track->GetTrackLength();
	part = track->GetParticleDefinition();
	
	copyNum = track->GetTouchable()->GetCopyNumber();
	trackID = track->GetTrackID();
	atomicMass = part->GetAtomicMass();
	inScint = false;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

nDetRunAction::nDetRunAction(){
	timer = new G4Timer;
	
	outputDebug = false;
	verbose = false;
	printTrace = false;
	
	eventAction = NULL;
	stacking = NULL;
	tracking = NULL;
	stepping = NULL;
	
	// Setup the particle source.
	source = new nDetParticleSource();

	baselineFraction = 0;
	baselineJitterFraction = 0;
	polyCfdFraction = 0.5;

	pulseIntegralLow = 5;
	pulseIntegralHigh = 10;
	
	numPhotonsTotal = 0;
	numPhotonsDetTotal = 0;
  
	// Create a messenger for this class
	fActionMessenger = new nDetRunActionMessenger(this); 
	
	// Get a pointer to the detector.
	detector = &nDetConstruction::getInstance(); // The detector builder is a singleton class.
	
	// Set data structure addresses.
	data.setDataAddresses(&evtData, &outData, &multData, &debugData);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

nDetRunAction::~nDetRunAction(){
	delete timer;
	delete fActionMessenger;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

void nDetRunAction::BeginOfRunAction(const G4Run* aRun)
{
	// Update the debug output flag (may not be thread safe CRT)
	nDetMasterOutputFile *outputFile = &nDetMasterOutputFile::getInstance();
	outputDebug = outputFile->getOutputDebug();

	numPhotonsTotal = 0;
	numPhotonsDetTotal = 0;

	// Get RunId and threadID
	evtData.runNb = aRun->GetRunID();
	evtData.threadID = G4Threading::G4GetThreadId();

	// Update the source
	source->UpdateAll();

	if(G4Threading::G4GetThreadId() >= 0) return; // Master thread only.
	
	G4cout << "nDetRunAction::BeginOfRunAction()->"<< G4endl;
	G4cout << "### Run " << aRun->GetRunID() << " start." << G4endl; 
	timer->Start();

	if(userDetectors.size() > 1){
		outputFile->setMultiDetectorMode(true);
		if(outputDebug){ // Check for output debug mode with multiple detectors
			Display::WarningPrint("Debug output is not supported for more than one detector!", "nDetRunAction");
			Display::WarningPrint(" Disabling output debug mode.", "nDetRunAction");
			outputFile->setOutputDebug((outputDebug = false));
			nDetThreadContainer *container = &nDetThreadContainer::getInstance();
			for(size_t index = 0; index < container->size(); index++){ // Disable for all other threads
				container->getAction(index)->setOutputDebug(false);
			}
		}
	}

	// Open a root file.
	outputFile->openRootFile(aRun);

	// Set the total number of events
	outputFile->setTotalEvents(aRun->GetNumberOfEventToBeProcessed());
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

void nDetRunAction::EndOfRunAction(const G4Run* aRun)
{
	if(!IsMaster()) return; // Master thread only.
	
	timer->Stop();
	G4cout << "number of event = " << aRun->GetNumberOfEvent() << " " << *timer << G4endl;
}

void nDetRunAction::setSegmentedPmt(const short &col_, const short &row_, const double &width_, const double &height_){
	for(std::vector<centerOfMass>::iterator iterL = cmL.begin(), iterR = cmR.begin(); iterL != cmL.end() && iterR != cmR.end(); iterL++, iterR++){
		iterL->setSegmentedPmt(col_, row_, width_, height_);
		iterR->setSegmentedPmt(col_, row_, width_, height_);
	}
}

void nDetRunAction::setPmtSpectralResponse(centerOfMass *left, centerOfMass *right){
	for(std::vector<centerOfMass>::iterator iterL = cmL.begin(), iterR = cmR.begin(); iterL != cmL.end() && iterR != cmR.end(); iterL++, iterR++){
		iterL->copySpectralResponse(left);
		iterR->copySpectralResponse(right);
	}
}

void nDetRunAction::setPmtGainMatrix(centerOfMass *left, centerOfMass *right){
	for(std::vector<centerOfMass>::iterator iterL = cmL.begin(), iterR = cmR.begin(); iterL != cmL.end() && iterR != cmR.end(); iterL++, iterR++){
		iterL->copyGainMatrix(left);
		iterR->copyGainMatrix(right);
	}
}

void nDetRunAction::updateDetector(nDetConstruction *det){
	source->SetDetector(det);
	userDetectors = det->GetUserDetectors();
	cmL.clear();
	cmR.clear();
	cmL.reserve(userDetectors.size());
	cmR.reserve(userDetectors.size());
	for(std::vector<userAddDetector>::iterator iter = userDetectors.begin(); iter != userDetectors.end(); iter++){ // Link the PMTs to the detectors
		cmL.push_back(centerOfMass());
		cmR.push_back(centerOfMass());
		iter->setCenterOfMass(&cmL.back(), &cmR.back());
	}
}

G4int nDetRunAction::checkCopyNumber(const G4int &num) const {
	for(std::vector<userAddDetector>::const_iterator iter = userDetectors.begin(); iter != userDetectors.end(); iter++){
		if(iter->checkCopyNumber(num))
			return iter->getParentCopyNumber();
	}
	return -1;
}

bool nDetRunAction::getSegmentFromCopyNum(const G4int &copyNum, G4int &col, G4int &row) const {
	for(std::vector<userAddDetector>::const_iterator iter = userDetectors.begin(); iter != userDetectors.end(); iter++){
		if(iter->getSegmentFromCopyNum(copyNum, col, row))
			return true;
	}
	return false;
}

void nDetRunAction::process(){
	while(this->scatterEvent()){
	}
	
	if(counter){ 
		debugData.photonsProd.clear(); // A little messy :(
		for(short i = evtData.nScatters+1; i >= 2; i--) // The first recoil particle ID is equal to 2
			debugData.photonsProd.push_back(counter->getPhotonCount(i));
	}

	// Get the time offset due to straggling in the target (if applicable)
	double targetTimeOffset = source->GetTargetTimeOffset();
	
	short detID = 0;
	for(std::vector<centerOfMass>::iterator iterL = cmL.begin(), iterR = cmR.begin(); iterL != cmL.end() && iterR != cmR.end(); iterL++, iterR++){
		if(iterL->empty() && iterR->empty()){ // Skip events with no detected photons
			detID++;
			continue;
		}
	
		debugData.nPhotons[0] += iterL->getNumDetected();
		debugData.nPhotons[1] += iterR->getNumDetected();
		
		// Compute the total number of detected photons
		outData.nPhotonsDetTot += iterL->getNumDetected() + iterR->getNumDetected();
			
		// Check for valid bar detection
		if(iterL->getNumDetected() > 0 && iterR->getNumDetected() > 0)
			evtData.goodEvent = true;
			
		// Compute the photon detection efficiency
		outData.nPhotonsTot = stacking->GetNumPhotonsProduced();
		if(outData.nPhotonsTot > 0)
			outData.photonDetEff = outData.nPhotonsDetTot/(double)outData.nPhotonsTot;
		else
			outData.photonDetEff = -1;			

		// Get the photon center-of-mass positions
		G4ThreeVector centerL = iterL->getCenter();
		G4ThreeVector centerR = iterR->getCenter();
		debugData.photonDetCenterOfMassX[0] = centerL.getX(); debugData.photonDetCenterOfMassX[1] = centerR.getX(); 
		debugData.photonDetCenterOfMassY[0] = centerL.getY(); debugData.photonDetCenterOfMassY[1] = centerR.getY(); 
		//debugData.photonDetCenterOfMassZ[0] = centerL.getZ(); debugData.photonDetCenterOfMassZ[1] = centerR.getZ(); 

		// Get photon arrival times at the PMTs
		debugData.photonMinTime[0] = iterL->getMinArrivalTime();
		debugData.photonAvgTime[0] = iterL->getAvgArrivalTime();

		debugData.photonMinTime[1] = iterR->getMinArrivalTime();
		debugData.photonAvgTime[1] = iterR->getAvgArrivalTime();
		
		pmtResponse *pmtL = iterL->getPmtResponse();
		pmtResponse *pmtR = iterR->getPmtResponse();

		// "Digitize" the light pulses.
		pmtL->digitize(baselineFraction, baselineJitterFraction);
		pmtR->digitize(baselineFraction, baselineJitterFraction);
		
		// Check for saturated pulse.
		if(pmtL->getPulseIsSaturated() || pmtR->getPulseIsSaturated()){
			Display::WarningPrint("One or more PMT's traces have saturated! Recommend lowering the gain.", "nDetRunAction");
		}
		
		// Copy the trace into the trace vector.
		/*if(outputTraces){
			pmtL->copyTrace(data.lightPulseL);
			pmtR->copyTrace(data.lightPulseR);
		}*/		
		
		// Do some light pulse analysis
		debugData.pulsePhase[0] = pmtL->analyzePolyCFD(polyCfdFraction) + targetTimeOffset;
		debugData.pulseQDC[0] = pmtL->integratePulseFromMaximum(pulseIntegralLow, pulseIntegralHigh);
		debugData.pulseMax[0] = pmtL->getMaximum();
		debugData.pulseMaxTime[0] = pmtL->getMaximumTime();
		debugData.pulseArrival[0] = pmtL->getWeightedPhotonArrivalTime();

		debugData.pulsePhase[1] = pmtR->analyzePolyCFD(polyCfdFraction) + targetTimeOffset;
		debugData.pulseQDC[1] = pmtR->integratePulseFromMaximum(pulseIntegralLow, pulseIntegralHigh);
		debugData.pulseMax[1] = pmtR->getMaximum();
		debugData.pulseMaxTime[1] = pmtR->getMaximumTime();
		debugData.pulseArrival[1] = pmtR->getWeightedPhotonArrivalTime();		

		// Print the digitized traces.
		if(printTrace){
			size_t traceLength = pmtL->getPulseLength();
			unsigned short *traceL = pmtL->getDigitizedPulse();
			unsigned short *traceR = pmtR->getDigitizedPulse();
			std::cout << "***********************************************************\n";
			std::cout << "* PhotonsTot	 : " << outData.nPhotonsTot << std::endl;
			std::cout << "* PhotonsDet	 : " << outData.nPhotonsDetTot << std::endl;
			std::cout << "* MaxIndex	   : " << pmtL->getMaximumIndex() << "\t" << pmtR->getMaximumIndex() << std::endl;
			std::cout << "* Baseline	   : " << pmtL->getBaseline() << "\t" << pmtR->getBaseline() << std::endl;	
			std::cout << "* Maximum		: " << pmtL->getMaximum() << "\t" << pmtR->getMaximum() << std::endl;
			std::cout << "* MaxTime		: " << pmtL->getMaximumTime() << "\t" << pmtR->getMaximumTime() << std::endl;
			std::cout << "* WeightedArrival: " << pmtL->getWeightedPhotonArrivalTime() << "\t" << pmtR->getWeightedPhotonArrivalTime() << std::endl;
			std::cout << "* MinimumArrival : " << pmtL->getMinimumPhotonArrivalTime() << "\t" << pmtR->getMinimumPhotonArrivalTime() << std::endl;
			std::cout << "***********************************************************\n";

			int adcClockTick = pmtL->getAdcClockTick();
			for(size_t i = 0; i < traceLength; i++){
				std::cout << i*adcClockTick << "\t" << traceL[i] << "\t" << traceR[i] << std::endl;
			}
		}
		
		// Get the digitizer response of the anodes.
		pmtResponse *anodeResponseL = iterL->getAnodeResponse();
		pmtResponse *anodeResponseR = iterR->getAnodeResponse();

		// Digitize anode waveforms and integrate.
		float anodeQDC[2][4];
		for(size_t i = 0; i < 4; i++){
			anodeResponseL[i].digitize(baselineFraction, baselineJitterFraction);
			anodeResponseR[i].digitize(baselineFraction, baselineJitterFraction);
			debugData.anodeQDC[0][i] = anodeResponseL[i].integratePulseFromMaximum(pulseIntegralLow, pulseIntegralHigh);
			debugData.anodeQDC[1][i] = anodeResponseR[i].integratePulseFromMaximum(pulseIntegralLow, pulseIntegralHigh);
		}		
		
		// Compute the anode positions.
		for(size_t i = 0; i < 2; i++){
			debugData.reconstructedCenterX[i] = -((anodeQDC[i][0]+anodeQDC[i][1])-(anodeQDC[i][2]+anodeQDC[i][3]))/(anodeQDC[i][0]+anodeQDC[i][1]+anodeQDC[i][2]+anodeQDC[i][3]);
			debugData.reconstructedCenterY[i] = ((anodeQDC[i][1]+anodeQDC[i][2])-(anodeQDC[i][3]+anodeQDC[i][0]))/(anodeQDC[i][0]+anodeQDC[i][1]+anodeQDC[i][2]+anodeQDC[i][3]);
		}
		outData.reconComX = (debugData.reconstructedCenterX[0] + debugData.reconstructedCenterX[1]) / 2;
		outData.reconComY = (debugData.reconstructedCenterY[0] + debugData.reconstructedCenterY[1]) / 2;
		
		if(outputDebug){
			// Perform CFD on digitized anode waveforms.
			for(size_t i = 0; i < 4; i++){
				debugData.anodePhase[0][i] = anodeResponseL[i].analyzePolyCFD(polyCfdFraction) + targetTimeOffset; // left
				debugData.anodePhase[1][i] = anodeResponseR[i].analyzePolyCFD(polyCfdFraction) + targetTimeOffset; // right
			}

			G4ThreeVector nCenterMass(debugData.nComX, debugData.nComY, debugData.nComZ);
			G4ThreeVector nIncidentPos(debugData.nEnterPosX, debugData.nEnterPosY, debugData.nEnterPosZ);
			G4ThreeVector nExitPos(debugData.nExitPosX, debugData.nExitPosY, debugData.nExitPosZ);

			// Compute the neutron scatter center-of-mass.
			nCenterMass = (1/debugData.neutronWeight)*nCenterMass;
		
			// Convert the neutron incident/exit positions to the frame of the detector.
			nIncidentPos = nIncidentPos;
			nExitPos = nExitPos;

			// Now in the rotated frame of the detector.
			debugData.nComX = nCenterMass.getX();
			debugData.nComY = nCenterMass.getY();
			debugData.nComZ = nCenterMass.getZ();
			debugData.nEnterPosX = nIncidentPos.getX();
			debugData.nEnterPosY = nIncidentPos.getY();
			debugData.nEnterPosZ = nIncidentPos.getZ();
			debugData.nExitPosX = nExitPos.getX();
			debugData.nExitPosY = nExitPos.getY();
			debugData.nExitPosZ = nExitPos.getZ();
		}

		// Compute the light balance (Z).
		outData.lightBalance = (debugData.pulseQDC[0]-debugData.pulseQDC[1])/(debugData.pulseQDC[0]+debugData.pulseQDC[1]);

		// Compute "bar" variables.
		outData.barTOF = (debugData.pulsePhase[0]+debugData.pulsePhase[1])/2;
		outData.barQDC = std::sqrt(debugData.pulseQDC[0]*debugData.pulseQDC[1]);
		outData.barMaxADC = std::sqrt(debugData.pulseMax[0]*debugData.pulseMax[1]);
		outData.photonComX = (debugData.photonDetCenterOfMassX[0] + debugData.photonDetCenterOfMassX[1]) / 2;
		outData.photonComY = (debugData.photonDetCenterOfMassY[0] + debugData.photonDetCenterOfMassY[1]) / 2;
		//debugData.photonComZ = (debugData.photonDetCenterOfMassZ[0] + debugData.photonDetCenterOfMassZ[1]) / 2;

		// Get the segment of the detector where the photon CoM occurs.
		iterL->getCenterSegment(debugData.centerOfMassColumn[0], debugData.centerOfMassRow[0]);
		iterR->getCenterSegment(debugData.centerOfMassColumn[1], debugData.centerOfMassRow[1]);		

		// Update photon statistics.
		numPhotonsTotal += outData.nPhotonsTot;
		numPhotonsDetTotal += outData.nPhotonsDetTot;
		
		// Push data onto the output branch for multiple detectors
		if(userDetectors.size() > 1)
			multData.Append(outData, detID++);
	}
	
	// Write the data (mutex protected, thread safe).
	nDetMasterOutputFile::getInstance().fillBranch(data); // The master output file is a singleton class.

	// Clear all data structures.
	data.clear();

	// Clear all statistics.
	for(std::vector<centerOfMass>::iterator iterL = cmL.begin(), iterR = cmR.begin(); iterL != cmL.end() && iterR != cmR.end(); iterL++, iterR++){
		iterL->clear();
		iterR->clear();
	}
	
	if(stacking) stacking->Reset();
	if(tracking) tracking->Reset();
	if(stepping) stepping->Reset();
}

void nDetRunAction::setActions(nDetEventAction *event_, nDetStackingAction *stacking_, nDetTrackingAction *tracking_, nDetSteppingAction *stepping_){
	eventAction = event_;
	stacking = stacking_;
	tracking = tracking_;
	stepping = stepping_;

	// Get the photon counter
	counter = stacking->GetCounter();
}

bool nDetRunAction::AddDetectedPhoton(const G4Step *step, const double &mass/*=1*/){
	if(!step->GetPostStepPoint()){
		Display::WarningPrint("INVALID POST POINT!", "nDetRunAction");
		return false;
	}
	
	// Find which detector this optical photon is inside.
	G4int copyNum = step->GetPostStepPoint()->GetTouchable()->GetCopyNumber();
	bool foundMatch=false, isLeft;
	G4ThreeVector *detPos;
	G4RotationMatrix *detRot;
	centerOfMass *hitDetPmtL;
	centerOfMass *hitDetPmtR;
	for(std::vector<userAddDetector>::iterator iter = userDetectors.begin(); iter != userDetectors.end(); iter++){
		if(iter->checkPmtCopyNumber(copyNum, isLeft)){
			foundMatch = true;
			detPos = iter->getPosition();
			detRot = iter->getRotation();
			hitDetPmtL = iter->getCenterOfMassL();
			hitDetPmtR = iter->getCenterOfMassR();
			break;
		}
	}
	
	if(!foundMatch){
		Display::WarningPrint("Failed to find matching detector for detected photon?", "nDetRunAction");
		return false;
	}
	
	double energy = step->GetTrack()->GetTotalEnergy();
	double time = step->GetPostStepPoint()->GetGlobalTime();
	G4ThreeVector position = step->GetPostStepPoint()->GetPosition() - (*detPos);
	position = (*detRot)*position;
	
	if(isLeft){
		if(hitDetPmtL->addPoint(energy, time, position, mass))
			return true;
	}
	if(hitDetPmtR->addPoint(energy, time, position, mass))
		return true;
	return false;
}


bool nDetRunAction::scatterEvent(){
	if(primaryTracks.size() <= 1)
		return false;

	// Get a primary particle step off the stack.
	primaryTrackInfo *priTrack = &primaryTracks.back();

	if(evtData.nScatters++ == 0){ // First scatter event (CAREFUL! These are in reverse order).
		if(primaryTracks.size() < 2){
			if(verbose){
				Display::WarningPrint("Expected at least two events in primaryTracks.", "nDetRunAction");
				priTrack->print();
			}
			return false;
		}
		if(outputDebug){
			G4ThreeVector firstScatter = primaryTracks.at(0).pos + primaryTracks.at(1).pos;
			debugData.nFirstScatterTime = primaryTracks.at(1).gtime;
			debugData.nFirstScatterLen = firstScatter.mag();
		}
	}
	
	// Add the deposited energy to the total.
	evtData.nDepEnergy += priTrack->dkE;

	G4ThreeVector vertex = priTrack->pos;

	if(verbose){
		std::cout << " id=" << priTrack->trackID << " "; priTrack->print();	
		std::cout << "	pos=(" << vertex.getX() << ", " << vertex.getY() << ", " << vertex.getZ() << ")\n";
		std::cout << "	dE=" << priTrack->dkE << ", dE2=" << priTrack->kE << ", size=" << primaryTracks.size() << std::endl;
	}

	if(outputDebug){
		G4int segCol, segRow;
		getSegmentFromCopyNum(priTrack->copyNum, segCol, segRow);
		debugData.Append(vertex.getX(), vertex.getY(), vertex.getZ(), priTrack->angle, priTrack->plength, (priTrack->gtime - debugData.nEnterTime), 
		                 priTrack->dkE, segCol, segRow, 0, priTrack->atomicMass, priTrack->inScint);
		
		// Update the neutron scatter center-of-mass
		debugData.nComX += priTrack->dkE*vertex.getX();
		debugData.nComY += priTrack->dkE*vertex.getY();
		debugData.nComZ += priTrack->dkE*vertex.getZ();
		debugData.neutronWeight += priTrack->dkE;
	}
	
	primaryTracks.pop_back();
	
	return true;
}

void nDetRunAction::initializeNeutron(const G4Step *step){
	evtData.nInitEnergy = step->GetPreStepPoint()->GetKineticEnergy();
	if(outputDebug){
		debugData.nEnterTime = step->GetTrack()->GetGlobalTime();
		debugData.nEnterPosX = step->GetPostStepPoint()->GetPosition().getX();
		debugData.nEnterPosY = step->GetPostStepPoint()->GetPosition().getY();
		debugData.nEnterPosZ = step->GetPostStepPoint()->GetPosition().getZ();
		debugData.timeInMaterial = 0;
		debugData.nExitPosX = 0;
		debugData.nExitPosY = 0;
		debugData.nExitPosZ = 0;
		primaryTracks.clear();
		primaryTracks.push_back(step);
		prevDirection = primaryTracks.back().dir;
		prevPosition = primaryTracks.back().pos;
	}
	if(verbose){ 
		std::cout << "IN: "; primaryTracks.back().print();
	}
}

void nDetRunAction::scatterNeutron(const G4Step *step){
	double dE = -1*(step->GetPostStepPoint()->GetKineticEnergy()-step->GetPreStepPoint()->GetKineticEnergy());
	G4Track *track = step->GetTrack();
	if(dE > 0){
		primaryTracks.push_back(step);
		if(outputDebug){
			primaryTracks.back().getAngle(prevDirection);
			primaryTracks.back().getPathLength(prevPosition);
			prevDirection = primaryTracks.back().dir;
			prevPosition = primaryTracks.back().pos;
		}
		if(verbose){
			std::cout << "SC: "; primaryTracks.back().print();
		}
		if(track->GetKineticEnergy() < KINETIC_ENERGY_THRESHOLD){ // Check particle kinematic energy.
			evtData.nAbsorbed = true;
			track->SetTrackStatus(fStopAndKill); // Kill the neutron.
			if(verbose) 
				std::cout << "OT: final kE=0 (absorbed)" << std::endl;
		}
	}
}

void nDetRunAction::finalizeNeutron(const G4Step *step){
	evtData.nAbsorbed = false;
	G4Track *track = step->GetTrack();
	if(outputDebug){
		debugData.timeInMaterial = track->GetGlobalTime() - debugData.nEnterTime;
		debugData.nExitPosX = step->GetPreStepPoint()->GetPosition().getX();
		debugData.nExitPosY = step->GetPreStepPoint()->GetPosition().getY();
		debugData.nExitPosZ = step->GetPreStepPoint()->GetPosition().getZ();
	}
	if(verbose) 
		std::cout << "OT: final kE=" << track->GetKineticEnergy() << std::endl;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
