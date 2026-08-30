#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdbool.h>

// fused = reads go through PhysFS (mounted zip). Not fused = reads go
// through fopen relative to base_dir.
void vfs_set_fused(bool fused);
bool vfs_is_fused(void);

// Only used when not fused — e.g. "mygame" if launched as `engine mygame`.
void vfs_set_base_dir(const char *dir);

// Reads a whole file into a malloc'd buffer (size, not null-terminated).
unsigned char* vfs_read_file(const char *path, size_t *out_size);

// Same but null-terminated (for shader/lua source).
char* vfs_read_text(const char *path);

void vfs_free(void *ptr);

// Registers a package.loaders entry so require() works identically fused
// or not, routed through vfs_read_text instead of raw fopen.
void vfs_install_lua_loader(void *lua_state);

#endif
