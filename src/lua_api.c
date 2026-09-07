#include "engine/lua_api.h"
#include "engine/graphics.h"
#include "engine/app.h"
#include <GLFW/glfw3.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "engine/audio.h"
#include "engine/vfs.h"

static GLFWwindow *s_window = NULL;

static char s_input_queue[256] = "";
static size_t s_input_queue_len = 0;

static void char_callback(GLFWwindow *window, unsigned int codepoint) {
    (void)window;
    if (codepoint > 127) return; // ASCII only, keeps this minimal
    if (s_input_queue_len < sizeof(s_input_queue) - 1) {
        s_input_queue[s_input_queue_len++] = (char)codepoint;
        s_input_queue[s_input_queue_len] = '\0';
    }
}

static double s_scroll_x = 0.0;
static double s_scroll_y = 0.0;

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    (void)window;
    s_scroll_x += xoffset;
    s_scroll_y += yoffset;
}

static int l_get_scroll(lua_State *L) {
    lua_pushnumber(L, s_scroll_x);
    lua_pushnumber(L, s_scroll_y);
    s_scroll_x = 0.0;
    s_scroll_y = 0.0;
    return 2;
}

// set_cursor_visible(bool)
static int l_set_cursor_visible(lua_State *L) {
    bool visible = lua_toboolean(L, 1);
    if (s_window) {
        glfwSetInputMode(s_window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }
    return 0;
}

static double s_last_mouse_x = 0.0;
static double s_last_mouse_y = 0.0;

// set_mouse_relative(bool) — locks and hides the cursor (GLFW_CURSOR_DISABLED)
// for FPS-style mouse-look, and enables raw motion input where the platform
// supports it (bypasses OS pointer acceleration for more accurate deltas).
// While enabled, get_mouse_pos() returns an unbounded virtual position, not
// screen coordinates — use get_mouse_delta() instead.
static int l_set_mouse_relative(lua_State *L) {
    bool enabled = lua_toboolean(L, 1);
    if (s_window) {
        glfwSetInputMode(s_window, GLFW_CURSOR, enabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(s_window, GLFW_RAW_MOUSE_MOTION, enabled ? GLFW_TRUE : GLFW_FALSE);
        }
        // reset the delta baseline so toggling this on doesn't produce one
        // huge jump from wherever the cursor happened to be
        glfwGetCursorPos(s_window, &s_last_mouse_x, &s_last_mouse_y);
    }
    return 0;
}

// get_mouse_delta() -> dx, dy since the last call. Meant to be called once
// per frame (e.g. from update()) while set_mouse_relative(true) is active.
static int l_get_mouse_delta(lua_State *L) {
    double dx = 0.0, dy = 0.0;
    if (s_window) {
        double x, y;
        glfwGetCursorPos(s_window, &x, &y);
        dx = x - s_last_mouse_x;
        dy = y - s_last_mouse_y;
        s_last_mouse_x = x;
        s_last_mouse_y = y;
    }
    lua_pushnumber(L, dx);
    lua_pushnumber(L, dy);
    return 2;
}

static int l_poll_text_input(lua_State *L) {
    lua_pushstring(L, s_input_queue);
    s_input_queue_len = 0;
    s_input_queue[0] = '\0';
    return 1;
}

static int l_sound_new(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    Sound *snd = audio_sound_load(path);
    if (!snd) { lua_pushnil(L); return 1; }

    Sound **ud = lua_newuserdata(L, sizeof(Sound*));
    *ud = snd;
    luaL_setmetatable(L, "Sound");
    return 1;
}

static int l_sound_gc(lua_State *L) {
    Sound **ud = luaL_checkudata(L, 1, "Sound");
    audio_sound_free(*ud);
    return 0;
}

static int l_sound_play(lua_State *L) {
    Sound **ud = luaL_checkudata(L, 1, "Sound");
    audio_sound_play(*ud);
    return 0;
}

static int l_sound_stop(lua_State *L) {
    Sound **ud = luaL_checkudata(L, 1, "Sound");
    audio_sound_stop(*ud);
    return 0;
}

static int l_sound_set_looping(lua_State *L) {
    Sound **ud = luaL_checkudata(L, 1, "Sound");
    bool loop = lua_toboolean(L, 2);
    audio_sound_set_looping(*ud, loop);
    return 0;
}

static int l_sound_set_volume(lua_State *L) {
    Sound **ud = luaL_checkudata(L, 1, "Sound");
    float vol = (float)luaL_checknumber(L, 2);
    audio_sound_set_volume(*ud, vol);
    return 0;
}

static int l_sound_set_pitch(lua_State *L) {
    Sound **ud = luaL_checkudata(L, 1, "Sound");
    float pitch = (float)luaL_checknumber(L, 2);
    audio_sound_set_pitch(*ud, pitch);
    return 0;
}

static const luaL_Reg sound_methods[] = {
    {"play", l_sound_play},
    {"stop", l_sound_stop},
    {"setLooping", l_sound_set_looping},
    {"setVolume", l_sound_set_volume},
    {"setPitch", l_sound_set_pitch},
    {"__gc", l_sound_gc},
    {NULL, NULL}
};

int get_glfw_key(const char *name) {
    if (strcmp(name, "space") == 0) return GLFW_KEY_SPACE;
    if (strcmp(name, "up") == 0) return GLFW_KEY_UP;
    if (strcmp(name, "down") == 0) return GLFW_KEY_DOWN;
    if (strcmp(name, "left") == 0) return GLFW_KEY_LEFT;
    if (strcmp(name, "right") == 0) return GLFW_KEY_RIGHT;

    if (strcmp(name, "tab") == 0) return GLFW_KEY_TAB;
    if (strcmp(name, "return") == 0) return GLFW_KEY_ENTER; // GLFW has no GLFW_KEY_RETURN
    if (strcmp(name, "escape") == 0) return GLFW_KEY_ESCAPE;
    if (strcmp(name, "backspace") == 0) return GLFW_KEY_BACKSPACE;

    if (strcmp(name, "ctrl") == 0 || strcmp(name, "lctrl") == 0) return GLFW_KEY_LEFT_CONTROL;
    if (strcmp(name, "rctrl") == 0) return GLFW_KEY_RIGHT_CONTROL;
    if (strcmp(name, "shift") == 0 || strcmp(name, "lshift") == 0) return GLFW_KEY_LEFT_SHIFT;
    if (strcmp(name, "rshift") == 0) return GLFW_KEY_RIGHT_SHIFT;
    if (strcmp(name, "alt") == 0 || strcmp(name, "lalt") == 0) return GLFW_KEY_LEFT_ALT;
    if (strcmp(name, "ralt") == 0) return GLFW_KEY_RIGHT_ALT;

    if (strlen(name) == 1) {
        char c = toupper(name[0]);
        if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }

    return GLFW_KEY_UNKNOWN;
}

static int l_is_key_down(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);

    if (!s_window) {
        lua_pushboolean(L, 0);
        return 1;
    }

    int glfw_key = get_glfw_key(key);
    bool pressed = false;

    if (glfw_key != GLFW_KEY_UNKNOWN) {
        pressed = (glfwGetKey(s_window, glfw_key) == GLFW_PRESS);
    }

    lua_pushboolean(L, pressed);
    return 1;
}

