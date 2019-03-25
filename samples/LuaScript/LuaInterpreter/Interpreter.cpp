#define INTERPRETER_IMPLEMENTATION

#include "Interpreter.h"
#include "VesselAPI.h"
#include "MFDAPI.h"
#include "DrawAPI.h"

VESSEL *vfocus = (VESSEL*)0x1;
NOTEHANDLE Interpreter::hnote = NULL;

// ============================================================================
// nonmember functions

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

// ============================================================================
// class Interpreter

Interpreter::Interpreter ()
{
	L = luaL_newstate();  // create new Lua context
	is_busy = false;      // waiting for input
	is_term = false;      // no attached terminal by default
	jobs = 0;             // background jobs
	status = 0;           // normal
	term_verbose = 0;     // verbosity level
	postfunc = 0;
	postcontext = 0;
	// store interpreter context in the registry
	lua_pushlightuserdata (L, this);
	lua_setfield (L, LUA_REGISTRYINDEX, "interp");

	hExecMutex = CreateMutex (NULL, TRUE, NULL);
	hWaitMutex = CreateMutex (NULL, FALSE, NULL);
}

Interpreter::~Interpreter ()
{
	lua_close (L);

	if (hExecMutex) CloseHandle (hExecMutex);
	if (hWaitMutex) CloseHandle (hWaitMutex);
}

void Interpreter::Initialise ()
{
	luaL_openlibs (L);    // load the default libraries
	LoadAPI ();           // load default set of API interface functions
	LoadVesselAPI ();     // load vessel-specific part of API
	LoadLightEmitterMethods (); // load light source methods
	LoadMFDAPI ();        // load MFD methods
	LoadSketchpadAPI ();  // load Sketchpad methods
	LoadAnnotationAPI (); // load screen annotation methods
	LoadStartupScript (); // load default initialisation script
}

int Interpreter::Status () const
{
	return status;
}

bool Interpreter::IsBusy () const
{
	return is_busy;
}

void Interpreter::Terminate ()
{
	status = 1;
}

void Interpreter::PostStep (double simt, double simdt, double mjd)
{
	if (postfunc) {
		postfunc (postcontext);
		postfunc = 0;
		postcontext = 0;
	}
}

const char *Interpreter::lua_tostringex (lua_State *L, int idx, char *cbuf)
{
	static char cbuf_loc[256];
	if (!cbuf) cbuf = cbuf_loc;
	const char *str = lua_tostring (L,idx);
	if (str) {
		return str;
	} else if (lua_isvector (L,idx)) {
		VECTOR3 v = lua_tovector (L,idx);
		sprintf (cbuf, "[%g %g %g]", v.x, v.y, v.z);
		return cbuf;
	} else if (lua_ismatrix (L,idx)) {
		MATRIX3 m = lua_tomatrix(L,idx);
		int i, len[9], lmax[3];
		for (i = 0; i < 9; i++) {
			sprintf (cbuf, "%g", m.data[i]);
			len[i] = strlen(cbuf);
		}
		lmax[0] = max(len[0], max(len[3], len[6]));
		lmax[1] = max(len[1], max(len[4], len[7]));
		lmax[2] = max(len[2], max(len[5], len[8]));

		sprintf (cbuf, "[%*g %*g %*g]\n[%*g %*g %*g]\n[%*g %*g %*g]",
			lmax[0], m.m11, lmax[1], m.m12, lmax[2], m.m13,
			lmax[0], m.m21, lmax[1], m.m22, lmax[2], m.m23,
			lmax[0], m.m31, lmax[1], m.m32, lmax[2], m.m33);
		return cbuf;
	} else if (lua_isnil (L,idx)) {
		strcpy (cbuf, "nil");
		return cbuf;
	} else if (lua_isboolean (L,idx)) {
		int res = lua_toboolean (L,idx);
		strcpy (cbuf, res ? "true":"false");
		return cbuf;
	} else if (lua_islightuserdata (L,idx)) {
		void *p = lua_touserdata(L,idx);
		sprintf (cbuf, "0x%08x [data]", p);
		return cbuf;
	} else if (lua_isuserdata (L,idx)) {
		void *p = lua_touserdata(L,idx);
		sprintf (cbuf, "0x%08x [object]", p);
		return cbuf;
	} else if (lua_istable (L, idx)) {
		if (idx < 0) idx--;
		lua_pushnil(L);  /* first key */
		static char tbuf[1024] = "\0";
		while (lua_next(L, idx) != 0) {
			/* uses 'key' (at index -2) and 'value' (at index -1) */
			char fieldstr[256] = "\0";
			if (lua_isstring(L,-2)) sprintf (fieldstr, "%s=", lua_tostring(L,-2));
			strcat (fieldstr, lua_tostringex (L,-1));
			strcat (tbuf, fieldstr); strcat (tbuf, "\n");
			lua_pop(L, 1);
		}
		return tbuf;
	} else {
		cbuf[0] = '\0';
		return cbuf;
	}
}

void Interpreter::lua_pushvector (lua_State *L, const VECTOR3 &vec)
{
	lua_createtable (L, 0, 3);
	lua_pushnumber (L, vec.x);
	lua_setfield (L, -2, "x");
	lua_pushnumber (L, vec.y);
	lua_setfield (L, -2, "y");
	lua_pushnumber (L, vec.z);
	lua_setfield (L, -2, "z");
}

int Interpreter::lua_isvector (lua_State *L, int idx)
{
	if (!lua_istable (L, idx)) return 0;
	static char fieldname[3] = {'x','y','z'};
	static char field[2] = "x";
	int i, ii, n;
	bool fail;

	lua_pushnil(L);
	ii = (idx >= 0 ? idx : idx-1);
	n = 0;
	while(lua_next(L,ii)) {
		lua_pop(L,1);
		n++;
	}
	if (n != 3) return 0;

	for (i = 0; i < 3; i++) {
		field[0] = fieldname[i];
		lua_getfield (L, idx, field);
		fail = (lua_isnil (L,-1));
		lua_pop (L,1);
		if (fail) return 0;
	}
	return 1;
}

void Interpreter::lua_pushmatrix (lua_State *L, const MATRIX3 &mat)
{
	lua_createtable(L,0,9);
	lua_pushnumber(L,mat.m11);  lua_setfield(L,-2,"m11");
	lua_pushnumber(L,mat.m12);  lua_setfield(L,-2,"m12");
	lua_pushnumber(L,mat.m13);  lua_setfield(L,-2,"m13");
	lua_pushnumber(L,mat.m21);  lua_setfield(L,-2,"m21");
	lua_pushnumber(L,mat.m22);  lua_setfield(L,-2,"m22");
	lua_pushnumber(L,mat.m23);  lua_setfield(L,-2,"m23");
	lua_pushnumber(L,mat.m31);  lua_setfield(L,-2,"m31");
	lua_pushnumber(L,mat.m32);  lua_setfield(L,-2,"m32");
	lua_pushnumber(L,mat.m33);  lua_setfield(L,-2,"m33");
}

MATRIX3 Interpreter::lua_tomatrix (lua_State *L, int idx)
{
	MATRIX3 mat;
	lua_getfield (L, idx, "m11");  mat.m11 = lua_tonumber (L, -1);  lua_pop (L,1);
	lua_getfield (L, idx, "m12");  mat.m12 = lua_tonumber (L, -1);  lua_pop (L,1);
	lua_getfield (L, idx, "m13");  mat.m13 = lua_tonumber (L, -1);  lua_pop (L,1);
	lua_getfield (L, idx, "m21");  mat.m21 = lua_tonumber (L, -1);  lua_pop (L,1);
	lua_getfield (L, idx, "m22");  mat.m22 = lua_tonumber (L, -1);  lua_pop (L,1);
	lua_getfield (L, idx, "m23");  mat.m23 = lua_tonumber (L, -1);  lua_pop (L,1);
	lua_getfield (L, idx, "m31");  mat.m31 = lua_tonumber (L, -1);  lua_pop (L,1);
	lua_getfield (L, idx, "m32");  mat.m32 = lua_tonumber (L, -1);  lua_pop (L,1);
	lua_getfield (L, idx, "m33");  mat.m33 = lua_tonumber (L, -1);  lua_pop (L,1);
	return mat;
}

int Interpreter::lua_ismatrix (lua_State *L, int idx)
{
	if (!lua_istable (L, idx)) return 0;
	static char *fieldname[9] = {"m11","m12","m13","m21","m22","m23","m31","m32","m33"};
	int i, ii, n;
	bool fail;

	lua_pushnil(L);
	ii = (idx >= 0 ? idx : idx-1);
	n = 0;
	while(lua_next(L,ii)) {
		lua_pop(L,1);
		n++;
	}
	if (n != 9) return 0;

	for (i = 0; i < 9; i++) {
		lua_getfield (L, idx, fieldname[i]);
		fail = (lua_isnil (L,-1));
		lua_pop (L,1);
		if (fail) return 0;
	}
	return 1;
}

COLOUR4 Interpreter::lua_torgba (lua_State *L, int idx)
{
	COLOUR4 col = {0,0,0,0};
	lua_getfield (L, idx, "r");
	if (lua_isnumber(L,-1)) col.r = (float)lua_tonumber (L, -1);
	lua_pop (L,1);
	lua_getfield (L, idx, "g");
	if (lua_isnumber(L,-1)) col.g = (float)lua_tonumber (L, -1);
	lua_pop (L,1);
	lua_getfield (L, idx, "b");
	if (lua_isnumber(L,-1)) col.b = (float)lua_tonumber (L, -1);
	lua_pop (L,1);
	lua_getfield (L, idx, "a");
	if (lua_isnumber(L,-1)) col.a = (float)lua_tonumber (L, -1);
	lua_pop (L,1);
	return col;
}

void Interpreter::lua_pushvessel (lua_State *L, VESSEL *v)
{
	lua_pushlightuserdata(L,v);         // use object pointer as key
	lua_gettable(L,LUA_REGISTRYINDEX);  // retrieve object from registry
	if (lua_isnil(L,-1)) {              // object not found
		lua_pop(L,1);                   // pop nil
		VESSEL **pv = (VESSEL**)lua_newuserdata(L,sizeof(VESSEL*));
		*pv = v;
		luaL_getmetatable (L, "VESSEL.vtable"); // retrieve metatable
		lua_setmetatable (L,-2);             // and attach to new object
		LoadVesselExtensions(L,v);           // vessel environment
		lua_pushlightuserdata(L,v);          // create key
		lua_pushvalue(L,-2);                 // push object
		lua_settable(L,LUA_REGISTRYINDEX);   // and store in registry
		// note that now the object is on top of the stack
	}
}

VESSEL *Interpreter::lua_tovessel (lua_State *L, int idx)
{
	VESSEL **pv = (VESSEL**)lua_touserdata (L, idx);
	if (pv && *pv == vfocus) // flag for focus object
		*pv = oapiGetFocusInterface();
	return *pv;
}

void Interpreter::lua_pushmfd (lua_State *L, MFD2 *mfd)
{
	lua_pushlightuserdata(L,mfd);       // use object pointer as key
	lua_gettable(L,LUA_REGISTRYINDEX);  // retrieve object from registry
	if (lua_isnil(L,-1)) {              // object not found
		lua_pop(L,1);                   // pop nil
		MFD2 **pmfd = (MFD2**)lua_newuserdata(L,sizeof(MFD2*));
		*pmfd = mfd;
		luaL_getmetatable (L, "MFD.vtable"); // retrieve metatable
		lua_setmetatable (L,-2);             // and attach to new object
		lua_pushlightuserdata(L, mfd);       // create key
		lua_pushvalue(L,-2);                 // push object
		lua_settable(L, LUA_REGISTRYINDEX);  // and store in registry
		// note that now the object is on top of the stack
	}
}

MFD2 *Interpreter::lua_tomfd (lua_State *L, int idx)
{
	MFD2 **pmfd = (MFD2**)lua_touserdata(L,idx);
	return *pmfd;
}

#ifdef UNDEF
void Interpreter::lua_pushmfd (lua_State *L, MFD2 *mfd)
{
	lua_pushlightuserdata(L,mfd);
	//MFD2 **pm = (MFD2**)lua_newuserdata (L, sizeof(MFD*));
	//*pm = mfd;
	luaL_getmetatable (L, "MFD.vtable");
	lua_setmetatable (L, -2);
}
#endif

void Interpreter::lua_pushlightemitter (lua_State *L, const LightEmitter *le)
{
	lua_pushlightuserdata (L, (void*)le);   // use object pointer as key
	lua_gettable (L, LUA_REGISTRYINDEX);    // retrieve object from registry
	if (lua_isnil (L,-1)) {                 // object not found
		lua_pop (L,1);                      // pop nil
		const LightEmitter **ple = (const LightEmitter**)lua_newuserdata(L,sizeof(const LightEmitter*));
		*ple = le;
		luaL_getmetatable (L, "LightEmitter.vtable"); // retrieve metatable
		lua_setmetatable (L,-2);            // and attach to new object
		lua_pushlightuserdata (L, (void*)le);  // create key
		lua_pushvalue (L,-2);               // push object
		lua_settable (L,LUA_REGISTRYINDEX); // and store in registry
		// note that now the object is on top of the stack
	}
}

LightEmitter *Interpreter::lua_tolightemitter (lua_State *L, int idx)
{
	LightEmitter **le = (LightEmitter**)lua_touserdata (L, idx);
	return *le;
}

void Interpreter::lua_pushsketchpad (lua_State *L, oapi::Sketchpad *skp)
{
	lua_pushlightuserdata(L,skp);       // use object pointer as key
	lua_gettable(L,LUA_REGISTRYINDEX);  // retrieve object from registry
	if (lua_isnil(L,-1)) {              // object not found
		lua_pop(L,1);                   // pop nil
		oapi::Sketchpad **pskp = (oapi::Sketchpad**)lua_newuserdata(L,sizeof(oapi::Sketchpad*));
		*pskp = skp;
		luaL_getmetatable (L, "SKP.vtable"); // retrieve metatable
		lua_setmetatable (L,-2);             // and attach to new object
		lua_pushlightuserdata(L,skp);        // create key
		lua_pushvalue(L,-2);                 // push object
		lua_settable(L, LUA_REGISTRYINDEX);  // and store in registry
		// note that now the object is on top of the stack
	}
#ifdef UNDEF
	//lua_pushlightuserdata(L,skp);
	oapi::Sketchpad **ps = (oapi::Sketchpad**)lua_newuserdata (L, sizeof(oapi::Sketchpad*));
	*ps = skp;
	luaL_getmetatable (L, "SKP.vtable");
	lua_setmetatable (L, -2);
#endif
}

void Interpreter::WaitExec (DWORD timeout)
{
	// Called by orbiter thread or interpreter thread to wait its turn
	// Orbiter waits for the script for 1 second to return
	WaitForSingleObject (hWaitMutex, timeout); // wait for synchronisation mutex
	WaitForSingleObject (hExecMutex, timeout); // wait for execution mutex
	ReleaseMutex (hWaitMutex);              // release synchronisation mutex
}

void Interpreter::EndExec ()
{
	// called by orbiter thread or interpreter thread to hand over control
	ReleaseMutex (hExecMutex);
}

void Interpreter::frameskip (lua_State *L)
{
	if (status == 1) { // termination request
		lua_pushboolean(L, 1);
		lua_setfield (L, LUA_GLOBALSINDEX, "wait_exit");
	} else {
		EndExec();
		WaitExec();
	}
}

int Interpreter::ProcessChunk (const char *chunk, int n)
{
	WaitExec();
	int res = RunChunk (chunk, n);
	EndExec();
	return res;
}

int Interpreter::RunChunk (const char *chunk, int n)
{
	int res = 0;
	if (chunk[0]) {
		is_busy = true;
		// run command
		luaL_loadbuffer (L, chunk, n, "line");
		res = lua_pcall (L, 0, 0, 0);
		if (res && is_term)
			term_strout ("Execution error.");
		// check for leftover background jobs
		lua_getfield (L, LUA_GLOBALSINDEX, "_nbranch");
		lua_call (L, 0, 1);
		jobs = lua_tointeger (L, -1);
		lua_pop (L, 1);
		is_busy = false;
	} else {
		// idle loop: execute background jobs
		lua_getfield (L, LUA_GLOBALSINDEX, "_idle");
		lua_call (L, 0, 1);
		jobs = lua_tointeger (L, -1);
		lua_pop (L, 1);
		res = -1;
	}
	return res;
}

void Interpreter::term_out (lua_State *L, bool iserr)
{
	const char *str = lua_tostringex (L,-1);
	if (str) term_strout (str, iserr);
}

