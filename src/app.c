#include "engine/app.h"
#include "engine/lua_api.h"
#include "engine/vfs.h"
#include "noto_sans_font.h"
#include "engine/audio.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

AppContext g_app = {0};

static WindowConfig load_config(lua_State *L) {
    WindowConfig cfg = {
        .width = 800, .height = 600, .title = "Window",
        .msaa = 0, .highdpi = false, .resizable = true,
        .min_width = -1, .min_height = -1, .vsync = true
    };

    lua_getglobal(L, "config");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "width");
        if (lua_isnumber(L, -1)) cfg.width = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "height");
        if (lua_isnumber(L, -1)) cfg.height = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "title");
        if (lua_isstring(L, -1)) snprintf(cfg.title, sizeof(cfg.title), "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, -1, "msaa");
        if (lua_isnumber(L, -1)) cfg.msaa = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "highdpi");
        if (lua_isboolean(L, -1)) cfg.highdpi = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "resizable");
        if (lua_isboolean(L, -1)) cfg.resizable = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "minwidth");
        if (lua_isnumber(L, -1)) cfg.min_width = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "minheight");
        if (lua_isnumber(L, -1)) cfg.min_height = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "vsync");
        if (lua_isboolean(L, -1)) cfg.vsync = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return cfg;
}

static void window_size_callback(GLFWwindow* window, int width, int height) {
    AppContext *app = (AppContext*)glfwGetWindowUserPointer(window);
    if (!app || width == 0 || height == 0) return;

    gfx_set_projection(width, height);

    lua_getglobal(app->L, "resize");
    if (lua_isfunction(app->L, -1)) {
        lua_pushinteger(app->L, width);
        lua_pushinteger(app->L, height);
        if (lua_pcall(app->L, 2, 0, 0) != LUA_OK) {
            fprintf(stderr, "lua resize error: %s\n", lua_tostring(app->L, -1));
            lua_pop(app->L, 1);
        }
    } else {
        lua_pop(app->L, 1);
    }
}

bool app_init(AppContext *app, const char *lua_script) {
    app->L = luaL_newstate();
    if (!app->L) return false;

    luaL_openlibs(app->L);
    vfs_install_lua_loader(app->L);

    char *src = vfs_read_text(lua_script);
    if (!src) {
        fprintf(stderr, "lua script error: could not read %s\n", lua_script);
        app_cleanup(app);
        return false;
    }
    if (luaL_loadbuffer(app->L, src, strlen(src), lua_script) != LUA_OK ||
        lua_pcall(app->L, 0, LUA_MULTRET, 0) != LUA_OK) {
        fprintf(stderr, "lua script error: %s\n", lua_tostring(app->L, -1));
        vfs_free(src);
        app_cleanup(app);
        return false;
    }
    vfs_free(src);

    app->config = load_config(app->L);
    app->current_canvas = NULL;

    glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);
    if (!glfwInit()) { app_cleanup(app); return false; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_RESIZABLE, app->config.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, app->config.highdpi ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, app->config.highdpi ? GLFW_TRUE : GLFW_FALSE);
    if (app->config.msaa > 0) glfwWindowHint(GLFW_SAMPLES, app->config.msaa);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    app->window = glfwCreateWindow(app->config.width, app->config.height, app->config.title, NULL, NULL);
    if (!app->window) { fprintf(stderr, "failed to create GLFW window\n"); app_cleanup(app); return false; }

    glfwSetWindowUserPointer(app->window, app);
    glfwSetWindowSizeCallback(app->window, window_size_callback);

    int min_w = app->config.min_width > 0 ? app->config.min_width : GLFW_DONT_CARE;
    int min_h = app->config.min_height > 0 ? app->config.min_height : GLFW_DONT_CARE;
    glfwSetWindowSizeLimits(app->window, min_w, min_h, GLFW_DONT_CARE, GLFW_DONT_CARE);

    glfwMakeContextCurrent(app->window);
    glfwSwapInterval(app->config.vsync ? 1 : 0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "failed to initialize GLAD\n"); app_cleanup(app); return false;
    }
    if (app->config.msaa > 0) glEnable(GL_MULTISAMPLE);

    lua_api_register(app->L, app->window);

    if (!gfx_init()) { fprintf(stderr, "failed to initialize graphics shaders\n"); app_cleanup(app); return false; }
    gfx_set_projection(app->config.width, app->config.height);

    if (FT_Init_FreeType(&app->ft)) { fprintf(stderr, "failed to initialize freetype\n"); app_cleanup(app); return false; }

    FT_Error ft_err = FT_New_Memory_Face(app->ft, ttf_NotoSans_Regular_ttf,
        (FT_Long)ttf_NotoSans_Regular_ttf_len, 0, &app->face);
    if (ft_err) {
        fprintf(stderr, "failed to load embedded font face (error code: %d)\n", ft_err);
        app->face = NULL;
        app_cleanup(app);
        return false;
    }
    FT_Set_Pixel_Sizes(app->face, 0, 24);

    if (!audio_init()) fprintf(stderr, "warning: failed to initialize audio subsystem.\n");

    return true;
}

void app_run(AppContext *app) {
    lua_api_call_global(app->L, "init", 0);
    app->last_time = glfwGetTime();

    while (!glfwWindowShouldClose(app->window)) {
        double current_time = glfwGetTime();
        double dt = current_time - app->last_time;
        app->last_time = current_time;

        lua_pushnumber(app->L, dt);
        lua_api_call_global(app->L, "update", 1);
        lua_api_call_global(app->L, "render", 0);

        glfwSwapBuffers(app->window);
        glfwPollEvents();
    }
}

void app_cleanup(AppContext *app) {
    if (app->L) lua_gc(app->L, LUA_GCCOLLECT, 0);

    gfx_cleanup();
    audio_cleanup();

    if (app->face) { FT_Done_Face(app->face); app->face = NULL; }
    if (app->ft) { FT_Done_FreeType(app->ft); app->ft = NULL; }
    if (app->window) { glfwDestroyWindow(app->window); app->window = NULL; }

    glfwTerminate();

    if (app->L) { lua_close(app->L); app->L = NULL; }
}
