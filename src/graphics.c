#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include "app.h"
#include "graphics.h"
#include "shader.h"
#include "vfs.h"
#include <GLFW/glfw3.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// helper for the weird vertex format
#define VERT(x_, y_, z_, u_, v_, r_, g_, b_, a_)                               \
    (Vertex){.x = (x_),                                                        \
             .y = (y_),                                                        \
             .z = (z_),                                                        \
             .u = (u_),                                                        \
             .v = (v_),                                                        \
             .r = (r_),                                                        \
             .g = (g_),                                                        \
             .b = (b_),                                                        \
             .a = (a_),                                                        \
             .nx = 0,                                                          \
             .ny = 0,                                                          \
             .nz = 0,                                                          \
             .nw = 0}

static float cur_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static Shader *g_main_shader = NULL;
static Shader *g_text_shader = NULL;
static Shader *g_active_shader = NULL;
static float g_cur_proj[16];
static float g_cur_view[16];
static bool g_depth_enabled = false;

static void mat4_identity(float m[16]) {
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
static void mat4_ortho(float m[16], float l, float r, float b, float t, float n,
                       float f) {
    memset(m, 0, sizeof(float) * 16);
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}
static void mat4_perspective(float m[16], float fovy_rad, float aspect,
                             float near, float far) {
    memset(m, 0, sizeof(float) * 16);
    float f = 1.0f / tanf(fovy_rad * 0.5f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}
static void mat4_translate(float m[16], float x, float y, float z) {
    mat4_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}
static void mat4_rotate_x(float m[16], float rad) {
    mat4_identity(m);
    float c = cosf(rad), s = sinf(rad);
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
}
static void mat4_rotate_y(float m[16], float rad) {
    mat4_identity(m);
    float c = cosf(rad), s = sinf(rad);
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
}
static void mat4_rotate_z(float m[16], float rad) {
    mat4_identity(m);
    float c = cosf(rad), s = sinf(rad);
    m[0] = c;
    m[1] = s;
    m[4] = -s;
    m[5] = c;
}
static void mat4_mul(float result[16], const float a[16], const float b[16]) {
    float tmp[16];
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[col * 4 + k];
            tmp[col * 4 + row] = sum;
        }
    memcpy(result, tmp, sizeof(tmp));
}

static Canvas *g_active_canvas = NULL;

static const char *vert_shader_src =
    "#version 460 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "layout (location = 2) in vec4 aColor;\n"
    "out vec2 FragTexCoord;\n"
    "out vec4 FragColor;\n"
    "uniform mat4 uProjection;\n"
    "uniform mat4 uView;\n"
    "void main() {\n"
    "    gl_Position = uProjection * uView * vec4(aPos, 1.0);\n"
    "    FragTexCoord = aTexCoord;\n"
    "    FragColor = aColor;\n"
    "}\n";

static const char *frag_shader_src =
    "#version 460 core\n"
    "in vec2 FragTexCoord;\n"
    "in vec4 FragColor;\n"
    "out vec4 FragColorOut;\n"
    "uniform sampler2D uTexture;\n"
    "uniform bool uUseTexture;\n"
    "void main() {\n"
    "    if (uUseTexture) FragColorOut = texture(uTexture, FragTexCoord) * "
    "FragColor;\n"
    "    else FragColorOut = FragColor;\n"
    "}\n";

static const char *text_frag_shader_src =
    "#version 460 core\n"
    "in vec2 FragTexCoord;\n"
    "in vec4 FragColor;\n"
    "out vec4 FragColorOut;\n"
    "uniform sampler2D uTexture;\n"
    "void main() {\n"
    "    float alpha = texture(uTexture, FragTexCoord).r;\n"
    "    FragColorOut = vec4(FragColor.rgb, FragColor.a * alpha);\n"
    "}\n";

typedef struct {
    GLuint texture;
    int width, height;
    int bearing_x, bearing_y;
    long advance;
    bool loaded;
} GlyphEntry;
#define GLYPH_CACHE_SIZE 128
static GlyphEntry g_glyph_cache[GLYPH_CACHE_SIZE];
struct Font {
    FT_Face face;
    unsigned char *data;
    GlyphEntry cache[GLYPH_CACHE_SIZE];
};
static Font *g_active_font = NULL;

static GlyphEntry *get_glyph(FT_Face face, GlyphEntry *cache, unsigned char c) {
    if (!face || c >= GLYPH_CACHE_SIZE)
        return NULL;
    GlyphEntry *g = &cache[c];
    if (g->loaded)
        return g;
    if (FT_Load_Char(face, c, FT_LOAD_RENDER))
        return NULL;
    FT_GlyphSlot slot = face->glyph;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, slot->bitmap.pitch);
    glGenTextures(1, &g->texture);
    glBindTexture(GL_TEXTURE_2D, g->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, slot->bitmap.width,
                 slot->bitmap.rows, 0, GL_RED, GL_UNSIGNED_BYTE,
                 slot->bitmap.buffer);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    g->width = slot->bitmap.width;
    g->height = slot->bitmap.rows;
    g->bearing_x = slot->bitmap_left;
    g->bearing_y = slot->bitmap_top;
    g->advance = slot->advance.x;
    g->loaded = true;
    return g;
}