void Interpreter::LoadAPI ()
{
	// Load global functions
	static const struct luaL_reg glob[] = {
		{"help", help},
		//{"api", help_api},
		{NULL, NULL}
	};
	for (int i = 0; glob[i].name; i++) {
		lua_pushcfunction (L, glob[i].func);
		lua_setglobal (L, glob[i].name);
	}

	// Load the vector library
	static const struct luaL_reg vecLib[] = {
		{"set", vec_set},
		{"add", vec_add},
		{"sub", vec_sub},
		{"mul", vec_mul},
		{"div", vec_div},
		{"dotp", vec_dotp},
		{"crossp", vec_crossp},
		{"length", vec_length},
		{"dist", vec_dist},
		{"unit", vec_unit},
		{NULL, NULL}
	};
	luaL_openlib (L, "vec", vecLib, 0);

	static const struct luaL_reg matLib[] = {
		{"identity", mat_identity},
		{"mul", mat_mul},
		{"tmul", mat_tmul},
		{"mmul", mat_mmul},
		{NULL, NULL}
	};
	luaL_openlib (L, "mat", matLib, 0);

	// Load the process library
	static const struct luaL_reg procLib[] = {
		{"Frameskip", procFrameskip},
		{NULL, NULL}
	};
	luaL_openlib (L, "proc", procLib, 0);

	// Load the oapi library
	static const struct luaL_reg oapiLib[] = {
		{"get_objhandle", oapiGetObjectHandle},
		{"get_objcount", oapiGetObjectCount},
		{"get_objname", oapiGetObjectName},
		{"create_annotation", oapiCreateAnnotation},
		{"del_annotation", oapiDelAnnotation},
		{"dbg_out", oapiDbgOut},
		{"open_help", oapiOpenHelp},
		{"open_inputbox", oapiOpenInputBox},
		{"receive_input", oapiReceiveInput},
		{"del_vessel", oapi_del_vessel},

		// time functions
		{"get_simtime", oapi_get_simtime},
		{"get_simstep", oapi_get_simstep},
		{"get_systime", oapi_get_systime},
		{"get_sysstep", oapi_get_sysstep},
		{"get_simmjd", oapi_get_simmjd},
		{"set_simmjd", oapi_set_simmjd},
		{"get_sysmjd", oapi_get_sysmjd},
		{"time2mjd", oapi_time2mjd},
		{"get_tacc", oapi_get_tacc},
		{"set_tacc", oapi_set_tacc},
		{"get_pause", oapi_get_pause},
		{"set_pause", oapi_set_pause},

		// menu functions
		{"get_mainmenuvisibilitymode", oapi_get_mainmenuvisibilitymode},
		{"set_mainmenuvisibilitymode", oapi_set_mainmenuvisibilitymode},
		{"get_maininfovisibilitymode", oapi_get_maininfovisibilitymode},
		{"set_maininfovisibilitymode", oapi_set_maininfovisibilitymode},

		// coordinate transformations
		{"global_to_equ", oapi_global_to_equ},
		{"equ_to_global", oapi_equ_to_global},
		{"orthodome", oapi_orthodome},

		// body functions
		{"get_size", oapi_get_size},
		{"get_mass", oapi_get_mass},
		{"get_globalpos", oapi_get_globalpos},
		{"get_globalvel", oapi_get_globalvel},
		{"get_relativepos", oapi_get_relativepos},
		{"get_relativevel", oapi_get_relativevel},

		// vessel functions
		{"get_propellanthandle", oapi_get_propellanthandle},
		{"get_propellantmass", oapi_get_propellantmass},
		{"get_propellantmaxmass", oapi_get_propellantmaxmass},
		{"get_fuelmass", oapi_get_fuelmass},
		{"get_maxfuelmass", oapi_get_maxfuelmass},
		{"get_emptymass", oapi_get_emptymass},
		{"set_emptymass", oapi_set_emptymass},
		{"get_altitude", oapi_get_altitude},
		{"get_pitch", oapi_get_pitch},
		{"get_bank", oapi_get_bank},
		{"get_heading", oapi_get_heading},
		{"get_groundspeed", oapi_get_groundspeed},
		{"get_groundspeedvector", oapi_get_groundspeedvector},
		{"get_airspeed", oapi_get_airspeed},
		{"get_airspeedvector", oapi_get_airspeedvector},
		{"get_shipairspeedvector", oapi_get_shipairspeedvector},
		{"get_equpos", oapi_get_equpos},
		{"get_atm", oapi_get_atm},
		{"get_induceddrag", oapi_get_induceddrag},
		{"get_wavedrag", oapi_get_wavedrag},

		// Navigation radio transmitter functions
		{"get_navpos", oapi_get_navpos},
		{"get_navchannel", oapi_get_navchannel},
		{"get_navrange", oapi_get_navrange},
		{"get_navdata", oapi_get_navdata},
		{"get_navsignal", oapi_get_navsignal},
		{"get_navtype", oapi_get_navtype},

		// Camera functions
		{"set_cameramode", oapi_set_cameramode},
		{"get_cameratarget", oapi_get_cameratarget},
		{"set_cameratarget", oapi_set_cameratarget},
		{"get_cameraaperture", oapi_get_cameraaperture},
		{"set_cameraaperture", oapi_set_cameraaperture},
		{"get_cameraglobalpos", oapi_get_cameraglobalpos},
		{"get_cameraglobaldir", oapi_get_cameraglobaldir},
		{"move_groundcamera", oapi_move_groundcamera},

		// animation functions
		{"create_animationcomponent", oapi_create_animationcomponent},
		{"del_animationcomponent", oapi_del_animationcomponent},

		// instrument panel functions
		{"open_mfd", oapi_open_mfd},
		{"set_hudmode", oapi_set_hudmode},
		{"set_panelblink", oapi_set_panelblink},

		// user i/o functions
		{"keydown", oapi_keydown},
		{"resetkey", oapi_resetkey},

		{NULL, NULL}
	};
	luaL_openlib (L, "oapi", oapiLib, 0);

	// Load the (dummy) term library
	static const struct luaL_reg termLib[] = {
		{"out", termOut},
		{NULL, NULL}
	};
	luaL_openlib (L, "term", termLib, 0);

	// Set up global tables of constants

	// Key ID table
	lua_createtable (L, 0, 100);
	lua_pushnumber (L, OAPI_KEY_ESCAPE);      lua_setfield (L, -2, "ESCAPE");
	lua_pushnumber (L, OAPI_KEY_1);           lua_setfield (L, -2, "1");
	lua_pushnumber (L, OAPI_KEY_2);           lua_setfield (L, -2, "2");
	lua_pushnumber (L, OAPI_KEY_3);           lua_setfield (L, -2, "3");
	lua_pushnumber (L, OAPI_KEY_4);           lua_setfield (L, -2, "4");
	lua_pushnumber (L, OAPI_KEY_5);           lua_setfield (L, -2, "5");
	lua_pushnumber (L, OAPI_KEY_6);           lua_setfield (L, -2, "6");
	lua_pushnumber (L, OAPI_KEY_7);           lua_setfield (L, -2, "7");
	lua_pushnumber (L, OAPI_KEY_8);           lua_setfield (L, -2, "8");
	lua_pushnumber (L, OAPI_KEY_9);           lua_setfield (L, -2, "9");
	lua_pushnumber (L, OAPI_KEY_0);           lua_setfield (L, -2, "0");
	lua_pushnumber (L, OAPI_KEY_MINUS);       lua_setfield (L, -2, "MINUS");
	lua_pushnumber (L, OAPI_KEY_EQUALS);      lua_setfield (L, -2, "EQUALS");
	lua_pushnumber (L, OAPI_KEY_BACK);        lua_setfield (L, -2, "BACK");
	lua_pushnumber (L, OAPI_KEY_TAB);         lua_setfield (L, -2, "TAB");
	lua_pushnumber (L, OAPI_KEY_Q);           lua_setfield (L, -2, "Q");
	lua_pushnumber (L, OAPI_KEY_W);           lua_setfield (L, -2, "W");
	lua_pushnumber (L, OAPI_KEY_E);           lua_setfield (L, -2, "E");
	lua_pushnumber (L, OAPI_KEY_R);           lua_setfield (L, -2, "R");
	lua_pushnumber (L, OAPI_KEY_T);           lua_setfield (L, -2, "T");
	lua_pushnumber (L, OAPI_KEY_Y);           lua_setfield (L, -2, "Y");
	lua_pushnumber (L, OAPI_KEY_U);           lua_setfield (L, -2, "U");
	lua_pushnumber (L, OAPI_KEY_I);           lua_setfield (L, -2, "I");
	lua_pushnumber (L, OAPI_KEY_O);           lua_setfield (L, -2, "O");
	lua_pushnumber (L, OAPI_KEY_P);           lua_setfield (L, -2, "P");
	lua_pushnumber (L, OAPI_KEY_LBRACKET);    lua_setfield (L, -2, "LBRACKET");
	lua_pushnumber (L, OAPI_KEY_RBRACKET);    lua_setfield (L, -2, "RBRACKET");
	lua_pushnumber (L, OAPI_KEY_RETURN);      lua_setfield (L, -2, "RETURN");
	lua_pushnumber (L, OAPI_KEY_LCONTROL);    lua_setfield (L, -2, "LCONTROL");
	lua_pushnumber (L, OAPI_KEY_A);           lua_setfield (L, -2, "A");
	lua_pushnumber (L, OAPI_KEY_S);           lua_setfield (L, -2, "S");
	lua_pushnumber (L, OAPI_KEY_D);           lua_setfield (L, -2, "D");
	lua_pushnumber (L, OAPI_KEY_F);           lua_setfield (L, -2, "F");
	lua_pushnumber (L, OAPI_KEY_G);           lua_setfield (L, -2, "G");
	lua_pushnumber (L, OAPI_KEY_H);           lua_setfield (L, -2, "H");
	lua_pushnumber (L, OAPI_KEY_J);           lua_setfield (L, -2, "J");
	lua_pushnumber (L, OAPI_KEY_K);           lua_setfield (L, -2, "K");
	lua_pushnumber (L, OAPI_KEY_L);           lua_setfield (L, -2, "L");
	lua_pushnumber (L, OAPI_KEY_SEMICOLON);   lua_setfield (L, -2, "SEMICOLON");
	lua_pushnumber (L, OAPI_KEY_APOSTROPHE);  lua_setfield (L, -2, "APOSTROPHE");
	lua_pushnumber (L, OAPI_KEY_GRAVE);       lua_setfield (L, -2, "GRAVE");
	lua_pushnumber (L, OAPI_KEY_LSHIFT);      lua_setfield (L, -2, "LSHIFT");
	lua_pushnumber (L, OAPI_KEY_BACKSLASH);   lua_setfield (L, -2, "BACKSLASH");
	lua_pushnumber (L, OAPI_KEY_Z);           lua_setfield (L, -2, "Z");
	lua_pushnumber (L, OAPI_KEY_X);           lua_setfield (L, -2, "X");
	lua_pushnumber (L, OAPI_KEY_C);           lua_setfield (L, -2, "C");
	lua_pushnumber (L, OAPI_KEY_V);           lua_setfield (L, -2, "V");
	lua_pushnumber (L, OAPI_KEY_B);           lua_setfield (L, -2, "B");
	lua_pushnumber (L, OAPI_KEY_N);           lua_setfield (L, -2, "N");
	lua_pushnumber (L, OAPI_KEY_M);           lua_setfield (L, -2, "M");
	lua_pushnumber (L, OAPI_KEY_COMMA);       lua_setfield (L, -2, "COMMA");
	lua_pushnumber (L, OAPI_KEY_PERIOD);      lua_setfield (L, -2, "PERIOD");
	lua_pushnumber (L, OAPI_KEY_SLASH);       lua_setfield (L, -2, "SLASH");
	lua_pushnumber (L, OAPI_KEY_RSHIFT);      lua_setfield (L, -2, "RSHIFT");
	lua_pushnumber (L, OAPI_KEY_MULTIPLY);    lua_setfield (L, -2, "MULTIPLY");
	lua_pushnumber (L, OAPI_KEY_LALT);        lua_setfield (L, -2, "LALT");
	lua_pushnumber (L, OAPI_KEY_SPACE);       lua_setfield (L, -2, "SPACE");
	lua_pushnumber (L, OAPI_KEY_CAPITAL);     lua_setfield (L, -2, "CAPITAL");
	lua_pushnumber (L, OAPI_KEY_F1);          lua_setfield (L, -2, "F1");
	lua_pushnumber (L, OAPI_KEY_F2);          lua_setfield (L, -2, "F2");
	lua_pushnumber (L, OAPI_KEY_F3);          lua_setfield (L, -2, "F3");
	lua_pushnumber (L, OAPI_KEY_F4);          lua_setfield (L, -2, "F4");
	lua_pushnumber (L, OAPI_KEY_F5);          lua_setfield (L, -2, "F5");
	lua_pushnumber (L, OAPI_KEY_F6);          lua_setfield (L, -2, "F6");
	lua_pushnumber (L, OAPI_KEY_F7);          lua_setfield (L, -2, "F7");
	lua_pushnumber (L, OAPI_KEY_F8);          lua_setfield (L, -2, "F8");
	lua_pushnumber (L, OAPI_KEY_F9);          lua_setfield (L, -2, "F9");
	lua_pushnumber (L, OAPI_KEY_F10);         lua_setfield (L, -2, "F10");
	lua_pushnumber (L, OAPI_KEY_NUMLOCK);     lua_setfield (L, -2, "NUMLOCK");
	lua_pushnumber (L, OAPI_KEY_SCROLL);      lua_setfield (L, -2, "SCROLL");
	lua_pushnumber (L, OAPI_KEY_NUMPAD7);     lua_setfield (L, -2, "NUMPAD7");
	lua_pushnumber (L, OAPI_KEY_NUMPAD8);     lua_setfield (L, -2, "NUMPAD8");
	lua_pushnumber (L, OAPI_KEY_NUMPAD9);     lua_setfield (L, -2, "NUMPAD9");
	lua_pushnumber (L, OAPI_KEY_SUBTRACT);    lua_setfield (L, -2, "SUBTRACT");
	lua_pushnumber (L, OAPI_KEY_NUMPAD4);     lua_setfield (L, -2, "NUMPAD4");
	lua_pushnumber (L, OAPI_KEY_NUMPAD5);     lua_setfield (L, -2, "NUMPAD5");
	lua_pushnumber (L, OAPI_KEY_NUMPAD6);     lua_setfield (L, -2, "NUMPAD6");
	lua_pushnumber (L, OAPI_KEY_ADD);         lua_setfield (L, -2, "ADD");
	lua_pushnumber (L, OAPI_KEY_NUMPAD1);     lua_setfield (L, -2, "NUMPAD1");
	lua_pushnumber (L, OAPI_KEY_NUMPAD2);     lua_setfield (L, -2, "NUMPAD2");
	lua_pushnumber (L, OAPI_KEY_NUMPAD3);     lua_setfield (L, -2, "NUMPAD3");
	lua_pushnumber (L, OAPI_KEY_NUMPAD0);     lua_setfield (L, -2, "NUMPAD0");
	lua_pushnumber (L, OAPI_KEY_DECIMAL);     lua_setfield (L, -2, "DECIMAL");
	lua_pushnumber (L, OAPI_KEY_OEM_102);     lua_setfield (L, -2, "OEM_102");
	lua_pushnumber (L, OAPI_KEY_F11);         lua_setfield (L, -2, "F11");
	lua_pushnumber (L, OAPI_KEY_F12);         lua_setfield (L, -2, "F12");
	lua_pushnumber (L, OAPI_KEY_NUMPADENTER); lua_setfield (L, -2, "NUMPADENTER");
	lua_pushnumber (L, OAPI_KEY_RCONTROL);    lua_setfield (L, -2, "RCONTROL");
	lua_pushnumber (L, OAPI_KEY_DIVIDE);      lua_setfield (L, -2, "DIVIDE");
	lua_pushnumber (L, OAPI_KEY_RALT);        lua_setfield (L, -2, "RALT");
	lua_pushnumber (L, OAPI_KEY_HOME);        lua_setfield (L, -2, "HOME");
	lua_pushnumber (L, OAPI_KEY_UP);          lua_setfield (L, -2, "UP");
	lua_pushnumber (L, OAPI_KEY_PRIOR);       lua_setfield (L, -2, "PRIOR");
	lua_pushnumber (L, OAPI_KEY_LEFT);        lua_setfield (L, -2, "LEFT");
	lua_pushnumber (L, OAPI_KEY_RIGHT);       lua_setfield (L, -2, "RIGHT");
	lua_pushnumber (L, OAPI_KEY_END);         lua_setfield (L, -2, "END");
	lua_pushnumber (L, OAPI_KEY_DOWN);        lua_setfield (L, -2, "DOWN");
	lua_pushnumber (L, OAPI_KEY_NEXT);        lua_setfield (L, -2, "NEXT");
	lua_pushnumber (L, OAPI_KEY_INSERT);      lua_setfield (L, -2, "INSERT");
	lua_pushnumber (L, OAPI_KEY_DELETE);      lua_setfield (L, -2, "DELETE");
	lua_setglobal (L, "OAPI_KEY");

	// mouse event identifiers
	lua_createtable (L, 0, 11);
	lua_pushnumber (L, PANEL_MOUSE_IGNORE);   lua_setfield (L, -2, "IGNORE");
	lua_pushnumber (L, PANEL_MOUSE_LBDOWN);   lua_setfield (L, -2, "LBDOWN");
	lua_pushnumber (L, PANEL_MOUSE_RBDOWN);   lua_setfield (L, -2, "RBDOWN");
	lua_pushnumber (L, PANEL_MOUSE_LBUP);     lua_setfield (L, -2, "LBUP");
	lua_pushnumber (L, PANEL_MOUSE_RBUP);     lua_setfield (L, -2, "RBUP");
	lua_pushnumber (L, PANEL_MOUSE_LBPRESSED);lua_setfield (L, -2, "LBPRESSED");
	lua_pushnumber (L, PANEL_MOUSE_RBPRESSED);lua_setfield (L, -2, "RBPRESSED");
	lua_pushnumber (L, PANEL_MOUSE_DOWN);     lua_setfield (L, -2, "DOWN");
	lua_pushnumber (L, PANEL_MOUSE_UP);       lua_setfield (L, -2, "UP");
	lua_pushnumber (L, PANEL_MOUSE_PRESSED);  lua_setfield (L, -2, "PRESSED");
	lua_pushnumber (L, PANEL_MOUSE_ONREPLAY); lua_setfield (L, -2, "ONREPLAY");
	lua_setglobal (L, "PANEL_MOUSE");

	// frame of reference identifiers
	lua_createtable (L, 0, 4);
	lua_pushnumber (L, FRAME_GLOBAL);   lua_setfield (L, -2, "GLOBAL");
	lua_pushnumber (L, FRAME_LOCAL);    lua_setfield (L, -2, "LOCAL");
	lua_pushnumber (L, FRAME_REFLOCAL); lua_setfield (L, -2, "REFLOCAL");
	lua_pushnumber (L, FRAME_HORIZON);  lua_setfield (L, -2, "HORIZON");
	lua_setglobal (L, "REFFRAME");

	// altitude mode identifiers
	lua_createtable (L, 0, 2);
	lua_pushnumber (L, ALTMODE_MEANRAD); lua_setfield (L, -2, "MEANRAD");
	lua_pushnumber (L, ALTMODE_GROUND);  lua_setfield (L, -2, "GROUND");
	lua_setglobal (L, "ALTMODE");
}

void Interpreter::LoadVesselAPI ()
{
	static const struct luaL_reg vesselAcc[] = {
		{"get_handle", vesselGetHandle},
		{"get_focushandle", vesselGetFocusHandle},
		{"get_interface", vesselGetInterface},
		{"get_focusinterface", vesselGetFocusInterface},
		{"get_count", vesselGetCount},
		{NULL, NULL}
	};
	static const struct luaL_reg vesselLib[] = {
		{"get_handle", vGetHandle},
		{"send_bufferedkey", vesselSendBufferedKey},
		{"get_gravityref", vesselGetGravityRef},
		{"get_surfaceref", vesselGetSurfaceRef},
		{"get_altitude", vesselGetAltitude},
		{"get_pitch", vesselGetPitch},
		{"get_bank", vesselGetBank},
		{"get_yaw", vesselGetYaw},
		{"get_angvel", vesselGetAngularVel},
		{"set_angvel", vesselSetAngularVel},
		{"get_elements", vesselGetElements},
		{"get_elementsex", vesselGetElementsEx},
		{"set_elements", vesselSetElements},
		{"get_progradedir", vesselGetProgradeDir},
		{"get_weightvector", vesselGetWeightVector},
		{"get_thrustvector", vesselGetThrustVector},
		{"get_liftvector", vesselGetLiftVector},
		{"get_rcsmode", vesselGetRCSmode},
		{"set_rcsmode", vesselSetRCSmode},

		// General vessel properties
		{"get_name", v_get_name},
		{"get_classname", v_get_classname},
		{"get_flightmodel", v_get_flightmodel},
		{"get_damagemodel", v_get_damagemodel},
		{"get_enablefocus", v_get_enablefocus},
		{"set_enablefocus", v_set_enablefocus},
		{"get_size", v_get_size},
		{"set_size", v_set_size},
		{"get_emptymass", v_get_emptymass},
		{"set_emptymass", v_set_emptymass},
		{"get_pmi", v_get_pmi},
		{"set_pmi", v_set_pmi},
		{"get_crosssections", v_get_crosssections},
		{"set_crosssections", v_set_crosssections},
		{"get_gravitygradientdamping", v_get_gravitygradientdamping},
		{"set_gravitygradientdamping", v_set_gravitygradientdamping},
		{"get_touchdownpoints", v_get_touchdownpoints},
		{"set_touchdownpoints", v_set_touchdownpoints},
		{"set_visibilitylimit", v_set_visibilitylimit},

		// vessel state
		{"get_mass", v_get_mass},
		{"get_globalpos", v_get_globalpos},
		{"get_globalvel", v_get_globalvel},
		{"get_relativepos", v_get_relativepos},
		{"get_relativevel", v_get_relativevel},
		{"get_rotationmatrix", v_get_rotationmatrix},

		// atmospheric parameters
		{"get_atmref", v_get_atmref},
		{"get_atmtemperature", v_get_atmtemperature},
		{"get_atmdensity", v_get_atmdensity},
		{"get_atmpressure", v_get_atmpressure},

		// aerodynamic state parameters
		{"get_dynpressure", v_get_dynpressure},
		{"get_machnumber", v_get_machnumber},
		{"get_airspeed", v_get_airspeed},
		{"get_airspeedvector", v_get_airspeedvector},
		{"get_shipairspeedvector", v_get_shipairspeedvector},
		{"get_horizonairspeedvector", v_get_horizonairspeedvector},
		{"get_groundspeed", v_get_groundspeed},
		{"get_groundspeedvector", v_get_groundspeedvector},
		{"get_aoa", v_get_aoa},
		{"get_slipangle", v_get_slipangle},

		// airfoil methods
		{"create_airfoil", v_create_airfoil},
		{"del_airfoil", v_del_airfoil},
		{"create_controlsurface", v_create_controlsurface},

		// aerodynamic properties (legacy model)
		{"get_cw", v_get_cw},
		{"set_cw", v_set_cw},
		{"get_wingaspect", v_get_wingaspect},
		{"set_wingaspect", v_set_wingaspect},
		{"get_wingeffectiveness", v_get_wingeffectiveness},
		{"set_wingeffectiveness", v_set_wingeffectiveness},
		{"get_rotdrag", v_get_rotdrag},
		{"set_rotdrag", v_set_rotdrag},
		{"get_pitchmomentscale", v_get_pitchmomentscale},
		{"set_pitchmomentscale", v_set_pitchmomentscale},
		{"get_yawmomentscale", v_get_yawmomentscale},
		{"set_yawmomentscale", v_set_yawmomentscale},
		{"get_trimscale", v_get_trimscale},
		{"set_trimscale", v_set_trimscale},

		// vessel status
		{"is_landed", v_is_landed},
		{"get_groundcontact", v_get_groundcontact},
		{"get_navmode", v_get_navmode},
		{"set_navmode", v_set_navmode},
		{"get_adcmode", vesselGetADCmode},
		{"set_adcmode", vesselSetADCmode},
		{"get_adclevel", vesselGetADCLevel},
		{"set_adclevel", vesselSetADCLevel},

		// fuel management
		{"create_propellantresource", vesselCreatePropellantResource},
		{"del_propellantresource", vesselDelPropellantResource},
		{"clear_propellantresources", vesselClearPropellantResources},
		{"get_propellantcount", vesselGetPropellantCount},
		{"get_propellanthandle", vesselGetPropellantHandle},
		{"get_propellantmaxmass", vesselGetPropellantMaxMass},
		{"set_propellantmaxmass", vesselSetPropellantMaxMass},
		{"get_propellantmass", vesselGetPropellantMass},
		{"set_propellantmass", v_set_propellantmass},
		{"get_totalpropellantmass", v_get_totalpropellantmass},
		{"get_propellantefficiency", v_get_propellantefficiency},
		{"set_propellantefficiency", v_set_propellantefficiency},
		{"get_propellantflowrate", v_get_propellantflowrate},
		{"get_totalpropellantflowrate", v_get_totalpropellantflowrate},

		// Thruster management
		{"create_thruster", v_create_thruster},
		{"del_thruster", v_del_thruster},
		{"clear_thrusters", v_clear_thrusters},
		{"get_thrustercount", v_get_thrustercount},
		{"get_thrusterhandle", v_get_thrusterhandle},
		{"get_thrusterresource", v_get_thrusterresource},
		{"set_thrusterresource", v_set_thrusterresource},
		{"get_thrusterpos", v_get_thrusterpos},
		{"set_thrusterpos", v_set_thrusterpos},
		{"get_thrusterdir", v_get_thrusterdir},
		{"set_thrusterdir", v_set_thrusterdir},
		{"get_thrustermax0", v_get_thrustermax0},
		{"set_thrustermax0", v_set_thrustermax0},
		{"get_thrustermax", v_get_thrustermax},
		{"get_thrusterisp0", v_get_thrusterisp0},
		{"get_thrusterisp", v_get_thrusterisp},
		{"set_thrusterisp", v_set_thrusterisp},
		{"get_thrusterlevel", v_get_thrusterlevel},
		{"set_thrusterlevel", v_set_thrusterlevel},
		{"inc_thrusterlevel", v_inc_thrusterlevel},
		{"inc_thrusterlevel_singlestep", v_inc_thrusterlevel_singlestep},

		// Thruster group management
		{"create_thrustergroup", v_create_thrustergroup},
		{"del_thrustergroup", v_del_thrustergroup},
		{"get_thrustergrouphandle", v_get_thrustergrouphandle},
		{"get_thrustergrouphandlebyindex", v_get_thrustergrouphandlebyindex},
		{"get_groupthrustercount", v_get_groupthrustercount},
		{"get_groupthruster", v_get_groupthruster},
		{"get_thrustergrouplevel", v_get_thrustergrouplevel},
		{"set_thrustergrouplevel", v_set_thrustergrouplevel},
		{"inc_thrustergrouplevel", v_inc_thrustergrouplevel},
		{"inc_thrustergrouplevel_singlestep", v_inc_thrustergrouplevel_singlestep},

		// Docking port management
		{"create_dock", v_create_dock},
		{"del_dock", v_del_dock},
		{"set_dockparams", v_set_dockparams},
		{"get_dockparams", v_get_dockparams},
		{"get_dockcount", v_get_dockcount},
		{"get_dockhandle", v_get_dockhandle},
		{"get_dockstatus", v_get_dockstatus},
		{"undock", v_undock},

		// Attachment management
		{"create_attachment", v_create_attachment},
		{"del_attachment", v_del_attachment},
		{"clear_attachments", v_clear_attachments},
		{"set_attachmentparams", v_set_attachmentparams},
		{"get_attachmentparams", v_get_attachmentparams},
		{"get_attachmentid", v_get_attachmentid},
		{"get_attachmentstatus", v_get_attachmentstatus},
		{"get_attachmentcount", v_get_attachmentcount},
		{"get_attachmentindex", v_get_attachmentindex},
		{"get_attachmenthandle", v_get_attachmenthandle},
		{"attach_child", v_attach_child},
		{"detach_child", v_detach_child},

		// Navigation radio interface
		{"enable_transponder", v_enable_transponder},
		{"get_transponder", v_get_transponder},
		{"set_transponderchannel", v_set_transponderchannel},
		{"enable_ids", v_enable_ids},
		{"get_ids", v_get_ids},
		{"set_idschannel", v_set_idschannel},
		{"init_navradios", v_init_navradios},
		{"get_navcount", v_get_navcount},
		{"set_navchannel", v_set_navchannel},
		{"get_navchannel", v_get_navchannel},
		{"get_navsource", v_get_navsource},

		// exhaust and reentry render options
		{"add_exhaust", v_add_exhaust},
		{"del_exhaust", v_del_exhaust},
		{"get_exhaustcount", v_get_exhaustcount},
		{"add_exhauststream", v_add_exhauststream},

		// light source methods
		{"add_pointlight", v_add_pointlight},
		{"add_spotlight", v_add_spotlight},
		{"get_lightemitter", v_get_lightemitter},
		{"get_lightemittercount", v_get_lightemittercount},
		{"del_lightemitter", v_del_lightemitter},
		{"clear_lightemitters", v_clear_lightemitters},

		// Camera management
		{"get_cameraoffset", v_get_cameraoffset},
		{"set_cameraoffset", v_set_cameraoffset},

		// mesh methods
		{"add_mesh", v_add_mesh},
		{"insert_mesh", v_insert_mesh},
		{"del_mesh", v_del_mesh},
		{"clear_meshes", v_clear_meshes},
		{"get_meshcount", v_get_meshcount},
		{"shift_mesh", v_shift_mesh},
		{"shift_meshes", v_shift_meshes},
		{"get_meshoffset", v_get_meshoffset},

		// animation methods
		{"create_animation", v_create_animation},
		{"del_animation", v_del_animation},
		{"set_animation", v_set_animation},
		{"add_animationcomponent", v_add_animationcomponent},

		{NULL, NULL}
	};
	luaL_newmetatable (L, "VESSEL.vtable");
	lua_pushstring (L, "__index");
	lua_pushvalue (L, -2); // push metatable
	lua_settable (L, -3);  // metatable.__index = metatable
	luaL_openlib (L, NULL, vesselLib, 0);
	luaL_openlib (L, "vessel", vesselAcc, 0);

	// create pseudo-instance "focus"
	lua_pushlightuserdata (L, vfocus);
	luaL_getmetatable (L, "VESSEL.vtable");  // push metatable
	lua_setmetatable (L, -2);               // set metatable for user data
	lua_setglobal (L, "focus");

	// store thruster group identifiers in global "THGROUP" table
	// C identifiers "THGROUP_xxx" become table entries "THGROUP.xxx"
	lua_createtable (L, 0, 15);
	lua_pushnumber (L, THGROUP_MAIN);          lua_setfield (L, -2, "MAIN");
	lua_pushnumber (L, THGROUP_RETRO);         lua_setfield (L, -2, "RETRO");
	lua_pushnumber (L, THGROUP_HOVER);         lua_setfield (L, -2, "HOVER");
	lua_pushnumber (L, THGROUP_ATT_PITCHUP);   lua_setfield (L, -2, "ATT_PITCHUP");
	lua_pushnumber (L, THGROUP_ATT_PITCHDOWN); lua_setfield (L, -2, "ATT_PITCHDOWN");
	lua_pushnumber (L, THGROUP_ATT_YAWLEFT);   lua_setfield (L, -2, "ATT_YAWLEFT");
	lua_pushnumber (L, THGROUP_ATT_YAWRIGHT);  lua_setfield (L, -2, "ATT_YAWRIGHT");
	lua_pushnumber (L, THGROUP_ATT_BANKLEFT);  lua_setfield (L, -2, "ATT_BANKLEFT");
	lua_pushnumber (L, THGROUP_ATT_BANKRIGHT); lua_setfield (L, -2, "ATT_BANKRIGHT");
	lua_pushnumber (L, THGROUP_ATT_RIGHT);     lua_setfield (L, -2, "ATT_RIGHT");
	lua_pushnumber (L, THGROUP_ATT_LEFT);      lua_setfield (L, -2, "ATT_LEFT");
	lua_pushnumber (L, THGROUP_ATT_UP);        lua_setfield (L, -2, "ATT_UP");
	lua_pushnumber (L, THGROUP_ATT_DOWN);      lua_setfield (L, -2, "ATT_DOWN");
	lua_pushnumber (L, THGROUP_ATT_FORWARD);   lua_setfield (L, -2, "ATT_FORWARD");
	lua_pushnumber (L, THGROUP_ATT_BACK);      lua_setfield (L, -2, "ATT_BACK");
	lua_setglobal (L, "THGROUP");

	// store navmode identifiers in global NAVMODE table
	lua_createtable (L, 0, 7);
	lua_pushnumber (L, NAVMODE_KILLROT);       lua_setfield (L, -2, "KILLROT");
	lua_pushnumber (L, NAVMODE_HLEVEL);        lua_setfield (L, -2, "HLEVEL");
	lua_pushnumber (L, NAVMODE_PROGRADE);      lua_setfield (L, -2, "PROGRADE");
	lua_pushnumber (L, NAVMODE_RETROGRADE);    lua_setfield (L, -2, "RETROGRADE");
	lua_pushnumber (L, NAVMODE_NORMAL);        lua_setfield (L, -2, "NORMAL");
	lua_pushnumber (L, NAVMODE_ANTINORMAL);    lua_setfield (L, -2, "ANTINORMAL");
	lua_pushnumber (L, NAVMODE_HOLDALT);       lua_setfield (L, -2, "HOLDALT");
	lua_setglobal (L, "NAVMODE");

	// store RCS mode identifiers in global RCSMODE table
	lua_createtable (L, 0, 3);
	lua_pushnumber (L, RCS_NONE);              lua_setfield (L, -2, "OFF");
	lua_pushnumber (L, RCS_ROT);               lua_setfield (L, -2, "ROT");
	lua_pushnumber (L, RCS_LIN);               lua_setfield (L, -2, "LIN");
	lua_setglobal (L, "RCSMODE");

	// store aerodynamic control surface mode identifiers in global ADCMODE table
	lua_createtable (L, 0, 5);
	lua_pushnumber (L, 0);                     lua_setfield (L, -2, "OFF");
	lua_pushnumber (L, 0x1);                   lua_setfield (L, -2, "ELEVATOR");
	lua_pushnumber (L, 0x2);                   lua_setfield (L, -2, "RUDDER");
	lua_pushnumber (L, 0x4);                   lua_setfield (L, -2, "AILERON");
	lua_pushnumber (L, 0x7);                   lua_setfield (L, -2, "ON");
	lua_setglobal (L, "ADCMODE");

	// store control surface types in global AIRCTRL table
	lua_createtable (L, 0, 6);
	lua_pushnumber (L, AIRCTRL_ELEVATOR);      lua_setfield (L, -2, "ELEVATOR");
	lua_pushnumber (L, AIRCTRL_RUDDER);        lua_setfield (L, -2, "RUDDER");
	lua_pushnumber (L, AIRCTRL_AILERON);       lua_setfield (L, -2, "AILERON");
	lua_pushnumber (L, AIRCTRL_FLAP);          lua_setfield (L, -2, "FLAP");
	lua_pushnumber (L, AIRCTRL_ELEVATORTRIM);  lua_setfield (L, -2, "ELEVATORTRIM");
	lua_pushnumber (L, AIRCTRL_RUDDERTRIM);    lua_setfield (L, -2, "RUDDERTRIM");
	lua_setglobal (L, "AIRCTRL");

	// store control surface axis orientations in global AIRCTRL_AXIS table
	lua_createtable (L, 0, 5);
	lua_pushnumber (L, AIRCTRL_AXIS_AUTO);     lua_setfield (L, -2, "AUTO");
	lua_pushnumber (L, AIRCTRL_AXIS_YPOS);     lua_setfield (L, -2, "YPOS");
	lua_pushnumber (L, AIRCTRL_AXIS_YNEG);     lua_setfield (L, -2, "YNEG");
	lua_pushnumber (L, AIRCTRL_AXIS_XPOS);     lua_setfield (L, -2, "XPOS");
	lua_pushnumber (L, AIRCTRL_AXIS_XNEG);     lua_setfield (L, -2, "XNEG");
	lua_setglobal (L, "AIRCTRL_AXIS");

	// store airfoil orientation types in global LIFT table
	lua_createtable (L, 0, 2);
	lua_pushnumber (L, LIFT_VERTICAL);         lua_setfield (L, -2, "VERTICAL");
	lua_pushnumber (L, LIFT_HORIZONTAL);       lua_setfield (L, -2, "HORIZONTAL");
	lua_setglobal (L, "LIFT");

	// store vessel propagation modes in global PROP table
	lua_createtable (L, 0, 7);
	lua_pushnumber (L, PROP_ORBITAL_ELEMENTS);   lua_setfield (L, -2, "ORBITAL_ELEMENTS");
	lua_pushnumber (L, PROP_ORBITAL_FIXEDSTATE); lua_setfield (L, -2, "ORBITAL_FIXEDSTATE");
	lua_pushnumber (L, PROP_ORBITAL_FIXEDSURF);  lua_setfield (L, -2, "ORBITAL_FIXEDSURF");
	lua_pushnumber (L, PROP_SORBITAL_ELEMENTS);  lua_setfield (L, -2, "SORBITAL_ELEMENTS");
	lua_pushnumber (L, PROP_SORBITAL_FIXEDSTATE);lua_setfield (L, -2, "SORBITAL_FIXEDSTATE");
	lua_pushnumber (L, PROP_SORBITAL_FIXEDSURF); lua_setfield (L, -2, "SORBITAL_FIXEDSURF");
	lua_pushnumber (L, PROP_SORBITAL_DESTROY);   lua_setfield (L, -2, "SORBITAL_DESTROY");
	lua_setglobal (L, "PROP");

	// store navigation radio transmitter types in global TRANSMITTER table
	lua_createtable (L, 0, 6);
	lua_pushnumber (L, TRANSMITTER_NONE);   lua_setfield (L, -2, "NONE");
	lua_pushnumber (L, TRANSMITTER_VOR);    lua_setfield (L, -2, "VOR");
	lua_pushnumber (L, TRANSMITTER_VTOL);   lua_setfield (L, -2, "VTOL");
	lua_pushnumber (L, TRANSMITTER_ILS);    lua_setfield (L, -2, "ILS");
	lua_pushnumber (L, TRANSMITTER_IDS);    lua_setfield (L, -2, "IDS");
	lua_pushnumber (L, TRANSMITTER_XPDR);   lua_setfield (L, -2, "XPDR");
	lua_setglobal (L, "TRANSMITTER");

	// store particle stream identifiers in global PARTICLE table
	lua_createtable(L, 0, 10);
	lua_pushnumber (L, PARTICLESTREAMSPEC::EMISSIVE);  lua_setfield(L,-2,"EMISSIVE");
	lua_pushnumber (L, PARTICLESTREAMSPEC::DIFFUSE);   lua_setfield(L,-2,"DIFFUSE");
	lua_pushnumber (L, PARTICLESTREAMSPEC::LVL_FLAT);  lua_setfield(L,-2,"LVL_FLAT");
	lua_pushnumber (L, PARTICLESTREAMSPEC::LVL_LIN);   lua_setfield(L,-2,"LVL_LIN");
	lua_pushnumber (L, PARTICLESTREAMSPEC::LVL_SQRT);  lua_setfield(L,-2,"LVL_SQRT");
	lua_pushnumber (L, PARTICLESTREAMSPEC::LVL_PLIN);  lua_setfield(L,-2,"LVL_PLIN");
	lua_pushnumber (L, PARTICLESTREAMSPEC::LVL_PSQRT); lua_setfield(L,-2,"LVL_PSQRT");
	lua_pushnumber (L, PARTICLESTREAMSPEC::ATM_FLAT);  lua_setfield(L,-2,"ATM_FLAT");
	lua_pushnumber (L, PARTICLESTREAMSPEC::ATM_PLIN);  lua_setfield(L,-2,"ATM_PLIN");
	lua_pushnumber (L, PARTICLESTREAMSPEC::ATM_PLOG);  lua_setfield(L,-2,"ATM_PLOG");
	lua_setglobal (L, "PARTICLE");

	// some useful global constants
	lua_pushnumber (L, 0); lua_setfield (L, LUA_GLOBALSINDEX, "CLOSE");
	lua_pushnumber (L, 1); lua_setfield (L, LUA_GLOBALSINDEX, "OPEN");
	lua_pushnumber (L, 2); lua_setfield (L, LUA_GLOBALSINDEX, "UP");
	lua_pushnumber (L, 3); lua_setfield (L, LUA_GLOBALSINDEX, "DOWN");
	lua_pushnumber (L, ALLDOCKS); lua_setfield (L, LUA_GLOBALSINDEX, "ALLDOCKS");

	// predefined help contexts
	lua_pushstring (L, "intro.htm"); lua_setfield (L, LUA_GLOBALSINDEX, "orbiter");
	lua_pushstring (L, "script/ScriptRef.htm"); lua_setfield (L, LUA_GLOBALSINDEX, "api");
}

