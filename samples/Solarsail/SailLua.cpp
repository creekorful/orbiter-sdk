#include "SolarSail.h"

extern "C" {
#include <lua\lua.h>
#include <lua\lualib.h>
#include <lua\lauxlib.h>
}

// ==========================================================================
// API function prototypes

SolarSail *lua_toSSail (lua_State *L, int idx = 1);
int sailSetPaddle (lua_State *L);

// ==========================================================================
// API initialisation

int SolarSail::Lua_InitInterpreter (void *context)
{
	lua_State *L = (lua_State*)context;

	// load vessel-specific scripts here
	//luaL_dofile (L, "Script\\ssail\\... .lua");

	return 0;
}

int SolarSail::Lua_InitInstance (void *context)
{
	lua_State *L = (lua_State*)context;

	// check if interpreter has DG table loaded already
	luaL_getmetatable (L, "VESSEL.DG");

	if (lua_isnil (L, -1)) { // register new functions
		lua_pop (L, 1);
		static const struct luaL_reg dgLib[] = {
			{"set_paddle", sailSetPaddle},
			{NULL, NULL}
		};

		// create metatable for vessel userdata
		luaL_newmetatable (L, "SSail.vtable");

		// create a table for the overloaded methods
		luaL_openlib (L, "SSail.method", dgLib, 0);

		// create metatable for accessing inherited methods from VESSEL
		luaL_newmetatable (L, "SSail.base");
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
// Script API functions

SolarSail *lua_toSSail (lua_State *L, int idx)
{
	VESSEL **pv = (VESSEL**)lua_touserdata (L, idx);
	SolarSail *sail = (SolarSail*)*pv;
	return sail;
}

static int sailSetPaddle (lua_State *L)
{
	SolarSail *sail = lua_toSSail (L, 1);
	int p = lua_tointeger (L, 2);
	if (p < 1 || p > 4) return 0;
	double pos = lua_tonumber (L, 3);
	pos = max (-1.0, min (1.0, pos));
	sail->SetPaddle (p-1, (pos+1)*0.5);
	return 0;
}