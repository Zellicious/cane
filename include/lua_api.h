#ifndef LUA_API_H
#define LUA_API_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

typedef struct GLFWwindow GLFWwindow;

void lua_api_register(lua_State *L, GLFWwindow *window);
void lua_api_call_global(lua_State *L, const char *func, int nargs);

#endif
