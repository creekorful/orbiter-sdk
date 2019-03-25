// ==============================================================
//                ORBITER MODULE: DeltaGlider
//                  Part of the ORBITER SDK
//          Copyright (C) 2001-2016 Martin Schweiger
//                   All rights reserved
//
// CoolingSubsys.cpp
// Subsystem for coolant loop controls
// ==============================================================

#define STRICT 1

#include "CoolingSubsys.h"
#include "meshres.h"
#include "meshres_p1.h"
#include "meshres_vc.h"
#include "dg_vc_anim.h"

// ==============================================================
// Cooling subsystem
// ==============================================================

CoolingSubsystem::CoolingSubsystem (DeltaGlider *v)
: DGSubsystem (v)
{
	// create component instances
	AddSubsystem (radiatorctrl = new RadiatorControl (this));
}

// --------------------------------------------------------------

void CoolingSubsystem::OpenRadiator ()
{
	radiatorctrl->OpenRadiator();
}

// --------------------------------------------------------------

void CoolingSubsystem::CloseRadiator ()
{
	radiatorctrl->CloseRadiator();
}

// --------------------------------------------------------------

const AnimState2 &CoolingSubsystem::RadiatorState() const
{
	return radiatorctrl->State();
}

// ==============================================================
// Radiator control
// ==============================================================

RadiatorControl::RadiatorControl (CoolingSubsystem *_subsys)
: DGSubsystem(_subsys)
{
	radiator_state.SetOperatingSpeed (RADIATOR_OPERATING_SPEED);
	radiator_extend = false;

	ELID_SWITCH = AddElement (sw = new RadiatorSwitch (this));

	// Radiator animation
	static UINT RaddoorGrp[2] = {GRP_Raddoor1,GRP_Raddoor2};
	static MGROUP_ROTATE Raddoor (0, RaddoorGrp, 2,
		_V(0,1.481,-3.986), _V(1,0,0), (float)(170*RAD));
	static UINT RadiatorGrp[3] = {GRP_Radiator1,GRP_Radiator2,GRP_Radiator3};
	static MGROUP_TRANSLATE Radiator (0, RadiatorGrp, 3,
		_V(0,0.584,-0.157));
	static UINT LRadiatorGrp[1] = {GRP_Radiator1};
	static MGROUP_ROTATE LRadiator (0, LRadiatorGrp, 1,
		_V(-0.88,1.94,-4.211), _V(0,0.260,0.966), (float)(135*RAD));
	static UINT RRadiatorGrp[1] = {GRP_Radiator2};
	static MGROUP_ROTATE RRadiator (0, RRadiatorGrp, 1,
		_V(0.93,1.91,-4.211), _V(0,0.260,0.966), (float)(-135*RAD));
	anim_radiator = DG()->CreateAnimation (0);
	DG()->AddAnimationComponent (anim_radiator, 0, 0.33, &Raddoor);
	DG()->AddAnimationComponent (anim_radiator, 0.25, 0.5, &Radiator);
	DG()->AddAnimationComponent (anim_radiator, 0.5, 0.75, &RRadiator);
	DG()->AddAnimationComponent (anim_radiator, 0.75, 1, &LRadiator);

}

// --------------------------------------------------------------

void RadiatorControl::OpenRadiator ()
{
	void UpdateCtrlDialog (DeltaGlider *dg, HWND hWnd = 0);

	radiator_extend = true;
	radiator_state.Open();
	if (sw->GetState() != DGSwitch1::UP) {
		sw->SetState(DGSwitch1::UP);
		DG()->TriggerRedrawArea(1, 0, ELID_SWITCH);
	}
	DG()->UpdateStatusIndicators();
	UpdateCtrlDialog (DG());
	DG()->RecordEvent ("RADIATOR", "OPEN");
}

// --------------------------------------------------------------

void RadiatorControl::CloseRadiator ()
{
	void UpdateCtrlDialog (DeltaGlider *dg, HWND hWnd = 0);

	radiator_extend = false;
	radiator_state.Close();
	if (sw->GetState() != DGSwitch1::DOWN) {
		sw->SetState(DGSwitch1::DOWN);
		DG()->TriggerRedrawArea(1, 0, ELID_SWITCH);
	}
	DG()->UpdateStatusIndicators();
	UpdateCtrlDialog (DG());
	DG()->RecordEvent ("RADIATOR", "CLOSE");
}

// --------------------------------------------------------------

void RadiatorControl::Revert (void)
{
	if (radiator_state.IsOpen() || radiator_state.IsOpening())
		CloseRadiator();
	else
		OpenRadiator();
}

// --------------------------------------------------------------

void RadiatorControl::clbkPostCreation ()
{
	DG()->SetAnimation (anim_radiator, radiator_state.State());
	radiator_extend = (radiator_state.IsOpen() || radiator_state.IsOpening());
}