static void glyph_cache_clear(void) {
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++)
        if (g_glyph_cache[i].loaded) {
            glDeleteTextures(1, &g_glyph_cache[i].texture);
            g_glyph_cache[i].loaded = false;
        }
}

Font *gfx_font_load(const char *path, int pixel_size) {
    size_t size;
    unsigned char *data = vfs_read_file(path, &size);
    if (!data) {
        fprintf(stderr, "font: failed to read %s\n", path);
        return NULL;
    }
    Font *f = malloc(sizeof(Font));
    if (!f) {
        vfs_free(data);
        return NULL;
    }
    memset(f->cache, 0, sizeof(f->cache));
    FT_Error err =
        FT_New_Memory_Face(g_app.ft, data, (FT_Long)size, 0, &f->face);
    if (err) {
        fprintf(stderr, "font: failed to load %s\n", path);
        vfs_free(data);
        free(f);
        return NULL;
    }
    FT_Set_Pixel_Sizes(f->face, 0, pixel_size > 0 ? pixel_size : 24);
    f->data = data;
    return f;
}

void gfx_font_free(Font *font) {
    if (!font)
        return;
    if (g_active_font == font)
        g_active_font = NULL;
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++)
        if (font->cache[i].loaded)
            glDeleteTextures(1, &font->cache[i].texture);
    if (font->face)
        FT_Done_Face(font->face);
    vfs_free(font->data);
    free(font);
}

void gfx_set_font(Font *font) { g_active_font = font; }

static Mesh *g_scratch_mesh = NULL;

