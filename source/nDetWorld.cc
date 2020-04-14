
#include "nDetWorld.hh"
#include "nDetWorldObject.hh"
#include "nDetWorldMessenger.hh"
#include "nDetMaterials.hh"

#include "gdmlSolid.hh"
#include "optionHandler.hh" // split_str
#include "termColors.hh"

#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4VisAttributes.hh"
#include "G4SystemOfUnits.hh"
#include "G4SubtractionSolid.hh"

//CERN
#include "Tape.hh"
#include "IS530_Chamber.hh"
#include "IS530_Plastic.hh"
#include "Polyhedron.hh"
#include "CloverSingleDetector.hh"
#include "CloverQuadDetector.hh"
#include "CloverSingleBuchDetector.hh"
#include "CloverQuadBuchDetector.hh"
#include "global.hh"

#include "CERNFrame.hh"
#include "CERNFloor.hh"
#include "RIKENFloor.hh"
#include "CERNTapeBox.hh"
#include "CERNSupport.hh"
#include "RIKENSupport.hh"

#define DEFAULT_FLOOR_MATERIAL "G4_CONCRETE"

nDetWorld::nDetWorld() : solidV(NULL), logV(NULL), physV(NULL), fillMaterial("air"), floorMaterial(), floorThickness(0), floorSurfaceY(0)
{
	// Set the default size of the experimental hall
	hallSize = G4ThreeVector(10 * m, 10 * m, 10 * m);

	messenger = new nDetWorldMessenger(this);
}

bool nDetWorld::setWorldFloor(const G4String &input)
{
	// Expects a space-delimited string of the form:
	//  "centerY(cm) thickness(cm) [material=G4_CONCRETE]"
	std::vector<std::string> args;
	unsigned int Nargs = split_str(input, args);
	if (Nargs < 2)
	{
		std::cout << " nDetConstruction: Invalid number of arguments given to ::SetWorldFloor(). Expected 2, received " << Nargs << ".\n";
		std::cout << " nDetConstruction:  SYNTAX: <centerY> <thickness> [material=G4_CONCRETE]\n";
		return false;
	}
	floorSurfaceY = strtod(args.at(0).c_str(), NULL) * cm;
	floorThickness = strtod(args.at(1).c_str(), NULL) * cm;
	if (Nargs < 3) // Defaults to concrete
		floorMaterial = DEFAULT_FLOOR_MATERIAL;
	else
		floorMaterial = args.at(2);
	return true;
}