void Interpreter::LoadMFDAPI ()
{
	static const struct luaL_reg mfdLib[] = {
		{"get_size", mfd_get_size},
		{"set_title", mfd_set_title},
		{"get_defaultpen", mfd_get_defaultpen},
		{"get_defaultfont", mfd_get_defaultfont},
		{"invalidate_display", mfd_invalidate_display},
		{"invalidate_buttons", mfd_invalidate_buttons},
		{NULL, NULL}
	};

	luaL_newmetatable (L, "MFD.vtable");
	lua_pushstring (L, "__index");
	lua_pushvalue (L, -2); // push metatable
	lua_settable (L, -3);  // metatable.__index = metatable
	luaL_openlib (L, NULL, mfdLib, 0);
}

void Interpreter::LoadLightEmitterMethods ()
{
	static const struct luaL_reg methodLib[] = {
		{"get_position", le_get_position},
		{"set_position", le_set_position},
		{"get_direction", le_get_direction},
		{"set_direction", le_set_direction},
		{"get_intensity", le_get_intensity},
		{"set_intensity", le_set_intensity},
		{"get_range", le_get_range},
		{"set_range", le_set_range},
		{"get_attenuation", le_get_attenuation},
		{"set_attenuation", le_set_attenuation},
		{"get_spotaperture", le_get_spotaperture},
		{"set_spotaperture", le_set_spotaperture},
		{"activate", le_activate},
		{"is_active", le_is_active},
		{NULL, NULL}
	};

	luaL_newmetatable (L, "LightEmitter.vtable");
	lua_pushstring (L, "__index");
	lua_pushvalue (L, -2); // push metatable
	lua_settable (L, -3); // metatable.__index = metatable
	luaL_openlib (L, NULL, methodLib, 0);
}

void Interpreter::LoadSketchpadAPI ()
{
	static const struct luaL_reg skpLib[] = {
		{"text", skp_text},
		{"moveto", skp_moveto},
		{"lineto", skp_lineto},
		{"line", skp_line},
		{"rectangle", skp_rectangle},
		{"ellipse", skp_ellipse},
		{"polygon", skp_polygon},
		{"polyline", skp_polyline},
		{"set_origin", skp_set_origin},
		{"set_textalign", skp_set_textalign},
		{"set_textcolor", skp_set_textcolor},
		{"set_backgroundcolor", skp_set_backgroundcolor},
		{"set_backgroundmode", skp_set_backgroundmode},
		{"set_pen", skp_set_pen},
		{"set_font", skp_set_font},
		{"get_charsize", skp_get_charsize},
		{"get_textwidth", skp_get_textwidth},
		{NULL, NULL}
	};

	luaL_newmetatable (L, "SKP.vtable");
	lua_pushstring (L, "__index");
	lua_pushvalue (L, -2); // push metatable
	lua_settable (L, -3); // metatable.__index = metatable
	luaL_openlib (L, NULL, skpLib, 0);

	lua_createtable (L, 0, 8);
	lua_pushnumber (L, oapi::Sketchpad::BK_OPAQUE);      lua_setfield (L, -2, "OPAQUE");
	lua_pushnumber (L, oapi::Sketchpad::BK_TRANSPARENT); lua_setfield (L, -2, "TRANSPARENT");
	lua_pushnumber (L, oapi::Sketchpad::LEFT);           lua_setfield (L, -2, "LEFT");
	lua_pushnumber (L, oapi::Sketchpad::CENTER);         lua_setfield (L, -2, "CENTER");
	lua_pushnumber (L, oapi::Sketchpad::RIGHT);          lua_setfield (L, -2, "RIGHT");
	lua_pushnumber (L, oapi::Sketchpad::TOP);            lua_setfield (L, -2, "TOP");
	lua_pushnumber (L, oapi::Sketchpad::BASELINE);       lua_setfield (L, -2, "BASELINE");
	lua_pushnumber (L, oapi::Sketchpad::BOTTOM);         lua_setfield (L, -2, "BOTTOM");
	lua_setglobal (L, "SKP");
}

void Interpreter::LoadAnnotationAPI ()
{
	static const struct luaL_reg noteMtd[] = {
		{"set_text", noteSetText},
		{"set_pos", noteSetPos},
		{"set_size", noteSetSize},
		{"set_colour", noteSetColour},
		{NULL, NULL}
	};
	luaL_newmetatable (L, "NOTE.table");
	lua_pushstring (L, "__index");
	lua_pushvalue (L, -2); // push metatable
	lua_settable (L, -3);  // metatable.__index = metatable
	luaL_openlib (L, NULL, noteMtd, 0);
}

void Interpreter::LoadStartupScript ()
{
	luaL_dofile (L, "Script\\oapi_init.lua");
}

bool Interpreter::InitialiseVessel (lua_State *L, VESSEL *v)
{
	if (v->Version() < 2) return false;
	VESSEL3 *v3 = (VESSEL3*)v;
	return (v3->clbkGeneric (VMSG_LUAINTERPRETER, 0, (void*)L) != 0);
}

bool Interpreter::LoadVesselExtensions (lua_State *L, VESSEL *v)
{
	if (v->Version() < 2) return false;
	VESSEL3 *v3 = (VESSEL3*)v;
	return (v3->clbkGeneric (VMSG_LUAINSTANCE, 0, (void*)L) != 0);
}

Interpreter *Interpreter::GetInterpreter (lua_State *L)
{
	lua_getfield (L, LUA_REGISTRYINDEX, "interp");
	Interpreter *interp = (Interpreter*)lua_touserdata (L, -1);
	lua_pop (L, 1);
	return interp;
}

void Interpreter::term_echo (lua_State *L, int level)
{
	if (is_term && term_verbose >= level) term_out (L);
}

void Interpreter::term_strout (lua_State *L, const char *str, bool iserr)
{
	Interpreter *interp = GetInterpreter(L);
	interp->term_strout (str, iserr);
}

// ============================================================================

int Interpreter::AssertPrmtp(lua_State *L, const char *fname, int idx, int prm, int tp)
{
	static char *tpname[] = {
		"number",
		"vector",
		"string",
		"handle",
		"table"
	};
	char cbuf[1024];
	int res = 1;
	switch (tp) {
	case PRMTP_NUMBER:
		res = lua_isnumber(L,idx);
		break;
	case PRMTP_VECTOR:
		res = lua_isvector(L,idx);
		break;
	case PRMTP_STRING:
		res = lua_isstring(L,idx);
		break;
	case PRMTP_LIGHTUSERDATA:
		res = lua_islightuserdata(L,idx);
		break;
	case PRMTP_TABLE:
		res = lua_istable(L,idx);
		break;
	}
	if (!res) {
		sprintf (cbuf, "%s: argument %d: invalid type (expected %s)", fname, prm, tpname[tp]);
		term_strout(L,cbuf);
	}
	return res;
}

// ============================================================================
// global functions

int Interpreter::help (lua_State *L)
{
	Interpreter *interp = GetInterpreter (L);
	int narg = lua_gettop (L);

	if (!narg) {
		if (!interp->is_term) return 0; // no terminal help without terminal - sorry
		static const int nline = 10;
		static char *stdhelp[nline] = {
			"Orbiter script interpreter",
			"Based on Lua script language (" LUA_RELEASE ")",
			"  " LUA_COPYRIGHT,
			"  " LUA_AUTHORS,
			"For general orbiter-related help,",
			"  type: help(orbiter).",
			"For Orbiter-specific script extensions",
			"  type: help(api).",
			"For general help on the Lua language,",
			"  see the resources at www.lua.org."
		};
		for (int i = 0; i < nline; i++) {
			interp->term_strout (stdhelp[i]);
		}
	} else if (lua_isstring (L,1)) {
		// call a help page from the main Orbiter help file
		char topic[256];
		strncpy (topic, lua_tostring (L, 1), 255); lua_pop (L, 1);
		lua_pushstring (L, "html/orbiter.chm");
		lua_pushstring (L, topic);
		interp->oapiOpenHelp (L);
	} else if (lua_istable (L,1)) {
		// call a help page from an external help file
		char file[256], topic[256];
		lua_getfield (L, 1, "file");
		lua_getfield (L, 1, "topic");
		strcpy (file, lua_tostring(L,-2));
		if (!lua_isnil(L,-1))
			strcpy (topic, lua_tostring(L,-1));
		else topic[0] = '\0';
		lua_settop (L, 0);
		lua_pushstring (L, file);
		if (topic[0])
			lua_pushstring (L, topic);
		interp->oapiOpenHelp (L);
	}

	return 0;
}

int Interpreter::oapiOpenHelp (lua_State *L)
{
	static char fname[256], topic[256];
	static HELPCONTEXT hc = {fname, 0, 0, 0};

	Interpreter *interp = GetInterpreter (L);
	int narg = lua_gettop(L);
	if (narg) {
		strncpy (fname, lua_tostring (L,1), 255);
		if (narg > 1) {
			strncpy (topic, lua_tostring (L,2), 255);
			hc.topic = topic;
		} else
			hc.topic = 0;
		interp->postfunc = OpenHelp;
		interp->postcontext = &hc;
	}
	return 0;
}

int Interpreter::help_api (lua_State *L)
{
	lua_getglobal (L, "oapi");
	lua_getfield (L, -1, "open_help");
	lua_pushstring (L, "Html/Script/API/Reference.chm");
	lua_pcall (L, 1, 0, 0);
	return 0;
}

// ============================================================================
// vector library functions

int Interpreter::vec_set (lua_State *L)
{
	int i;
	VECTOR3 v;
	for (i = 0; i < 3; i++) {
		ASSERT_SYNTAX(lua_isnumber(L,i+1), "expected three numeric arguments");
		v.data[i] = lua_tonumber(L,i+1);
	}
	lua_pushvector(L,v);
	return 1;
}

int Interpreter::vec_add (lua_State *L)
{
	VECTOR3 va, vb;
	double fa, fb;
	if (lua_isvector(L,1)) {
		va = lua_tovector (L,1);
		if (lua_isvector(L,2)) {
			vb = lua_tovector (L,2);
			lua_pushvector (L, va+vb);
		} else {
			ASSERT_SYNTAX (lua_isnumber(L,2), "Argument 2: expected vector or number");
			fb = lua_tonumber (L,2);
			lua_pushvector (L, _V(va.x+fb, va.y+fb, va.z+fb));
		}
	} else {
		ASSERT_SYNTAX (lua_isnumber(L,1), "Argument 1: expected vector or number");
		fa = lua_tonumber (L,1);
		if (lua_isvector (L,2)) {
			vb = lua_tovector (L,2);
			lua_pushvector (L, _V(fa+vb.x, fa+vb.y, fa+vb.z));
		} else {
			ASSERT_SYNTAX (lua_isnumber(L,2), "Argument 2: expected vector or number");
			fb = lua_tonumber (L,2);
			lua_pushnumber (L, fa+fb);
		}
	}
	return 1;
}

int Interpreter::vec_sub (lua_State *L)
{
	VECTOR3 va, vb;
	double fa, fb;
	if (lua_isvector(L,1)) {
		va = lua_tovector (L,1);
		if (lua_isvector(L,2)) {
			vb = lua_tovector (L,2);
			lua_pushvector (L, va-vb);
		} else {
			ASSERT_SYNTAX (lua_isnumber(L,2), "Argument 2: expected vector or number");
			fb = lua_tonumber (L,2);
			lua_pushvector (L, _V(va.x-fb, va.y-fb, va.z-fb));
		}
	} else {
		ASSERT_SYNTAX (lua_isnumber(L,1), "Argument 1: expected vector or number");
		fa = lua_tonumber (L,1);
		if (lua_isvector (L,2)) {
			vb = lua_tovector (L,2);
			lua_pushvector (L, _V(fa-vb.x, fa-vb.y, fa-vb.z));
		} else {
			ASSERT_SYNTAX (lua_isnumber(L,2), "Argument 2: expected vector or number");
			fb = lua_tonumber (L,2);
			lua_pushnumber (L, fa-fb);
		}
	}
	return 1;
}

int Interpreter::vec_mul (lua_State *L)
{
	VECTOR3 v1, v2, res;
	double f1, f2;
	int i;
	if (lua_isvector(L,1)) {
		v1 = lua_tovector(L,1);
		if (lua_isvector(L,2)) {
			v2 = lua_tovector(L,2);
			for (i = 0; i < 3; i++) res.data[i] = v1.data[i]*v2.data[i];
		} else {
			ASSERT_SYNTAX (lua_isnumber(L,2), "Argument 2: expected vector or number");
			f2 = lua_tonumber(L,2);
			for (i = 0; i < 3; i++) res.data[i] = v1.data[i]*f2;
		}
	} else {
		ASSERT_SYNTAX (lua_isnumber(L,1), "Argument 1: expected vector or number");
		f1 = lua_tonumber(L,1);
		if (lua_isvector(L,2)) {
			v2 = lua_tovector(L,2);
			for (i = 0; i < 3; i++) res.data[i] = f1*v2.data[i];
		} else {
			ASSERT_SYNTAX (lua_isnumber(L,2), "Argument 2: expected vector or number");
			f2 = lua_tonumber(L,2);
			lua_pushnumber (L,f1*f2);
			return 1;
		}
	}
	lua_pushvector(L,res);
	return 1;
}

int Interpreter::vec_div (lua_State *L)
{
	VECTOR3 v1, v2, res;
	double f1, f2;
	int i;
	if (lua_isvector(L,1)) {
		v1 = lua_tovector(L,1);
		if (lua_isvector(L,2)) {
			v2 = lua_tovector(L,2);
			for (i = 0; i < 3; i++) res.data[i] = v1.data[i]/v2.data[i];
		} else {
			ASSERT_SYNTAX (lua_isnumber(L,2), "Argument 2: expected vector or number");
			f2 = lua_tonumber(L,2);
			for (i = 0; i < 3; i++) res.data[i] = v1.data[i]/f2;
		}
	} else {
		ASSERT_SYNTAX (lua_isnumber(L,1), "Argument 1: expected vector or number");
		f1 = lua_tonumber(L,1);
		if (lua_isvector(L,2)) {
			v2 = lua_tovector(L,2);
			for (i = 0; i < 3; i++) res.data[i] = f1/v2.data[i];
		} else {
			ASSERT_SYNTAX (lua_isnumber(L,2), "Argument 2: expected vector or number");
			f2 = lua_tonumber(L,2);
			lua_pushnumber(L,f1/f2);
			return 1;
		}
	}
	lua_pushvector(L,res);
	return 1;
}

int Interpreter::vec_dotp (lua_State *L)
{
	VECTOR3 v1, v2;
	ASSERT_SYNTAX(lua_isvector(L,1), "Argument 1: expected vector");
	v1 = lua_tovector(L,1);
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 2: expected vector");
	v2 = lua_tovector(L,2);
	lua_pushnumber (L, dotp(v1,v2));
	return 1;
}

int Interpreter::vec_crossp (lua_State *L)
{
	VECTOR3 v1, v2;
	ASSERT_SYNTAX(lua_isvector(L,1), "Argument 1: expected vector");
	v1 = lua_tovector(L,1);
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 2: expected vector");
	v2 = lua_tovector(L,2);
	lua_pushvector (L, crossp(v1,v2));
	return 1;
}

int Interpreter::vec_length (lua_State *L)
{
	VECTOR3 v;
	ASSERT_SYNTAX(lua_isvector(L,1), "Argument 1: expected vector");
	v = lua_tovector(L,1);
	lua_pushnumber (L, length(v));
	return 1;
}