static void draw_mesh_with_shader(Mesh *m, GLuint texture, Shader *shader) {
    if (!m || !shader)
        return;
    shader_use(shader);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    shader_set_mat4(shader, "uProjection", g_cur_proj);
    shader_set_mat4(shader, "uView", g_cur_view);
    if (texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        shader_set_int(shader, "uTexture", 0);
        shader_set_int(shader, "uUseTexture", 1);
    } else {
        shader_set_int(shader, "uUseTexture", 0);
    }
    glBindVertexArray(m->vao);
    glDrawArrays(m->draw_mode, 0, m->vertex_count);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

static void scratch_draw_ex(Vertex *verts, int count, GLenum mode,
                            GLuint texture, Shader *shader) {
    if (!g_scratch_mesh) {
        g_scratch_mesh =
            gfx_mesh_new(verts, count, mode, NULL); // NULL = legacy layout
        if (!g_scratch_mesh)
            return;
    } else {
        g_scratch_mesh->draw_mode = mode;
        gfx_mesh_set_vertices(g_scratch_mesh, verts, count);
    }
    draw_mesh_with_shader(g_scratch_mesh, texture, shader);
}

static void scratch_draw(Vertex *verts, int count, GLenum mode,
                         GLuint texture) {
    scratch_draw_ex(verts, count, mode, texture,
                    g_active_shader ? g_active_shader : g_main_shader);
}

bool gfx_init(void) {
    g_main_shader = shader_create(vert_shader_src, frag_shader_src);
    g_text_shader = shader_create(vert_shader_src, text_frag_shader_src);
    g_active_shader = NULL;
    mat4_identity(g_cur_view);
    mat4_identity(g_cur_proj);
    memset(g_glyph_cache, 0, sizeof(g_glyph_cache));
    return (g_main_shader != NULL && g_text_shader != NULL);
}

void gfx_cleanup(void) {
    glyph_cache_clear();
    if (g_scratch_mesh) {
        gfx_mesh_free(g_scratch_mesh);
        g_scratch_mesh = NULL;
    }
    shader_free(g_main_shader);
    shader_free(g_text_shader);
    g_main_shader = NULL;
    g_text_shader = NULL;
}

void gfx_get_dimensions(int *width, int *height) {
    if (g_app.window)
        glfwGetWindowSize(g_app.window, width, height);
    else {
        if (width)
            *width = 0;
        if (height)
            *height = 0;
    }
}

void gfx_set_projection(int width, int height) {
    if (g_active_canvas) {
        width = g_active_canvas->width;
        height = g_active_canvas->height;
    }
    glViewport(0, 0, width, height);
    mat4_ortho(g_cur_proj, 0.0f, (float)width, (float)height, 0.0f, -1.0f,
               1.0f);
    gfx_set_camera(0.0f, 0.0f, 0.0f);
    g_depth_enabled = false;
    glDisable(GL_DEPTH_TEST);
}

void gfx_set_ortho_projection(float left, float right, float bottom, float top,
                              float near_val, float far_val) {
    if (g_active_canvas) {
        glViewport(0, 0, g_active_canvas->width, g_active_canvas->height);
    } else {
        int w = g_app.config.width, h = g_app.config.height;
        if (g_app.window) glfwGetWindowSize(g_app.window, &w, &h);
        glViewport(0, 0, w, h);
    }

    mat4_ortho(g_cur_proj, left, right, bottom, top, near_val, far_val);
    
    g_depth_enabled = true;
    glEnable(GL_DEPTH_TEST);
}

void gfx_set_perspective(int width, int height, float fovy_deg, float near,
                         float far) {
    if (g_active_canvas) {
        width = g_active_canvas->width;
        height = g_active_canvas->height;
    }
    glViewport(0, 0, width, height);
    float aspect = (float)width / (float)height;
    mat4_perspective(g_cur_proj, fovy_deg * ((float)M_PI / 180.0f), aspect,
                     near, far);
    g_depth_enabled = true;
    glEnable(GL_DEPTH_TEST);
}

void gfx_set_camera(float x, float y, float z) {
    gfx_set_camera_look(x, y, z, 0.0f, 0.0f, 0.0f);
}

void gfx_set_camera_lookat(float x1, float y1, float z1, float x2, float y2,
                           float z2) {
    float fx = x2 - x1, fy = y2 - y1, fz = z2 - z1;
    float flen = sqrtf(fx * fx + fy * fy + fz * fz);
    if (flen > 0.00001f) {
        fx /= flen;
        fy /= flen;
        fz /= flen;
    } else {
        fx = 0.0f;
        fy = 0.0f;
        fz = -1.0f;
    }
    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    float rx = uy * fz - uz * fy, ry = uz * fx - ux * fz,
          rz = ux * fy - uy * fx;
    float rlen = sqrtf(rx * rx + ry * ry + rz * rz);
    if (rlen > 0.00001f) {
        rx /= rlen;
        ry /= rlen;
        rz /= rlen;
    } else {
        rx = 1.0f;
        ry = 0.0f;
        rz = 0.0f;
    }
    ux = fy * rz - fz * ry;
    uy = fz * rx - fx * rz;
    uz = fx * ry - fy * rx;
    memset(g_cur_view, 0, sizeof(float) * 16);
    g_cur_view[0] = rx;
    g_cur_view[4] = ry;
    g_cur_view[8] = rz;
    g_cur_view[12] = -(rx * x1 + ry * y1 + rz * z1);
    g_cur_view[1] = ux;
    g_cur_view[5] = uy;
    g_cur_view[9] = uz;
    g_cur_view[13] = -(ux * x1 + uy * y1 + uz * z1);
    g_cur_view[2] = -fx;
    g_cur_view[6] = -fy;
    g_cur_view[10] = -fz;
    g_cur_view[14] = -(-fx * x1 - fy * y1 - fz * z1);
    g_cur_view[3] = 0.0f;
    g_cur_view[7] = 0.0f;
    g_cur_view[11] = 0.0f;
    g_cur_view[15] = 1.0f;
}

void gfx_set_camera_look(float x, float y, float z, float yaw_deg,
                         float pitch_deg, float roll_deg) {
    float yaw_rad = yaw_deg * ((float)M_PI / 180.0f);
    float pitch_rad = pitch_deg * ((float)M_PI / 180.0f);
    float roll_rad = roll_deg * ((float)M_PI / 180.0f);
    float t[16], ry[16], rx[16], rz[16], tmp1[16], tmp2[16];
    mat4_translate(t, -x, -y, -z);
    mat4_rotate_y(ry, -yaw_rad);
    mat4_rotate_x(rx, -pitch_rad);
    mat4_rotate_z(rz, -roll_rad);
    mat4_mul(tmp1, ry, t);
    mat4_mul(tmp2, rx, tmp1);
    mat4_mul(g_cur_view, rz, tmp2);
}

void gfx_set_shader(Shader *shader) { g_active_shader = shader; }
void gfx_clear_shader_if_active(Shader *shader) {
    if (g_active_shader == shader)
        g_active_shader = NULL;
}
Shader *gfx_default_shader(void) { return g_main_shader; }

// ============================================================================
// Canvas format parsing
// ============================================================================
typedef struct {
    GLint internal_format;
    GLenum pixel_format;
    GLenum pixel_type;
    bool has_color;
    bool depth_only;
    bool has_stencil;
} CanvasFormat;

static bool parse_canvas_format(const char *fmt, CanvasFormat *out) {
    if (!fmt || !out) return false;
    memset(out, 0, sizeof(CanvasFormat));

    // Lowercase copy
    char buf[32];
    size_t len = strlen(fmt);
    if (len >= sizeof(buf)) return false;
    for (size_t i = 0; i < len; i++)
        buf[i] = (fmt[i] >= 'A' && fmt[i] <= 'Z') ? fmt[i] + 32 : fmt[i];
    buf[len] = '\0';

    // Check for depth-attachment suffix on color formats
    bool want_depth = false;
    if (len > 0 && buf[len - 1] == 'd') {
        want_depth = true;
        buf[--len] = '\0';
    }

    if (strcmp(buf, "rgba8") == 0) {
        out->internal_format = GL_RGBA8;
        out->pixel_format = GL_RGBA;
        out->pixel_type = GL_UNSIGNED_BYTE;
        out->has_color = true;
    } else if (strcmp(buf, "rgb8") == 0) {
        out->internal_format = GL_RGB8;
        out->pixel_format = GL_RGB;
        out->pixel_type = GL_UNSIGNED_BYTE;
        out->has_color = true;
    } else if (strcmp(buf, "rg8") == 0) {
        out->internal_format = GL_RG8;
        out->pixel_format = GL_RG;
        out->pixel_type = GL_UNSIGNED_BYTE;
        out->has_color = true;
    } else if (strcmp(buf, "r8") == 0) {
        out->internal_format = GL_R8;
        out->pixel_format = GL_RED;
        out->pixel_type = GL_UNSIGNED_BYTE;
        out->has_color = true;
    } else if (strcmp(buf, "rgba16f") == 0) {
        out->internal_format = GL_RGBA16F;
        out->pixel_format = GL_RGBA;
        out->pixel_type = GL_HALF_FLOAT;
        out->has_color = true;
    } else if (strcmp(buf, "rgba32f") == 0) {
        out->internal_format = GL_RGBA32F;
        out->pixel_format = GL_RGBA;
        out->pixel_type = GL_FLOAT;
        out->has_color = true;
    } else if (strcmp(buf, "r16f") == 0) {
        out->internal_format = GL_R16F;
        out->pixel_format = GL_RED;
        out->pixel_type = GL_HALF_FLOAT;
        out->has_color = true;
    } else if (strcmp(buf, "r32f") == 0) {
        out->internal_format = GL_R32F;
        out->pixel_format = GL_RED;
        out->pixel_type = GL_FLOAT;
        out->has_color = true;
    } else if (strcmp(buf, "depth24") == 0) {
        out->internal_format = GL_DEPTH_COMPONENT24;
        out->pixel_format = GL_DEPTH_COMPONENT;
        out->pixel_type = GL_UNSIGNED_INT;
        out->depth_only = true;
    } else if (strcmp(buf, "depth32f") == 0) {
        out->internal_format = GL_DEPTH_COMPONENT32F;
        out->pixel_format = GL_DEPTH_COMPONENT;
        out->pixel_type = GL_FLOAT;
        out->depth_only = true;
    } else if (strcmp(buf, "depth24stencil8") == 0) {
        out->internal_format = GL_DEPTH24_STENCIL8;
        out->pixel_format = GL_DEPTH_STENCIL;
        out->pixel_type = GL_UNSIGNED_INT_24_8;
        out->depth_only = true;
        out->has_stencil = true;
    } else {
        fprintf(stderr, "canvas: unknown format '%s'\n", fmt);
        return false;
    }

    // Depth-only formats already have depth; ignore 'd' suffix
    if (out->depth_only) want_depth = false;

    // Store whether caller wants a depth attachment alongside color
    // We reuse has_stencil as a flag only for stencil; use a local trick:
    // depth_only formats never need extra depth_rbo
    // Color formats: want_depth controls whether we add depth_rbo
    // We'll pass want_depth through by abusing has_stencil for color+depth
    // Actually cleaner: just return it via a separate mechanism.
    // Simplest: color formats always get depth_rbo (matching existing behavior),
    // so want_depth just means "explicitly requested" — but since existing
    // gfx_canvas_new always adds depth, we keep that default.
    // The 'd' suffix is effectively a no-op for color formats since they
    // always get depth. It's only meaningful if we later add color-only formats.
    (void)want_depth;

    return true;
}

Canvas *gfx_canvas_new_fmt(int width, int height, int samples, const char *format) {
    CanvasFormat fmt;
    if (!parse_canvas_format(format, &fmt)) {
        fprintf(stderr, "canvas_new_fmt: falling back to rgba8\n");
        return gfx_canvas_new(width, height, samples);
    }

    Canvas *c = calloc(1, sizeof(Canvas));
    if (!c) return NULL;
    c->width = width;
    c->height = height;
    c->is_msaa = (samples > 0);

    glGenFramebuffers(1, &c->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, c->fbo);

    if (fmt.depth_only) {
        // ==================================================================
        // DEPTH-ONLY: texture is the depth attachment (sampleable)
        // ==================================================================
        c->texture = 0;
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        glGenTextures(1, &c->texture);
        glBindTexture(GL_TEXTURE_2D, c->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt.internal_format,
                     width, height, 0, fmt.pixel_format, fmt.pixel_type, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (c->is_msaa) {
            // MSAA depth-only: use renderbuffer, resolve not applicable
            glGenRenderbuffers(1, &c->msaa_color_rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, c->msaa_color_rbo);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                fmt.has_stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24,
                width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            GLenum attach = fmt.has_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, attach,
                                      GL_RENDERBUFFER, c->msaa_color_rbo);
        } else {
            GLenum attach = fmt.has_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
            glFramebufferTexture2D(GL_FRAMEBUFFER, attach,
                                   GL_TEXTURE_2D, c->texture, 0);
        }

        c->depth_rbo = 0; // No separate depth RBO needed

    } else {
        // ==================================================================
        // COLOR FORMAT: texture is the color attachment
        // ==================================================================
        glGenTextures(1, &c->texture);
        glBindTexture(GL_TEXTURE_2D, c->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt.internal_format,
                     width, height, 0, fmt.pixel_format, fmt.pixel_type, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (c->is_msaa) {
            glGenRenderbuffers(1, &c->msaa_color_rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, c->msaa_color_rbo);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                             fmt.internal_format, width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, c->msaa_color_rbo);

            glGenRenderbuffers(1, &c->depth_rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, c->depth_rbo);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                             GL_DEPTH_COMPONENT24, width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, c->depth_rbo);

            glGenFramebuffers(1, &c->resolve_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, c->resolve_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, c->texture, 0);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, c->texture, 0);

            glGenRenderbuffers(1, &c->depth_rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, c->depth_rbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                  width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, c->depth_rbo);
        }
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "canvas_new_fmt: framebuffer incomplete for '%s'\n", format);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // Reuse existing free which handles all fields
        gfx_canvas_free(c);
        return NULL;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return c;
}

