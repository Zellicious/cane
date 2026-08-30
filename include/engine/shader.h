#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <stdbool.h>

#define SHADER_MAX_UNIFORMS 64

typedef struct {
    char name[64];
    GLint location;
} ShaderUniform;

typedef struct {
    GLuint program;
    ShaderUniform uniforms[SHADER_MAX_UNIFORMS];
    int uniform_count;
} Shader;


Shader* shader_create(const char *vert_src, const char *frag_src);
Shader* shader_load(const char *vert_path, const char *frag_path);
void shader_free(Shader *s);
void shader_use(Shader *s);

GLint shader_uniform(Shader *s, const char *name);
void shader_set_int(Shader *s, const char *name, int value);
void shader_set_float(Shader *s, const char *name, float value);
void shader_set_vec2(Shader *s, const char *name, float x, float y);
void shader_set_vec3(Shader *s, const char *name, float x, float y, float z);
void shader_set_vec4(Shader *s, const char *name, float x, float y, float z, float w);
void shader_set_mat3(Shader *s, const char *name, const float *m);
void shader_set_mat4(Shader *s, const char *name, const float *m);

#endif
