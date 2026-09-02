# engine API reference

see also: [dependencies](dependencies.md)

## config

global table read at startup from `main.lua`.

```lua
config = {
    width = 800, height = 600, title = "Window", identity = "game",
    msaa = 0, highdpi = false, resizable = true,
    minwidth = -1, minheight = -1, vsync = true
}
```

## lifecycle

| function | called |
|---|---|
| `init()` | once, after window/graphics setup |
| `update(dt)` | every frame, `dt` in seconds |
| `render()` | every frame, after `update` |
| `resize(w, h)` | optional, on window resize |

## app control

```lua
quit()                    -- closes the window, ends the app loop
get_dimensions() -> width, height
```

## drawing (2d)

```lua
set_color(r, g, b, a)
clear(r, g, b, a)
draw_rectangle(mode, x, y, w, h)   -- mode: "fill" | "line"
draw_triangle(mode, x1,y1, x2,y2, x3,y3)
draw_circle(mode, cx, cy, radius, segments)
print_text(str, x, y, scale)
get_text_width(str, scale) -> width  -- for centering/aligning text before drawing it
```

## images

```lua
img = new_image(path)
img:draw(x, y, scale)
img:drawShader(x, y, scale, shader)  -- shader optional, nil = default
img:getWidth() / img:getHeight()
img:setFilter(min, mag)  -- "nearest" | "linear"
```

## canvas (render target)

```lua
canvas = new_canvas(w, h)
set_canvas(canvas)            -- nil to draw to screen
get_active_canvas() -> canvas -- returns active Canvas userdata or nil if screen targeted
canvas:draw(x, y, w, h)
canvas:getWidth() / canvas:getHeight()
canvas:setFilter(min, mag)
```

## mesh (3d)

vertex table fields: `x,y,z, u,v, r,g,b,a` (all optional except x/y/z; color defaults white, uv defaults 0).

```lua
mesh = new_mesh(vertices, mode)  -- mode: "triangles" | "lines" | "line_loop" | "triangle_fan" | "triangle_strip"
mesh:setVertices(vertices)
mesh:draw(image, shader)  -- both optional
```

## shader

```lua
shader = new_shader(vert_path, frag_path)
shader:send(name, ...)     -- 1-4 numbers -> float/vec2/vec3/vec4; a 9 or 16-length table -> mat3/mat4
shader:sendInt(name, value)
set_shader(shader)  -- nil resets to default; affects all drawing until changed
```

built-in uniforms set automatically: `uProjection`, `uView`.

## fonts

```lua
font = new_font(path, pixel_size)  -- pixel_size optional, defaults to 24
set_font(font)                     -- nil resets to default font
```

## camera / projection

```lua
set_ortho()                                  -- 2D screen-space (default)
set_perspective(fovy_deg, near, far)
set_camera(x, y, z)                          -- translate-only
set_camera_look(x, y, z, yaw_deg, pitch_deg)  -- full fly camera
```

## audio

```lua
snd = new_sound(path)
snd:play()
snd:stop()
snd:setLooping(bool)
snd:setVolume(0.0-1.0)
snd:setPitch(1.0)  -- 1.0 = normal
```

## input

```lua
is_key_down(name) -> bool
-- names: single letters/digits, "space", "up"/"down"/"left"/"right",
-- "ctrl"/"lctrl"/"rctrl", "shift"/"lshift"/"rshift", "alt"/"lalt"/"ralt"

get_mouse_pos() -> x, y
is_mouse_down(button)  -- 1=left, 2=right, 3=middle
poll_text_input()      -- returns text input
```

## saving / loading

files are written to a real per-OS user data directory (not inside a fused builds zip), so save data persists across app updates and works the same in fused or loose builds.

```lua
write_file(name, data) -> true/false
read_file(name) -> string or nil
```

## modules

`require("modname")` resolves `modname.lua` or `modname/init.lua` relative to the game root (works identically in fused and loose builds).