int Interpreter::vec_dist (lua_State *L)
{
	VECTOR3 v1, v2;
	ASSERT_SYNTAX(lua_isvector(L,1), "Argument 1: expected vector");
	v1 = lua_tovector(L,1);
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 2: expected vector");
	v2 = lua_tovector(L,2);
	lua_pushnumber (L, dist(v1,v2));
	return 1;
}

int Interpreter::vec_unit (lua_State *L)
{
	VECTOR3 v;
	ASSERT_SYNTAX(lua_isvector(L,1), "Argument 1: expected vector");
	v = lua_tovector(L,1);
	lua_pushvector (L, unit(v));
	return 1;
}

int Interpreter::mat_identity (lua_State *L)
{
	lua_pushmatrix (L,identity());
	return 1;
}

int Interpreter::mat_mul (lua_State *L)
{
	ASSERT_SYNTAX(lua_ismatrix(L,1), "Argument 1: expected matrix");
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 2: expected vector");
	lua_pushvector (L, mul (lua_tomatrix(L,1), lua_tovector(L,2)));
	return 1;
}

int Interpreter::mat_tmul (lua_State *L)
{
	ASSERT_SYNTAX(lua_ismatrix(L,1), "Argument 1: expected matrix");
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 2: expected vector");
	lua_pushvector (L, tmul (lua_tomatrix(L,1), lua_tovector(L,2)));
	return 1;
}

int Interpreter::mat_mmul (lua_State *L)
{
	ASSERT_SYNTAX(lua_ismatrix(L,1), "Argument 1: expected matrix");
	ASSERT_SYNTAX(lua_ismatrix(L,2), "Argument 2: expected matrix");
	lua_pushmatrix (L, mul(lua_tomatrix(L,1), lua_tomatrix(L,2)));
	return 1;
}

// ============================================================================
// process library functions

int Interpreter::procFrameskip (lua_State *L)
{
	// return control to the orbiter core for execution of one time step
	// This should be called in the loop of any "wait"-type function

	Interpreter *interp = GetInterpreter(L);
	interp->frameskip (L);
	return 0;
}

// ============================================================================
// oapi library functions

int Interpreter::oapi_get_simtime (lua_State *L)
{
	lua_pushnumber (L, oapiGetSimTime());
	return 1;
}

int Interpreter::oapi_get_simstep (lua_State *L)
{
	lua_pushnumber (L, oapiGetSimStep());
	return 1;
}

int Interpreter::oapi_get_systime (lua_State *L)
{
	lua_pushnumber (L, oapiGetSysTime());
	return 1;
}

int Interpreter::oapi_get_sysstep (lua_State *L)
{
	lua_pushnumber (L, oapiGetSysStep());
	return 1;
}

int Interpreter::oapi_get_simmjd (lua_State *L)
{
	lua_pushnumber (L, oapiGetSimMJD());
	return 1;
}

int Interpreter::oapi_set_simmjd (lua_State *L)
{
	ASSERT_SYNTAX (lua_isnumber (L,1), "Argument 1: invalid type (expected number)");
	double mjd = lua_tonumber (L,1);
	int pmode = 0;
	if (lua_gettop (L) >= 2) {
		ASSERT_SYNTAX (lua_isnumber (L,2), "Argument 2: invalid type (expected number)");
		pmode = (int)(lua_tonumber (L,2)+0.5);
	}
	oapiSetSimMJD (mjd, pmode);
	return 0;
}

int Interpreter::oapi_get_sysmjd (lua_State *L)
{
	lua_pushnumber (L, oapiGetSysMJD());
	return 1;
}

int Interpreter::oapi_time2mjd (lua_State *L)
{
	ASSERT_SYNTAX (lua_isnumber (L,1), "Argument 1: invalid type (expected number)");
	double simt = lua_tonumber(L,1);
	double mjd = oapiTime2MJD(simt);
	lua_pushnumber (L, mjd);
	return 1;
}

int Interpreter::oapi_get_tacc (lua_State *L)
{
	lua_pushnumber (L, oapiGetTimeAcceleration());
	return 1;
}

int Interpreter::oapi_set_tacc (lua_State *L)
{
	double warp = lua_tonumber (L, -1);
	oapiSetTimeAcceleration (warp);
	return 0;
}

int Interpreter::oapi_get_pause (lua_State *L)
{
	lua_pushboolean (L, oapiGetPause() ? 1:0);
	return 1;
}

int Interpreter::oapi_set_pause (lua_State *L)
{
	oapiSetPause (lua_toboolean (L, -1) != 0);
	return 0;
}

int Interpreter::oapiGetObjectHandle (lua_State *L)
{
	OBJHANDLE hObj;
	if (lua_isnumber (L, 1)) { // select by index
		int idx = (int)lua_tointeger (L, 1);
		hObj = oapiGetObjectByIndex (idx);
	} else {
		char *name = (char*)luaL_checkstring (L, 1);
		hObj = oapiGetObjectByName (name);
	}
	if (hObj) lua_pushlightuserdata (L, hObj);
	else lua_pushnil (L);
	return 1;
}

int Interpreter::oapiGetObjectCount (lua_State *L)
{
	lua_pushinteger (L, ::oapiGetObjectCount());
	return 1;
}