void nDetWorld::buildExpHall(nDetMaterials *materials)
{
	solidV = new G4Box("solidV", hallSize.getX() / 2, hallSize.getY() / 2, hallSize.getZ() / 2);

	G4Material *expHallFill = materials->getMaterial(fillMaterial);
	if (!expHallFill)
	{ // Use the default material, if
		std::cout << Display::WarningStr("nDetWorld") << "Failed to find user-specified world material (" << fillMaterial << ")!" << Display::ResetStr() << std::endl;
		std::cout << Display::WarningStr("nDetWorld") << " Defaulting to filling world volume with air" << Display::ResetStr() << std::endl;
		expHallFill = materials->fAir;
	}

	logV = new G4LogicalVolume(solidV, expHallFill, "expHallLogV", 0, 0, 0);
	logV->SetVisAttributes(G4VisAttributes::Invisible);

	// Add a floor to the experimental hall (disabled by default)
	if (!floorMaterial.empty() && floorThickness > 0)
	{
		G4Material *mat = materials->getMaterial(floorMaterial);
		if (mat)
		{
			G4Box *floorBox = new G4Box("floor", hallSize.getX() / 2, floorThickness / 2, hallSize.getZ() / 2);
			G4LogicalVolume *floor_logV;
			if (floorPitSize.getX() > 0 && floorPitSize.getX() > 0 && floorPitSize.getX() > 0)
			{ // Dig a pit
				G4double pitCenterOffsetY = 0.5 * (floorThickness - floorPitSize.getY());
				G4Box *pitBox = new G4Box("pitBox", floorPitSize.getX() / 2, floorPitSize.getY() / 2, floorPitSize.getZ() / 2);
				G4SubtractionSolid *floorWithPit = new G4SubtractionSolid("floorWithPit", floorBox, pitBox, NULL, G4ThreeVector(0, pitCenterOffsetY, 0));
				floor_logV = new G4LogicalVolume(floorWithPit, mat, "floor_logV");
			}
			else
			{
				floor_logV = new G4LogicalVolume(floorBox, mat, "floor_logV");
			}
			floor_logV->SetVisAttributes(materials->visShadow);
			logV->SetVisAttributes(materials->visAssembly);
			new G4PVPlacement(NULL, G4ThreeVector(0, -(floorSurfaceY + floorThickness / 2), 0), floor_logV, "floorBox_physV", logV, 0, 0, false);
		}
		else
		{
			std::cout << Display::WarningStr("nDetWorld") << "Failed to find user-specified floor material (" << floorMaterial << ")!" << Display::ResetStr() << std::endl;
			std::cout << Display::WarningStr("nDetWorld") << " Disabling the use of a floor" << Display::ResetStr() << std::endl;
			floorMaterial = "";
			floorThickness = 0;
		}
	}

	// Add additional objects
	for (auto obj : objects)
	{
		obj->placeObject(logV, materials);
	}

	if (!expName.empty() && expName != "isolde" && expName != "RIKEN" && expName != "ORNL2016" && expName != "Argonne")
		cout << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n"
			 << "<<<<<<<<<<<<<<<<<<<<<<<<< Unrecognizable expriment name. Please check for appropriate naming schemes. >>>>>>>>>>>>>>>>>>>>>>>>>\n"
			 << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n";

	//if(expName=="RIKEN") BuildCERNStructures();

	if (expName == "RIKEN")
		BuildRIKENStructures();

	// Place the experimental hall into the world
	physV = new G4PVPlacement(0, G4ThreeVector(0, 0, 0), logV, "expHallPhysV", 0, false, 0);

	//Placing BRIKEN Detector
/*
	G4VSolid *BRIKEN_BLOCK = new G4Box("BRIKEN_BLOCK", 450 * mm, 450 * mm, 375 * mm);
	G4VSolid *BRIKEN_HOLE = new G4Box("BRIKEN_HOLE", 70 * mm, 70 * mm, 375 * mm);
	G4VSolid *BRIKEN = new G4SubtractionSolid("BRIKEN_BLOCK-BRIKEN_HOLE", BRIKEN_BLOCK, BRIKEN_HOLE, 0, G4ThreeVector(0, 0, 0));
	G4LogicalVolume *BRIKEN_Log = new G4LogicalVolume(BRIKEN, materials->getMaterial("HDPE"), "BRIKEN", 0, 0, 0);
	G4VPhysicalVolume *BRIKEN_phys = new G4PVPlacement(0, G4ThreeVector(0, 0, 1.25 * m), BRIKEN_Log, "BRIKEN_phys", logV, false, 0);
*/
	// Placing HDPE floor in the experiemt.
	/*
	G4VSolid *HDPE_Floor = new G4Box("HDPE_Floor", 2. * m, 2.54 * cm, 2. * m);
	G4LogicalVolume *Floor_Log = new G4LogicalVolume(HDPE_Floor, materials->getMaterial("HDPE"), "Floor_Log", 0, 0, 0);
	G4VPhysicalVolume *Floor_phys = new G4PVPlacement(0, G4ThreeVector(0, -210 * cm, 0), Floor_Log, "Floor_phys", logV, false, 0);
*/
	//Placing PSPMT
	/*
	G4RotationMatrix *PSPMTRot = new G4RotationMatrix();
	PSPMTRot->rotateZ(45 * deg);
	G4VSolid *PSPMT_Outer = new G4Box("PSPMT_Outer", 25 * mm, 25 * mm, 20 * mm);
	G4VSolid *PSPMT_Inner = new G4Box("PSPMT_Inner", 23 * mm, 23 * mm, 17 * mm);
	G4VSolid *PSPMT = new G4SubtractionSolid("PSPMT_Outer-PSPMT_Inner", PSPMT_Outer, PSPMT_Inner, 0, G4ThreeVector(0, 0, 0));
	G4LogicalVolume *PSPMT_Log = new G4LogicalVolume(PSPMT, materials->getMaterial("G4_Al"), "PSPMT", 0, 0, 0);
	G4VPhysicalVolume *PSPMT_phys = new G4PVPlacement(PSPMTRot, G4ThreeVector(0, 0, -78. * mm), PSPMT_Log, "PSPMT_phys", logV, false, 0);
*/
	if (expName == "RIKEN")
		BuildRIKENElements();

	return;
}
/*
void nDetWorld::BuildCERNStructures(){
  
   CERNFloor* cernFloor = new CERNFloor();
   G4RotationMatrix* floorRot = new G4RotationMatrix();
   floorRot->rotateZ(90*deg);
   G4double floorXPos = -126.5*cm;
   G4ThreeVector floorPosition = G4ThreeVector(0, -210*cm, 0);
   cernFloor->Place(floorRot, floorPosition, "cernFloor", logV); 


   CERNSupport* cernSupport = new CERNSupport();
   G4RotationMatrix* rotSupport = new G4RotationMatrix(); 
   G4ThreeVector supportPos(0.0,4*cm, 7*cm);  
   cernSupport->Place(rotSupport, supportPos, "cernSupport", logV);       

   return;
}
*/
void nDetWorld::BuildRIKENStructures()
{

	RIKENFloor *rikenFloor = new RIKENFloor();
	G4RotationMatrix *floorRot = new G4RotationMatrix();
	floorRot->rotateZ(90 * deg);
	G4double floorXPos = -126.5 * cm;
	G4ThreeVector floorPosition = G4ThreeVector(0, -210 * cm, 0);
	rikenFloor->Place(floorRot, floorPosition, "rikenFloor", logV);

	RIKENSupport *rikenSupport = new RIKENSupport();
	G4RotationMatrix *rotSupport = new G4RotationMatrix();
	G4ThreeVector supportPos(0.0, 4 * cm, 7 * cm);
	rikenSupport->Place(rotSupport, supportPos, "rikenSupport", logV);

	return;
}