static int l_get_mouse_pos(lua_State *L) {
    double x, y;
    glfwGetCursorPos(s_window, &x, &y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

static int l_is_mouse_down(lua_State *L) {
    int button = (int)luaL_optinteger(L, 1, 1) - 1;
    bool pressed = glfwGetMouseButton(s_window, button) == GLFW_PRESS;
    lua_pushboolean(L, pressed);
    return 1;
}

static int l_font_new(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int pixel_size = (int)luaL_optinteger(L, 2, 24);
    Font *font = gfx_font_load(path, pixel_size);
    if (!font) { lua_pushnil(L); return 1; }

    Font **ud = lua_newuserdata(L, sizeof(Font*));
    *ud = font;
    luaL_getmetatable(L, "Font");
    lua_setmetatable(L, -2);
    return 1;
}

static int l_font_gc(lua_State *L) {
    Font **ud = luaL_checkudata(L, 1, "Font");
    gfx_font_free(*ud);
    return 0;
}

static const luaL_Reg font_methods[] = {
    {"__gc", l_font_gc},
    {NULL, NULL}
};

static int l_set_font(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        gfx_set_font(NULL);
    } else {
        Font **ud = luaL_checkudata(L, 1, "Font");
        gfx_set_font(*ud);
    }
    return 0;
}

static int l_image_new(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    Image *img = gfx_image_load(path);
    if (!img) { lua_pushnil(L); return 1; }

    Image **ud = lua_newuserdata(L, sizeof(Image*));
    *ud = img;
    luaL_getmetatable(L, "Image");
    lua_setmetatable(L, -2);
    return 1;
}

static int l_image_gc(lua_State *L) {
    Image **ud = luaL_checkudata(L, 1, "Image");
    gfx_image_free(*ud);
    return 0;
}

static int l_image_draw(lua_State *L) {
    Image **ud = luaL_checkudata(L, 1, "Image");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float scale = (float)luaL_optnumber(L, 4, 1.0);
    gfx_draw_image(*ud, x, y, scale);
    return 0;
}

static int l_image_draw_shader(lua_State *L) {
    Image **ud = luaL_checkudata(L, 1, "Image");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float scale = (float)luaL_optnumber(L, 4, 1.0f);
    Shader *shader = NULL;
    if (!lua_isnoneornil(L, 5)) {
        Shader **shader_ud = luaL_checkudata(L, 5, "Shader");
        shader = *shader_ud;
    }
    gfx_draw_image_shader(*ud, x, y, scale, shader);
    return 0;
}

static int l_image_draw_quad(lua_State *L) {
    Image **ud = luaL_checkudata(L, 1, "Image");
    float sx = (float)luaL_checknumber(L, 2);
    float sy = (float)luaL_checknumber(L, 3);
    float sw = (float)luaL_checknumber(L, 4);
    float sh = (float)luaL_checknumber(L, 5);
    float dx = (float)luaL_checknumber(L, 6);
    float dy = (float)luaL_checknumber(L, 7);
    float scale = (float)luaL_optnumber(L, 8, 1.0f);

    Shader *shader = NULL;
    if (!lua_isnoneornil(L, 9)) {
        Shader **shader_ud = luaL_checkudata(L, 9, "Shader");
        shader = *shader_ud;
    }

    gfx_draw_image_quad(*ud, sx, sy, sw, sh, dx, dy, scale, shader);
    return 0;
}

static int l_image_get_width(lua_State *L) {
    Image **ud = luaL_checkudata(L, 1, "Image");
    lua_pushinteger(L, (*ud)->width);
    return 1;
}

static int l_image_get_height(lua_State *L) {
    Image **ud = luaL_checkudata(L, 1, "Image");
    lua_pushinteger(L, (*ud)->height);
    return 1;
}

static int l_image_set_filter(lua_State *L) {
    Image **ud = luaL_checkudata(L, 1, "Image");
    const char *min_filter = luaL_optstring(L, 2, "nearest");
    const char *mag_filter = luaL_optstring(L, 3, min_filter);
    gfx_image_set_filter(*ud, min_filter, mag_filter);
    return 0;
}

static const luaL_Reg image_methods[] = {
    {"draw", l_image_draw},
    {"drawShader", l_image_draw_shader},
    {"drawQuad", l_image_draw_quad},
    {"getWidth", l_image_get_width},
    {"getHeight", l_image_get_height},
    {"setFilter", l_image_set_filter},
    {"__gc", l_image_gc},
    {NULL, NULL}
};


static int l_canvas_new(lua_State *L) {
    int w = (int)luaL_checkinteger(L, 1);
    int h = (int)luaL_checkinteger(L, 2);
    Canvas *c = gfx_canvas_new(w, h);
    if (!c) { lua_pushnil(L); return 1; }

    Canvas **ud = lua_newuserdata(L, sizeof(Canvas*));
    *ud = c;
    luaL_getmetatable(L, "Canvas");
    lua_setmetatable(L, -2);
    return 1;
}

static int l_canvas_gc(lua_State *L) {
    Canvas **ud = luaL_checkudata(L, 1, "Canvas");
    gfx_canvas_free(*ud);
    return 0;
}

static int l_canvas_draw(lua_State *L) {
    Canvas **ud = luaL_checkudata(L, 1, "Canvas");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_optnumber(L, 4, (*ud)->width);
    float h = (float)luaL_optnumber(L, 5, (*ud)->height);
    gfx_draw_canvas(*ud, x, y, w, h);
    return 0;
}

static int l_canvas_get_width(lua_State *L) {
    Canvas **ud = luaL_checkudata(L, 1, "Canvas");
    lua_pushinteger(L, (*ud)->width);
    return 1;
}

static int l_canvas_get_height(lua_State *L) {
    Canvas **ud = luaL_checkudata(L, 1, "Canvas");
    lua_pushinteger(L, (*ud)->height);
    return 1;
}

static int l_canvas_set_filter(lua_State *L) {
    Canvas **ud = luaL_checkudata(L, 1, "Canvas");
    const char *min_filter = luaL_optstring(L, 2, "nearest");
    const char *mag_filter = luaL_optstring(L, 3, min_filter);
    gfx_canvas_set_filter(*ud, min_filter, mag_filter);
    return 0;
}

static const luaL_Reg canvas_methods[] = {
    {"draw", l_canvas_draw},
    {"getWidth", l_canvas_get_width},
    {"getHeight", l_canvas_get_height},
    {"setFilter", l_canvas_set_filter},
    {"__gc", l_canvas_gc},
    {NULL, NULL}
};

static int l_set_canvas(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        gfx_set_canvas(NULL);
    } else {
        Canvas **ud = luaL_checkudata(L, 1, "Canvas");
        gfx_set_canvas(*ud);
    }
    return 0;
}