int Interpreter::oapiGetObjectName (lua_State *L)
{
	OBJHANDLE hObj;
	if (lua_islightuserdata (L, 1) && (hObj = lua_toObject (L, 1))) {
		char name[1024];
		::oapiGetObjectName (hObj, name, 1024);
		lua_pushstring (L, name);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_mainmenuvisibilitymode (lua_State *L)
{
	lua_pushnumber (L, oapiGetMainMenuVisibilityMode());
	return 1;
}

int Interpreter::oapi_set_mainmenuvisibilitymode (lua_State *L)
{
	ASSERT_SYNTAX (lua_isnumber (L,1), "Argument 1: invalid type (expected number)");
	DWORD mode = (DWORD)lua_tonumber (L,1);
	ASSERT_SYNTAX (mode <= 2, "Argument 1: out of range");
	oapiSetMainMenuVisibilityMode (mode);
	return 0;
}

int Interpreter::oapi_get_maininfovisibilitymode (lua_State *L)
{
	lua_pushnumber (L, oapiGetMainInfoVisibilityMode());
	return 1;
}

int Interpreter::oapi_set_maininfovisibilitymode (lua_State *L)
{
	ASSERT_SYNTAX (lua_isnumber (L,1), "Argument 1: invalid type (expected number)");
	DWORD mode = (DWORD)lua_tonumber (L,1);
	ASSERT_SYNTAX (mode <= 2, "Argument 1: out of range");
	oapiSetMainInfoVisibilityMode (mode);
	return 0;
}

int Interpreter::oapiCreateAnnotation (lua_State *L)
{
	NOTEHANDLE *pnote = (NOTEHANDLE*)lua_newuserdata (L, sizeof(NOTEHANDLE));
	*pnote = ::oapiCreateAnnotation (true, 1.0, _V(1,0.8,0.6));
	oapiAnnotationSetPos (*pnote, 0.03, 0.2, 0.4, 0.4);

	luaL_getmetatable (L, "NOTE.table");   // push metatable
	lua_setmetatable (L, -2);              // set metatable for annotation objects
	return 1;
}

int Interpreter::oapiDelAnnotation (lua_State *L)
{
	NOTEHANDLE *pnote = (NOTEHANDLE*)lua_touserdata (L, 1);
	::oapiDelAnnotation (*pnote);
	*pnote = NULL;
	return 0;
}

int Interpreter::oapiDbgOut (lua_State *L)
{
	const char *str = lua_tostringex (L, 1);
	strcpy (oapiDebugString(), str);
	return 0;
}

static bool bInputClosed;
static char cInput[1024];

bool inputClbk (void *id, char *str, void *usrdata)
{
	strncpy (cInput, str, 1024);
	bInputClosed = true;
	return true;
}

bool inputCancel (void *id, char *str, void *usrdata)
{
	cInput[0] = '\0';
	bInputClosed = true;
	return true;
}

int Interpreter::oapiOpenInputBox (lua_State *L)
{
	const char *title = lua_tostring (L, 1);
	int vislen = lua_tointeger (L, 2);
	bInputClosed = false;
	oapiOpenInputBoxEx (title, inputClbk, inputCancel, 0, 40, 0, USRINPUT_NEEDANSWER);
	return 0;
}

int Interpreter::oapiReceiveInput (lua_State *L)
{
	if (bInputClosed)
		lua_pushstring (L, cInput);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_global_to_equ (lua_State *L)
{
	OBJHANDLE hObj;
	if (lua_islightuserdata (L,1) && (hObj = lua_toObject (L,1))) {
		VECTOR3 glob = lua_tovector(L,2);
		double lng, lat, rad;
		oapiGlobalToEqu (hObj, glob, &lng, &lat, &rad);
		lua_createtable (L, 0, 3);
		lua_pushnumber (L, lng);
		lua_setfield (L, -2, "lng");
		lua_pushnumber (L, lat);
		lua_setfield (L, -2, "lat");
		lua_pushnumber (L, rad);
		lua_setfield (L, -2, "rad");
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_equ_to_global (lua_State *L)
{
	OBJHANDLE hObj;
	double lng, lat, rad;
	VECTOR3 glob;
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	ASSERT_SYNTAX (lua_istable (L,2), "Argument 2: invalid type (expected table)");
	lua_getfield (L,2,"lng");
	ASSERT_SYNTAX (lua_isnumber (L,-1), "Argument 2: missing field 'lng'");
	lng = (double)lua_tonumber (L,-1); lua_pop (L,1);
	lua_getfield (L,2,"lat");
	ASSERT_SYNTAX (lua_isnumber (L,-1), "Argument 2: missing field 'lat'");
	lat = (double)lua_tonumber (L,-1); lua_pop (L,1);
	lua_getfield (L,2,"rad");
	ASSERT_SYNTAX (lua_isnumber (L,-1), "Argument 2: missing field 'rad'");
	rad = (double)lua_tonumber (L,-1); lua_pop (L,1);

	oapiEquToGlobal (hObj, lng, lat, rad, &glob);
	lua_pushvector (L, glob);
	return 1;
}

int Interpreter::oapi_orthodome (lua_State *L)
{
	double lng1, lat1, lng2, lat2, alpha;
	ASSERT_SYNTAX (lua_gettop (L) >= 2, "Too few arguments");
	ASSERT_SYNTAX (lua_istable (L,1), "Argument 1: invalid type (expected table)");
	ASSERT_SYNTAX (lua_istable (L,2), "Argument 2: invalid type (expected table)");
	
	lua_getfield (L, 1, "lng");
	ASSERT_SYNTAX (lua_isnumber (L,-1), "Argument 1: missing field 'lng'");
	lng1 = (double)lua_tonumber (L,-1); lua_pop (L,1);
	lua_getfield (L, 1, "lat");
	ASSERT_SYNTAX (lua_isnumber (L,-1), "Argument 1: missing field 'lat'");
	lat1 = (double)lua_tonumber (L,-1); lua_pop (L,1);

	lua_getfield (L, 2, "lng");
	ASSERT_SYNTAX (lua_isnumber (L,-1), "Argument 2: missing field 'lng'");
	lng2 = (double)lua_tonumber (L,-1); lua_pop (L,1);
	lua_getfield (L, 2, "lat");
	ASSERT_SYNTAX (lua_isnumber (L,-1), "Argument 2: missing field 'lat'");
	lat2 = (double)lua_tonumber (L,-1); lua_pop (L,1);

	alpha = oapiOrthodome (lng1, lat1, lng2, lat2);
	lua_pushnumber (L, alpha);
	return 1;
}

int Interpreter::oapi_del_vessel (lua_State *L)
{
	OBJHANDLE hObj;
	if (lua_islightuserdata (L,1) && (hObj = lua_toObject (L,1))) {
		oapiDeleteVessel (hObj);
	} else if (lua_isstring (L,1)) {
		const char *name = lua_tostring (L,1);
		if (hObj = oapiGetVesselByName ((char*)name))
			oapiDeleteVessel (hObj);
	}
	return 0;
}

int Interpreter::oapi_get_size (lua_State *L)
{
	OBJHANDLE hObj;
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	ASSERT_SYNTAX(lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(hObj = lua_toObject (L,1), "Argument 1: invalid object");
	lua_pushnumber (L, oapiGetSize(hObj));
	return 1;
}

int Interpreter::oapi_get_mass (lua_State *L)
{
	OBJHANDLE hObj;
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	lua_pushnumber (L, oapiGetMass (hObj));
	return 1;
}

int Interpreter::oapi_get_globalpos (lua_State *L)
{
	VECTOR3 pos;
	if (lua_gettop(L) < 1) {
		oapiGetFocusGlobalPos (&pos);
	} else {
		OBJHANDLE hObj;
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
		oapiGetGlobalPos (hObj, &pos);
	}
	lua_pushvector (L, pos);
	return 1;
}

int Interpreter::oapi_get_globalvel (lua_State *L)
{
	VECTOR3 vel;
	if (lua_gettop(L) < 1) {
		oapiGetFocusGlobalVel (&vel);
	} else {
		OBJHANDLE hObj;
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
		oapiGetGlobalVel (hObj, &vel);
	}
	lua_pushvector (L, vel);
	return 1;
}

int Interpreter::oapi_get_relativepos (lua_State *L)
{
	OBJHANDLE hObj, hRef;
	VECTOR3 pos;
	int narg = min(lua_gettop(L),2);
	ASSERT_SYNTAX (lua_islightuserdata (L,narg), "Argument 2: invalid type (expected handle)");
	ASSERT_SYNTAX (hRef = lua_toObject (L,narg), "Argument 2: invalid object");
	if (narg > 1) {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
		oapiGetRelativePos (hObj, hRef, &pos);
	} else {
		oapiGetFocusRelativePos (hRef, &pos);
	}
	lua_pushvector (L, pos);
	return 1;
}

int Interpreter::oapi_get_relativevel (lua_State *L)
{
	OBJHANDLE hObj, hRef;
	VECTOR3 vel;
	int narg = min(lua_gettop(L),2);
	ASSERT_SYNTAX (lua_islightuserdata (L,narg), "Argument 2: invalid type (expected handle)");
	ASSERT_SYNTAX (hRef = lua_toObject (L,narg), "Argument 2: invalid object");
	if (narg > 1) {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
		oapiGetRelativeVel (hObj, hRef, &vel);
	} else {
		oapiGetFocusRelativeVel (hRef, &vel);
	}
	lua_pushvector (L, vel);
	return 1;
}

int Interpreter::oapi_get_propellanthandle (lua_State *L)
{
	OBJHANDLE hObj;
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	ASSERT_SYNTAX (lua_isnumber (L,2), "Argument 2: invalid type (expected number)");
	int idx = lua_tointeger (L,2);

	PROPELLANT_HANDLE hp = oapiGetPropellantHandle (hObj, idx);
	if (hp) lua_pushlightuserdata (L, hp);
	else    lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_propellantmass (lua_State *L)
{
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	PROPELLANT_HANDLE hp = (PROPELLANT_HANDLE)lua_touserdata (L, 1);
	ASSERT_SYNTAX(hp, "Argument 1: invalid object");
	lua_pushnumber (L, oapiGetPropellantMass (hp));
	return 1;
}

int Interpreter::oapi_get_propellantmaxmass (lua_State *L)
{
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	PROPELLANT_HANDLE hp = (PROPELLANT_HANDLE)lua_touserdata (L, 1);
	ASSERT_SYNTAX(hp, "Argument 1: invalid object");
	lua_pushnumber (L, oapiGetPropellantMaxMass (hp));
	return 1;
}

int Interpreter::oapi_get_fuelmass (lua_State *L)
{
	OBJHANDLE hObj;
	double fmass;
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	fmass = oapiGetFuelMass (hObj);
	lua_pushnumber (L, fmass);
	return 1;
}

int Interpreter::oapi_get_maxfuelmass (lua_State *L)
{
	OBJHANDLE hObj;
	double fmass;
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	fmass = oapiGetMaxFuelMass (hObj);
	lua_pushnumber (L, fmass);
	return 1;
}

int Interpreter::oapi_get_emptymass (lua_State *L)
{
	OBJHANDLE hObj;
	double emass;
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	emass = oapiGetEmptyMass (hObj);
	lua_pushnumber (L, emass);
	return 1;
}

int Interpreter::oapi_set_emptymass (lua_State *L)
{
	OBJHANDLE hObj;
	double emass;
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	ASSERT_SYNTAX (lua_isnumber (L,2), "Argument 2: invalid type (expected number)");
	emass = lua_tonumber(L,2);
	ASSERT_SYNTAX (emass >= 0, "Argument 2: value >= 0 required");
	oapiSetEmptyMass (hObj, emass);
	return 0;
}

int Interpreter::oapi_get_altitude (lua_State *L)
{
	OBJHANDLE hObj = oapiGetFocusObject ();
	AltitudeMode mode = ALTMODE_MEANRAD;
	int modeidx = 1;
	double alt;
	if (lua_gettop(L) >= 1) {
		if (lua_islightuserdata (L,1)) {
			ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
			modeidx++;
		}
	}
	if (lua_gettop(L) >= modeidx) {
		if (lua_isnumber(L,modeidx))
			mode = (AltitudeMode)(int)lua_tonumber(L,modeidx);
	}
	if (oapiGetAltitude (hObj, mode, &alt))
		lua_pushnumber (L, alt);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_pitch (lua_State *L)
{
	OBJHANDLE hObj;
	double pitch;
	if (lua_gettop(L) < 1) {
		hObj = oapiGetFocusObject ();
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	if (oapiGetPitch (hObj, &pitch))
		lua_pushnumber (L, pitch);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_bank (lua_State *L)
{
	OBJHANDLE hObj;
	double bank;
	if (lua_gettop(L) < 1) {
		hObj = oapiGetFocusObject ();
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	if (oapiGetBank (hObj, &bank))
		lua_pushnumber (L, bank);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_heading (lua_State *L)
{
	OBJHANDLE hObj;
	double heading;
	if (lua_gettop(L) < 1) {
		hObj = oapiGetFocusObject ();
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	if (oapiGetHeading (hObj, &heading))
		lua_pushnumber (L, heading);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_groundspeed (lua_State *L)
{
	OBJHANDLE hObj;
	double speed;
	if (lua_gettop(L) < 1) {
		hObj = oapiGetFocusObject ();
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	if (oapiGetGroundspeed (hObj, &speed))
		lua_pushnumber (L, speed);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_groundspeedvector (lua_State *L)
{
	OBJHANDLE hObj;
	VECTOR3 speedv;
	int idx = 2;
	if (lua_gettop(L) < 2) {
		hObj = oapiGetFocusObject ();
		idx = 1;
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	ASSERT_NUMBER(L,idx);
	REFFRAME frame = (REFFRAME)lua_tointeger (L, idx);
	if (oapiGetGroundspeedVector (hObj, frame, &speedv))
		lua_pushvector (L, speedv);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_airspeed (lua_State *L)
{
	OBJHANDLE hObj;
	double speed;
	if (lua_gettop(L) < 1) {
		hObj = oapiGetFocusObject ();
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	if (oapiGetAirspeed (hObj, &speed))
		lua_pushnumber (L, speed);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_airspeedvector (lua_State *L)
{
	OBJHANDLE hObj;
	VECTOR3 speedv;
	int idx = 2;
	if (lua_gettop(L) < 2) {
		hObj = oapiGetFocusObject ();
		idx = 1;
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	ASSERT_NUMBER(L,idx);
	REFFRAME frame = (REFFRAME)lua_tointeger (L, idx);
	if (oapiGetAirspeedVector (hObj, frame, &speedv))
		lua_pushvector (L, speedv);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_shipairspeedvector (lua_State *L)
{
	GetInterpreter(L)->term_strout (L, "Obsolete function used: oapi.get_shipairspeedvector.\nUse oapi.get_airspeedvector instead", true);
	OBJHANDLE hObj;
	VECTOR3 speedv;
	if (lua_gettop(L) < 1) {
		hObj = oapiGetFocusObject ();
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	if (oapiGetAirspeedVector(hObj, FRAME_LOCAL, &speedv))
		lua_pushvector (L, speedv);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_get_equpos (lua_State *L)
{
	OBJHANDLE hObj;
	double lng, lat, rad;
	if (lua_gettop(L) < 1) {
		hObj = oapiGetFocusObject ();
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	if (oapiGetEquPos (hObj, &lng, &lat, &rad)) {
		lua_createtable (L, 0, 3);
		lua_pushnumber (L, lng);
		lua_setfield (L, -2, "lng");
		lua_pushnumber (L, lat);
		lua_setfield (L, -2, "lat");
		lua_pushnumber (L, rad);
		lua_setfield (L, -2, "rad");
	} else {
		lua_pushnil (L);
	}
	return 1;
}

int Interpreter::oapi_get_atm (lua_State *L)
{
	OBJHANDLE hObj;
	ATMPARAM prm;
	if (lua_gettop(L) < 1) {
		hObj = 0;
	} else {
		ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
		ASSERT_SYNTAX (hObj = lua_toObject (L,1), "Argument 1: invalid object");
	}
	oapiGetAtm(hObj, &prm);
	lua_createtable (L, 0, 3);
	lua_pushnumber (L, prm.p);
	lua_setfield (L, -2, "p");
	lua_pushnumber (L, prm.rho);
	lua_setfield (L, -2, "rho");
	lua_pushnumber (L, prm.T);
	lua_setfield (L, -2, "T");
	return 1;
}

int Interpreter::oapi_get_induceddrag (lua_State *L)
{
	ASSERT_SYNTAX(lua_isnumber(L,1), "Argument 1: invalid type (expected number)");
	double cl = lua_tonumber(L,1);
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 2: invalid type (expected number)");
	double A = lua_tonumber(L,2);
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 3: invalid type (expected number)");
	double e = lua_tonumber(L,3);
	lua_pushnumber(L,oapiGetInducedDrag(cl,A,e));
	return 1;
}

int Interpreter::oapi_get_wavedrag (lua_State *L)
{
	ASSERT_SYNTAX(lua_isnumber(L,1), "Argument 1: invalid type (expected number)");
	double M = lua_tonumber(L,1);
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 2: invalid type (expected number)");
	double M1 = lua_tonumber(L,2);
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 3: invalid type (expected number)");
	double M2 = lua_tonumber(L,3);
	ASSERT_SYNTAX(lua_isnumber(L,4), "Argument 4: invalid type (expected number)");
	double M3 = lua_tonumber(L,4);
	ASSERT_SYNTAX(lua_isnumber(L,5), "Argument 5: invalid type (expected number)");
	double cmax = lua_tonumber(L,5);
	lua_pushnumber(L,oapiGetWaveDrag(M,M1,M2,M3,cmax));
	return 1;
}

int Interpreter::oapi_get_navpos (lua_State *L)
{
	NAVHANDLE hNav;
	VECTOR3 pos;
	ASSERT_SYNTAX (lua_gettop(L) >= 1, "Too few arguments");
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hNav = (NAVHANDLE)lua_touserdata (L,1), "Argument 1: invalid object");
	oapiGetNavPos (hNav, &pos);
	lua_pushvector (L, pos);
	return 1;
}

int Interpreter::oapi_get_navchannel (lua_State *L)
{
	NAVHANDLE hNav;
	ASSERT_SYNTAX (lua_gettop(L) >= 1, "Too few arguments");
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hNav = (NAVHANDLE)lua_touserdata (L,1), "Argument 1: invalid object");
	DWORD ch = oapiGetNavChannel (hNav);
	lua_pushnumber (L, ch);
	return 1;
}

int Interpreter::oapi_get_navrange (lua_State *L)
{
	NAVHANDLE hNav;
	ASSERT_SYNTAX (lua_gettop(L) >= 1, "Too few arguments");
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hNav = (NAVHANDLE)lua_touserdata (L,1), "Argument 1: invalid object");
	float range = oapiGetNavRange (hNav);
	lua_pushnumber (L, range);
	return 1;
}

int Interpreter::oapi_get_navdata (lua_State *L)
{
	NAVHANDLE hNav;
	ASSERT_SYNTAX (lua_gettop(L) >= 1, "Too few arguments");
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hNav = (NAVHANDLE)lua_touserdata (L,1), "Argument 1: invalid object");
	NAVDATA ndata;
	oapiGetNavData (hNav, &ndata);
	lua_newtable (L);
	lua_pushnumber (L, ndata.type);
	lua_setfield (L, -2, "type");
	lua_pushnumber (L, ndata.ch);
	lua_setfield (L, -2, "ch");
	lua_pushnumber (L, ndata.power);
	lua_setfield (L, -2, "power");
	char descr[256];
	oapiGetNavDescr(hNav,descr,256);
	lua_pushstring (L, descr);
	lua_setfield (L, -2, "descr");
	switch (ndata.type) {
	case TRANSMITTER_VOR:
		lua_pushlightuserdata (L, ndata.vor.hPlanet);
		lua_setfield (L, -2, "hplanet");
		lua_pushnumber (L, ndata.vor.lng);
		lua_setfield (L, -2, "lng");
		lua_pushnumber (L, ndata.vor.lat);
		lua_setfield (L, -2, "lat");
		break;
	case TRANSMITTER_VTOL:
		lua_pushlightuserdata (L, ndata.vtol.hBase);
		lua_setfield (L, -2, "hbase");
		lua_pushnumber (L, ndata.vtol.npad);
		lua_setfield (L, -2, "npad");
		break;
	case TRANSMITTER_ILS:
		lua_pushlightuserdata (L, ndata.ils.hBase);
		lua_setfield (L, -2, "hbase");
		lua_pushnumber (L, ndata.ils.appdir);
		lua_setfield (L, -2, "appdir");
		break;
	case TRANSMITTER_IDS:
		lua_pushlightuserdata (L, ndata.ids.hVessel);
		lua_setfield (L, -2, "hvessel");
		lua_pushlightuserdata (L, ndata.ids.hDock);
		lua_setfield (L, -2, "hdock");
		break;
	case TRANSMITTER_XPDR:
		lua_pushlightuserdata (L, ndata.xpdr.hVessel);
		lua_setfield (L, -2, "hvessel");
		break;
	}
	return 1;
}

int Interpreter::oapi_get_navsignal (lua_State *L)
{
	NAVHANDLE hNav;
	ASSERT_SYNTAX (lua_gettop(L) >= 1, "Too few arguments");
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hNav = (NAVHANDLE)lua_touserdata (L,1), "Argument 1: invalid object");
	ASSERT_SYNTAX (lua_isvector (L, 2), "Argument 2: invalid type (expected vector)");
	VECTOR3 gpos = lua_tovector(L,2);
	double sig = oapiGetNavSignal (hNav, gpos);
	lua_pushnumber (L, sig);
	return 1;	
}

int Interpreter::oapi_get_navtype (lua_State *L)
{
	NAVHANDLE hNav;
	ASSERT_SYNTAX (lua_gettop(L) >= 1, "Too few arguments");
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hNav = (NAVHANDLE)lua_touserdata (L,1), "Argument 1: invalid object");
	DWORD ntype = oapiGetNavType (hNav);
	lua_pushnumber (L, ntype);
	return 1;
}

int Interpreter::oapi_get_cameratarget (lua_State *L)
{
	OBJHANDLE hObj = oapiCameraTarget();
	if (hObj)
		lua_pushlightuserdata (L, hObj);
	else
		lua_pushnil (L);
	return 1;
}

int Interpreter::oapi_set_cameratarget (lua_State *L)
{
	OBJHANDLE hObj;
	int mode = 2;
	ASSERT_SYNTAX (lua_islightuserdata (L,1), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX (hObj = (OBJHANDLE)lua_touserdata (L,1), "Argument 1: invalid object");
	if (lua_gettop(L) > 1) {
		ASSERT_SYNTAX (lua_isnumber (L,2), "Argument 2: invalid type (expected number)");
		mode = (int)lua_tonumber (L,2);
		ASSERT_SYNTAX (mode >= 0 && mode <= 2, "Argument 2: out of range");
	}
	oapiCameraAttach (hObj, mode);
	return 0;
}

int Interpreter::oapi_get_cameraaperture (lua_State *L)
{
	double ap = oapiCameraAperture();
	lua_pushnumber (L, ap);
	return 1;
}

int Interpreter::oapi_set_cameraaperture (lua_State *L)
{
	ASSERT_SYNTAX (lua_isnumber (L,1), "Argument 1: invalid type (expected number)");
	double ap = lua_tonumber (L,1);
	oapiCameraSetAperture (ap);
	return 0;
}

int Interpreter::oapi_get_cameraglobalpos (lua_State *L)
{
	VECTOR3 pos;
	oapiCameraGlobalPos (&pos);
	lua_pushvector (L, pos);
	return 1;
}

int Interpreter::oapi_get_cameraglobaldir (lua_State *L)
{
	VECTOR3 dir;
	oapiCameraGlobalDir (&dir);
	lua_pushvector (L, dir);
	return 1;
}

int Interpreter::oapi_set_cameramode (lua_State *L)
{
	char initstr[1024], modestr[256];
	double ap, lng, lat, alt, phi=0.0, tht=0.0;
	CameraMode *cm = 0;
	ASSERT_TABLE(L,1);

	lua_getfield(L,1,"mode");
	ASSERT_STRING(L,-1);
	strcpy(modestr, lua_tostring(L,-1));
	lua_pop(L,1);
	if (!stricmp(modestr, "ground")) {

		lua_getfield(L,1,"ref");
		ASSERT_STRING(L,-1);
		strcpy (initstr,lua_tostring(L,-1));
		lua_pop(L,1);
		lua_getfield(L,1,"lng");
		ASSERT_NUMBER(L,-1);
		lng = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,1,"lat");
		ASSERT_NUMBER(L,-1);
		lat = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,1,"alt");
		ASSERT_NUMBER(L,-1);
		alt = lua_tonumber(L,-1);
		lua_pop(L,1);
		sprintf (initstr + strlen(initstr), " %lf %lf %lf", lng, lat, alt);
		lua_getfield(L,1,"alt_above_ground");
		if (lua_isnumber(L,-1) && lua_tonumber(L,-1) == 0)
			strcat(initstr, "M");
		lua_pop(L,1);
		lua_getfield(L,1,"phi");
		if (lua_isnumber(L,-1)) {
			phi = lua_tonumber(L,-1);
			lua_getfield(L,1,"tht");
			if (lua_isnumber(L,-1)) {
				tht = lua_tonumber(L,-1);
				sprintf (initstr+strlen(initstr), " %lf %lf", phi, tht);
			}
			lua_pop(L,1);
		}
		lua_pop(L,1);
		cm = new CameraMode_Ground();

	} else if (!stricmp(modestr, "track")) {

		lua_getfield(L,1,"trackmode");
		ASSERT_STRING(L,-1);
		strcpy (initstr, lua_tostring(L,-1));
		lua_pop(L,1);
		lua_getfield(L,1,"reldist");
		ASSERT_NUMBER(L,-1);
		double reldist = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,1,"phi");
		if (lua_isnumber(L,-1))
			phi = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,1,"tht");
		if (lua_isnumber(L,-1))
			tht = lua_tonumber(L,-1);
		lua_pop(L,1);
		sprintf (initstr+strlen(initstr), " %lf %lf %lf", reldist, phi, tht);
		lua_getfield(L,1,"ref");
		if (lua_isstring(L,-1)) {
			strcat(initstr, " ");
			strcat(initstr, lua_tostring(L,-1));
		}
		lua_pop(L,1);
		cm = new CameraMode_Track();

	} else if (!stricmp(modestr, "cockpit")) {

		lua_getfield(L,1,"cockpitmode");
		if (lua_isstring(L,-1)) {
			strcpy (initstr, lua_tostring(L,-1));
			lua_getfield(L,1,"pos");
			if (lua_isnumber(L,-1)) {
				sprintf (initstr+strlen(initstr), ":%d", (int)lua_tonumber(L,-1));
				lua_getfield(L,1,"lean");
				if (lua_isnumber(L,-1)) {
					sprintf (initstr+strlen(initstr), ":%d", (int)lua_tonumber(L,-1));
				} else {
					lua_getfield(L,1,"lean_smooth");
					if (lua_isnumber(L,-1)) {
						sprintf (initstr+strlen(initstr), ":%dS", (int)lua_tonumber(L,-1));
					}
					lua_pop(L,1);
				}
				lua_pop(L,1);
			}
			lua_pop(L,1);

		} else
			initstr[0] = '\0';
		lua_pop(L,1);
		cm = new CameraMode_Cockpit();

	}

	if (cm) {
		cm->Init(initstr);
		oapiSetCameraMode (*cm);
		delete cm;
	}
	return 0;
}

int Interpreter::oapi_move_groundcamera (lua_State *L)
{
	double forward=0.0, right=0.0, up=0.0;
	ASSERT_TABLE(L,1);
	lua_getfield(L,1,"f");
	if (lua_isnumber(L,-1))
		forward = lua_tonumber(L,-1);
	lua_pop(L,1);
	lua_getfield(L,1,"r");
	if (lua_isnumber(L,-1))
		right = lua_tonumber(L,-1);
	lua_pop(L,1);
	lua_getfield(L,1,"u");
	if (lua_isnumber(L,-1))
		up = lua_tonumber(L,-1);
	lua_pop(L,1);
	oapiMoveGroundCamera (forward, right, up);
	return 0;
}

int Interpreter::oapi_create_animationcomponent (lua_State *L)
{
	MGROUP_TRANSFORM *trans;
	UINT mesh, *grp, ngrp, nbuf;
	ASSERT_TABLE(L,1);
	lua_getfield(L,1,"type");
	ASSERT_STRING(L,-1);
	char typestr[128];
	strcpy (typestr,lua_tostring(L,-1));
	lua_pop(L,1);
	lua_getfield(L,1,"mesh");
	ASSERT_NUMBER(L,-1);
	mesh = (UINT)lua_tointeger(L,-1);
	lua_pop(L,1);
	lua_getfield(L,1,"grp");
	if (lua_isnumber(L,-1)) { // single group index
		grp = new UINT[1];
		*grp = (UINT)lua_tointeger(L,-1);
		ngrp = 1;
	} else {
		ASSERT_TABLE(L,-1);
		ngrp = nbuf = 0;
		lua_pushnil(L);
		while(lua_next(L,-2)) {
			if (ngrp == nbuf) { // grow buffer
				UINT *tmp = new UINT[nbuf+=16];
				if (ngrp) {
					memcpy (tmp, grp, ngrp*sizeof(UINT));
					delete []grp;
				}
				grp = tmp;
			}
			grp[ngrp++] = (UINT)lua_tointeger(L,-1);
			lua_pop(L,1);
		}
	}
	lua_pop(L,1); // pop table of group indices

	if (!_stricmp(typestr, "rotation")) {
		lua_getfield(L,1,"ref");
		ASSERT_VECTOR(L,-1);
		VECTOR3 ref = lua_tovector(L,-1);
		lua_pop(L,1);
		lua_getfield(L,1,"axis");
		ASSERT_VECTOR(L,-1);
		VECTOR3 axis = lua_tovector(L,-1);
		lua_pop(L,1);
		lua_getfield(L,1,"angle");
		ASSERT_NUMBER(L,-1);
		double angle = lua_tonumber(L,-1);
		lua_pop(L,1);
		trans = new MGROUP_ROTATE(mesh,grp,ngrp,ref,axis,(float)angle);
	} else if (!_stricmp(typestr, "translation")) {
		lua_getfield(L,1,"shift");
		ASSERT_VECTOR(L,-1);
		VECTOR3 shift = lua_tovector(L,-1);
		lua_pop(L,1);
		trans = new MGROUP_TRANSLATE(mesh,grp,ngrp,shift);
	} else if (!_stricmp(typestr, "scaling")) {
		lua_getfield(L,1,"ref");
		ASSERT_VECTOR(L,-1);
		VECTOR3 ref = lua_tovector(L,-1);
		lua_pop(L,1);
		lua_getfield(L,1,"scale");
		ASSERT_VECTOR(L,-1);
		VECTOR3 scale = lua_tovector(L,-1);
		lua_pop(L,1);
		trans = new MGROUP_SCALE(mesh,grp,ngrp,ref,scale);
	} else {
		ASSERT_SYNTAX(0,"Invalid animation type");
	}
	lua_pushlightuserdata(L,trans);
	return 1;
}

int Interpreter::oapi_del_animationcomponent (lua_State *L)
{
	ASSERT_LIGHTUSERDATA(L,1);
	MGROUP_TRANSFORM *trans = (MGROUP_TRANSFORM*)lua_touserdata(L,1);
	delete trans;
	return 0;
}

int Interpreter::oapi_open_mfd (lua_State *L)
{
	ASSERT_NUMBER(L,1);
	int mfdid = lua_tointeger(L,1);
	ASSERT_NUMBER(L,2);
	int mfdmode = lua_tointeger(L,2);
	oapiOpenMFD (mfdmode, mfdid);
	return 0;
}

int Interpreter::oapi_set_hudmode (lua_State *L)
{
	ASSERT_NUMBER(L,1);
	int hudmode = lua_tointeger(L,1);
	oapiSetHUDMode (hudmode);
	return 0;
}

int Interpreter::oapi_set_panelblink (lua_State *L)
{
	int i;
	VECTOR3 v[4];
	if (lua_gettop(L) == 0) {
		oapiSetPanelBlink (NULL);
	} else {
		for (i = 0; i < 4; i++) {
			ASSERT_VECTOR(L,i+1);
			v[i] = lua_tovector(L,i+1);
		}
		oapiSetPanelBlink (v);
	}
	return 0;
}

int Interpreter::oapi_keydown (lua_State *L)
{
	ASSERT_LIGHTUSERDATA(L,1);
	char *kstate = (char*)lua_touserdata(L,1);
	ASSERT_NUMBER(L,2);
	int key = lua_tointeger(L, 2);
	lua_pushboolean (L, KEYDOWN(kstate,key));
	return 1;
}

int Interpreter::oapi_resetkey (lua_State *L)
{
	ASSERT_LIGHTUSERDATA(L,1);
	char *kstate = (char*)lua_touserdata(L,1);
	ASSERT_NUMBER(L,2);
	int key = lua_tointeger(L, 2);
	RESETKEY(kstate,key);
	return 0;
}

// ============================================================================
// terminal library functions

int Interpreter::termOut (lua_State *L)
{
	return 0;
}

// ============================================================================
// screen annotation library functions

int Interpreter::noteSetText (lua_State *L)
{
	NOTEHANDLE *pnote = (NOTEHANDLE*)lua_touserdata (L, -2);
	const char *str = lua_tostringex (L, -1);
	oapiAnnotationSetText (*pnote, (char*)str);
	return 0;
}

int Interpreter::noteSetPos (lua_State *L)
{
	NOTEHANDLE *pnote = (NOTEHANDLE*)lua_touserdata (L, 1);
	double x1 = lua_tonumber (L, 2);
	double y1 = lua_tonumber (L, 3);
	double x2 = lua_tonumber (L, 4);
	double y2 = lua_tonumber (L, 5);
	oapiAnnotationSetPos (*pnote, x1, y1, x2, y2);
	return 0;
}

int Interpreter::noteSetSize (lua_State *L)
{
	NOTEHANDLE *pnote = (NOTEHANDLE*)lua_touserdata (L, 1);
	double size = lua_tonumber (L, 2);
	oapiAnnotationSetSize (*pnote, size);
	return 0;
}

int Interpreter::noteSetColour (lua_State *L)
{
	NOTEHANDLE *pnote = (NOTEHANDLE*)lua_touserdata (L, 1);
	VECTOR3 col;
	lua_getfield (L, 2, "r");
	col.x = lua_tonumber (L, -1);  lua_pop (L, 1);
	lua_getfield (L, 2, "g");
	col.y = lua_tonumber (L, -1);  lua_pop (L, 1);
	lua_getfield (L, 2, "b");
	col.z = lua_tonumber (L, -1);  lua_pop (L, 1);
	oapiAnnotationSetColour (*pnote, col);
	return 0;
}

// ============================================================================
// vessel library functions

OBJHANDLE Interpreter::lua_toObject (lua_State *L, int idx)
{
	return (OBJHANDLE)lua_touserdata (L, idx); 
}

oapi::Sketchpad *Interpreter::lua_tosketchpad (lua_State *L, int idx)
{
	oapi::Sketchpad **skp = (oapi::Sketchpad**)lua_touserdata (L, idx);
	return *skp;
	//oapi::Sketchpad *skp = (oapi::Sketchpad*)lua_touserdata(L,idx);
	//return skp;
}

int Interpreter::vesselGetHandle (lua_State *L)
{
	OBJHANDLE hObj;
	if (lua_isnumber (L, 1)) { // select by index
		int idx = (int)lua_tointeger (L, 1);
		hObj = oapiGetVesselByIndex (idx);
	} else {                   // select by name
		char *name = (char*)luaL_checkstring (L, 1);
		hObj = oapiGetVesselByName (name);
	}
	if (hObj) lua_pushlightuserdata (L, hObj);  // push vessel handle
	else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetFocusHandle (lua_State *L)
{
	lua_pushlightuserdata (L, oapiGetFocusObject());
	return 1;
}

int Interpreter::vesselGetInterface (lua_State *L)
{
	OBJHANDLE hObj;
	if (lua_islightuserdata (L, 1)) { // select by handle
		hObj = lua_toObject (L, 1);
	} else if (lua_isnumber (L, 1)) { // select by index
		int idx = (int)lua_tointeger (L, 1);
		hObj = oapiGetVesselByIndex (idx);
	} else {                          // select by name
		char *name = (char*)luaL_checkstring (L, 1);
		hObj = oapiGetVesselByName (name);
	}
	if (hObj) {
		VESSEL *v = oapiGetVesselInterface(hObj);
		lua_pushvessel(L,v);
	} else {
		lua_pushnil (L);
	}
	return 1;
}

int Interpreter::vesselGetFocusInterface (lua_State *L)
{
	VESSEL *v = oapiGetFocusInterface();
	lua_pushvessel (L, v);
	return 1;
}

int Interpreter::vesselGetCount (lua_State *L)
{
	lua_pushinteger (L, oapiGetVesselCount());
	return 1;
}

int Interpreter::vGetHandle (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		const OBJHANDLE hV = v->GetHandle();
		if (hV) lua_pushlightuserdata (L, hV);
		else lua_pushnil (L);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_name (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushstring (L, v->GetName());
	GetInterpreter(L)->term_echo(L);
	return 1;
}

int Interpreter::v_get_classname (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushstring (L, v->GetClassName());
	GetInterpreter(L)->term_echo(L);
	return 1;
}

int Interpreter::v_get_flightmodel (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetFlightModel());
	return 1;
}

int Interpreter::v_get_damagemodel (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetDamageModel());
	return 1;
}

int Interpreter::v_get_enablefocus (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushboolean (L, v->GetEnableFocus());
	return 1;
}

int Interpreter::v_set_enablefocus (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isboolean (L,2), "Argument 1: invalid type (expected boolean)");
	bool enable = (lua_toboolean(L,2) != 0);
	v->SetEnableFocus (enable);
	return 0;
}

int Interpreter::v_get_size (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetSize());
	GetInterpreter(L)->term_echo(L);
	return 1;
}

int Interpreter::v_set_size (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	lua_Number size = lua_tonumber(L,2);
	v->SetSize(size);
	return 0;
}

int Interpreter::v_get_emptymass (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		lua_pushnumber (L, v->GetEmptyMass());
		GetInterpreter(L)->term_echo(L);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::v_set_emptymass (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	lua_Number emass = lua_tonumber(L,2);
	v->SetEmptyMass(emass);
	return 0;
}

int Interpreter::v_get_pmi (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		VECTOR3 pmi;
		v->GetPMI (pmi);
		lua_pushvector (L, pmi);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::v_set_pmi (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 1: invalid type (expected vector)");
	VECTOR3 pmi = lua_tovector (L,2);
	v->SetPMI (pmi);
	return 0;
}

int Interpreter::v_get_crosssections (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 cs;
	v->GetCrossSections (cs);
	lua_pushvector(L,cs);
	return 1;
}

int Interpreter::v_set_crosssections (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 1: invalid type (expected vector)");
	VECTOR3 cs = lua_tovector (L,2);
	v->SetCrossSections (cs);
	return 0;
}

int Interpreter::v_get_gravitygradientdamping (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double ggd = v->GetGravityGradientDamping();
	lua_pushnumber(L,ggd);
	return 1;
}

int Interpreter::v_set_gravitygradientdamping (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	double ggd = lua_tonumber(L,2);
	bool ok = v->SetGravityGradientDamping (ggd);
	lua_pushboolean (L, ok);
	return 1;
}

int Interpreter::v_get_touchdownpoints (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 pt1, pt2, pt3;
	v->GetTouchdownPoints (pt1, pt2, pt3);
	lua_pushvector(L,pt1);
	lua_pushvector(L,pt2);
	lua_pushvector(L,pt3);
	return 3;
}

int Interpreter::v_set_touchdownpoints (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDVECTOR(L,2);   VECTOR3 pt1 = lua_tovector(L,2);
	ASSERT_MTDVECTOR(L,3);   VECTOR3 pt2 = lua_tovector(L,3);
	ASSERT_MTDVECTOR(L,4);   VECTOR3 pt3 = lua_tovector(L,4);
	v->SetTouchdownPoints (pt1, pt2, pt3);
	return 0;
}

int Interpreter::v_set_visibilitylimit (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	double vislimit, spotlimit = -1.0;
	vislimit = lua_tonumber (L,2);
	if (lua_gettop (L) > 2) {
		ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
		spotlimit = lua_tonumber (L,3);
	}
	v->SetVisibilityLimit (vislimit, spotlimit);
	return 0;
}

void AirfoilFunc (VESSEL *v, double aoa, double M, double Re,
        void *context, double *cl, double *cm, double *cd)
{
	// The airfoil callback function for aerodynamic coefficients
	// The call is passed on to the designated script function

	AirfoilContext *ac = (AirfoilContext*)context;
	lua_State *L = ac->L;                             // interpreter instance
	lua_getfield (L, LUA_GLOBALSINDEX, ac->funcname); // the callback function

	// push callback arguments
	lua_pushlightuserdata (L, v->GetHandle());  // vessel handle
	lua_pushnumber (L, aoa);                    // angle of attack
	lua_pushnumber (L, M);                      // Mach number
	lua_pushnumber (L, Re);                     // Reynolds number
	
	// call the script callback function
	lua_call (L, 4, 3); // 4 arguments, 3 results

	// retrieve results
	*cl = lua_tonumber (L,-3);
	*cm = lua_tonumber (L,-2);
	*cd = lua_tonumber (L,-1);
	lua_pop(L,3);
}

int Interpreter::v_get_mass (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetMass());
	GetInterpreter(L)->term_echo(L);
	return 1;
}

int Interpreter::v_get_globalpos (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 pos;
	v->GetGlobalPos (pos);
	lua_pushvector (L, pos);
	return 1;
}

int Interpreter::v_get_globalvel (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 vel;
	v->GetGlobalVel (vel);
	lua_pushvector (L, vel);
	return 1;
}

int Interpreter::v_get_relativepos (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	OBJHANDLE hRef = (OBJHANDLE)lua_touserdata (L, 2);
	VECTOR3 pos;
	v->GetRelativePos (hRef, pos);
	lua_pushvector (L, pos);
	return 1;
}

int Interpreter::v_get_relativevel (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	OBJHANDLE hRef = (OBJHANDLE)lua_touserdata (L, 2);
	VECTOR3 vel;
	v->GetRelativeVel (hRef, vel);
	lua_pushvector (L, vel);
	return 1;
}

int Interpreter::v_get_rotationmatrix (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	MATRIX3 rot;
	v->GetRotationMatrix (rot);
	lua_pushmatrix (L, rot);
	return 1;
}

int Interpreter::v_get_atmref (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	OBJHANDLE hA = v->GetAtmRef();
	if (hA) lua_pushlightuserdata (L, hA);
	else lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_atmtemperature (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double temp = v->GetAtmTemperature();
	lua_pushnumber (L,temp);
	return 1;
}

int Interpreter::v_get_atmdensity (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double rho = v->GetAtmDensity();
	lua_pushnumber (L,rho);
	return 1;
}

int Interpreter::v_get_atmpressure (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double p = v->GetAtmPressure();
	lua_pushnumber (L,p);
	return 1;
}

int Interpreter::v_get_dynpressure (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetDynPressure());
	return 1;
}

int Interpreter::v_get_machnumber (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetMachNumber());
	return 1;
}

int Interpreter::v_get_groundspeed (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetGroundspeed ());
	GetInterpreter(L)->term_echo(L);
	return 1;
}

int Interpreter::v_get_groundspeedvector (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	REFFRAME frame = (REFFRAME)lua_tointeger(L,2);
	VECTOR3 sp;
	v->GetGroundspeedVector (frame, sp);
	lua_pushvector (L, sp);
	return 1;
}

int Interpreter::v_get_airspeed (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetAirspeed ());
	GetInterpreter(L)->term_echo(L);
	return 1;
}

int Interpreter::v_get_airspeedvector (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	REFFRAME frame = (REFFRAME)lua_tointeger(L,2);
	VECTOR3 sp;
	v->GetAirspeedVector (frame, sp);
	lua_pushvector (L, sp);
	return 1;
}

int Interpreter::v_get_shipairspeedvector (lua_State *L)
{
	GetInterpreter(L)->term_strout (L, "Obsolete function used: v:get_shipairspeedvector.\nUse v:get_airspeedvector instead", true);
	VESSEL *v = lua_tovessel(L,-1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 sp;
	v->GetAirspeedVector (FRAME_LOCAL, sp);
	lua_pushvector (L, sp);
	return 1;
}

int Interpreter::v_get_horizonairspeedvector (lua_State *L)
{
	GetInterpreter(L)->term_strout (L, "Obsolete function used: v:get_horizonairspeedvector.\nUse v:get_airspeedvector instead", true);
	VESSEL *v = lua_tovessel(L,-1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 sp;
	v->GetAirspeedVector (FRAME_HORIZON, sp);
	lua_pushvector (L, sp);
	return 1;
}

int Interpreter::v_get_aoa (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetAOA());
	GetInterpreter(L)->term_echo(L);
	return 1;
}

int Interpreter::v_get_slipangle (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetSlipAngle());
	GetInterpreter(L)->term_echo(L);
	return 1;
}

int Interpreter::v_create_airfoil (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	AIRFOIL_ORIENTATION ao = (AIRFOIL_ORIENTATION)(int)(lua_tonumber(L,2)+0.5);
	ASSERT_SYNTAX(lua_isvector(L,3), "Argument 2: invalid type (expected vector)");
	VECTOR3 ref = lua_tovector (L,3);
	ASSERT_SYNTAX(lua_isstring(L,4), "Argument 3: invalid type (expected string)");
	const char *funcname = lua_tostring(L,4);
	ASSERT_SYNTAX(lua_isnumber(L,5), "Argument 4: invalid type (expected number)");
	double c = lua_tonumber(L,5);
	ASSERT_SYNTAX(lua_isnumber(L,6), "Argument 5: invalid type (expected number)");
	double S = lua_tonumber(L,6);
	ASSERT_SYNTAX(lua_isnumber(L,7), "Argument 6: invalid type (expected number)");
	double A = lua_tonumber(L,7);
	AirfoilContext *ac = new AirfoilContext;
	ac->L = L;
	strncpy (ac->funcname, funcname, 127);
	AIRFOILHANDLE ha = v->CreateAirfoil3 (ao, ref, AirfoilFunc, ac, c, S, A);
	lua_pushlightuserdata (L, ha);
	return 1;
}

int Interpreter::v_del_airfoil (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	AIRFOILHANDLE ha = (AIRFOILHANDLE)lua_touserdata(L,2);
	AirfoilContext *ac;
	if (v->GetAirfoilParam (ha, 0, 0, (void**)&ac, 0, 0, 0)) {
		if (ac) delete ac; // delete the context buffer before deleting the airfoil
	}
	bool ok = v->DelAirfoil (ha);
	lua_pushboolean (L, ok?1:0);
	return 1;
}

int Interpreter::v_create_controlsurface (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	AIRCTRL_TYPE type = (AIRCTRL_TYPE)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	double area = lua_tonumber(L,3);
	ASSERT_MTDNUMBER(L,4);
	double dCl = lua_tonumber(L,4);
	ASSERT_MTDVECTOR(L,5);
	VECTOR3 ref = lua_tovector(L,5);
	int axis = AIRCTRL_AXIS_AUTO;
	double delay = 1.0;
	UINT anim = (UINT)-1;
	if (lua_isnumber(L,6)) {
		axis = (int)lua_tointeger(L,6);
		if (lua_isnumber(L,7)) {
			delay = lua_tonumber(L,7);
			if (lua_isnumber(L,8)) {
				anim = (UINT)lua_tointeger(L,8);
			}
		}
	}
	CTRLSURFHANDLE hctrl = v->CreateControlSurface3 (type, area, dCl, ref, axis, delay, anim);
	lua_pushlightuserdata (L, hctrl);
	return 1;
}

int Interpreter::v_get_cw (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 cw;
	double cw_zn;
	v->GetCW (cw.z, cw_zn, cw.x, cw.y);
	lua_pushvector(L,cw);
	lua_pushnumber(L,cw_zn);
	lua_setfield(L,-2,"zn");
	return 1;
}

int Interpreter::v_set_cw (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_istable(L,2), "Argument 1: invalid type (expected table)");
	VECTOR3 cw = lua_tovector (L,2);
	lua_getfield(L,2,"zn");
	double zn = lua_tonumber(L,-1);
	v->SetCW (cw.z, zn, cw.x, cw.y);
	return 0;
}

int Interpreter::v_get_wingaspect (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double aspect = v->GetWingAspect ();
	lua_pushnumber (L, aspect);
	return 1;
}

int Interpreter::v_set_wingaspect (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	double aspect = lua_tonumber(L,2);
	v->SetWingAspect (aspect);
	return 0;
}

int Interpreter::v_get_wingeffectiveness (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double eff = v->GetWingEffectiveness ();
	lua_pushnumber (L, eff);
	return 1;
}

int Interpreter::v_set_wingeffectiveness (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	double eff = lua_tonumber(L,2);
	v->SetWingEffectiveness (eff);
	return 0;
}

int Interpreter::v_get_rotdrag (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 rd;
	v->GetRotDrag (rd);
	lua_pushvector (L,rd);
	return 1;
}

int Interpreter::v_set_rotdrag (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 1: invalid type (expected vector)");
	VECTOR3 rd = lua_tovector(L,2);
	v->SetRotDrag (rd);
	return 0;
}

int Interpreter::v_get_pitchmomentscale (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double pms = v->GetPitchMomentScale ();
	lua_pushnumber (L,pms);
	return 1;
}

int Interpreter::v_set_pitchmomentscale (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	double pms = lua_tonumber(L,2);
	v->SetPitchMomentScale (pms);
	return 0;
}

int Interpreter::v_get_yawmomentscale (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double yms = v->GetYawMomentScale ();
	lua_pushnumber (L,yms);
	return 1;
}

int Interpreter::v_set_yawmomentscale (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	double yms = lua_tonumber(L,2);
	v->SetYawMomentScale (yms);
	return 0;
}

int Interpreter::v_get_trimscale (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double ts = v->GetTrimScale ();
	lua_pushnumber (L,ts);
	return 1;
}

int Interpreter::v_set_trimscale (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	double ts = lua_tonumber(L,2);
	v->SetTrimScale (ts);
	return 0;
}

int Interpreter::v_create_dock (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isvector(L,2), "Argument 1: invalid type (expected vector)");
	VECTOR3 pos = lua_tovector(L,2);
	ASSERT_SYNTAX(lua_isvector(L,3), "Argument 2: invalid type (expected vector)");
	VECTOR3 dir = lua_tovector(L,3);
	ASSERT_SYNTAX(lua_isvector(L,4), "Argument 3: invalid type (expected vector)");
	VECTOR3 rot = lua_tovector(L,4);
	lua_pushlightuserdata (L, v->CreateDock (pos, dir, rot));
	return 1;
}

int Interpreter::v_del_dock (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	DOCKHANDLE hDock = (DOCKHANDLE)lua_touserdata(L,2);
	lua_pushboolean (L, v->DelDock (hDock));
	return 1;
}

int Interpreter::v_set_dockparams (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	DOCKHANDLE hDock = 0;
	int idx = 2;
	if (lua_islightuserdata(L,2)) {
		hDock = (DOCKHANDLE)lua_touserdata(L,2);
		idx++;
	}
	ASSERT_MTDVECTOR(L,idx);   VECTOR3 pos = lua_tovector(L,idx++);
	ASSERT_MTDVECTOR(L,idx);   VECTOR3 dir = lua_tovector(L,idx++);
	ASSERT_MTDVECTOR(L,idx);   VECTOR3 rot = lua_tovector(L,idx++);
	if (hDock)
		v->SetDockParams(hDock,pos,dir,rot);
	else
		v->SetDockParams(pos,dir,rot);
	return 0;
}

int Interpreter::v_get_dockparams (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	DOCKHANDLE hDock = (DOCKHANDLE)lua_touserdata(L,2);
	VECTOR3 pos, dir, rot;
	v->GetDockParams (hDock, pos, dir, rot);
	lua_pushvector(L,pos);
	lua_pushvector(L,dir);
	lua_pushvector(L,rot);
	return 3;
}

int Interpreter::v_get_dockcount (lua_State *L)
{
	VESSEL *v = lua_tovessel(L, 1);
	if (v) {
		lua_pushinteger (L, v->DockCount());
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_dockhandle (lua_State *L)
{
	VESSEL *v = lua_tovessel(L, 1);
	if (v) {
		int idx = lua_tointeger (L, 2);
		DOCKHANDLE h = v->GetDockHandle (idx);
		if (h) lua_pushlightuserdata (L, h);
		else lua_pushnil (L);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_dockstatus (lua_State *L)
{
	VESSEL *v = lua_tovessel(L, 1);
	if (v) {
		DOCKHANDLE hDock = (DOCKHANDLE)lua_touserdata (L, 2);
		OBJHANDLE hObj = v->GetDockStatus (hDock);
		if (hObj) lua_pushlightuserdata (L, hObj);
		else lua_pushnil (L);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::v_undock (lua_State *L)
{
	VESSEL *v = lua_tovessel(L, 1);
	if (v) {
		UINT idx = (UINT)(lua_tonumber (L, 2)+0.5);
		v->Undock (idx);
	}
	return 0;
}

int Interpreter::v_create_attachment (lua_State *L)
{
	VESSEL *v = lua_tovessel (L, 1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isboolean(L,2), "Argument 1: invalid type (expected boolean)");
	bool toparent = (lua_toboolean(L,2) != 0);
	ASSERT_SYNTAX(lua_isvector(L,3), "Argument 2: invalid type (expected vector)");
	VECTOR3 pos = lua_tovector(L,3);
	ASSERT_SYNTAX(lua_isvector(L,4), "Argument 3: invalid type (expected vector)");
	VECTOR3 dir = lua_tovector(L,4);
	ASSERT_SYNTAX(lua_isvector(L,5), "Argument 4: invalid type (expected vector)");
	VECTOR3 rot = lua_tovector(L,5);
	ASSERT_SYNTAX(lua_isstring(L,6), "Argument 5: invalid type (expected string)");
	const char *id = lua_tostring(L,6);
	bool loose = false;
	if (lua_gettop(L) >= 7) {
		ASSERT_SYNTAX(lua_isboolean(L,7), "Argument 6: invalid type (expected boolean)");
		loose = (lua_toboolean(L,7) != 0);
	}
	lua_pushlightuserdata (L, v->CreateAttachment (toparent, pos, dir, rot, id, loose));
	return 1;
}

int Interpreter::v_del_attachment (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	ATTACHMENTHANDLE hAttachment = (ATTACHMENTHANDLE)lua_touserdata(L,2);
	lua_pushboolean (L, v->DelAttachment (hAttachment));
	return 1;
}

int Interpreter::v_clear_attachments (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	v->ClearAttachments();
	return 0;
}

int Interpreter::v_set_attachmentparams (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	ATTACHMENTHANDLE hAttachment = (ATTACHMENTHANDLE)lua_touserdata(L,2);
	ASSERT_MTDVECTOR(L,3);
	VECTOR3 pos = lua_tovector(L,3);
	ASSERT_MTDVECTOR(L,4);
	VECTOR3 dir = lua_tovector(L,4);
	ASSERT_MTDVECTOR(L,5);
	VECTOR3 rot = lua_tovector(L,5);
	v->SetAttachmentParams (hAttachment, pos, dir, rot);
	return 0;
}

int Interpreter::v_get_attachmentparams (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	ATTACHMENTHANDLE hAttachment = (ATTACHMENTHANDLE)lua_touserdata(L,2);
	VECTOR3 pos, dir, rot;
	v->GetAttachmentParams (hAttachment, pos, dir, rot);
	lua_pushvector(L,pos);
	lua_pushvector(L,dir);
	lua_pushvector(L,rot);
	return 3;
}

int Interpreter::v_get_attachmentid (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	ATTACHMENTHANDLE hAttachment = (ATTACHMENTHANDLE)lua_touserdata(L,2);
	const char *id = v->GetAttachmentId (hAttachment);
	lua_pushstring (L, id);
	return 1;
}

int Interpreter::v_get_attachmentstatus (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	ATTACHMENTHANDLE hAttachment = (ATTACHMENTHANDLE)lua_touserdata(L,2);
	OBJHANDLE hVessel = v->GetAttachmentStatus (hAttachment);
	if (hVessel) lua_pushlightuserdata (L, hVessel);
	else         lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_attachmentcount (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDBOOLEAN(L,2);
	bool toparent = (lua_toboolean(L,2) != 0);
	lua_pushinteger(L,v->AttachmentCount(toparent));
	return 1;
}

int Interpreter::v_get_attachmentindex (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	ATTACHMENTHANDLE hAttachment = (ATTACHMENTHANDLE)lua_touserdata(L,2);
	lua_pushinteger(L,v->GetAttachmentIndex(hAttachment));
	return 1;
}

int Interpreter::v_get_attachmenthandle (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDBOOLEAN(L,2);
	bool toparent = (lua_toboolean(L,2) != 0);
	ASSERT_MTDNUMBER(L,3);
	DWORD idx = (DWORD)lua_tointeger(L,3);
	ATTACHMENTHANDLE hAttachment = v->GetAttachmentHandle (toparent, idx);
	if (hAttachment) lua_pushlightuserdata (L, hAttachment);
	else lua_pushnil (L);
	return 1;
}

int Interpreter::v_attach_child (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	OBJHANDLE hChild = (OBJHANDLE)lua_touserdata (L,2);
	ASSERT_LIGHTUSERDATA(L,3);
	ATTACHMENTHANDLE hAttach = (ATTACHMENTHANDLE)lua_touserdata(L,3);
	ASSERT_LIGHTUSERDATA(L,4);
	ATTACHMENTHANDLE hChildAttach = (ATTACHMENTHANDLE)lua_touserdata(L,4);
	bool ok = v->AttachChild (hChild, hAttach, hChildAttach);
	lua_pushboolean (L, ok ? 1:0);
	return 1;
}

int Interpreter::v_detach_child (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	ATTACHMENTHANDLE hAttachment = (ATTACHMENTHANDLE)lua_touserdata(L,2);
	double vel = 0.0;
	if (lua_gettop(L) >= 3) {
		ASSERT_MTDNUMBER(L,3);
		vel = lua_tonumber(L,3);
	}
	bool ok = v->DetachChild (hAttachment, vel);
	lua_pushboolean (L, ok ? 1:0);
	return 1;
}

int Interpreter::vesselSendBufferedKey (lua_State *L)
{
	VESSEL *v = lua_tovessel(L, -2);
	if (v) {
		int key = lua_tointeger (L, -1);
		int res = v->SendBufferedKey (key);
		lua_pushnumber (L, res);
	} else {
		lua_pushnil (L);
	}
	return 1;
}

int Interpreter::vesselGetGravityRef (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) lua_pushlightuserdata (L, v->GetGravityRef());
	else   lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetSurfaceRef (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) lua_pushlightuserdata (L, v->GetSurfaceRef());
	else   lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetAltitude (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	if (v) {
		if (lua_isnumber(L,2)) {
			AltitudeMode mode = (AltitudeMode)(int)lua_tonumber(L,2);
			if (mode != ALTMODE_GROUND)
				mode = ALTMODE_MEANRAD;
			lua_pushnumber (L, v->GetAltitude(mode));
		} else
			lua_pushnumber (L, v->GetAltitude());
		GetInterpreter(L)->term_echo(L);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetPitch (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		lua_pushnumber (L, v->GetPitch());
		GetInterpreter(L)->term_echo(L);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetBank (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		lua_pushnumber (L, v->GetBank());
		GetInterpreter(L)->term_echo(L);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetYaw (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		lua_pushnumber (L, v->GetYaw());
		GetInterpreter(L)->term_echo(L);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetAngularVel (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		VECTOR3 av;
		v->GetAngularVel (av);
		lua_pushvector (L, av);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselSetAngularVel (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	if (v) {
		VECTOR3 av = lua_tovector (L,2);
		v->SetAngularVel (av);
	}
	return 0;
}

int Interpreter::vesselGetElements (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		ELEMENTS el;
		v->GetElements (0, el);
		lua_createtable (L, 0, 6);
		lua_pushnumber (L, el.a);
		lua_setfield (L, -2, "a");
		lua_pushnumber (L, el.e);
		lua_setfield (L, -2, "e");
		lua_pushnumber (L, el.i);
		lua_setfield (L, -2, "i");
		lua_pushnumber (L, el.theta);
		lua_setfield (L, -2, "theta");
		lua_pushnumber (L, el.omegab);
		lua_setfield (L, -2, "omegab");
		lua_pushnumber (L, el.L);
		lua_setfield (L, -2, "L");
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetElementsEx (lua_State *L)
{
	VESSEL *v = lua_tovessel(L);
	if (v) {
		ELEMENTS el;
		ORBITPARAM prm;
		v->GetElements (0, el, &prm);
		lua_createtable (L, 0, 6);
		lua_pushnumber (L, el.a);
		lua_setfield (L, -2, "a");
		lua_pushnumber (L, el.e);
		lua_setfield (L, -2, "e");
		lua_pushnumber (L, el.i);
		lua_setfield (L, -2, "i");
		lua_pushnumber (L, el.theta);
		lua_setfield (L, -2, "theta");
		lua_pushnumber (L, el.omegab);
		lua_setfield (L, -2, "omegab");
		lua_pushnumber (L, el.L);
		lua_setfield (L, -2, "L");
		lua_createtable (L, 0, 12);
		lua_pushnumber (L, prm.SMi);
		lua_setfield (L, -2, "SMi");
		lua_pushnumber (L, prm.PeD);
		lua_setfield (L, -2, "PeD");
		lua_pushnumber (L, prm.ApD);
		lua_setfield (L, -2, "ApD");
		lua_pushnumber (L, prm.MnA);
		lua_setfield (L, -2, "MnA");
		lua_pushnumber (L, prm.TrA);
		lua_setfield (L, -2, "TrA");
		lua_pushnumber (L, prm.MnL);
		lua_setfield (L, -2, "MnL");
		lua_pushnumber (L, prm.TrL);
		lua_setfield (L, -2, "TrL");
		lua_pushnumber (L, prm.EcA);
		lua_setfield (L, -2, "EcA");
		lua_pushnumber (L, prm.Lec);
		lua_setfield (L, -2, "Lec");
		lua_pushnumber (L, prm.T);
		lua_setfield (L, -2, "T");
		lua_pushnumber (L, prm.PeT);
		lua_setfield (L, -2, "PeT");
		lua_pushnumber (L, prm.ApT);
		lua_setfield (L, -2, "ApT");
	} else {
		lua_pushnil (L);
		lua_pushnil (L);
	}
	return 2;
}

int Interpreter::vesselSetElements (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	if (v && lua_gettop (L) >= 2) {

		ELEMENTS el;
		OBJHANDLE hRef = v->GetGravityRef();
		double mjd_ref = 0;
		int frame = FRAME_ECL;

		if (lua_istable (L, 2)) {
			lua_getfield (L, 2, "a");
			if (lua_isnumber (L, -1)) el.a = (double)lua_tonumber (L, -1);
			lua_pop (L, 1);
			lua_getfield (L, 2, "e");
			if (lua_isnumber (L, -1)) el.e = (double)lua_tonumber (L, -1);
			lua_pop (L, 1);
			lua_getfield (L, 2, "i");
			if (lua_isnumber (L, -1)) el.i = (double)lua_tonumber (L, -1);
			lua_pop (L, 1);
			lua_getfield (L, 2, "theta");
			if (lua_isnumber (L, -1)) el.theta = (double)lua_tonumber (L, -1);
			lua_pop (L, 1);
			lua_getfield (L, 2, "omegab");
			if (lua_isnumber (L, -1)) el.omegab = (double)lua_tonumber (L, -1);
			lua_pop (L, 1);
			lua_getfield (L, 2, "L");
			if (lua_isnumber (L, -1)) el.L = (double)lua_tonumber (L, -1);
			lua_pop (L, 1);
		} else return 0;

		if (lua_gettop (L) >= 3 && lua_istable (L, 3)) {
			lua_getfield (L, 3, "href");
			if (lua_islightuserdata (L, -1)) hRef = (OBJHANDLE)lua_touserdata (L, -1);
			lua_pop (L, 1);
			lua_getfield (L, 3, "mjd_ref");
			if (lua_isnumber (L, -1)) mjd_ref = (double)lua_tonumber (L, -1);
			lua_pop (L, 1);
			lua_getfield (L, 3, "frame");
			if (lua_isstring (L, -1)) {
				const char *framestr = lua_tostring (L, -1);
				if (!_stricmp (framestr, "equ")) frame = FRAME_EQU;
			}
			lua_pop (L, 1);
		}

		v->SetElements (hRef, el, 0, mjd_ref, frame);
	}
	return 0;
}

int Interpreter::vesselGetProgradeDir (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	if (v) {
		OBJHANDLE hRef = v->GetGravityRef();
		VECTOR3 vel;
		MATRIX3 rot;
		v->GetRelativeVel (hRef, vel);
		v->GetRotationMatrix (rot);
		vel = tmul (rot, vel);  // rotate into vessel frame
		normalise (vel);
		lua_pushvector (L, vel);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetWeightVector (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		VECTOR3 G;
		if (v->GetWeightVector (G)) {
			lua_pushvector (L, G);
			return 1;
		}
	}
	lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetThrustVector (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		VECTOR3 T;
		v->GetThrustVector (T);
		lua_pushvector (L, T);
		return 1;
	}
	lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetLiftVector (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		VECTOR3 Lf;
		v->GetLiftVector (Lf);
		lua_pushvector (L, Lf);
		return 1;
	}
	lua_pushnil (L);
	return 1;
}

int Interpreter::v_is_landed (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	DWORD status = v->GetFlightStatus();
	if (status & 1) {
		OBJHANDLE hBody = v->GetSurfaceRef ();
		lua_pushlightuserdata (L, hBody);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_groundcontact (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop (L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushboolean (L, v->GroundContact() ? 1:0);
	return 1;
}

int Interpreter::v_set_navmode (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	int mode = (int)lua_tointeger (L,2);
	int active = true;
	if (lua_gettop(L) > 2)
		active = lua_toboolean(L,3);
	if (active)
		v->ActivateNavmode (mode);
	else
		v->DeactivateNavmode (mode);
	return 0;
}

int Interpreter::v_get_navmode (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	int mode = (int)lua_tointeger (L,2);
	bool active = v->GetNavmodeState (mode);
	lua_pushboolean (L, active?1:0);
	return 1;
}

int Interpreter::vesselGetRCSmode (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		int mode = v->GetAttitudeMode();
		lua_pushnumber (L, mode);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselSetRCSmode (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		int mode = (int)lua_tointeger (L,2);
		v->SetAttitudeMode (mode);
	}
	return 0;
}

int Interpreter::vesselGetADCmode (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		int mode = v->GetADCtrlMode();
		lua_pushnumber (L, mode);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselSetADCmode (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		int mode = (int)lua_tointeger (L,2);
		v->SetADCtrlMode (mode);
	}
	return 0;
}

int Interpreter::vesselGetADCLevel (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		AIRCTRL_TYPE surfid = (AIRCTRL_TYPE)lua_tointeger (L,2);
		double lvl = v->GetControlSurfaceLevel (surfid);
		lua_pushnumber (L, lvl);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselSetADCLevel (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		AIRCTRL_TYPE surfid = (AIRCTRL_TYPE)lua_tointeger (L,2);
		double lvl = lua_tonumber (L,3);
		v->SetControlSurfaceLevel (surfid, lvl);
	}
	return 0;
}

int Interpreter::vesselCreatePropellantResource (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	double maxmass = 1.0;
	double mass = -1.0;
	double efficiency = 1.0;
	if (v) {
		if (lua_gettop(L) > 1 && lua_isnumber (L, 2))
			maxmass = (double)lua_tonumber (L, 2);
		if (lua_gettop(L) > 2 && lua_isnumber (L, 3))
			mass = (double)lua_tonumber (L, 3);
		if (lua_gettop(L) > 3 && lua_isnumber (L, 4))
			efficiency = (double)lua_tonumber (L, 4);
		PROPELLANT_HANDLE hPrp = v->CreatePropellantResource (maxmass, mass, efficiency);
		lua_pushlightuserdata (L, hPrp);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselDelPropellantResource (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		if (lua_gettop(L) > 1 && lua_islightuserdata (L, 2)) {
			PROPELLANT_HANDLE hPrp = (PROPELLANT_HANDLE)lua_touserdata (L, 2);
			v->DelPropellantResource (hPrp);
		}
	}
	return 0;
}

int Interpreter::vesselClearPropellantResources (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		v->ClearPropellantResources();
	}
	return 0;
}

int Interpreter::vesselGetPropellantCount (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		DWORD n = v->GetPropellantCount();
		lua_pushnumber (L, n);
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetPropellantHandle (lua_State *L)
{
	if (lua_gettop(L) >= 2) {
		VESSEL *v = lua_tovessel (L,1);
		if (v && lua_isnumber (L,2)) {
			int idx = lua_tointeger (L,2);
			PROPELLANT_HANDLE hp = v->GetPropellantHandleByIndex (idx);
			if (hp) {
				lua_pushlightuserdata (L, hp);
				return 1;
			}
		}
	}
	lua_pushnil (L);
	return 1;
}

int Interpreter::vesselGetPropellantMaxMass (lua_State *L)
{
	if (lua_gettop(L) >= 2) {
		VESSEL *v = lua_tovessel(L, 1);
		if (v && lua_islightuserdata (L,2)) {
			PROPELLANT_HANDLE hp = (PROPELLANT_HANDLE)lua_touserdata (L, 2);
			if (hp) {
				lua_pushnumber (L, v->GetPropellantMaxMass (hp));
				return 1;
			}
		}
	}
	lua_pushnil (L);
	return 1;
}

int Interpreter::vesselSetPropellantMaxMass (lua_State *L)
{
	if (lua_gettop(L) >= 3) {
		VESSEL *v = lua_tovessel(L,1);
		if (v && lua_islightuserdata (L,2) && lua_isnumber (L,3)) {
			PROPELLANT_HANDLE hp = (PROPELLANT_HANDLE)lua_touserdata (L,2);
			double maxmass = (double)lua_tonumber (L,3);
			if (hp && maxmass >= 0) {
				v->SetPropellantMaxMass (hp, maxmass);
			}
		}
	}
	return 0;
}

int Interpreter::vesselGetPropellantMass (lua_State *L)
{
	VESSEL *v = lua_tovessel(L, 1);
	if (v) {
		PROPELLANT_HANDLE hProp = (PROPELLANT_HANDLE)lua_touserdata (L, 2);
		lua_pushnumber (L, v->GetPropellantMass (hProp));
	} else lua_pushnil (L);
	return 1;
}

int Interpreter::v_set_propellantmass (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
	PROPELLANT_HANDLE hp = (PROPELLANT_HANDLE)lua_touserdata (L,2);
	ASSERT_SYNTAX(hp, "Argument 1: not a propellant handle");
	double mass = (double)lua_tonumber (L,3);
	ASSERT_SYNTAX(mass >= 0, "Argument 2: expected value >= 0");
	v->SetPropellantMass (hp, mass);
	return 0;
}

int Interpreter::v_get_totalpropellantmass (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetTotalPropellantMass());
	return 1;
}

int Interpreter::v_get_propellantefficiency (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	PROPELLANT_HANDLE hp = (PROPELLANT_HANDLE)lua_touserdata (L,2);
	ASSERT_SYNTAX(hp, "Argument 1: not a propellant handle");
	lua_pushnumber (L, v->GetPropellantEfficiency(hp));
	return 1;
}

int Interpreter::v_set_propellantefficiency (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
	PROPELLANT_HANDLE hp = (PROPELLANT_HANDLE)lua_touserdata (L,2);
	ASSERT_SYNTAX(hp, "Argument 1: expected propellant handle");
	double eff = (double)lua_tonumber(L,3);
	v->SetPropellantEfficiency (hp,eff);
	return 0;
}

int Interpreter::v_get_propellantflowrate (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	PROPELLANT_HANDLE hp = (PROPELLANT_HANDLE)lua_touserdata (L,2);
	ASSERT_SYNTAX(hp, "Argument 1: expected propellant handle");
	double rate = v->GetPropellantFlowrate (hp);
	lua_pushnumber(L,rate);
	return 1;
}

int Interpreter::v_get_totalpropellantflowrate (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double rate = v->GetTotalPropellantFlowrate ();
	lua_pushnumber(L,rate);
	return 1;
}

int Interpreter::v_create_thruster (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_istable(L,2), "Argument 1: invalid type (expected table)");

	lua_getfield(L,2,"pos");
	ASSERT_SYNTAX(lua_isvector(L,-1), "Argument 1, field 'pos': expected vector");
	VECTOR3 pos = lua_tovector(L,-1);
	lua_pop(L,1);

	lua_getfield(L,2,"dir");
	ASSERT_SYNTAX(lua_isvector(L,-1), "Argument 1, field 'dir': expected vector");
	VECTOR3 dir = lua_tovector(L,-1);
	lua_pop(L,1);

	lua_getfield(L,2,"maxth0");
	ASSERT_SYNTAX(lua_isnumber(L,-1), "Argument 1, field 'maxth0': expected number");
	double maxth0 = (double)lua_tonumber(L,-1);
	lua_pop(L,1);

	PROPELLANT_HANDLE hp = NULL;
	lua_getfield(L,2,"hprop");
	if (lua_islightuserdata(L,-1)) hp = (PROPELLANT_HANDLE)lua_touserdata(L,-1);
	lua_pop(L,1);

	double isp0 = 0.0;
	lua_getfield(L,2,"isp0");
	if (lua_isnumber(L,-1)) isp0 = (double)lua_tonumber(L,-1);
	lua_pop(L,1);

	double ispr = 0.0;
	lua_getfield(L,2,"ispr");
	if (lua_isnumber(L,-1)) ispr = (double)lua_tonumber(L,-1);
	lua_pop(L,1);

	double pr = 101.4e3;
	lua_getfield(L,2,"pr");
	if (lua_isnumber(L,-1)) pr = (double)lua_tonumber(L,-1);
	lua_pop(L,1);

	THRUSTER_HANDLE th = v->CreateThruster (pos, dir, maxth0, hp, isp0, ispr, pr);
	lua_pushlightuserdata (L, th);
	return 1;
}

int Interpreter::v_del_thruster (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	bool ok = v->DelThruster (ht);
	lua_pushboolean (L, ok);
	return 1;
}

int Interpreter::v_clear_thrusters (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	v->ClearThrusterDefinitions();
	return 0;
}

int Interpreter::v_get_thrustercount (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 1, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	DWORD count = v->GetThrusterCount();
	lua_pushnumber(L,count);
	return 1;
}

int Interpreter::v_get_thrusterhandle (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");
	int idx = lua_tointeger(L,2);
	THRUSTER_HANDLE ht = v->GetThrusterHandleByIndex(idx);
	if (ht) lua_pushlightuserdata (L, ht);
	else    lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_thrusterresource (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	PROPELLANT_HANDLE hp = v->GetThrusterResource(ht);
	if (hp) lua_pushlightuserdata (L, hp);
	else    lua_pushnil (L);
	return 1;
}

int Interpreter::v_set_thrusterresource (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_islightuserdata(L,3) || lua_isnil(L,3), "Argument 2: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	PROPELLANT_HANDLE hp = (lua_isnil(L,3) ? NULL:(PROPELLANT_HANDLE)lua_touserdata(L,3));
	v->SetThrusterResource (ht, hp);
	return 0;
}

int Interpreter::v_get_thrusterpos (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	VECTOR3 pos;
	v->GetThrusterRef (ht, pos);
	lua_pushvector(L, pos);
	return 1;
}

int Interpreter::v_set_thrusterpos (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_isvector(L,3), "Argument 2: invalid type (expected vector)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	VECTOR3 pos = lua_tovector(L,3);
	v->SetThrusterRef (ht, pos);
	return 0;
}

int Interpreter::v_get_thrusterdir (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	VECTOR3 dir;
	v->GetThrusterDir (ht, dir);
	lua_pushvector(L, dir);
	return 1;
}

int Interpreter::v_set_thrusterdir (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_isvector(L,3), "Argument 2: invalid type (expected vector)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	VECTOR3 dir = lua_tovector(L,3);
	v->SetThrusterDir (ht, dir);
	return 0;
}

int Interpreter::v_get_thrustermax0 (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	double max0 = v->GetThrusterMax0 (ht);
	lua_pushnumber(L, max0);
	return 1;
}

int Interpreter::v_set_thrustermax0 (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	double max0 = lua_tonumber(L,3);
	v->SetThrusterMax0 (ht, max0);
	return 0;
}

int Interpreter::v_get_thrustermax (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	double thmax;
	if (lua_gettop(L) >= 3) {
		ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
		double pr = lua_tonumber(L,3);
		thmax = v->GetThrusterMax (ht, pr);
	} else {
		thmax = v->GetThrusterMax (ht);
	}
	lua_pushnumber(L, thmax);
	return 1;
}

int Interpreter::v_get_thrusterisp0 (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	lua_pushnumber (L, v->GetThrusterIsp0 (ht));
	return 1;
}

int Interpreter::v_get_thrusterisp (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	double isp;
	if (lua_gettop(L) >= 3) {
		ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
		double pr = lua_tonumber(L,3);
		isp = v->GetThrusterIsp (ht, pr);
	} else {
		isp = v->GetThrusterIsp (ht);
	}
	lua_pushnumber (L, isp);
	return 1;
}

int Interpreter::v_set_thrusterisp (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
	double isp0 = (double)lua_tonumber(L,3);
	if (lua_gettop(L) >= 4) {
		ASSERT_SYNTAX(lua_isnumber(L,4), "Argument 3: invalid type (expected number)");
		double ispr = (double)lua_tonumber(L,4);
		double pr = 101.4e3;
		if (lua_gettop(L) >= 5) {
			ASSERT_SYNTAX(lua_isnumber(L,5), "Argument 4: invalid type (expected number)");
			pr = (double)lua_tonumber(L,5);
		}
		v->SetThrusterIsp (ht, isp0, ispr, pr);
	} else {
		v->SetThrusterIsp (ht, isp0);
	}
	return 0;
}

int Interpreter::v_get_thrusterlevel (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	lua_pushnumber (L, v->GetThrusterLevel (ht));
	return 1;
}

int Interpreter::v_set_thrusterlevel (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	double level = (double)lua_tonumber(L,3);
	ASSERT_SYNTAX(level>=0 && level<=1, "Argument 2: value out of range (expected 0..1)");
	v->SetThrusterLevel (ht, level);
	return 0;
}

int Interpreter::v_inc_thrusterlevel (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	double dlevel = (double)lua_tonumber(L,3);
	v->IncThrusterLevel (ht, dlevel);
	return 0;
}

int Interpreter::v_inc_thrusterlevel_singlestep (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	double dlevel = (double)lua_tonumber(L,3);
	v->IncThrusterLevel_SingleStep (ht, dlevel);
	return 0;
}

int Interpreter::v_create_thrustergroup (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_istable(L,2), "Argument 1: invalid type (expected table)");
	THGROUP_TYPE thgt;
	if (lua_gettop(L) >= 3) {
		ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
		thgt = (THGROUP_TYPE)lua_tointeger(L,3);
	} else {
		thgt = THGROUP_USER;
	}

	// traverse the thruster array
	static int nht = 1;
	static THRUSTER_HANDLE *ht = new THRUSTER_HANDLE[nht];

	lua_pushnil(L);
	int i = 0;
	while (lua_next(L,2)) {
		if (i >= nht) {
			THRUSTER_HANDLE *tmp = new THRUSTER_HANDLE[i+1];
			memcpy(tmp, ht, nht*sizeof(THRUSTER_HANDLE));
			delete []ht;
			ht = tmp;
			nht = i+1;
		}
		ht[i++] = (THRUSTER_HANDLE)lua_touserdata(L,-1);
		lua_pop(L,1);
	}
	lua_pop(L,1);
	THGROUP_HANDLE htg = v->CreateThrusterGroup (ht, i, thgt);
	lua_pushlightuserdata(L,htg);
	return 1;
}

int Interpreter::v_del_thrustergroup (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	if (lua_isnumber(L,2)) {
		THGROUP_TYPE thgt = (THGROUP_TYPE)lua_tointeger(L,2);
		v->DelThrusterGroup (thgt);
	} else if (lua_islightuserdata(L,2)) {
		THGROUP_HANDLE htg = (THGROUP_HANDLE)lua_touserdata(L,2);
		v->DelThrusterGroup (htg);
	} else {
		ASSERT_SYNTAX(0, "Argument 1: invalid type (expected handle or number)");
	}
	return 0;
}

int Interpreter::v_get_thrustergrouphandle (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");

	int i = lua_tointeger(L,2);
	ASSERT_SYNTAX (i >= THGROUP_MAIN && i <= THGROUP_ATT_BACK, "Argument 1: index out of range");
	THGROUP_TYPE thgt = (THGROUP_TYPE)i;
	THGROUP_HANDLE htg = v->GetThrusterGroupHandle(thgt);
	if (htg) lua_pushlightuserdata (L, htg);
	else     lua_pushnil(L);
	return 1;
}

int Interpreter::v_get_thrustergrouphandlebyindex (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,2), "Argument 1: invalid type (expected number)");

	int idx = lua_tointeger(L,2);
	THGROUP_HANDLE htg = v->GetUserThrusterGroupHandleByIndex(idx);
	if (htg) lua_pushlightuserdata (L, htg);
	else     lua_pushnil(L);
	return 1;
}

int Interpreter::v_get_groupthrustercount (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	int count;
	if (lua_islightuserdata (L,2)) { // identify by handle
		THGROUP_HANDLE htg = (THGROUP_HANDLE)lua_touserdata (L,2);
		count = v->GetGroupThrusterCount (htg);
	} else if (lua_isnumber (L,2)) { // identify by type
		THGROUP_TYPE thgt = (THGROUP_TYPE)lua_tointeger (L,2);
		count = v->GetGroupThrusterCount (thgt);
	} else {
		ASSERT_SYNTAX(0,"Argument 1: invalid type (expected handle or number)");
	}
	lua_pushnumber (L, count);
	return 1;
}

int Interpreter::v_get_groupthruster (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber(L,3), "Argument 2: invalid type (expected number)");
	int idx = (int)lua_tointeger (L,3);
	THRUSTER_HANDLE ht;
	if (lua_islightuserdata (L,2)) { // identify by handle
		THGROUP_HANDLE htg = (THGROUP_HANDLE)lua_touserdata (L,2);
		ht = v->GetGroupThruster (htg, idx);
	} else if (lua_isnumber (L,2)) { // identify by type
		THGROUP_TYPE thgt = (THGROUP_TYPE)lua_tointeger (L,2);
		ASSERT_SYNTAX(thgt <= THGROUP_ATT_BACK, "Argument 1: out of range");
		ht = v->GetGroupThruster (thgt, idx);
	} else {
		ASSERT_SYNTAX(0,"Argument 1: invalid type (expected handle or number)");
	}
	if (ht) lua_pushlightuserdata (L, ht);
	else    lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_thrustergrouplevel (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 2, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	double level;
	if (lua_islightuserdata(L,2)) { // identified by handle
		THGROUP_HANDLE htg = (THGROUP_HANDLE)lua_touserdata (L,2);
		level = v->GetThrusterGroupLevel (htg);
	} else {                        // identified by type
		THGROUP_TYPE thgt = (THGROUP_TYPE)lua_tointeger (L,2);
		level = v->GetThrusterGroupLevel (thgt);
	}
	lua_pushnumber (L, level);
	return 1;
}

int Interpreter::v_set_thrustergrouplevel (lua_State *L)
{
	VESSEL *v = lua_tovessel (L,1);
	if (v) {
		THGROUP_TYPE thgt = (THGROUP_TYPE)(int)lua_tonumber (L,2);
		double level = lua_tonumber(L,3);
		v->SetThrusterGroupLevel (thgt, level);
	}
	return 0;
}

int Interpreter::v_inc_thrustergrouplevel (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber (L,3), "Argument 2: invalid type (expected number)");
	double dlevel = lua_tonumber (L,3);
	if (lua_islightuserdata (L,2)) { // identify by handle
		THGROUP_HANDLE htg = (THGROUP_HANDLE)lua_touserdata (L,2);
		v->IncThrusterGroupLevel (htg, dlevel);
	} else if (lua_isnumber (L,2)) { // identify by type
		THGROUP_TYPE thgt = (THGROUP_TYPE)lua_tointeger (L,2);
		v->IncThrusterGroupLevel (thgt, dlevel);
	} else {
		ASSERT_SYNTAX(0,"Argument 1: invalid type (expected handle or number)");
	}
	return 0;
}

int Interpreter::v_inc_thrustergrouplevel_singlestep (lua_State *L)
{
	ASSERT_SYNTAX(lua_gettop(L) >= 3, "Too few arguments");
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber (L,3), "Argument 2: invalid type (expected number)");
	double dlevel = lua_tonumber (L,3);
	if (lua_islightuserdata (L,2)) { // identify by handle
		THGROUP_HANDLE htg = (THGROUP_HANDLE)lua_touserdata (L,2);
		v->IncThrusterGroupLevel_SingleStep (htg, dlevel);
	} else if (lua_isnumber (L,2)) { // identify by type
		THGROUP_TYPE thgt = (THGROUP_TYPE)lua_tointeger (L,2);
		v->IncThrusterGroupLevel_SingleStep (thgt, dlevel);
	} else {
		ASSERT_SYNTAX(0,"Argument 1: invalid type (expected handle or number)");
	}
	return 0;
}

int Interpreter::v_enable_transponder (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isboolean (L,2), "Argument 1: invalid type (expected boolean)");
	int enable = lua_toboolean(L,2);
	v->EnableTransponder (enable!=0);
	return 0;
}

int Interpreter::v_get_transponder (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	NAVHANDLE hTrans = v->GetTransponder();
	if (hTrans) lua_pushlightuserdata (L, hTrans);
	else        lua_pushnil (L);
	return 1;
}

int Interpreter::v_set_transponderchannel (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber (L,2), "Argument 1: invalid type (expected number)");
	DWORD ch = (DWORD)(lua_tonumber (L,2)+0.5);
	ASSERT_SYNTAX(ch < 640, "Argument 1: out of range");
	v->SetTransponderChannel (ch);
	return 0;
}

int Interpreter::v_enable_ids (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	DOCKHANDLE hDock = (DOCKHANDLE)lua_touserdata(L,2);
	ASSERT_SYNTAX(lua_isboolean (L,3), "Argument 2: invalid type (expected boolean)");
	int enable = lua_toboolean(L,3);
	v->EnableIDS (hDock, enable!=0);
	return 0;
}

int Interpreter::v_get_ids (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	DOCKHANDLE hDock = (DOCKHANDLE)lua_touserdata(L,2);
	NAVHANDLE hIDS = v->GetIDS(hDock);
	if (hIDS) lua_pushlightuserdata (L, hIDS);
	else      lua_pushnil (L);
	return 1;
}

int Interpreter::v_set_idschannel (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_islightuserdata(L,2), "Argument 1: invalid type (expected handle)");
	DOCKHANDLE hDock = (DOCKHANDLE)lua_touserdata(L,2);
	ASSERT_SYNTAX(lua_isnumber (L,3), "Argument 2: invalid type (expected number)");
	DWORD ch = (DWORD)(lua_tonumber (L,3)+0.5);
	ASSERT_SYNTAX(ch < 640, "Argument 2: out of range");
	v->SetIDSChannel (hDock, ch);
	return 0;
}

int Interpreter::v_init_navradios (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber (L,2), "Argument 1: invalid type (expected number)");
	DWORD nnav = (DWORD)(lua_tonumber (L,2)+0.5);
	ASSERT_SYNTAX(nnav < 100, "Argument 1: out of range"); // sanity check
	v->InitNavRadios (nnav);
	return 0;
}

int Interpreter::v_get_navcount (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	lua_pushnumber (L, v->GetNavCount());
	return 1;
}

int Interpreter::v_set_navchannel (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber (L,2), "Argument 1: invalid type (expected number)");
	DWORD n = (DWORD)(lua_tonumber (L,2)+0.5);
	ASSERT_SYNTAX(lua_isnumber (L,3), "Argument 2: invalid type (expected number)");
	DWORD ch = (DWORD)(lua_tonumber (L,3)+0.5);
	ASSERT_SYNTAX(ch < 640, "Argument 2: out of range");
	v->SetNavChannel (n, ch);
	return 0;
}

int Interpreter::v_get_navchannel (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber (L,2), "Argument 1: invalid type (expected number)");
	DWORD n = (DWORD)(lua_tonumber (L,2)+0.5);
	DWORD ch = v->GetNavChannel (n);
	lua_pushnumber (L, ch);
	return 1;
}

int Interpreter::v_get_navsource (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isnumber (L,2), "Argument 1: invalid type (expected number)");
	DWORD n = (DWORD)(lua_tonumber (L,2)+0.5);
	NAVHANDLE hNav = v->GetNavSource (n);
	if (hNav) lua_pushlightuserdata(L,hNav);
	else      lua_pushnil (L);
	return 1;
}

int Interpreter::v_add_exhaust (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	ASSERT_MTDNUMBER(L,3);
	double lscale = (double)lua_tonumber(L,3);
	ASSERT_MTDNUMBER(L,4);
	double wscale = (double)lua_tonumber(L,4);
	int idx = 5;
	VECTOR3 pos, dir;
	double lofs;
	SURFHANDLE tex = 0;
	bool do_posdir = false;
	bool do_lofs = false;

	if (lua_isvector(L,idx)) { // explicit position and direction arguments
		pos = lua_tovector(L,idx++);
		ASSERT_MTDVECTOR(L,idx);
		dir = lua_tovector(L,idx++);
		do_posdir = true;
	} else if (lua_isnumber(L,idx)) {
		lofs = lua_tonumber(L,idx++);
		do_lofs = true;
	}
	if (lua_islightuserdata(L,idx)) {
		tex = (SURFHANDLE)lua_touserdata(L,idx++);
	}

	UINT exh;
	if (do_posdir)
		exh = v->AddExhaust (ht, lscale, wscale, pos, dir, tex);
	else if (do_lofs)
		exh = v->AddExhaust (ht, lscale, wscale, lofs, tex);
	else
		exh = v->AddExhaust (ht, lscale, wscale, tex);
	lua_pushnumber(L,(lua_Number)exh);
	return 1;
}

int Interpreter::v_del_exhaust (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	UINT idx = (UINT)lua_tointeger(L,2);
	bool ok = v->DelExhaust (idx);
	lua_pushboolean (L,ok);
	return 1;
}

int Interpreter::v_get_exhaustcount (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	DWORD count = v->GetExhaustCount();
	lua_pushnumber (L, count);
	return 1;
}

int Interpreter::v_add_exhauststream (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	THRUSTER_HANDLE ht = (THRUSTER_HANDLE)lua_touserdata(L,2);
	PARTICLESTREAMSPEC pss;  memset (&pss, 0, sizeof(PARTICLESTREAMSPEC));
	VECTOR3 pos;
	bool do_pos = false;
	int idx = 3;
	if (lua_isvector(L,idx)) {
		pos = lua_tovector(L,idx++);
		do_pos = true;
	}

	ASSERT_MTDTABLE(L,idx);

	lua_getfield(L,idx,"flags");
	pss.flags = (lua_isnumber(L,-1) ? (DWORD)(lua_tonumber(L,-1)+0.5) : 0);
	lua_pop(L,1);

	lua_getfield(L,idx,"srcsize");
	pss.srcsize = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 1.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"srcrate");
	pss.srcrate = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 1.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"v0");
	pss.v0 = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 0.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"srcspread");
	pss.srcspread = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 0.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"lifetime");
	pss.lifetime = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 10.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"growthrate");
	pss.growthrate = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 0.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"atmslowdown");
	pss.atmslowdown = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 0.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"ltype");
	pss.ltype = (lua_isnumber(L,-1) ? (PARTICLESTREAMSPEC::LTYPE)(int)(lua_tonumber(L,-1)+0.5) : PARTICLESTREAMSPEC::DIFFUSE);
	lua_pop(L,1);

	lua_getfield(L,idx,"levelmap");
	pss.levelmap = (lua_isnumber(L,-1) ? (PARTICLESTREAMSPEC::LEVELMAP)(int)(lua_tonumber(L,-1)+0.5) : PARTICLESTREAMSPEC::LVL_LIN);
	lua_pop(L,1);

	lua_getfield(L,idx,"lmin");
	pss.lmin = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 0.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"lmax");
	pss.lmax = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 1.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"atmsmap");
	pss.atmsmap = (lua_isnumber(L,-1) ? (PARTICLESTREAMSPEC::ATMSMAP)(int)(lua_tonumber(L,-1)+0.5) : PARTICLESTREAMSPEC::ATM_FLAT);
	lua_pop(L,1);

	lua_getfield(L,idx,"amin");
	pss.amin = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 0.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"amax");
	pss.amax = (lua_isnumber(L,-1) ? lua_tonumber(L,-1) : 1.0);
	lua_pop(L,1);

	lua_getfield(L,idx,"tex");
	pss.tex = (lua_islightuserdata(L,-1) ? (SURFHANDLE)lua_touserdata(L,-1) : NULL);
	lua_pop(L,1);

	PSTREAM_HANDLE hp;
	if (do_pos) hp = v->AddExhaustStream (ht, pos, &pss);
	else        hp = v->AddExhaustStream (ht, &pss);
	lua_pushlightuserdata(L,hp);
	return 1;
}

int Interpreter::v_add_pointlight (lua_State *L)
{
	int narg = lua_gettop(L);
	double att0 = 1e-3, att1 = 0, att2 = 1e-3;
	double range = 100;
	COLOUR4 col_diff = {1,1,1,0}, col_spec = {1,1,1,0}, col_ambi = {0,0,0,0};

	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDVECTOR(L,2);
	VECTOR3 pos = lua_tovector(L,2);
	if (narg >= 3) {
		ASSERT_MTDTABLE(L,3);
		lua_getfield(L,3,"range");
		if (lua_isnumber(L,-1)) range = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,3,"att0");
		if (lua_isnumber(L,-1)) att0 = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,3,"att1");
		if (lua_isnumber(L,-1)) att1 = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,3,"att2");
		if (lua_isnumber(L,-1)) att2 = lua_tonumber(L,-1);
		lua_pop(L,1);
		if (narg >= 4) {
			col_diff = lua_torgba(L,4);
			if (narg >= 5) {
				col_spec = lua_torgba(L,5);
				if (narg >= 6) {
					col_ambi = lua_torgba(L,6);
				}
			} else col_spec = col_diff;
		}
	}
	LightEmitter *le = v->AddPointLight (pos, range, att0, att1, att2, col_diff, col_spec, col_ambi);
	lua_pushlightemitter (L, le);
	return 1;
}

int Interpreter::v_add_spotlight (lua_State *L)
{
	int narg = lua_gettop(L);
	double att0 = 1e-3, att1 = 0, att2 = 1e-3;
	double range = 100, umbra = 20*RAD, penumbra = 40*RAD;
	COLOUR4 col_diff = {1,1,1,0}, col_spec = {1,1,1,0}, col_ambi = {0,0,0,0};

	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDVECTOR(L,2);
	VECTOR3 pos = lua_tovector(L,2);
	ASSERT_MTDVECTOR(L,3);
	VECTOR3 dir = lua_tovector(L,3);
	if (narg >= 4) {
		ASSERT_MTDTABLE(L,4);
		lua_getfield(L,4,"range");
		if (lua_isnumber(L,-1)) range = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,4,"att0");
		if (lua_isnumber(L,-1)) att0 = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,4,"att1");
		if (lua_isnumber(L,-1)) att1 = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,4,"att2");
		if (lua_isnumber(L,-1)) att2 = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,4,"umbra");
		if (lua_isnumber(L,-1)) umbra = lua_tonumber(L,-1);
		lua_pop(L,1);
		lua_getfield(L,4,"penumbra");
		if (lua_isnumber(L,-1)) penumbra = lua_tonumber(L,-1);
		lua_pop(L,1);
		if (narg >= 5) {
			col_diff = lua_torgba(L,5);
			if (narg >= 6) {
				col_spec = lua_torgba(L,6);
				if (narg >= 7) {
					col_ambi = lua_torgba(L,7);
				}
			} else col_spec = col_diff;
		}
	}
	LightEmitter *le = v->AddSpotLight (pos, dir, range, att0, att1, att2, umbra, penumbra, col_diff, col_spec, col_ambi);
	lua_pushlightemitter (L, le);
	return 1;
}

