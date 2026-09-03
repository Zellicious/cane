#include "engine/shader.h"
#include "engine/graphics.h"
#include "engine/vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GLuint compile_stage(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile error (%s): %s\n",
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "shader link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

Shader* shader_create(const char *vert_src, const char *frag_src) {
    GLuint vs = compile_stage(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, frag_src);

    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return NULL;
    }

    GLuint program = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!program) return NULL;

    Shader *s = malloc(sizeof(Shader));
    if (!s) { glDeleteProgram(program); return NULL; }
    s->program = program;
    s->uniform_count = 0;
    return s;
}

Shader* shader_load(const char *vert_path, const char *frag_path) {
    char *vert_src = vfs_read_text(vert_path);
    if (!vert_src) { fprintf(stderr, "shader: failed to open %s\n", vert_path); return NULL; }

    char *frag_src = vfs_read_text(frag_path);
    if (!frag_src) {
        fprintf(stderr, "shader: failed to open %s\n", frag_path);
        vfs_free(vert_src);
        return NULL;
    }

    Shader *s = shader_create(vert_src, frag_src);
    vfs_free(vert_src);
    vfs_free(frag_src);
    return s;
}

void shader_free(Shader *s) {
    if (!s) return;
    gfx_clear_shader_if_active(s);
    if (s->program) glDeleteProgram(s->program);
    free(s);
}

void shader_use(Shader *s) { glUseProgram(s ? s->program : 0); }

GLint shader_uniform(Shader *s, const char *name) {
    if (!s) return -1;
    for (int i = 0; i < s->uniform_count; i++) {
        if (strcmp(s->uniforms[i].name, name) == 0) return s->uniforms[i].location;
    }
    GLint loc = glGetUniformLocation(s->program, name);
    if (s->uniform_count < SHADER_MAX_UNIFORMS) {
        ShaderUniform *u = &s->uniforms[s->uniform_count++];
        snprintf(u->name, sizeof(u->name), "%s", name);
        u->location = loc;
    }
    return loc;
}

void shader_set_int(Shader *s, const char *name, int value) {
    GLint loc = shader_uniform(s, name);
    if (loc >= 0) glUniform1i(loc, value);
}
void shader_set_float(Shader *s, const char *name, float value) {
    GLint loc = shader_uniform(s, name);
    if (loc >= 0) glUniform1f(loc, value);
}
void shader_set_vec2(Shader *s, const char *name, float x, float y) {
    GLint loc = shader_uniform(s, name);
    if (loc >= 0) glUniform2f(loc, x, y);
}
void shader_set_vec3(Shader *s, const char *name, float x, float y, float z) {
    GLint loc = shader_uniform(s, name);
    if (loc >= 0) glUniform3f(loc, x, y, z);
}
void shader_set_vec4(Shader *s, const char *name, float x, float y, float z, float w) {
    GLint loc = shader_uniform(s, name);
    if (loc >= 0) glUniform4f(loc, x, y, z, w);
}
void shader_set_mat3(Shader *s, const char *name, const float *m) {
    GLint loc = shader_uniform(s, name);
    if (loc >= 0) glUniformMatrix3fv(loc, 1, GL_FALSE, m);
}
void shader_set_mat4(Shader *s, const char *name, const float *m) {
    GLint loc = shader_uniform(s, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m);
}
