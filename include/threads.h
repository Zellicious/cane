#ifndef THREADS_H
#define THREADS_H

#include <stdbool.h>

typedef struct LuaThread LuaThread;

LuaThread* thread_new(const char* script_path);
bool       thread_send(LuaThread* t, const char* msg);
char*      thread_receive_blocking(LuaThread* t, bool block);
bool       thread_is_running(LuaThread* t);
void       thread_stop(LuaThread* t);
void       thread_free(LuaThread* t);

#endif
