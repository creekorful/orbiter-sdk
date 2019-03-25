#include "ShuttleA.h"
#include "adiball.h"

extern "C" {
#include "lua\lua.h"
#include "lua\lualib.h"
#include "lua\lauxlib.h"
}

// ==========================================================================
// API function prototypes

ShuttleA *lua_toShuttleA (lua_State *L, int idx = 1);
int lua_gear (lua_State *L);
int set_adilayout (lua_State *L);
int set_attrefmode (lua_State *L);
int set_attreftgtmode (lua_State *L);
int set_attrefoffset (lua_State *L);
int set_atttgtoffset (lua_State *L);
int set_attoffsetmode (lua_State *L);
int set_atttgtframemode (lua_State *L);

// ==========================================================================
// ShuttleA Lua instance initialisation

int ShuttleA::Lua_InitInstance (void *context)
{
	lua_State *L = (lua_State*)context;

	// check if interpreter has ShuttleA table loaded already
	luaL_getmetatable (L, "VESSEL.SHUTTLEA");

	if (lua_isnil (L, -1)) { // register new functions
		lua_pop (L, 1);
		static const struct luaL_reg shuttleaLib[] = {
			{"gear", lua_gear},
			{"set_adilayout", set_adilayout},
			{"set_attrefmode", set_attrefmode},
			{"set_attreftgtmode", set_attreftgtmode},
			{"set_attrefoffset", set_attrefoffset},
			{"set_atttgtoffset", set_atttgtoffset},
			{"set_attoffsetmode", set_attoffsetmode},
			{"set_atttgtframemode", set_atttgtframemode},
			{NULL, NULL}
		};

		// create metatable for vessel userdata
		luaL_newmetatable (L, "SHUTTLEA.vtable");

		// create a table for the overloaded methods
		luaL_openlib (L, "SHUTTLEA.method", shuttleaLib, 0);

		// create metatable for accessing inherited methods from VESSEL
		luaL_newmetatable (L, "SHUTTLEA.base");
		lua_pushstring (L, "__index");
		luaL_getmetatable (L, "VESSEL.vtable");
		lua_settable (L, -3);

		// set SHUTTLEA.base as metatable for SHUTTLEA.method
		lua_setmetatable (L, -2);

		// point vessel userdata to SHUTTLEA.method
		lua_pushstring (L, "__index");
		lua_pushvalue (L, -2); // push SHUTTLEA.method
		lua_settable (L, -4);

		// pop SHUTTLEA.method from the stack
		lua_pop (L, 1);
	}

	lua_setmetatable (L, -2);

	return 0;
}

// ==========================================================================
// Shuttle-A Lua API extensions

VECTOR3 lua_tovector (lua_State *L, int idx)
{
	VECTOR3 vec;
	lua_getfield (L, idx, "x");
	vec.x = lua_tonumber (L, -1); lua_pop (L,1);
	lua_getfield (L, idx, "y");
	vec.y = lua_tonumber (L, -1); lua_pop (L,1);
	lua_getfield (L, idx, "z");
	vec.z = lua_tonumber (L, -1); lua_pop (L,1);
	return vec;
}

ShuttleA *lua_toShuttleA (lua_State *L, int idx)
{
	VESSEL **pv = (VESSEL**)lua_touserdata (L, idx);
	ShuttleA *sh = (ShuttleA*)*pv;
	return sh;
}

static int lua_gear (lua_State *L)
{
	ShuttleA *sh = lua_toShuttleA (L, 1);
	int action = lua_tointeger (L, 2);
	if (sh && action >= 2 && action < 4)
		sh->ActivateLandingGear (action == 2 ? ShuttleA::DOOR_CLOSING : ShuttleA::DOOR_OPENING);
	return 0;
}

static int set_adilayout (lua_State *L)
{
	ShuttleA *sh = lua_toShuttleA (L, 1);
	int layout = lua_tointeger (L, 2);
	sh->SetADILayout (layout);
	return 0;
}

static int set_attrefmode (lua_State *L)
{
	ShuttleA *sh = lua_toShuttleA (L, 1);
	int mode = lua_tointeger (L, 2);
	sh->SetAttrefMode (mode);
	return 0;
}

static int set_attreftgtmode (lua_State *L)
{
	ShuttleA *sh = lua_toShuttleA (L, 1);
	int mode = lua_tointeger (L, 2);
	sh->SetAttrefTgtMode (mode);
	return 0;
}

static int set_attrefoffset (lua_State *L)
{
	ShuttleA *sh = lua_toShuttleA (L, 1);
	VECTOR3 ofs = lua_tovector (L, 2);
	sh->SetAttrefOffset (ofs);
	return 0;
}

static int set_atttgtoffset (lua_State *L)
{
	ShuttleA *sh = lua_toShuttleA (L, 1);
	VECTOR3 ofs = lua_tovector (L, 2);
	sh->SetAtttgtOffset (ofs);
	return 0;
}

static int set_attoffsetmode (lua_State *L)
{
	ShuttleA *sh = lua_toShuttleA (L, 1);
	int mode = lua_tointeger (L, 2);
	sh->SetAttOffsetMode (mode);
	return 0;
}

static int set_atttgtframemode (lua_State *L)
{
	ShuttleA *sh = lua_toShuttleA (L, 1);
	int mode = lua_tointeger (L, 2);
	sh->SetAtttgtFrameMode (mode);
	return 0;
}