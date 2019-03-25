#ifndef __CONSOLEINTERPRETER_H
#define __CONSOLEINTERPRETER_H

#include "Interpreter.h"
#include "LuaConsole.h"

// ==============================================================
// class ConsoleInterpreter

class ConsoleInterpreter: public Interpreter {
	friend class LuaConsole;
public:
	ConsoleInterpreter (LuaConsole *_console);
	void LoadAPI();
	void term_strout (const char *str, bool iserr=false);

protected:
	static int termOut (lua_State *L);
	static int termLineUp (lua_State *L);
	static int termSetVerbosity (lua_State *L);

private:
	LuaConsole *console;
};

#endif // !__CONSOLEINTERPRETER_H