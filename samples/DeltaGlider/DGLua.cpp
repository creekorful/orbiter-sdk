#include "DeltaGlider.h"
#include "MainRetroSubsys.h"
#include "GearSubsys.h"
#include "DockingSubsys.h"
#include "AerodynSubsys.h"
#include "PressureSubsys.h"
#include "CoolingSubsys.h"
#include <stdio.h>

extern "C" {
#include "lua\lua.h"
#include "lua\lualib.h"
#include "lua\lauxlib.h"
}

// ==========================================================================
// API function prototypes

DeltaGlider *lua_toDG (lua_State *L, int idx = 1);
int dgGear (lua_State *L);
int dgNosecone (lua_State *L);
int dgHatch (lua_State *L);
int dgRetro (lua_State *L);
int dgOLock (lua_State *L);
int dgILock (lua_State *L);
int dgRadiator (lua_State *L);
int dgABrake (lua_State *L);

// ==========================================================================
// API initialisation

int DeltaGlider::Lua_InitInterpreter (void *context)
{
	lua_State *L = (lua_State*)context;

	// load atmospheric autopilot
	luaL_dofile (L, "Script\\dg\\aap.lua");

	return 0;
}

int DeltaGlider::Lua_InitInstance (void *context)
{
	lua_State *L = (lua_State*)context;

	// check if interpreter has DG table loaded already
	luaL_getmetatable (L, "VESSEL.DG");

	if (lua_isnil (L, -1)) { // register new functions
		lua_pop (L, 1);
		static const struct luaL_reg dgLib[] = {
			{"Gear", dgGear},
			{"Nosecone", dgNosecone},
			{"Hatch", dgHatch},
			{"Retro", dgRetro},
			{"OLock", dgOLock},
			{"ILock", dgILock},
			{"Radiator", dgRadiator},
			{"ABrake", dgABrake},
			{NULL, NULL}
		};

		// create metatable for vessel userdata
		luaL_newmetatable (L, "DG.vtable");

		// create a table for the overloaded methods
		luaL_openlib (L, "DG.method", dgLib, 0);

		// create metatable for accessing inherited methods from VESSEL
		luaL_newmetatable (L, "DG.base");
		lua_pushstring (L, "__index");
		luaL_getmetatable (L, "VESSEL.vtable");
		lua_settable (L, -3);

		// set DG.base as metatable for DG.method
		lua_setmetatable (L, -2);

		// point vessel userdata to DG.method
		lua_pushstring (L, "__index");
		lua_pushvalue (L, -2); // push DG.method
		lua_settable (L, -4);

		// pop DG.method from the stack
		lua_pop (L, 1);
	}

	lua_setmetatable (L, -2);

	return 0;
}

// ==========================================================================
// DeltaGlider Lua API extensions

DeltaGlider *lua_toDG (lua_State *L, int idx)
{
	VESSEL **pv = (VESSEL**)lua_touserdata (L, idx);
	DeltaGlider *dg = (DeltaGlider*)*pv;
	return dg;
}

static int dgGear (lua_State *L)
{
	DeltaGlider *dg = lua_toDG (L, 1);
	int action = lua_tointeger (L, 2);
	if (action & 1) dg->SubsysGear()->LowerGear();
	else            dg->SubsysGear()->RaiseGear();
	return 0;
}

static int dgNosecone (lua_State *L)
{
	DeltaGlider *dg = lua_toDG (L, 1);
	int action = lua_tointeger (L, 2);
	if (dg && action >= 0 && action < 2) {
		if (action == 0) dg->SubsysDocking()->CloseNcone();
		else             dg->SubsysDocking()->OpenNcone();
	}
	return 0;
}

static int dgHatch (lua_State *L)
{
	DeltaGlider *dg = lua_toDG (L, 1);
	int action = lua_tointeger (L, 2);
	if (dg && action >= 0 && action < 2) {
		if (action == 0) dg->SubsysPressure()->CloseHatch();
		else             dg->SubsysPressure()->OpenHatch();
	}
	return 0;
}

static int dgRetro (lua_State *L)
{
	DeltaGlider *dg = lua_toDG (L, 1);
	int action = lua_tointeger (L, 2);
	if (dg && action >= 0 && action < 2) {
		if (action == 0) dg->SubsysMainRetro()->CloseRetroCover();
		else             dg->SubsysMainRetro()->OpenRetroCover();
	}
	return 0;
}

static int dgOLock (lua_State *L)
{
	DeltaGlider *dg = lua_toDG (L, 1);
	int action = lua_tointeger (L, 2);
	if (dg && action >= 0 && action < 2) {
		if (action == 0) dg->SubsysPressure()->CloseOuterAirlock();
		else             dg->SubsysPressure()->OpenOuterAirlock();
	}
	return 0;
}

static int dgILock (lua_State *L)
{
	DeltaGlider *dg = lua_toDG (L, 1);
	int action = lua_tointeger (L, 2);
	if (dg && action >= 0 && action < 2) {
		if (action == 0) dg->SubsysPressure()->CloseInnerAirlock();
		else             dg->SubsysPressure()->OpenInnerAirlock();
	}
	return 0;
}

static int dgRadiator (lua_State *L)
{
	DeltaGlider *dg = lua_toDG (L, 1);
	int action = lua_tointeger (L, 2);
	if (dg && action >= 0 && action < 2) {
		if (action == 0) dg->SubsysCooling()->CloseRadiator();
		else             dg->SubsysCooling()->OpenRadiator();
	}
	return 0;
}

static int dgABrake (lua_State *L)
{
	DeltaGlider *dg = lua_toDG (L, 1);
	int action = lua_tointeger (L, 2);
	if (dg && action >= 0 && action < 2) {
		if (action == 0) dg->SubsysAerodyn()->RetractAirbrake();
		else             dg->SubsysAerodyn()->ExtendAirbrake();
	}
	return 0;
}