Canvas *gfx_canvas_new(int width, int height, int samples) {
    Canvas *c = calloc(1, sizeof(Canvas));
    if (!c)
        return NULL;
    c->width = width;
    c->height = height;
    c->is_msaa = (samples > 0);
    glGenFramebuffers(1, &c->fbo);
    glGenTextures(1, &c->texture);
    glBindTexture(GL_TEXTURE_2D, c->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (c->is_msaa) {
        glGenRenderbuffers(1, &c->msaa_color_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, c->msaa_color_rbo);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA,
                                         width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glGenRenderbuffers(1, &c->depth_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, c->depth_rbo);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                         GL_DEPTH_COMPONENT24, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, c->fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, c->msaa_color_rbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, c->depth_rbo);
        glGenFramebuffers(1, &c->resolve_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, c->resolve_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, c->texture, 0);
    } else {
        glGenRenderbuffers(1, &c->depth_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, c->depth_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width,
                              height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, c->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, c->texture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, c->depth_rbo);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "canvas: framebuffer incomplete\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &c->fbo);
        if (c->is_msaa) {
            glDeleteFramebuffers(1, &c->resolve_fbo);
            glDeleteRenderbuffers(1, &c->msaa_color_rbo);
        }
        glDeleteRenderbuffers(1, &c->depth_rbo);
        glDeleteTextures(1, &c->texture);
        free(c);
        return NULL;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return c;
}

void gfx_canvas_free(Canvas *c) {
    if (!c)
        return;
    glDeleteFramebuffers(1, &c->fbo);
    if (c->is_msaa) {
        glDeleteFramebuffers(1, &c->resolve_fbo);
        glDeleteRenderbuffers(1, &c->msaa_color_rbo);
    }
    glDeleteRenderbuffers(1, &c->depth_rbo);
    glDeleteTextures(1, &c->texture);
    free(c);
}

void gfx_set_canvas(Canvas *c) {
    g_active_canvas = c;
    if (c) {
        glBindFramebuffer(GL_FRAMEBUFFER, c->fbo);
        
        // If this is a depth-only canvas, ensure draw buffers are none
        if (c->depth_rbo == 0 && !c->is_msaa) {
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }
        
        glClear(GL_DEPTH_BUFFER_BIT);
        gfx_set_projection(c->width, c->height);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int w, h;
        gfx_get_dimensions(&w, &h);
        gfx_set_projection(w, h);
    }
}

Canvas *gfx_get_active_canvas(void) { return g_active_canvas; }

void gfx_draw_canvas(Canvas *c, float x, float y, float w, float h) {
    if (!c)
        return;
    GLuint tex_to_draw = c->texture;
    if (c->is_msaa) {
        GLint current_draw_fbo;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current_draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, c->fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, c->resolve_fbo);
        glBlitFramebuffer(0, 0, c->width, c->height, 0, 0, c->width, c->height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, current_draw_fbo);
    }
    Vertex verts[6] = {
        VERT(x, y, 0, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f),
        VERT(x + w, y, 0, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f),
        VERT(x + w, y + h, 0, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f),
        VERT(x, y, 0, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f),
        VERT(x + w, y + h, 0, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f),
        VERT(x, y + h, 0, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f)};
    scratch_draw(verts, 6, GL_TRIANGLES, tex_to_draw);
}

