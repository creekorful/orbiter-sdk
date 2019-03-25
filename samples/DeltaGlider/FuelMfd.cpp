// ==============================================================
//                ORBITER MODULE: DeltaGlider
//                  Part of the ORBITER SDK
//          Copyright (C) 2001-2008 Martin Schweiger
//                   All rights reserved
//
// FuelMfd.cpp
// Fuel status display
// ==============================================================

#define STRICT 1
#include "FuelMfd.h"
#include "DeltaGlider.h"
#include "ScramSubsys.h"
#include "meshres_p0.h"
#include "meshres_vc.h"

// ==============================================================

// constants for texture coordinates
static const float texw = (float)INSTR3D_TEXW;
static const float texh = (float)INSTR3D_TEXH;
static const float fd_y0 = 395.5f;
static const float fuelh =  86.0f;
static const float fuelw =  28.0f;
static const float fuely = fd_y0+29.5f+fuelh;

// ==============================================================

FuelMFD::FuelMFD (VESSEL3 *v): PanelElement (v)
{
	int i, j;
	isScram = false;
	doSetupVC = doSetup2D = true;
	Mmain = Mrcs = Mscram = 0.0;
	for (i = 0; i < 9; i++)
		for (j = 0; j < 5; j++) sout[i][j] = 0;
	memset (&vc_grp, 0, sizeof(GROUPREQUESTSPEC));
}

// ==============================================================

FuelMFD::~FuelMFD ()
{
	if (vc_grp.Vtx) delete []vc_grp.Vtx;
}

// ==============================================================

void FuelMFD::Reset_noscram (NTVERTEX *Vtx)
{
	int i;
	const float panelw = 267.0f, panelh = 167.0f, shiftx = 46.0f, titleh = 14.0f;
	float dx = shiftx/panelw*(Vtx[6].x-Vtx[0].x);
	float dy = titleh/panelh*(Vtx[1].y-Vtx[0].y);
	float dz = titleh/panelh*(Vtx[1].z-Vtx[0].z);
	Vtx[16].x = Vtx[18].x = Vtx[0].x;
	Vtx[17].x = Vtx[19].x = Vtx[6].x;
	Vtx[16].y = Vtx[17].y = Vtx[1].y - dy;
	Vtx[18].y = Vtx[19].y = Vtx[1].y;
	Vtx[16].z = Vtx[17].z = Vtx[1].z - dz;
	Vtx[18].z = Vtx[19].z = Vtx[1].z;
	Vtx[16].tu = Vtx[18].tu = Vtx[0].tu;
	Vtx[17].tu = Vtx[19].tu = Vtx[6].tu;
	Vtx[16].tv = Vtx[17].tv = Vtx[1].tv + titleh/texh;
	Vtx[18].tv = Vtx[19].tv = Vtx[1].tv;
	Vtx[2].x = Vtx[3].x = Vtx[0].x + dx;
	Vtx[4].x = Vtx[5].x = Vtx[6].x - dx;
	Vtx[6].tu = Vtx[7].tu = Vtx[4].tu;
	for (i = 1; i <= 7; i+=2) {
		Vtx[i].y -= dy;
		Vtx[i].z -= dz;
		Vtx[i].tv += titleh/texh;
	}
	for (i = 8; i < 16; i++)
		Vtx[i].x += dx;
}

// ==============================================================

void FuelMFD::Reset2D (MESHHANDLE hMesh)
{
	grp = oapiMeshGroup (hMesh, GRP_FUEL_DISP_P0);
	vtxofs = 0;

	DeltaGlider *dg = (DeltaGlider*)vessel;
	isScram = dg->ScramVersion();
	Mmain = dg->GetPropellantMass (dg->ph_main);
	Mrcs  = dg->GetPropellantMass (dg->ph_rcs);
	if (isScram) Mscram = dg->SubsysScram()->GetPropellantMass ();
	if (doSetup2D) {
		NTVERTEX *Vtx = grp->Vtx+vtxofs;
		crd_2D[0] = Vtx[8].y;   crd_2D[1] = Vtx[10].y;
		crd_2D[2] = Vtx[8].z;   crd_2D[3] = Vtx[10].z;
		if (!isScram) Reset_noscram (Vtx);
		doSetup2D = false;
	}
}

// ==============================================================

void FuelMFD::ResetVC (DEVMESHHANDLE hMesh)
{
	// NEED TO DO VERTEX TRANSFORMATIONS HERE!

	DeltaGlider *dg = (DeltaGlider*)vessel;
	isScram = dg->ScramVersion();
	Mmain = dg->GetPropellantMass (dg->ph_main);
	Mrcs  = dg->GetPropellantMass (dg->ph_rcs);
	if (isScram) Mscram = dg->SubsysScram()->GetPropellantMass ();

	vc_grp.nVtx = 20;
	if (!vc_grp.Vtx) vc_grp.Vtx = new NTVERTEX[vc_grp.nVtx];
	if (oapiGetMeshGroup (hMesh, GRP_PROPELLANT_STATUS_VC, &vc_grp) != 0) { // problems
		delete []vc_grp.Vtx;
		vc_grp.Vtx = 0;
	} else if (doSetupVC) {
		NTVERTEX *Vtx = vc_grp.Vtx;
		crd_VC[0] = Vtx[8].y;   crd_VC[1] = Vtx[10].y;
		crd_VC[2] = Vtx[8].z;   crd_VC[3] = Vtx[10].z;
		if (!isScram) {
			Reset_noscram (Vtx);
			GROUPEDITSPEC ges = {GRPEDIT_VTXCRD|GRPEDIT_VTXTEX, 0, Vtx, vc_grp.nVtx, 0};
			oapiEditMeshGroup (hMesh, GRP_PROPELLANT_STATUS_VC, &ges);
		}
		doSetupVC = false;
	}
}