int Interpreter::v_get_lightemitter (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	DWORD idx = (DWORD)lua_tointeger(L,2);
	const LightEmitter *le = v->GetLightEmitter (idx);
	if (le) lua_pushlightemitter (L, le);
	else    lua_pushnil (L);
	return 1;
}

int Interpreter::v_get_lightemittercount (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	DWORD n = v->LightEmitterCount();
	lua_pushinteger (L, n);
	return 1;
}

int Interpreter::v_del_lightemitter (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	LightEmitter *le = lua_tolightemitter (L,2);
	bool ok = v->DelLightEmitter (le);
	lua_pushboolean (L, ok);
	return 1;
}

int Interpreter::v_clear_lightemitters (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	v->ClearLightEmitters();
	return 0;
}

int Interpreter::v_get_cameraoffset (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	VECTOR3 ofs;
	v->GetCameraOffset (ofs);
	lua_pushvector (L, ofs);
	return 1;
}

int Interpreter::v_set_cameraoffset (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_SYNTAX(lua_isvector (L,2), "Argument 1: invalid type (expected vector)");
	VECTOR3 ofs = lua_tovector (L,2);
	v->SetCameraOffset (ofs);
	return 0;
}

int Interpreter::v_add_mesh (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	UINT midx;
	VECTOR3 ofs, *pofs = 0;
	if (lua_isvector(L,3)) {
		ofs = lua_tovector(L,3);
		pofs = &ofs;
	}
	if (lua_isstring(L,2)) {
		const char *str = lua_tostring(L,2);
		midx = v->AddMesh (str, pofs);
	} else {
		ASSERT_MTDLIGHTUSERDATA(L,2);
		MESHHANDLE hMesh = (MESHHANDLE)lua_touserdata(L,2);
		midx = v->AddMesh (hMesh, pofs);
	}
	lua_pushnumber (L, midx);
	return 1;
}