static int l_get_active_canvas(lua_State *L) {
    Canvas *c = gfx_get_active_canvas();
    if (!c) {
        lua_pushnil(L);
        return 1;
    }

    Canvas **ud = lua_newuserdata(L, sizeof(Canvas*));
    *ud = c;
    luaL_getmetatable(L, "Canvas");
    lua_setmetatable(L, -2);
    return 1;
}

static GLenum mode_from_string(const char *s) {
    if (!s) return GL_TRIANGLES;
    if (strcmp(s, "triangles") == 0) return GL_TRIANGLES;
    if (strcmp(s, "lines") == 0) return GL_LINES;
    if (strcmp(s, "line_loop") == 0) return GL_LINE_LOOP;
    if (strcmp(s, "triangle_fan") == 0) return GL_TRIANGLE_FAN;
    if (strcmp(s, "triangle_strip") == 0) return GL_TRIANGLE_STRIP;
    return GL_TRIANGLES;
}

static float table_field_num(lua_State *L, int idx, const char *key, float def) {
    lua_getfield(L, idx, key);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

static Vertex* read_vertex_array(lua_State *L, int idx, int *out_count) {
    luaL_checktype(L, idx, LUA_TTABLE);
    int n = (int)lua_objlen(L, idx);
    if (n <= 0) { *out_count = 0; return NULL; }

    Vertex *verts = malloc(sizeof(Vertex) * n);
    if (!verts) { *out_count = 0; return NULL; }

    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, idx, i + 1);
        int vt = lua_gettop(L);
        verts[i].x = table_field_num(L, vt, "x", 0.0f);
        verts[i].y = table_field_num(L, vt, "y", 0.0f);
        verts[i].z = table_field_num(L, vt, "z", 0.0f);
        verts[i].u = table_field_num(L, vt, "u", 0.0f);
        verts[i].v = table_field_num(L, vt, "v", 0.0f);
        verts[i].r = table_field_num(L, vt, "r", 1.0f);
        verts[i].g = table_field_num(L, vt, "g", 1.0f);
        verts[i].b = table_field_num(L, vt, "b", 1.0f);
        verts[i].a = table_field_num(L, vt, "a", 1.0f);
        verts[i].nx = table_field_num(L, vt, "nx", 0.0f);
        verts[i].ny = table_field_num(L, vt, "ny", 0.0f);
        verts[i].nz = table_field_num(L, vt, "nz", 0.0f);
        verts[i].nw = table_field_num(L, vt, "nw", 0.0f);
        lua_pop(L, 1);
    }

    *out_count = n;
    return verts;
}