Image *gfx_image_load(const char *path) {
    size_t file_size;
    unsigned char *file_data = vfs_read_file(path, &file_size);
    if (!file_data) {
        fprintf(stderr, "image read failed: %s\n", path);
        return NULL;
    }
    int w, h, channels;
    unsigned char *data =
        stbi_load_from_memory(file_data, (int)file_size, &w, &h, &channels, 4);
    vfs_free(file_data);
    if (!data) {
        fprintf(stderr, "image decode failed: %s\n", path);
        return NULL;
    }
    Image *img = malloc(sizeof(Image));
    if (!img) {
        stbi_image_free(data);
        return NULL;
    }
    img->width = w;
    img->height = h;
    glGenTextures(1, &img->texture);
    glBindTexture(GL_TEXTURE_2D, img->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return img;
}

void gfx_image_free(Image *img) {
    if (!img)
        return;
    glDeleteTextures(1, &img->texture);
    free(img);
}

void gfx_draw_image(Image *img, float x, float y, float scale) {
    if (!img)
        return;
    float w = img->width * scale, h = img->height * scale;
    Vertex verts[6] = {VERT(x, y, 0, 0, 0, 1, 1, 1, 1),
                       VERT(x + w, y, 0, 1, 0, 1, 1, 1, 1),
                       VERT(x + w, y + h, 0, 1, 1, 1, 1, 1, 1),
                       VERT(x, y, 0, 0, 0, 1, 1, 1, 1),
                       VERT(x + w, y + h, 0, 1, 1, 1, 1, 1, 1),
                       VERT(x, y + h, 0, 0, 1, 1, 1, 1, 1)};
    scratch_draw(verts, 6, GL_TRIANGLES, img->texture);
}

void gfx_draw_image_shader(Image *img, float x, float y, float scale,
                           Shader *shader) {
    if (!img)
        return;
    float w = img->width * scale, h = img->height * scale;
    Vertex verts[6] = {VERT(x, y, 0, 0, 0, 1, 1, 1, 1),
                       VERT(x + w, y, 0, 1, 0, 1, 1, 1, 1),
                       VERT(x + w, y + h, 0, 1, 1, 1, 1, 1, 1),
                       VERT(x, y, 0, 0, 0, 1, 1, 1, 1),
                       VERT(x + w, y + h, 0, 1, 1, 1, 1, 1, 1),
                       VERT(x, y + h, 0, 0, 1, 1, 1, 1, 1)};
    Shader *target =
        shader ? shader : (g_active_shader ? g_active_shader : g_main_shader);
    scratch_draw_ex(verts, 6, GL_TRIANGLES, img->texture, target);
}

void gfx_draw_image_quad(Image *img, float sx, float sy, float sw, float sh,
                         float dx, float dy, float scale, Shader *shader) {
    if (!img || img->width <= 0 || img->height <= 0)
        return;
    float u0 = sx / (float)img->width, v0 = sy / (float)img->height,
          u1 = (sx + sw) / (float)img->width,
          v1 = (sy + sh) / (float)img->height;
    float w = sw * scale, h = sh * scale;
    Vertex verts[6] = {VERT(dx, dy, 0, u0, v0, 1, 1, 1, 1),
                       VERT(dx + w, dy, 0, u1, v0, 1, 1, 1, 1),
                       VERT(dx + w, dy + h, 0, u1, v1, 1, 1, 1, 1),
                       VERT(dx, dy, 0, u0, v0, 1, 1, 1, 1),
                       VERT(dx + w, dy + h, 0, u1, v1, 1, 1, 1, 1),
                       VERT(dx, dy + h, 0, u0, v1, 1, 1, 1, 1)};
    Shader *target =
        shader ? shader : (g_active_shader ? g_active_shader : g_main_shader);
    scratch_draw_ex(verts, 6, GL_TRIANGLES, img->texture, target);
}

static GLenum parse_filter(const char *f) {
    return (f && strcmp(f, "linear") == 0) ? GL_LINEAR : GL_NEAREST;
}
void gfx_image_set_filter(Image *img, const char *min_f, const char *mag_f) {
    if (!img)
        return;
    glBindTexture(GL_TEXTURE_2D, img->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, parse_filter(min_f));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, parse_filter(mag_f));
    glBindTexture(GL_TEXTURE_2D, 0);
}
void gfx_canvas_set_filter(Canvas *c, const char *min_f, const char *mag_f) {
    if (!c)
        return;
    glBindTexture(GL_TEXTURE_2D, c->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, parse_filter(min_f));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, parse_filter(mag_f));
    glBindTexture(GL_TEXTURE_2D, 0);
}