int Interpreter::v_insert_mesh (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,3);
	UINT midx, idx = (UINT)(lua_tonumber(L,3)+0.5);
	VECTOR3 ofs, *pofs = 0;
	if (lua_isvector(L,4)) {
		ofs = lua_tovector(L,4);
		pofs = &ofs;
	}
	if (lua_isstring(L,2)) {
		const char *str = lua_tostring(L,2);
		midx = v->InsertMesh (str, idx, pofs);
	} else {
		ASSERT_MTDLIGHTUSERDATA(L,2);
		MESHHANDLE hMesh = (MESHHANDLE)lua_touserdata(L,2);
		midx = v->InsertMesh (hMesh, idx, pofs);
	}
	lua_pushnumber (L, midx);
	return 1;
}

int Interpreter::v_del_mesh (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	UINT idx = (UINT)(lua_tonumber(L,2)+0.5);
	bool retain_anim = false;
	if (lua_isboolean(L,3)) {
		int val = lua_toboolean(L,3);
		if (val) retain_anim = true;
	}
	bool ok = v->DelMesh (idx, retain_anim);
	lua_pushboolean (L,ok);
	return 1;
}

int Interpreter::v_clear_meshes (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	bool retain_anim = false;
	if (lua_isboolean(L,2)) {
		int val = lua_toboolean(L,2);
		if (val) retain_anim = true;
	}
	v->ClearMeshes (retain_anim);
	return 0;
}

