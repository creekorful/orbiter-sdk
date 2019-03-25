// ==============================================================
//                ORBITER MODULE: DeltaGlider
//                  Part of the ORBITER SDK
//          Copyright (C) 2001-2016 Martin Schweiger
//                   All rights reserved
//
// CoolingSubsys.h
// Subsystem for coolant loop controls
// ==============================================================

#ifndef __COOLINGSUBSYS_H
#define __COOLINGSUBSYS_H

#include "DGSubsys.h"
#include "DGSwitches.h"

// ==============================================================
// Cooling subsystem
// ==============================================================

class RadiatorControl;

class CoolingSubsystem: public DGSubsystem {
public:
	CoolingSubsystem (DeltaGlider *v);
	void OpenRadiator ();
	void CloseRadiator ();
	const AnimState2 &RadiatorState() const;

private:
	RadiatorControl *radiatorctrl;
};

// ==============================================================
// Radiator control
// ==============================================================

class RadiatorControl: public DGSubsystem {
	friend class RadiatorSwitch;

public:
	RadiatorControl (CoolingSubsystem *_subsys);
	void OpenRadiator();
	void CloseRadiator();
	void Revert ();
	inline const AnimState2 &State() const { return radiator_state; }
	inline bool GetRadiator () const { return radiator_extend; }
	void clbkPostCreation();
	void clbkSaveState (FILEHANDLE scn);
	bool clbkParseScenarioLine (const char *line);
	void clbkPostStep (double simt, double simdt, double mjd);
	bool clbkLoadPanel2D (int panelid, PANELHANDLE hPanel, DWORD viewW, DWORD viewH);
	bool clbkLoadVC (int vcid);
	void clbkResetVC (int vcid, DEVMESHHANDLE hMesh);
	bool clbkPlaybackEvent (double simt, double event_t, const char *event_type, const char *event);
	int clbkConsumeBufferedKey (DWORD key, bool down, char *kstate);

private:
	bool radiator_extend;
	AnimState2 radiator_state;
	RadiatorSwitch *sw;
	int ELID_SWITCH;
	UINT anim_radiator;         // handle for radiator animation
};

// ==============================================================

class RadiatorSwitch: public DGSwitch1 {
public:
	RadiatorSwitch (RadiatorControl *comp);
	void Reset2D (MESHHANDLE hMesh);
	void ResetVC (DEVMESHHANDLE hMesh);
	bool ProcessMouse2D (int event, int mx, int my);
	bool ProcessMouseVC (int event, VECTOR3 &p);

private:
	RadiatorControl *component;
};

#endif // !__COOLINGSUBSYS_H