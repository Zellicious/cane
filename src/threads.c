#define _DEFAULT_SOURCE
#include "threads.h"
#include "vfs.h"
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// thread-safe message queue
typedef struct MsgNode {
    char* msg;
    struct MsgNode* next;
} MsgNode;

typedef struct MsgQueue {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    MsgNode*        head;
    MsgNode*        tail;
    bool            closed;
} MsgQueue;

static void queue_init(MsgQueue* q) {
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    q->head = q->tail = NULL;
    q->closed = false;
}

static void queue_destroy(MsgQueue* q) {
    MsgNode* n = q->head;
    while (n) { MsgNode* nx = n->next; free(n->msg); free(n); n = nx; }
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

static bool queue_push(MsgQueue* q, const char* msg) {
    MsgNode* n = malloc(sizeof(MsgNode));
    if (!n) return false;
    n->msg = strdup(msg);
    n->next = NULL;
    pthread_mutex_lock(&q->mutex);
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

static char* queue_pop(MsgQueue* q, bool block) {
    pthread_mutex_lock(&q->mutex);
    while (!q->head && !q->closed && block)
        pthread_cond_wait(&q->cond, &q->mutex);
    if (!q->head) { pthread_mutex_unlock(&q->mutex); return NULL; }
    MsgNode* n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    pthread_mutex_unlock(&q->mutex);
    char* msg = n->msg;
    free(n);
    return msg;
}

static void queue_close(MsgQueue* q) {
    pthread_mutex_lock(&q->mutex);
    q->closed = true;
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

struct LuaThread {
    pthread_t  handle;
    MsgQueue   to_worker;   // main -> worker
    MsgQueue   to_main;     // worker -> main
    char*      script_path;
    bool       running;
    bool       joined;
};

typedef struct { LuaThread* thread; } WorkerCtx;

static int w_send(lua_State* L) {
    WorkerCtx* ctx = lua_touserdata(L, lua_upvalueindex(1));
    const char* msg = luaL_checkstring(L, 1);
    queue_push(&ctx->thread->to_main, msg);
    return 0;
}

static int w_receive(lua_State* L) {
    WorkerCtx* ctx = lua_touserdata(L, lua_upvalueindex(1));
    bool block = lua_isnoneornil(L, 1) ? true : lua_toboolean(L, 1);
    char* msg = queue_pop(&ctx->thread->to_worker, block);
    if (msg) { lua_pushstring(L, msg); free(msg); }
    else     { lua_pushnil(L); }
    return 1;
}

static void register_worker_api(lua_State* L, WorkerCtx* ctx) {
    luaL_openlibs(L);

    lua_newtable(L);
    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, w_send, 1);
    lua_setfield(L, -2, "send");
    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, w_receive, 1);
    lua_setfield(L, -2, "receive");
    lua_setglobal(L, "thread");

    vfs_install_lua_loader(L);
}

static void* worker_entry(void* arg) {
    LuaThread* t = arg;
    lua_State* L = luaL_newstate();
    if (!L) { t->running = false; return NULL; }

    WorkerCtx ctx = { .thread = t };
    register_worker_api(L, &ctx);

    size_t len = 0;
    char* data = (char*)vfs_read_binary(t->script_path, &len);
    if (!data) {
        fprintf(stderr, "thread: could not read %s\n", t->script_path);
        lua_close(L);
        t->running = false;
        queue_close(&t->to_main);
        return NULL;
    }

    if (luaL_loadbuffer(L, data, len, t->script_path) != LUA_OK ||
        lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "thread error: %s\n", lua_tostring(L, -1));
    }

    vfs_free(data);
    lua_close(L);
    t->running = false;
    queue_close(&t->to_main);
    return NULL;
}

LuaThread* thread_new(const char* script_path) {
    LuaThread* t = calloc(1, sizeof(LuaThread));
    if (!t) return NULL;
    t->script_path = strdup(script_path);
    queue_init(&t->to_worker);
    queue_init(&t->to_main);
    t->running = true;
    if (pthread_create(&t->handle, NULL, worker_entry, t) != 0) {
        free(t->script_path);
        queue_destroy(&t->to_worker);
        queue_destroy(&t->to_main);
        free(t);
        return NULL;
    }
    return t;
}

bool  thread_send(LuaThread* t, const char* msg)      { return t && t->running && queue_push(&t->to_worker, msg); }
char* thread_receive_blocking(LuaThread* t, bool block) {
    return t ? queue_pop(&t->to_main, block) : NULL;
}
bool  thread_is_running(LuaThread* t)                 { return t && t->running; }

void thread_stop(LuaThread* t) {
    if (!t) return;
    queue_close(&t->to_worker);   // unblocks a worker stuck in thread.receive()
}

void thread_free(LuaThread* t) {
    if (!t) return;
    if (!t->joined) { thread_stop(t); pthread_join(t->handle, NULL); t->joined = true; }
    queue_destroy(&t->to_worker);
    queue_destroy(&t->to_main);
    free(t->script_path);
    free(t);
}