void nDetWorld::BuildRIKENElements()
{

	vector<CloverQuadDetector *> clquad_array;
	vector<CloverQuadBuchDetector *> clquadbuch_array;

	///setup copied from Ola's config file
	/* 1	81		 45		-35.26		15		KU-Blue-103
       2	75		-45		-35.26		-15		Buch-Red-99
       1	82		-45		 35.26		15		Ku-Yellow-102
       2	72		 45		 35.26	 	-15		Buch-Green-101
       #2	60		180		0		0		Buch-Extra
       8	0		0		0		0	        Tape
       #10	0		0		0		0		OSIRIS
       #9	0		0		0		0		TPiece-Chamber
       12	0		0		0		0		IS530-Chamber
       13	0		0		0		0		IS530-Plastic
       #14	49		180		0		0		VetoPlastic */

	G4int gType[8] = {1, 1};
	G4double gDistance[8] = {50, 50};
	G4double gTheta[8] = {
		90,
		90,
	};
	G4double gPhi[8] = {135, -135};
	G4double gSpin[8] = {0, 0};
	G4int gLines = 2;

	for (int l = 0; l < gLines; l++)
	{

		if (1 == gType[l])
		{ // Clover KU
			clquad_array.push_back(new CloverQuadDetector(physV, (G4double)gDistance[l] * mm, (G4double)(gTheta[l] * deg), (G4double)(gPhi[l] * deg), (G4double)(gSpin[l] * deg), clquad_array.size()));
		}
		if (2 == gType[l])
		{ // Clover Buch
			clquadbuch_array.push_back(new CloverQuadBuchDetector(physV, (G4double)gDistance[l] * mm, (G4double)(gTheta[l] * deg), (G4double)(gPhi[l] * deg), (G4double)(gSpin[l] * deg), clquadbuch_array.size()));
		}
	}

	//Construction

	// 1. Clover KU Leuven
	for (int clq = 0; clq < clquad_array.size(); clq++)
	{
		clquad_array.at(clq)->Construct();
	}
	// 2. Clover Bucharest
	for (int clq = 0; clq < clquadbuch_array.size(); clq++)
	{
		clquadbuch_array.at(clq)->Construct();
	}

	cout << "CERN setup - DONE" << endl;
}