static int l_mesh_new(lua_State *L) {
    int count;
    Vertex *verts = read_vertex_array(L, 1, &count);
    const char *mode_str = luaL_optstring(L, 2, "triangles");

    if (count <= 0) { lua_pushnil(L); return 1; }

    Mesh *m = gfx_mesh_new(verts, count, mode_from_string(mode_str));
    free(verts);
    if (!m) { lua_pushnil(L); return 1; }

    Mesh **ud = lua_newuserdata(L, sizeof(Mesh*));
    *ud = m;
    luaL_getmetatable(L, "Mesh");
    lua_setmetatable(L, -2);
    return 1;
}

static int l_mesh_gc(lua_State *L) {
    Mesh **ud = luaL_checkudata(L, 1, "Mesh");
    gfx_mesh_free(*ud);
    return 0;
}

static int l_mesh_set_vertices(lua_State *L) {
    Mesh **ud = luaL_checkudata(L, 1, "Mesh");
    int count;
    Vertex *verts = read_vertex_array(L, 2, &count);
    if (verts) {
        gfx_mesh_set_vertices(*ud, verts, count);
        free(verts);
    }
    return 0;
}

static int l_mesh_draw(lua_State *L) {
    Mesh **ud = luaL_checkudata(L, 1, "Mesh");
    GLuint tex = 0;
    if (!lua_isnoneornil(L, 2)) {
        Image **img_ud = luaL_checkudata(L, 2, "Image");
        tex = (*img_ud)->texture;
    }
    if (!lua_isnoneornil(L, 3)) {
        Shader **shader_ud = luaL_checkudata(L, 3, "Shader");
        gfx_draw_mesh_shader(*ud, tex, *shader_ud);
    } else {
        gfx_draw_mesh(*ud, tex);
    }
    return 0;
}

