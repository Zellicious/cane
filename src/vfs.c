#include "vfs.h"
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
        PHYSFS_File *f = PHYSFS_openRead(full);
        if (!f) return NULL; // covers "doesnt exist" too, no need for a separate PHYSFS_exists check

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

unsigned char* vfs_read_binary(const char *path, size_t *out_size) {
    return vfs_read_file(path, out_size);
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

static char g_save_dir[512] = "";
static bool g_save_dir_ready = false;
static char g_identity[64] = "game";

void vfs_set_identity(const char *identity) {
    if (identity && identity[0]) {
        snprintf(g_identity, sizeof(g_identity), "%s", identity);
    }
    g_save_dir_ready = false;
}

static const char* save_dir(void) {
    if (!g_save_dir_ready) {
        const char *pref = PHYSFS_getPrefDir("cane", g_identity);
        if (pref) snprintf(g_save_dir, sizeof(g_save_dir), "%s", pref);
        g_save_dir_ready = true;
    }
    return g_save_dir;
}

static void save_path(const char *name, char *out, size_t out_size) {
    snprintf(out, out_size, "%s%s", save_dir(), name);
}

bool vfs_write_file(const char *name, const void *data, size_t size) {
    char full[768];
    save_path(name, full, sizeof(full));

    FILE *f = fopen(full, "wb");
    if (!f) return false;

    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size;
}

unsigned char* vfs_read_save_file(const char *name, size_t *out_size) {
    char full[768];
    save_path(name, full, sizeof(full));

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

char* vfs_read_save_text(const char *name) {
    size_t size;
    unsigned char *data = vfs_read_save_file(name, &size);
    if (!data) return NULL;

    char *text = malloc(size + 1);
    if (!text) { free(data); return NULL; }
    memcpy(text, data, size);
    text[size] = '\0';
    free(data);
    return text;
}

static int vfs_module_loader(lua_State *L) {
    const char *modname = luaL_checkstring(L, 1);

    char modpath[256];
    snprintf(modpath, sizeof(modpath), "%s", modname);
    for (char *p = modpath; *p; p++) if (*p == '.') *p = '/';

    char path[300];
    snprintf(path, sizeof(path), "%s.lua", modpath);
    size_t size = 0;
    unsigned char *data = vfs_read_binary(path, &size);

    if (!data) {
        snprintf(path, sizeof(path), "%s/init.lua", modpath);
        data = vfs_read_binary(path, &size);
    }

    if (!data) {
        lua_pushfstring(L, "\n\tno file '%s' (vfs)", path);
        return 1;
    }

    if (luaL_loadbuffer(L, (const char *)data, size, path) != LUA_OK) {
        vfs_free(data);
        return lua_error(L);
    }
    vfs_free(data);
    return 1;
}

void vfs_install_lua_loader(void *lua_state) {
    lua_State *L = (lua_State*)lua_state;

    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaders");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_getfield(L, -1, "searchers");
    }

    if (lua_istable(L, -1)) {
        int n = (int)lua_objlen(L, -1);
        lua_pushcfunction(L, vfs_module_loader);
        lua_rawseti(L, -2, n + 1);
    }
    lua_pop(L, 2);
}

