// ==============================================================
//                 ORBITER MODULE: ShuttleA
//                  Part of the ORBITER SDK
//          Copyright (C) 2001-2011 Martin Schweiger
//                   All rights reserved
//
// auxpodctrl.h
// User interface for auxiliary thruster pod control
// ==============================================================

#ifndef __AUXPODCTRL_H
#define __AUXPODCTRL_H

#include "..\Common\Vessel\Instrument.h"

// ==============================================================

class AuxPodCtrl: public PanelElement {
	friend class ShuttleA;

public:
	AuxPodCtrl (ShuttleA *shuttlea);
	void AddMeshData2D (MESHHANDLE hMesh, DWORD grpidx, DWORD grpidx_disp);
	void Reset2D();
	bool Redraw2D (SURFHANDLE surf);
	bool ProcessMouse2D (int event, int mx, int my);

private:
	ShuttleA *sh;
	UINT mode, ctrl, preset;
	double pod_angle_cmd[2];    // commanded pod angles
	double pod_angle_ind[2];    // indicated pod angles
	double pod_preset_ind[2];   // indicated pod presets
	bool preset_active[3];      // preset indicator light active?
	bool redraw_buttons;        // button redraw signal
	double toggle_t;
	MESHGROUP *ctrlgrp;
	DWORD ctrlofs, needleofs;
};

#endif // !__AUXPODCTRL_H