static const luaL_Reg mesh_methods[] = {
    {"draw", l_mesh_draw},
    {"setVertices", l_mesh_set_vertices},
    {"__gc", l_mesh_gc},
    {NULL, NULL}
};

static int l_shader_new(lua_State *L) {
    const char *vert_path = luaL_checkstring(L, 1);
    const char *frag_path = luaL_checkstring(L, 2);

    Shader *s = shader_load(vert_path, frag_path);
    if (!s) { lua_pushnil(L); return 1; }

    Shader **ud = lua_newuserdata(L, sizeof(Shader*));
    *ud = s;
    luaL_getmetatable(L, "Shader");
    lua_setmetatable(L, -2);
    return 1;
}

static int l_shader_gc(lua_State *L) {
    Shader **ud = luaL_checkudata(L, 1, "Shader");
    shader_free(*ud);
    return 0;
}

static int l_shader_send(lua_State *L) {
    Shader **ud = luaL_checkudata(L, 1, "Shader");
    const char *name = luaL_checkstring(L, 2);
    int nargs = lua_gettop(L) - 2;

    shader_use(*ud);

    if (nargs == 1) {
        if (lua_istable(L, 3)) {
            int len = (int)lua_objlen(L, 3);

            if (len == 16) {
                float m[16];
                for (int i = 0; i < 16; i++) {
                    lua_rawgeti(L, 3, i + 1);
                    m[i] = (float)luaL_checknumber(L, -1);
                    lua_pop(L, 1);
                }
                shader_set_mat4(*ud, name, m);
            } else if (len == 9) {
                float m[9];
                for (int i = 0; i < 9; i++) {
                    lua_rawgeti(L, 3, i + 1);
                    m[i] = (float)luaL_checknumber(L, -1);
                    lua_pop(L, 1);
                }
                shader_set_mat3(*ud, name, m);
            } else {
                return luaL_error(L, "shader:send matrix table must have 9 elements (mat3) or 16 elements (mat4)");
            }
        } else {
            shader_set_float(*ud, name, (float)luaL_checknumber(L, 3));
        }
    } else if (nargs == 2) {
        shader_set_vec2(*ud, name, (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
    } else if (nargs == 3) {
        shader_set_vec3(*ud, name, (float)luaL_checknumber(L, 3),
                                 (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
    } else if (nargs == 4) {
        shader_set_vec4(*ud, name, (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                                 (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6));
    } else {
        return luaL_error(L, "shader:send expects 1-4 numeric values or a matrix table after the uniform name");
    }
    return 0;
}

static int l_shader_send_int(lua_State *L) {
    Shader **ud = luaL_checkudata(L, 1, "Shader");
    const char *name = luaL_checkstring(L, 2);
    int value = (int)luaL_checkinteger(L, 3);
    shader_use(*ud);
    shader_set_int(*ud, name, value);
    return 0;
}

static int l_shader_send_texture(lua_State *L) {
    Shader **ud = luaL_checkudata(L, 1, "Shader");
    const char *name = luaL_checkstring(L, 2);
    Image **img_ud = luaL_checkudata(L, 3, "Image");
    int unit = (int)luaL_optinteger(L, 4, 1);
    shader_use(*ud);
    shader_set_texture(*ud, name, (*img_ud)->texture, unit);
    return 0;
}

static const luaL_Reg shader_methods[] = {
    {"send", l_shader_send},
    {"sendTexture", l_shader_send_texture},
    {"sendInt", l_shader_send_int},
    {"__gc", l_shader_gc},
    {NULL, NULL}
};

static int l_set_shader(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        gfx_set_shader(NULL);
    } else {
        Shader **ud = luaL_checkudata(L, 1, "Shader");
        gfx_set_shader(*ud);
    }
    return 0;
}

static int l_get_dimensions(lua_State *L) {
    int width = 0, height = 0;
    if (g_app.window) {
        glfwGetWindowSize(g_app.window, &width, &height);
    } else {
        width = g_app.config.width;
        height = g_app.config.height;
    }
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    return 2;
}

static int l_set_perspective(lua_State *L) {
    float fovy = (float)luaL_optnumber(L, 1, 60.0);
    float near = (float)luaL_optnumber(L, 2, 0.1);
    float far  = (float)luaL_optnumber(L, 3, 100.0);

    int width = g_app.config.width, height = g_app.config.height;
    if (s_window) glfwGetWindowSize(s_window, &width, &height);

    gfx_set_perspective(width, height, fovy, near, far);
    return 0;
}

static int l_set_ortho(lua_State *L) {
    (void)L;
    int width = g_app.config.width, height = g_app.config.height;
    if (s_window) glfwGetWindowSize(s_window, &width, &height);

    gfx_set_projection(width, height);
    return 0;
}

static int l_set_camera(lua_State *L) {
    float x = (float)luaL_optnumber(L, 1, 0.0);
    float y = (float)luaL_optnumber(L, 2, 0.0);
    float z = (float)luaL_optnumber(L, 3, 0.0);
    gfx_set_camera(x, y, z);
    return 0;
}

static int l_set_camera_look(lua_State *L) {
    float x = (float)luaL_optnumber(L, 1, 0.0);
    float y = (float)luaL_optnumber(L, 2, 0.0);
    float z = (float)luaL_optnumber(L, 3, 0.0);
    float yaw = (float)luaL_optnumber(L, 4, 0.0);
    float pitch = (float)luaL_optnumber(L, 5, 0.0);
    float roll = (float)luaL_optnumber(L, 6, 0.0);
    gfx_set_camera_look(x, y, z, yaw, pitch, roll);
    return 0;
}

static int l_set_camera_lookat(lua_State *L) {
    float x1 = (float)luaL_optnumber(L, 1, 0.0);
    float y1 = (float)luaL_optnumber(L, 2, 0.0);
    float z1 = (float)luaL_optnumber(L, 3, 0.0);
    float x2 = (float)luaL_optnumber(L, 4, 0.0);
    float y2 = (float)luaL_optnumber(L, 5, 0.0);
    float z2 = (float)luaL_optnumber(L, 6, 0.0);
    gfx_set_camera_lookat(x1, y1, z1, x2, y2, z2);
    return 0;
}

static int l_set_color(lua_State *L) {
    gfx_set_color((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
                  (float)luaL_checknumber(L, 3), (float)luaL_optnumber(L, 4, 1.0));
    return 0;
}

static int l_clear(lua_State *L) {
    gfx_clear((float)luaL_optnumber(L, 1, 0.0), (float)luaL_optnumber(L, 2, 0.0),
              (float)luaL_optnumber(L, 3, 0.0), (float)luaL_optnumber(L, 4, 1.0));
    return 0;
}

static int l_draw_rectangle(lua_State *L) {
    bool fill = (strcmp(luaL_checkstring(L, 1), "fill") == 0);
    gfx_draw_rectangle(fill, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                       (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
    return 0;
}

static int l_draw_triangle(lua_State *L) {
    bool fill = (strcmp(luaL_checkstring(L, 1), "fill") == 0);
    gfx_draw_triangle(fill, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                      (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                      (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7));
    return 0;
}

static int l_draw_circle(lua_State *L) {
    bool fill = (strcmp(luaL_checkstring(L, 1), "fill") == 0);
    gfx_draw_circle(fill, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                    (float)luaL_checknumber(L, 4), (int)luaL_optinteger(L, 5, 32));
    return 0;
}

static int l_print_text(lua_State *L) {
    gfx_print_text(luaL_checkstring(L, 1), (float)luaL_checknumber(L, 2),
                   (float)luaL_checknumber(L, 3), (float)luaL_optnumber(L, 4, 1.0));
    return 0;
}

static void register_type(lua_State *L, const char *name, const luaL_Reg *methods) {
    luaL_newmetatable(L, name);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, methods, 0);
    lua_pop(L, 1);
}

static int l_quit(lua_State *L) {
    (void)L;
    if (s_window) glfwSetWindowShouldClose(s_window, GLFW_TRUE);
    return 0;
}

static int l_get_text_width(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    float scale = (float)luaL_optnumber(L, 2, 1.0);
    lua_pushnumber(L, gfx_get_text_width(text, scale));
    return 1;
}

// write_file(name, data) -> true/false
static int l_write_file(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    lua_pushboolean(L, vfs_write_file(name, data, len));
    return 1;
}

// read_file(name) -> string or nil
static int l_read_file(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    char *text = vfs_read_save_text(name);
    if (!text) { lua_pushnil(L); return 1; }
    lua_pushstring(L, text);
    free(text);
    return 1;
}

void lua_api_register(lua_State *L, GLFWwindow *window) {
    s_window = window;

    lua_register(L, "set_color", l_set_color);
    lua_register(L, "clear", l_clear);
    lua_register(L, "draw_rectangle", l_draw_rectangle);
    lua_register(L, "draw_triangle", l_draw_triangle);
    lua_register(L, "draw_circle", l_draw_circle);
    lua_register(L, "print_text", l_print_text);

    lua_register(L, "new_image", l_image_new);
    lua_register(L, "new_font", l_font_new);
    lua_register(L, "set_font", l_set_font);
    lua_register(L, "new_canvas", l_canvas_new);
    lua_register(L, "set_canvas", l_set_canvas);
    lua_register(L, "get_active_canvas", l_get_active_canvas);

    lua_register(L, "new_mesh", l_mesh_new);
    lua_register(L, "new_shader", l_shader_new);
    lua_register(L, "set_shader", l_set_shader);

    lua_register(L, "set_perspective", l_set_perspective);
    lua_register(L, "set_ortho", l_set_ortho);
    lua_register(L, "set_camera", l_set_camera);
    lua_register(L, "set_camera_look", l_set_camera_look);
    lua_register(L, "set_camera_lookat", l_set_camera_lookat);

    lua_register(L, "get_dimensions", l_get_dimensions);
    lua_register(L, "quit", l_quit);
    lua_register(L, "get_text_width", l_get_text_width);
    lua_register(L, "write_file", l_write_file);
    lua_register(L, "read_file", l_read_file);

    register_type(L, "Image", image_methods);
    register_type(L, "Font", font_methods);
    register_type(L, "Canvas", canvas_methods);
    register_type(L, "Mesh", mesh_methods);
    register_type(L, "Shader", shader_methods);

    lua_register(L, "is_key_down", l_is_key_down);
    lua_register(L, "get_mouse_pos", l_get_mouse_pos);
    lua_register(L, "is_mouse_down", l_is_mouse_down);

    lua_register(L, "new_sound", l_sound_new);
    register_type(L, "Sound", sound_methods);

    glfwSetCharCallback(window, char_callback);
    glfwSetScrollCallback(window, scroll_callback);
    lua_register(L, "get_scroll", l_get_scroll);
    lua_register(L, "set_cursor_visible", l_set_cursor_visible);
    lua_register(L, "set_mouse_relative", l_set_mouse_relative);
    lua_register(L, "get_mouse_delta", l_get_mouse_delta);
    lua_register(L, "poll_text_input", l_poll_text_input);
}

void lua_api_call_global(lua_State *L, const char *func, int nargs) {
    lua_getglobal(L, func);
    if (lua_isfunction(L, -1)) {
        if (nargs > 0) lua_insert(L, -1 - nargs);
        if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
            fprintf(stderr, "lua error [%s]: %s\n", func, lua_tostring(L, -1));
        }
    } else {
        lua_pop(L, 1 + nargs);
    }
}