int Interpreter::v_get_meshcount (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	UINT count = v->GetMeshCount();
	lua_pushnumber (L, count);
	return 1;
}

int Interpreter::v_shift_mesh (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	UINT idx = (UINT)(lua_tonumber(L,2)+0.5);
	ASSERT_MTDVECTOR(L,3);
	VECTOR3 ofs = lua_tovector(L,3);
	bool ok = v->ShiftMesh (idx, ofs);
	lua_pushboolean (L,ok);
	return 1;
}

int Interpreter::v_shift_meshes (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDVECTOR(L,2);
	VECTOR3 ofs = lua_tovector(L,2);
	v->ShiftMeshes (ofs);
	return 0;
}

int Interpreter::v_get_meshoffset (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	UINT idx = (UINT)lua_tointeger(L,2);
	VECTOR3 ofs;
	bool ok = v->GetMeshOffset (idx, ofs);
	if (ok) lua_pushvector (L, ofs);
	else lua_pushnil (L);
	return 1;
}

int Interpreter::v_create_animation (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	double istate = lua_tonumber(L,2);
	UINT anim = v->CreateAnimation (istate);
	lua_pushnumber (L, anim);
	return 1;
}

int Interpreter::v_del_animation (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	UINT anim = (UINT)lua_tointeger(L,2);
	bool ok = v->DelAnimation (anim);
	lua_pushboolean (L,ok);
	return 1;
}

int Interpreter::v_set_animation (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	UINT anim = (UINT)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	double state = lua_tonumber (L,3);
	bool ok = v->SetAnimation (anim, state);
	lua_pushboolean (L,ok);
	return 1;
}

int Interpreter::v_add_animationcomponent (lua_State *L)
{
	VESSEL *v = lua_tovessel(L,1);
	ASSERT_SYNTAX(v, "Invalid vessel object");
	ASSERT_MTDNUMBER(L,2);
	UINT anim = (UINT)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	double state0 = lua_tonumber(L,3);
	ASSERT_MTDNUMBER(L,4);
	double state1 = lua_tonumber(L,4);
	ASSERT_MTDLIGHTUSERDATA(L,5);
	MGROUP_TRANSFORM *trans = (MGROUP_TRANSFORM*)lua_touserdata(L,5);
	ANIMATIONCOMPONENT_HANDLE hparent = NULL;
	if (lua_islightuserdata(L,6))
		hparent = (ANIMATIONCOMPONENT_HANDLE)lua_touserdata(L,6);
	ANIMATIONCOMPONENT_HANDLE hanimcomp =
		v->AddAnimationComponent (anim, state0, state1, trans, hparent);
	lua_pushlightuserdata (L,hanimcomp);
	return 1;
}

// ============================================================================
// MFD methods

int Interpreter::mfd_get_size (lua_State *L)
{
	MFD2 *mfd = lua_tomfd(L,1);
	ASSERT_SYNTAX(mfd, "Invalid MFD object");
	lua_pushnumber (L, mfd->GetWidth());
	lua_pushnumber (L, mfd->GetHeight());
	return 2;
}

int Interpreter::mfd_set_title (lua_State *L)
{
	MFD2 *mfd = lua_tomfd(L,1);
	ASSERT_SYNTAX(mfd, "Invalid MFD object");
	oapi::Sketchpad *skp = lua_tosketchpad (L,2);
	ASSERT_SYNTAX(skp, "Invalid Sketchpad object");
	ASSERT_MTDSTRING(L,3);
	const char *title = lua_tostring(L,3);
	mfd->Title (skp, title);
	return 0;
}

int Interpreter::mfd_get_defaultpen (lua_State *L)
{
	MFD2 *mfd = lua_tomfd(L,1);
	ASSERT_SYNTAX(mfd, "Invalid MFD object");
	ASSERT_MTDNUMBER(L,2);
	DWORD intens = 0, style = 1, colidx = (DWORD)lua_tointeger(L,2);
	if (lua_gettop(L) >= 3) {
		ASSERT_MTDNUMBER(L,3);
		intens = (DWORD)lua_tointeger(L,3);
		if (lua_gettop(L) >= 4) {
			ASSERT_MTDNUMBER(L,4);
			style = lua_tointeger(L,4);
		}
	}
	oapi::Pen *pen = mfd->GetDefaultPen (colidx,intens,style);
	if (pen) lua_pushlightuserdata(L,pen);
	else     lua_pushnil(L);
	return 1;
}

int Interpreter::mfd_get_defaultfont (lua_State *L)
{
	MFD2 *mfd = lua_tomfd(L,1);
	ASSERT_SYNTAX(mfd, "Invalid MFD object");
	ASSERT_MTDNUMBER(L,2);
	DWORD fontidx = (DWORD)lua_tointeger(L,2);
	oapi::Font *font = mfd->GetDefaultFont (fontidx);
	if (font) lua_pushlightuserdata(L,font);
	else     lua_pushnil(L);
	return 1;
}

int Interpreter::mfd_invalidate_display (lua_State *L)
{
	MFD2 *mfd = lua_tomfd(L,1);
	ASSERT_SYNTAX(mfd, "Invalid MFD object");
	mfd->InvalidateDisplay();
	return 0;
}

int Interpreter::mfd_invalidate_buttons (lua_State *L)
{
	MFD2 *mfd = lua_tomfd(L,1);
	ASSERT_SYNTAX(mfd, "Invalid MFD object");
	mfd->InvalidateButtons();
	return 0;
}

// ============================================================================
// LightEmitter methods

int Interpreter::le_get_position (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	VECTOR3 pos = le->GetPosition();
	lua_pushvector (L,pos);
	return 1;
}

int Interpreter::le_set_position (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	ASSERT_MTDVECTOR(L,2);
	VECTOR3 pos = lua_tovector(L,2);
	le->SetPosition (pos);
	return 0;
}

int Interpreter::le_get_direction (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	VECTOR3 dir = le->GetDirection();
	lua_pushvector (L,dir);
	return 1;
}

int Interpreter::le_set_direction (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	ASSERT_MTDVECTOR(L,2);
	VECTOR3 dir = lua_tovector(L,2);
	le->SetDirection (dir);
	return 0;
}

int Interpreter::le_get_intensity (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	double intens = le->GetIntensity();
	lua_pushnumber (L,intens);
	return 1;
}

int Interpreter::le_set_intensity (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	ASSERT_MTDNUMBER(L,2);
	double intens = lua_tonumber(L,2);
	le->SetIntensity (intens);
	return 0;
}

int Interpreter::le_get_range (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	if (le->GetType() == LightEmitter::LT_POINT || le->GetType() == LightEmitter::LT_SPOT) {
		PointLight *point = (PointLight*)le;
		lua_pushnumber (L, point->GetRange());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int Interpreter::le_set_range (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	if (le->GetType() == LightEmitter::LT_POINT || le->GetType() == LightEmitter::LT_SPOT) {
		PointLight *point = (PointLight*)le;
		ASSERT_MTDNUMBER(L,2);
		double range = lua_tonumber(L,2);
		point->SetRange (range);
	}
	return 0;
}

int Interpreter::le_get_attenuation (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	if (le->GetType() == LightEmitter::LT_POINT || le->GetType() == LightEmitter::LT_SPOT) {
		PointLight *point = (PointLight*)le;
		const double *att = point->GetAttenuation();
		lua_pushnumber (L,att[0]);
		lua_pushnumber (L,att[1]);
		lua_pushnumber (L,att[2]);
		return 3;
	} else {
		lua_pushnil(L);
		return 1;
	}
}

int Interpreter::le_set_attenuation (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	if (le->GetType() == LightEmitter::LT_POINT || le->GetType() == LightEmitter::LT_SPOT) {
		PointLight *point = (PointLight*)le;
		ASSERT_MTDNUMBER(L,2);
		ASSERT_MTDNUMBER(L,3);
		ASSERT_MTDNUMBER(L,4);
		double att0 = lua_tonumber(L,2);
		double att1 = lua_tonumber(L,3);
		double att2 = lua_tonumber(L,4);
		point->SetAttenuation (att0, att1, att2);
	}
	return 0;
}

int Interpreter::le_get_spotaperture (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	if (le->GetType() == LightEmitter::LT_SPOT) {
		SpotLight *spot = (SpotLight*)le;
		lua_pushnumber(L,spot->GetUmbra());
		lua_pushnumber(L,spot->GetPenumbra());
		return 2;
	} else {
		lua_pushnil(L);
		return 1;
	}
}

int Interpreter::le_set_spotaperture (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	if (le->GetType() == LightEmitter::LT_SPOT) {
		SpotLight *spot = (SpotLight*)le;
		ASSERT_MTDNUMBER(L,2);
		ASSERT_MTDNUMBER(L,3);
		double umbra = lua_tonumber(L,2);
		double penumbra = lua_tonumber(L,3);
		spot->SetAperture (umbra, penumbra);
	}
	return 0;
}

int Interpreter::le_activate (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	ASSERT_MTDBOOLEAN(L,2);
	int activate = lua_toboolean(L,2);
	le->Activate (activate != 0);
	return 0;
}

int Interpreter::le_is_active (lua_State *L)
{
	LightEmitter *le = lua_tolightemitter(L,1);
	ASSERT_SYNTAX(le, "Invalid emitter object");
	bool active = le->IsActive();
	lua_pushboolean (L,active);
	return 1;
}

// ============================================================================
// Sketchpad methods

int Interpreter::skp_text (lua_State *L)
{
	int x, y, len;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	x = (int)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	y = (int)lua_tointeger(L,3);
	ASSERT_MTDSTRING(L,4);
	const char *str = lua_tostring(L,4);
	ASSERT_MTDNUMBER(L,5);
	len = (int)lua_tointeger(L,5);
	bool ok = skp->Text (x, y, str, len);
	lua_pushboolean (L, ok ? 1:0);
	return 1;
}

int Interpreter::skp_moveto (lua_State *L)
{
	int x, y;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	x = (int)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	y = (int)lua_tointeger(L,3);
	skp->MoveTo (x, y);
	return 0;
}

int Interpreter::skp_lineto (lua_State *L)
{
	int x, y;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	x = (int)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	y = (int)lua_tointeger(L,3);
	skp->LineTo (x, y);
	return 0;
}

int Interpreter::skp_line (lua_State *L)
{
	int x0, y0, x1, y1;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	x0 = (int)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	y0 = (int)lua_tointeger(L,3);
	ASSERT_MTDNUMBER(L,4);
	x1 = (int)lua_tointeger(L,4);
	ASSERT_MTDNUMBER(L,5);
	y1 = (int)lua_tointeger(L,5);
	skp->Line (x0, y0, x1, y1);
	return 0;
}

int Interpreter::skp_rectangle (lua_State *L)
{
	int x0, y0, x1, y1;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	x0 = (int)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	y0 = (int)lua_tointeger(L,3);
	ASSERT_MTDNUMBER(L,4);
	x1 = (int)lua_tointeger(L,4);
	ASSERT_MTDNUMBER(L,5);
	y1 = (int)lua_tointeger(L,5);
	skp->Rectangle (x0, y0, x1, y1);
	return 0;
}

int Interpreter::skp_ellipse (lua_State *L)
{
	int x0, y0, x1, y1;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	x0 = (int)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	y0 = (int)lua_tointeger(L,3);
	ASSERT_MTDNUMBER(L,4);
	x1 = (int)lua_tointeger(L,4);
	ASSERT_MTDNUMBER(L,5);
	y1 = (int)lua_tointeger(L,5);
	skp->Ellipse (x0, y0, x1, y1);
	return 0;
}

int Interpreter::skp_polygon (lua_State *L)
{
	oapi::IVECTOR2 *pt = 0;
	int i, npt = 0, nbuf = 0;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDTABLE(L,2);
	lua_pushnil(L);
	while(lua_next(L,2)) {
		ASSERT_TABLE(L,-1);
		if (npt == nbuf) { // grow buffer
			oapi::IVECTOR2 *tmp = new oapi::IVECTOR2[nbuf+=32];
			if (npt) {
				memcpy (tmp, pt, npt*sizeof(oapi::IVECTOR2));
				delete []pt;
			}
			pt = tmp;
		}
		lua_pushnil(L);
		for (i = 0; i < 2; i++) {
			ASSERT_SYNTAX(lua_next(L,-2),"Inconsistent vertex array");
			pt[npt].data[i] = (long)lua_tointeger(L,-1);
			lua_pop(L,1);
		}
		npt++;
		lua_pop(L,2); // pop last key and table
	}
	if (npt) {
		skp->Polygon (pt, npt);
		delete []pt;
	}
	return 0;
}

int Interpreter::skp_polyline (lua_State *L)
{
	oapi::IVECTOR2 *pt = 0;
	int i, npt = 0, nbuf = 0;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDTABLE(L,2);
	lua_pushnil(L);
	while(lua_next(L,2)) {
		ASSERT_TABLE(L,-1);
		if (npt == nbuf) { // grow buffer
			oapi::IVECTOR2 *tmp = new oapi::IVECTOR2[nbuf+=32];
			if (npt) {
				memcpy (tmp, pt, npt*sizeof(oapi::IVECTOR2));
				delete []pt;
			}
			pt = tmp;
		}
		lua_pushnil(L);
		for (i = 0; i < 2; i++) {
			ASSERT_SYNTAX(lua_next(L,-2),"Inconsistent vertex array");
			pt[npt].data[i] = (long)lua_tointeger(L,-1);
			lua_pop(L,1);
		}
		npt++;
		lua_pop(L,2); // pop last key and table
	}
	if (npt) {
		skp->Polyline (pt, npt);
		delete []pt;
	}
	return 0;
}

int Interpreter::skp_set_origin (lua_State *L)
{
	int x, y;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	x = (int)lua_tointeger(L,2);
	ASSERT_MTDNUMBER(L,3);
	y = (int)lua_tointeger(L,3);
	skp->SetOrigin (x, y);
	return 0;
}

int Interpreter::skp_set_textalign (lua_State *L)
{
	oapi::Sketchpad::TAlign_horizontal tah = oapi::Sketchpad::LEFT;
	oapi::Sketchpad::TAlign_vertical   tav = oapi::Sketchpad::TOP;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	tah = (oapi::Sketchpad::TAlign_horizontal)lua_tointeger(L,2);
	if (lua_gettop(L) >= 3) {
		ASSERT_MTDNUMBER(L,3);
		tav = (oapi::Sketchpad::TAlign_vertical)lua_tointeger(L,3);
	}
	skp->SetTextAlign (tah, tav);
	return 0;
}

int Interpreter::skp_set_textcolor (lua_State *L)
{
	DWORD col, pcol;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	col = (DWORD)lua_tointeger(L,2);
	pcol = skp->SetTextColor(col);
	lua_pushnumber (L, pcol);
	return 1;
}

int Interpreter::skp_set_backgroundcolor (lua_State *L)
{
	DWORD col, pcol;
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	col = (DWORD)lua_tointeger(L,2);
	pcol = skp->SetBackgroundColor(col);
	lua_pushnumber (L, pcol);
	return 1;
}

int Interpreter::skp_set_backgroundmode (lua_State *L)
{
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDNUMBER(L,2);
	oapi::Sketchpad::BkgMode mode = (oapi::Sketchpad::BkgMode)lua_tointeger(L, 2);
	skp->SetBackgroundMode (mode);
	return 0;
}

int Interpreter::skp_set_pen (lua_State *L)
{
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	oapi::Pen *pen = (oapi::Pen*)lua_touserdata(L,2);
	oapi::Pen *ppen = skp->SetPen (pen);
	if (ppen) lua_pushlightuserdata(L,ppen);
	else      lua_pushnil(L);
	return 1;
}

int Interpreter::skp_set_font (lua_State *L)
{
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDLIGHTUSERDATA(L,2);
	oapi::Font *font = (oapi::Font*)lua_touserdata(L,2);
	oapi::Font *pfont = skp->SetFont (font);
	if (pfont) lua_pushlightuserdata(L,pfont);
	else       lua_pushnil(L);
	return 1;
}

int Interpreter::skp_get_charsize (lua_State *L)
{
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	DWORD size = skp->GetCharSize ();
	lua_pushnumber(L, LOWORD(size));
	lua_pushnumber(L, HIWORD(size));
	return 2;
}

int Interpreter::skp_get_textwidth (lua_State *L)
{
	oapi::Sketchpad *skp = lua_tosketchpad (L,1);
	ASSERT_SYNTAX(skp, "Invalid sketchpad object");
	ASSERT_MTDSTRING(L,2);
	const char *str = lua_tostring(L,2);
	DWORD w = skp->GetTextWidth (str);
	lua_pushnumber (L,w);
	return 1;
}

// ============================================================================
// core thread functions

int OpenHelp (void *context)
{
	HELPCONTEXT *hc = (HELPCONTEXT*)context;
	oapiOpenHelp (hc);
	return 0;

}