void nDetWorld::BuildCERNElements()
{
	vector<CloverQuadDetector *> clquad_array;
	vector<CloverQuadBuchDetector *> clquadbuch_array;
	vector<Tape *> tape_array;
	vector<Polyhedron *> poly_array;
	//vector<Cubic_Chamber*>			cubic_chamber_array;
	vector<IS530_Chamber *> IS530_chamber_array;
	vector<IS530_Plastic *> IS530_plastic_array;

	///setup copied from Ola's config file
	/* 1	81		 45		-35.26		15		KU-Blue-103
       2	75		-45		-35.26		-15		Buch-Red-99
       1	82		-45		 35.26		15		Ku-Yellow-102
       2	72		 45		 35.26	 	-15		Buch-Green-101
       #2	60		180		0		0		Buch-Extra
       8	0		0		0		0	        Tape
       #10	0		0		0		0		OSIRIS
       #9	0		0		0		0		TPiece-Chamber
       12	0		0		0		0		IS530-Chamber
       13	0		0		0		0		IS530-Plastic
       #14	49		180		0		0		VetoPlastic */

	G4int gType[8] = {1, 2, 1, 2, 8, 12, 13, 10};
	G4double gDistance[8] = {81, 75, 82, 72, 0, 0, 0, 0};
	G4double gTheta[8] = {45, -45, -45, 45, 0, 0, 0, 0};
	G4double gPhi[8] = {-35.26, -35.26, 35.26, 35.26, 0, 0, 0, 0};
	G4double gSpin[8] = {15, -15, 15, -15, 0, 0, 0, 0};
	G4int gLines = 8;

	for (int l = 0; l < gLines - 1; l++)
	{

		if (1 == gType[l])
		{ // Clover KU
			clquad_array.push_back(new CloverQuadDetector(physV, (G4double)gDistance[l] * mm, (G4double)(gTheta[l] * deg), (G4double)(gPhi[l] * deg), (G4double)(gSpin[l] * deg), clquad_array.size()));
		}
		if (2 == gType[l])
		{ // Clover Buch
			clquadbuch_array.push_back(new CloverQuadBuchDetector(physV, (G4double)gDistance[l] * mm, (G4double)(gTheta[l] * deg), (G4double)(gPhi[l] * deg), (G4double)(gSpin[l] * deg), clquadbuch_array.size()));
		}

		if (8 == gType[l])
		{ // Tape
			tape_array.push_back(new Tape(physV));
			if (1 < tape_array.size())
			{
				cout << "\nYou can only contruct the tape one time!!!\n";
				exit(0);
			}
		}
		if (10 == gType[l])
		{ // Polyhedron
			poly_array.push_back(new Polyhedron(physV, (G4double)gDistance[l] * mm, (G4double)(gTheta[l] * deg), (G4double)(gPhi[l] * deg), (G4double)(gSpin[l] * deg)));
			if (1 < poly_array.size())
			{
				cout << "\nYou can only contruct the Polyhedron frame one time!!!\n";
				exit(0);
			}
		}
		if (12 == gType[l])
		{ // IS530 Chamber
			IS530_chamber_array.push_back(new IS530_Chamber(physV, (G4double)gDistance[l] * mm, (G4double)(gTheta[l] * deg), (G4double)(gPhi[l] * deg), (G4double)(gSpin[l] * deg)));
			if (1 < IS530_chamber_array.size())
			{
				cout << "\nYou can only contruct the IS530 chamber frame one time!!!\n";
				exit(0);
			}
		}
		if (13 == gType[l])
		{ // IS530 Plastic - non-sensitive detector
			IS530_plastic_array.push_back(new IS530_Plastic(physV, (G4double)gDistance[l] * mm, (G4double)(gTheta[l] * deg), (G4double)(gPhi[l] * deg), (G4double)(gSpin[l] * deg)));
			if (1 < IS530_plastic_array.size())
			{
				cout << "\nYou can only contruct the IS530 Plastic one time!!!\n";
				exit(0);
			}
		}
	}

	//Construction

	// 1. Clover KU Leuven
	for (int clq = 0; clq < clquad_array.size(); clq++)
	{
		clquad_array.at(clq)->Construct();
	}
	// 2. Clover Bucharest
	for (int clq = 0; clq < clquadbuch_array.size(); clq++)
	{
		clquadbuch_array.at(clq)->Construct();
	}

	// 8. Tape
	for (int t = 0; t < tape_array.size(); t++)
	{
		tape_array.at(t)->Construct();
	}

	// 10. OSIRIS chamber
	for (int pl = 0; pl < poly_array.size(); pl++)
	{
		poly_array.at(pl)->Construct();
	}
	/*
  // 11. Cubic chamber
  for (int cc=0; cc<cubic_chamber_array.size(); cc++){
    cubic_chamber_array.at(cc)->Construct();
  }*/
	// 12. IS530 chamber
	for (int is = 0; is < IS530_chamber_array.size(); is++)
	{
		IS530_chamber_array.at(is)->Construct();
	}
	// 13. IS530 plastic - non sensitive detector
	for (int is = 0; is < IS530_plastic_array.size(); is++)
	{
		IS530_plastic_array.at(is)->Construct();
	}

	cout << "CERN setup - DONE" << endl;
}

