#define _DEFAULT_SOURCE
#include "app.h"
#include "vfs.h"
#include <physfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool is_directory(const char *path) {
    struct stat sb;
    return (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode));
}

static const char* resolve_exe_path(const char *argv0, char *buf, size_t buf_size) {
#if defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", buf, buf_size - 1);
    if (len > 0) {
        buf[len] = '\0';
        return buf;
    }
#endif
    return argv0;
}

static bool exe_has_appended_zip(const char *exe_path) {
    FILE *f = fopen(exe_path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    long scan_size = file_size < 65536 ? file_size : 65536;
    fseek(f, file_size - scan_size, SEEK_SET);

    unsigned char *buf = malloc(scan_size);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, scan_size, f);
    fclose(f);

    bool found = false;
    for (long i = scan_size - 22; i >= 0; i--) {
        if (buf[i] == 0x50 && buf[i+1] == 0x4B && buf[i+2] == 0x05 && buf[i+3] == 0x06) {
            found = true;
            break;
        }
    }
    free(buf);
    return found;
}

int main(int argc, char *argv[]) {
    char exe_path_buf[1024];
    const char *exe_path = resolve_exe_path(argv[0], exe_path_buf, sizeof(exe_path_buf));

    PHYSFS_init(argv[0]);

    bool fused = exe_has_appended_zip(exe_path) && PHYSFS_mount(exe_path, "/", 1);
    vfs_set_fused(fused);

    char entry_point[512];

    if (fused) {
        snprintf(entry_point, sizeof(entry_point), "main.lua");
        if (!PHYSFS_exists(entry_point)) {
            fprintf(stderr, "error: main.lua not found in fused archive\n");
            PHYSFS_deinit();
            return 1;
        }
        printf("running fused build\n");
    } else {
        if (argc < 2 || !is_directory(argv[1])) {
            fprintf(stderr, "usage: %s <path_to_game_folder>\n", argv[0]);
            PHYSFS_deinit();
            return 1;
        }
        vfs_set_base_dir(argv[1]);
        snprintf(entry_point, sizeof(entry_point), "main.lua");
    }

    if (!app_init(&g_app, entry_point)) {
        fprintf(stderr, "failed to initialize application\n");
        PHYSFS_deinit();
        return 1;
    }

    app_run(&g_app);
    app_cleanup(&g_app);
    PHYSFS_deinit();
    return 0;
}
