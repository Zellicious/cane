#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdbool.h>

void vfs_set_fused(bool fused);
bool vfs_is_fused(void);
void vfs_set_base_dir(const char *dir);

unsigned char* vfs_read_file(const char *path, size_t *out_size);

char* vfs_read_text(const char *path);

void vfs_free(void *ptr);
void vfs_install_lua_loader(void *lua_state);

bool vfs_write_file(const char *name, const void *data, size_t size);
unsigned char* vfs_read_binary(const char *path, size_t *out_size);
unsigned char* vfs_read_save_file(const char *name, size_t *out_size);
char* vfs_read_save_text(const char *name);
void vfs_set_identity(const char *identity);

#endif
