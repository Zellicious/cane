#include "engine/vfs.h"
#include <physfs.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_fused = false;
static char g_base_dir[256] = "";

void vfs_set_fused(bool fused) { g_fused = fused; }
bool vfs_is_fused(void) { return g_fused; }

void vfs_set_base_dir(const char *dir) {
    if (dir) snprintf(g_base_dir, sizeof(g_base_dir), "%s", dir);
    else g_base_dir[0] = '\0';
}

static void resolve_path(const char *path, char *out, size_t out_size) {
    if (!g_fused && g_base_dir[0]) {
        snprintf(out, out_size, "%s/%s", g_base_dir, path);
    } else {
        snprintf(out, out_size, "%s", path);
    }
}

unsigned char* vfs_read_file(const char *path, size_t *out_size) {
    char full[512];
    resolve_path(path, full, sizeof(full));

    if (g_fused) {
        if (!PHYSFS_exists(full)) return NULL;
        PHYSFS_File *f = PHYSFS_openRead(full);
        if (!f) return NULL;

        PHYSFS_sint64 len = PHYSFS_fileLength(f);
        if (len < 0) { PHYSFS_close(f); return NULL; }

        unsigned char *buf = malloc((size_t)len);
        if (!buf) { PHYSFS_close(f); return NULL; }

        PHYSFS_sint64 got = PHYSFS_readBytes(f, buf, (PHYSFS_uint64)len);
        PHYSFS_close(f);
        if (got != len) { free(buf); return NULL; }

        if (out_size) *out_size = (size_t)len;
        return buf;
    }

    FILE *f = fopen(full, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    unsigned char *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); return NULL; }

    if (out_size) *out_size = (size_t)len;
    return buf;
}

char* vfs_read_text(const char *path) {
    size_t size;
    unsigned char *data = vfs_read_file(path, &size);
    if (!data) return NULL;

    char *text = malloc(size + 1);
    if (!text) { free(data); return NULL; }
    memcpy(text, data, size);
    text[size] = '\0';
    free(data);
    return text;
}

void vfs_free(void *ptr) { free(ptr); }

// require("foo.bar") -> foo/bar.lua, then foo/bar/init.lua
static int vfs_module_loader(lua_State *L) {
    const char *modname = luaL_checkstring(L, 1);

    char modpath[256];
    snprintf(modpath, sizeof(modpath), "%s", modname);
    for (char *p = modpath; *p; p++) if (*p == '.') *p = '/';

    char path[300];
    snprintf(path, sizeof(path), "%s.lua", modpath);
    char *src = vfs_read_text(path);

    if (!src) {
        snprintf(path, sizeof(path), "%s/init.lua", modpath);
        src = vfs_read_text(path);
    }

    if (!src) {
        lua_pushfstring(L, "\n\tno file '%s' (vfs)", path);
        return 1;
    }

    if (luaL_loadbuffer(L, src, strlen(src), path) != LUA_OK) {
        free(src);
        return lua_error(L);
    }
    free(src);
    return 1;
}

void vfs_install_lua_loader(void *lua_state) {
    lua_State *L = (lua_State*)lua_state;

    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaders"); // LuaJIT/5.1 name (5.2+ would be "searchers")
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_getfield(L, -1, "searchers");
    }

    if (lua_istable(L, -1)) {
        int n = (int)lua_objlen(L, -1);
        lua_pushcfunction(L, vfs_module_loader);
        lua_rawseti(L, -2, n + 1);
    }
    lua_pop(L, 2); // loaders table, package table
}
