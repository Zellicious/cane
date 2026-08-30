#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <glad/glad.h>
#include <stdbool.h>
#include <stddef.h>
#include "shader.h"

typedef struct Canvas {
    GLuint fbo;
    GLuint texture;
    GLuint depth_rbo;
    int width, height;
} Canvas;

Canvas* gfx_canvas_new(int width, int height);
void gfx_canvas_free(Canvas *c);
void gfx_set_canvas(Canvas *c);
Canvas* gfx_get_active_canvas(void);
void gfx_draw_canvas(Canvas *c, float x, float y, float w, float h);

typedef struct {
    GLuint texture;
    int width, height;
} Image;

Image* gfx_image_load(const char *path);
void gfx_image_free(Image *img);
void gfx_draw_image(Image *img, float x, float y, float scale);
void gfx_draw_image_shader(Image *img, float x, float y, float scale, Shader *shader);

void gfx_image_set_filter(Image *img, const char *min_filter, const char *mag_filter);
void gfx_canvas_set_filter(Canvas *c, const char *min_filter, const char *mag_filter);

typedef struct { float x, y, z, u, v, r, g, b, a; } Vertex;

typedef struct {
    GLuint vao;
    GLuint vbo;
    int vertex_count;
    int capacity;      // allocated vertex slots in the VBO (for reuse without realloc)
    GLenum draw_mode;
} Mesh;

Mesh* gfx_mesh_new(Vertex *vertices, int count, GLenum mode);
void gfx_mesh_set_vertices(Mesh *m, Vertex *vertices, int count);
void gfx_mesh_free(Mesh *m);

void gfx_draw_mesh(Mesh *m, GLuint texture);
void gfx_draw_mesh_shader(Mesh *m, GLuint texture, Shader *shader);

void gfx_set_color(float r, float g, float b, float a);
void gfx_clear(float r, float g, float b, float a);
void gfx_draw_rectangle(bool fill, float x, float y, float w, float h);
void gfx_draw_triangle(bool fill, float x1, float y1, float x2, float y2, float x3, float y3);
void gfx_draw_circle(bool fill, float cx, float cy, float radius, int segments);
void gfx_print_text(const char *text, float x, float y, float scale);

bool gfx_init(void);
void gfx_cleanup(void);
void gfx_get_dimensions(int *width, int *height);

void gfx_set_projection(int width, int height);
void gfx_set_perspective(int width, int height, float fovy_deg, float near, float far);
void gfx_set_camera(float x, float y, float z);
void gfx_set_camera_look(float x, float y, float z, float yaw_deg, float pitch_deg);

void gfx_set_shader(Shader *shader);

Shader* gfx_default_shader(void);

#endif