nDetWorldPrimitive *nDetWorld::addNewPrimitive(const G4String &str)
{
	nDetWorldPrimitive *obj = new nDetWorldPrimitive(str);
	obj->decodeString();
	if (!obj->decodeString())
	{
		std::cout << " nDetWorld: Invalid number of arguments given to ::addNewPrimitive(). Expected at least " << obj->getNumRequiredArgs() << " but received " << obj->getNumSuppliedArgs() << ".\n";
		std::cout << " nDetWorld:  SYNTAX: " << obj->syntaxStr() << std::endl;
		delete obj;
		return NULL;
	}
	objects.push_back(obj);
	return obj;
}

void nDetWorld::listAllPrimitives()
{
	nDetWorldPrimitive dummy("");
	dummy.listAllPrimitives();
}

void nDetWorld::printDefinedObjects()
{
	if (!objects.empty())
	{
		for (auto obj : objects)
		{
			std::cout << "***********************************************************\n";
			obj->print();
		}
		std::cout << "***********************************************************\n";
	}
	else
		std::cout << " nDetWorldPrimitive: No 3d primitives are currently defined\n";
}

void nDetWorld::reset()
{
	for (auto obj : objects)
		delete obj;
	objects.clear();
}

gdmlObject *nDetWorld::loadGDML(const G4String &input)
{
	gdmlObject *obj = new gdmlObject(input);
	obj->decodeString();
	if (!obj->decodeString())
	{
		std::cout << " nDetWorld: Invalid number of arguments given to ::loadGDML(). Expected " << obj->getNumRequiredArgs() << " but received " << obj->getNumSuppliedArgs() << ".\n";
		std::cout << " nDetWorld:  SYNTAX: " << obj->syntaxStr() << std::endl;
		delete obj;
		return NULL;
	}
	objects.push_back(obj);
	return obj;
}