// --- UPDATED MESH FUNCTIONS ---
Mesh *gfx_mesh_new(void *data, int count, GLenum mode,
                   const VertexLayout *layout) {
    Mesh *m = malloc(sizeof(Mesh));
    if (!m)
        return NULL;
    m->vertex_count = count;
    m->draw_mode = mode;
    if (layout) {
        m->layout = *layout;
    } else {
        m->layout.attr_count = 4;
        m->layout.stride = sizeof(Vertex);
        m->layout.attrs[0] = (VertexAttr){0, ATTR_VEC3, offsetof(Vertex, x)};
        m->layout.attrs[1] = (VertexAttr){1, ATTR_VEC2, offsetof(Vertex, u)};
        m->layout.attrs[2] = (VertexAttr){2, ATTR_VEC4, offsetof(Vertex, r)};
        m->layout.attrs[3] = (VertexAttr){3, ATTR_VEC4, offsetof(Vertex, nx)};
    }
    glGenVertexArrays(1, &m->vao);
    glGenBuffers(1, &m->vbo);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, count * m->layout.stride, data,
                 GL_DYNAMIC_DRAW);
    for (int i = 0; i < m->layout.attr_count; i++) {
        const VertexAttr *attr = &m->layout.attrs[i];
        int size = 1;
        if (attr->type == ATTR_VEC2)
            size = 2;
        else if (attr->type == ATTR_VEC3)
            size = 3;
        else if (attr->type == ATTR_VEC4)
            size = 4;
        glVertexAttribPointer(attr->location, size, GL_FLOAT, GL_FALSE,
                              m->layout.stride,
                              (void *)(uintptr_t)attr->offset);
        glEnableVertexAttribArray(attr->location);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return m;
}

