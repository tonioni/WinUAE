/*
* UAE - The Un*x Amiga Emulator
*
* LUA Scripting Layer
*
* Copyright 2013 Frode SOlheim
*/

#ifndef LUASCRIPT_H_
#define LUASCRIPT_H_

#ifdef WITH_LUA
#include <lauxlib.h>

void uae_lua_init(void);
void uae_lua_load(const TCHAR *filename);
void uae_lua_loadall(void);
void uae_lua_free(void);
void uae_lua_init_state(lua_State *L);
void uae_lua_run_handler(const char *name);
void uae_lua_aquire_lock();
void uae_lua_release_lock();

#endif // WITH_LUA

#endif // LUASCRIPT_H_