// --------------------------------------------------------------

void RadiatorControl::clbkSaveState (FILEHANDLE scn)
{
	radiator_state.SaveState (scn, "RADIATOR");
}

// --------------------------------------------------------------

bool RadiatorControl::clbkParseScenarioLine (const char *line)
{
	return radiator_state.ParseScenarioLine (line, "RADIATOR");
}

// --------------------------------------------------------------

void RadiatorControl::clbkPostStep (double simt, double simdt, double mjd)
{
	// animate radiator
	if (radiator_state.Process (simdt)) {
		DG()->SetAnimation (anim_radiator, radiator_state.State());
		DG()->UpdateStatusIndicators();
	}
}

// --------------------------------------------------------------

bool RadiatorControl::clbkLoadPanel2D (int panelid, PANELHANDLE hPanel, DWORD viewW, DWORD viewH)
{
	if (panelid != 1) return false;

	// Radiator switch
	SURFHANDLE panel2dtex = oapiGetTextureHandle(DG()->panelmesh1,1);
	DG()->RegisterPanelArea (hPanel, ELID_SWITCH, _R(846,192,872,244), PANEL_REDRAW_MOUSE, PANEL_MOUSE_LBDOWN|PANEL_MOUSE_LBUP, panel2dtex, sw);
	sw->DefineAnimation2D (DG()->panelmesh1, GRP_INSTRUMENTS_ABOVE_P1, 44);

	return true;
}


// --------------------------------------------------------------

bool RadiatorControl::clbkLoadVC (int vcid)
{
	if (vcid != 0) return false;

	// Radiator switch
	oapiVCRegisterArea (ELID_SWITCH, PANEL_REDRAW_MOUSE, PANEL_MOUSE_LBDOWN | PANEL_MOUSE_LBUP);
	oapiVCSetAreaClickmode_Quadrilateral (ELID_SWITCH, VC_RADIATOR_SWITCH_mousearea[0], VC_RADIATOR_SWITCH_mousearea[1], VC_RADIATOR_SWITCH_mousearea[2], VC_RADIATOR_SWITCH_mousearea[3]);
	sw->DefineAnimationVC (VC_RADIATOR_SWITCH_ref, VC_RADIATOR_SWITCH_axis, GRP_SWITCH1_VC, VC_RADIATOR_SWITCH_vofs);

	return true;
}

// --------------------------------------------------------------

void RadiatorControl::clbkResetVC (int vcid, DEVMESHHANDLE hMesh)
{
	//if (radiator_extend) OpenRadiator();
	//else                 CloseRadiator();
}

// --------------------------------------------------------------

bool RadiatorControl::clbkPlaybackEvent (double simt, double event_t, const char *event_type, const char *event)
{
	if (!_stricmp (event_type, "RADIATOR")) {
		if (!_stricmp (event, "CLOSE")) CloseRadiator();
		else                            OpenRadiator();
		return true;
	}
	return false;
}

// --------------------------------------------------------------

int RadiatorControl::clbkConsumeBufferedKey (DWORD key, bool down, char *kstate)
{
	if (KEYMOD_ALT(kstate) || KEYMOD_CONTROL(kstate) || KEYMOD_SHIFT(kstate))
		return 0;

	if (key == OAPI_KEY_D) {
		Revert();
		return 1;
	}
	return 0;
}

// ==============================================================

RadiatorSwitch::RadiatorSwitch (RadiatorControl *comp)
: DGSwitch1(comp->DG(), DGSwitch1::TWOSTATE), component(comp)
{
}

// --------------------------------------------------------------

void RadiatorSwitch::Reset2D (MESHHANDLE hMesh)
{
	SetState ((DGSwitch1::State)component->GetRadiator() ? DGSwitch1::UP : DGSwitch1::DOWN);
	DGSwitch1::Reset2D (hMesh);
}

// --------------------------------------------------------------

void RadiatorSwitch::ResetVC (DEVMESHHANDLE hMesh)
{
	SetState (component->GetRadiator() ? UP:DOWN);
	DGSwitch1::ResetVC (hMesh);
}

// --------------------------------------------------------------

bool RadiatorSwitch::ProcessMouse2D (int event, int mx, int my)
{
	if (DGSwitch1::ProcessMouse2D (event, mx, my)) {
		if (GetState() == DGSwitch1::UP) component->OpenRadiator();
		else                             component->CloseRadiator();
		return true;
	}
	return false;
}

// --------------------------------------------------------------

bool RadiatorSwitch::ProcessMouseVC (int event, VECTOR3 &p)
{
	if (DGSwitch1::ProcessMouseVC (event, p)) {
		if (GetState() == DGSwitch1::UP) component->OpenRadiator();
		else                             component->CloseRadiator();
		return true;
	}
	return false;
}
