#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "graphics.h"

#include <lua.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct {
    int width;
    int height;
    char title[128];
    int msaa;           // e.g., 0, 2, 4, 8 samples
    bool highdpi;       // Enable HighDPI / Retina scale support
    bool resizable;     // Allow window resizing
    int min_width;      // Minimum window width constraint (-1 for none)
    int min_height;     // Minimum window height constraint (-1 for none)
    bool vsync;         // Enable/disable vertical sync
} WindowConfig;

typedef struct {
    lua_State *L;
    GLFWwindow *window;
    WindowConfig config;
    FT_Library ft;
    FT_Face face;
    double last_time;
    Canvas *current_canvas;
} AppContext;

extern AppContext g_app;

bool app_init(AppContext *app, const char *lua_script);
void app_run(AppContext *app);
void app_cleanup(AppContext *app);

#endif
