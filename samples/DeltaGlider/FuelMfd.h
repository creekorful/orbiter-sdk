// ==============================================================
//                ORBITER MODULE: DeltaGlider
//                  Part of the ORBITER SDK
//          Copyright (C) 2001-2008 Martin Schweiger
//                   All rights reserved
//
// FuelMfd.h
// Fuel status display
// ==============================================================

#ifndef __FUELMFD_H
#define __FUELMFD_H

#include "..\Common\Vessel\Instrument.h"

class FuelMFD: public PanelElement {
public:
	FuelMFD (VESSEL3 *v);
	~FuelMFD ();
	void Reset2D (MESHHANDLE hMesh);
	void ResetVC (DEVMESHHANDLE hMesh);
	bool Redraw2D (SURFHANDLE surf);
	bool RedrawVC (DEVMESHHANDLE hMesh, SURFHANDLE surf);

private:
	void Reset_noscram (NTVERTEX *Vtx);
	void Redraw (NTVERTEX *Vtx, SURFHANDLE surf, float crd[4]);
	void BltString (char *str, char *pstr, int maxlen, int x, int y, SURFHANDLE surf);

	bool isScram;
	bool doSetupVC, doSetup2D;
	double Tsample;
	double Mmain, Mrcs, Mscram;
	float crd_2D[4], crd_VC[4];
	char sout[9][5];
	GROUPREQUESTSPEC vc_grp; ///< Buffered VC vertex data
};

#endif // !__FUELMFD_H