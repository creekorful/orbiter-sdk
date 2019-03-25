// ==============================================================
//                ORBITER MODULE: DeltaGlider
//                  Part of the ORBITER SDK
//          Copyright (C) 2001-2016 Martin Schweiger
//                   All rights reserved
//
// DGSubsys.h
// Base classes for DG subsystems and panel elements
// ==============================================================

#ifndef __DGSUBSYS_H
#define __DGSUBSYS_H

#include "DeltaGlider.h"
#include "..\Common\Vessel\Instrument.h"

// ==============================================================

class DGSubsystem: public Subsystem {
public:
	DGSubsystem (DeltaGlider *v): Subsystem (v) {}
	DGSubsystem (DGSubsystem *subsys): Subsystem (subsys) {}
	inline DeltaGlider *DG() { return (DeltaGlider*)Vessel(); }
	inline const DeltaGlider *DG() const { return (DeltaGlider*)Vessel(); }
};

// ==============================================================

class DGPanelElement: public PanelElement {
public:
	DGPanelElement (DeltaGlider *_dg): PanelElement(_dg), dg(_dg) {}

protected:
	DeltaGlider *dg;
};

#endif // !__DGSUBSYS_H