# dependencies
system libraries required to build the engine.
| library | purpose | install (debian/ubuntu) | install (macos/brew) | install (arch) |
|---|---|---|---|---|
| glfw | window/input | `libglfw3-dev` | `glfw` | `glfw` |
| freetype | font rendering | `libfreetype6-dev` | `freetype` | `freetype2` |
| luajit | scripting | `libluajit-5.1-dev` | `luajit` | `luajit` |
| physfs | fused build archive mounting | `libphysfs-dev` | `physfs` | `physfs` |
| opengl | rendering | usually preinstalled (mesa) | preinstalled | `mesa` |
vendored (no system install needed, already in `vendor/`): glad, stb_image, miniaudio.
## quick install
**debian/ubuntu**
```bash
sudo apt install libglfw3-dev libfreetype6-dev libluajit-5.1-dev libphysfs-dev
```
**macos**
```bash
brew install glfw freetype luajit physfs
```
**arch**
```bash
sudo pacman -S glfw freetype2 luajit physfs
```
after installing, `pkg-config --cflags --libs freetype2 luajit physfs` should return non-empty output - if not, the Makefile's `PKG_CFLAGS`/`PKG_LIBS` will silently come up empty and the build will fail with missing header/link errors.