// ==============================================================

void FuelMFD::Redraw (NTVERTEX *Vtx, SURFHANDLE surf, float crd[4])
{
	DeltaGlider *dg = (DeltaGlider*)vessel;

	static const int xofs = INSTR3D_TEXW-424, yofs = 0;
	double m, m0, lvl, dv, isp;
	float y, z;
	int vofs;
	char cbuf[16];
	double T = oapiGetSimTime();
	double dT = T-Tsample;
	m0 = dg->GetMass();

	// main level
	m = dg->GetPropellantMass (dg->ph_main);
	lvl = m / max (1.0, dg->max_rocketfuel);
	isp = dg->GetThrusterIsp (dg->th_main[0]);
	dv = isp * log(m0/(m0-m));
	//y1 = (float)(fuely - lvl * fuelh);
	y = crd[0] + (float)lvl*(crd[1]-crd[0]);
	z = crd[2] + (float)lvl*(crd[3]-crd[2]);
	vofs = 8;
	Vtx[vofs+2].y = Vtx[vofs+3].y = y;
	Vtx[vofs+2].z = Vtx[vofs+3].z = z;
	sprintf (cbuf, "% 6d", (int)(m+0.5));
	BltString (cbuf+1, sout[0], 5, xofs+42, yofs+78, surf);
	sprintf (cbuf, "% 6d", (int)(dv+0.5));
	BltString (cbuf+1, sout[6], 5, xofs+42, yofs+106, surf);
	if (dT > 0.0) {
		sprintf (cbuf, "% 5.2f", (Mmain-m)/(T-Tsample));
		BltString (cbuf, sout[3], 5, xofs+42, yofs+156, surf);
		Mmain = m;
	}

	// rcs level
	m = dg->GetPropellantMass (dg->ph_rcs);
	lvl = m / RCS_FUEL_CAPACITY;
	isp = ISP;
	dv = isp * log(m0/(m0-m));
	//y1 = (float)(fuely - lvl * fuelh);
	y = crd[0] + (float)lvl*(crd[1]-crd[0]);
	z = crd[2] + (float)lvl*(crd[3]-crd[2]);
	vofs = 12;
	Vtx[vofs+2].y = Vtx[vofs+3].y = y;
	Vtx[vofs+2].z = Vtx[vofs+3].z = z;
	sprintf (cbuf, "% 6d", (int)(m+0.5));
	BltString (cbuf+1, sout[1], 5, xofs+134, yofs+78, surf);
	sprintf (cbuf, "% 6d", (int)(dv+0.5));
	BltString (cbuf+1, sout[7], 5, xofs+134, yofs+106, surf);
	if (dT > 0.0) {
		sprintf (cbuf, "% 5.2f", (Mrcs-m)/(T-Tsample));
		BltString (cbuf, sout[4], 5, xofs+134, yofs+156, surf);
		Mrcs = m;
	}

	if (isScram) {
		// scram level
		m = dg->SubsysScram()->GetPropellantMass ();
		lvl = m / max (1.0, dg->SubsysScram()->GetPropellantMaxMass());
		isp = dg->SubsysScram()->GetThrusterIsp (0);
		dv = isp * log(m0/(m0-m));
		//y1 = (float)(fuely - lvl * fuelh);
		y = crd[0] + (float)lvl*(crd[1]-crd[0]);
		z = crd[2] + (float)lvl*(crd[3]-crd[2]);
		vofs = 16;
		Vtx[vofs+2].y = Vtx[vofs+3].y = y;
		Vtx[vofs+2].z = Vtx[vofs+3].z = z;
		sprintf (cbuf, "% 6d", (int)(m+0.5));
		BltString (cbuf+1, sout[2], 5, xofs+226, yofs+78, surf);
		sprintf (cbuf, "% 6d", (int)(dv+0.5));
		BltString (cbuf+1, sout[8], 5, xofs+226, yofs+106, surf);
		if (dT > 0.0) {
			sprintf (cbuf, "% 5.2f", (Mscram-m)/(T-Tsample));
			BltString (cbuf, sout[5], 5, xofs+226, yofs+156, surf);
			Mscram = m;
		}
	}
	Tsample = T;
}

// ==============================================================

bool FuelMFD::Redraw2D (SURFHANDLE surf)
{
	Redraw (grp->Vtx+vtxofs, surf, crd_2D);
	return false;
}

// ==============================================================

bool FuelMFD::RedrawVC (DEVMESHHANDLE hMesh, SURFHANDLE surf)
{
	if (hMesh && surf) {
		Redraw (vc_grp.Vtx, surf, crd_VC);
		GROUPEDITSPEC ges = {GRPEDIT_VTXCRDY|GRPEDIT_VTXCRDZ, 0, vc_grp.Vtx, vc_grp.nVtx, 0};
		oapiEditMeshGroup (hMesh, GRP_PROPELLANT_STATUS_VC, &ges);
	}
	return false;
}

// ==============================================================

void FuelMFD::BltString (char *str, char *pstr, int maxlen, int x, int y, SURFHANDLE surf)
{
	int i, xsrc, xofs = INSTR3D_TEXW-293, ysrc = 1;
	char *c = str;
	for (i = 0; i < maxlen && *c; i++, c++) {
		if (*c != pstr[i]) {
			if (*c >= '0' && *c <= '9') {
				xsrc = xofs+(*c-'0')*8;
			} else switch(*c) {
				case '.': xsrc = xofs+80; break;
				default:  xsrc = xofs+88; break;
			}
			oapiBlt (surf, surf, x, y, xsrc, ysrc, 7, 9);
			pstr[i] = *c;
		}
		x += 7;
	}
}