void gfx_mesh_set_vertices(Mesh *m, void *data, int count) {
    if (!m)
        return;
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, count * m->layout.stride, data,
                 GL_DYNAMIC_DRAW);
    m->vertex_count = count;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void gfx_mesh_free(Mesh *m) {
    if (!m)
        return;
    glDeleteVertexArrays(1, &m->vao);
    glDeleteBuffers(1, &m->vbo);
    free(m);
}
void gfx_draw_mesh(Mesh *m, GLuint texture) {
    draw_mesh_with_shader(m, texture,
                          g_active_shader ? g_active_shader : g_main_shader);
}
void gfx_draw_mesh_shader(Mesh *m, GLuint texture, Shader *shader) {
    draw_mesh_with_shader(m, texture, shader ? shader : g_main_shader);
}

void gfx_set_color(float r, float g, float b, float a) {
    cur_color[0] = r;
    cur_color[1] = g;
    cur_color[2] = b;
    cur_color[3] = a;
}
void gfx_set_face_cull(const char *mode) {
    if (!mode || strcmp(mode, "none") == 0) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        if (strcmp(mode, "front") == 0) {
            glCullFace(GL_FRONT);
        } else if (strcmp(mode, "front_and_back") == 0) {
            glCullFace(GL_FRONT_AND_BACK);
        } else {
            // Default to back-face culling
            glCullFace(GL_BACK);
        }
    }
}
void gfx_clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(g_depth_enabled ? (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
                            : GL_COLOR_BUFFER_BIT);
}

void gfx_draw_rectangle(bool fill, float x, float y, float w, float h) {
    float r = cur_color[0], g = cur_color[1], b = cur_color[2],
          a = cur_color[3];
    if (fill) {
        Vertex verts[6] = {VERT(x, y, 0, 0, 0, r, g, b, a),
                           VERT(x + w, y, 0, 0, 0, r, g, b, a),
                           VERT(x + w, y + h, 0, 0, 0, r, g, b, a),
                           VERT(x, y, 0, 0, 0, r, g, b, a),
                           VERT(x + w, y + h, 0, 0, 0, r, g, b, a),
                           VERT(x, y + h, 0, 0, 0, r, g, b, a)};
        scratch_draw(verts, 6, GL_TRIANGLES, 0);
    } else {
        Vertex verts[4] = {VERT(x, y, 0, 0, 0, r, g, b, a),
                           VERT(x + w, y, 0, 0, 0, r, g, b, a),
                           VERT(x + w, y + h, 0, 0, 0, r, g, b, a),
                           VERT(x, y + h, 0, 0, 0, r, g, b, a)};
        scratch_draw(verts, 4, GL_LINE_LOOP, 0);
    }
}

void gfx_draw_triangle(bool fill, float x1, float y1, float x2, float y2,
                       float x3, float y3) {
    float r = cur_color[0], g = cur_color[1], b = cur_color[2],
          a = cur_color[3];
    Vertex verts[3] = {VERT(x1, y1, 0, 0, 0, r, g, b, a),
                       VERT(x2, y2, 0, 0, 0, r, g, b, a),
                       VERT(x3, y3, 0, 0, 0, r, g, b, a)};
    scratch_draw(verts, 3, fill ? GL_TRIANGLES : GL_LINE_LOOP, 0);
}

void gfx_draw_circle(bool fill, float cx, float cy, float radius,
                     int segments) {
    if (segments < 3)
        segments = 3;
    float r = cur_color[0], g = cur_color[1], b = cur_color[2],
          a = cur_color[3];
    int count = fill ? (segments + 2) : (segments + 1);
    Vertex stack_buf[130];
    Vertex *verts = (count <= 130) ? stack_buf : malloc(sizeof(Vertex) * count);
    if (!verts)
        return;
    int idx = 0;
    if (fill)
        verts[idx++] = (Vertex)VERT(cx, cy, 0, 0, 0, r, g, b, a);
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        verts[idx++] =
            (Vertex)VERT(cx + radius * cosf(theta), cy + radius * sinf(theta),
                         0, 0, 0, r, g, b, a);
    }
    scratch_draw(verts, count, fill ? GL_TRIANGLE_FAN : GL_LINE_LOOP, 0);
    if (verts != stack_buf)
        free(verts);
}

void gfx_print_text(const char *text, float x, float y, float scale) {
    FT_Face face = g_active_font ? g_active_font->face : g_app.face;
    GlyphEntry *cache = g_active_font ? g_active_font->cache : g_glyph_cache;
    if (!face)
        return;
    float r = cur_color[0], g = cur_color[1], b = cur_color[2],
          a = cur_color[3];
    float ascender = (float)(face->size->metrics.ascender >> 6);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        GlyphEntry *glyph = get_glyph(face, cache, *p);
        if (!glyph)
            continue;
        if (glyph->width > 0 && glyph->height > 0) {
            float xpos = x + glyph->bearing_x * scale,
                  ypos = y + (ascender - glyph->bearing_y) * scale;
            float w = glyph->width * scale, h = glyph->height * scale;
            Vertex verts[6] = {VERT(xpos, ypos, 0, 0, 0, r, g, b, a),
                               VERT(xpos + w, ypos, 0, 1, 0, r, g, b, a),
                               VERT(xpos + w, ypos + h, 0, 1, 1, r, g, b, a),
                               VERT(xpos, ypos, 0, 0, 0, r, g, b, a),
                               VERT(xpos + w, ypos + h, 0, 1, 1, r, g, b, a),
                               VERT(xpos, ypos + h, 0, 0, 1, r, g, b, a)};
            scratch_draw_ex(verts, 6, GL_TRIANGLES, glyph->texture,
                            g_text_shader);
        }
        x += (glyph->advance >> 6) * scale;
    }
}

float gfx_get_text_width(const char *text, float scale) {
    FT_Face face = g_active_font ? g_active_font->face : g_app.face;
    GlyphEntry *cache = g_active_font ? g_active_font->cache : g_glyph_cache;
    if (!face || !text)
        return 0.0f;
    float width = 0.0f;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        GlyphEntry *glyph = get_glyph(face, cache, *p);
        if (!glyph)
            continue;
        width += (glyph->advance >> 6) * scale;
    }
    return width;
}
