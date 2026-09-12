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
img:drawQuad(sx, sy, sw, sh, dx, dy, scale, shader)
-- draws a sub-rectangle of the image (source coords in pixels, top-left origin)
-- at (dx, dy). scale and shader both optional. this is the sprite-sheet /
-- tile-atlas primitive — img:draw always draws the whole texture, this draws
-- one frame of it.
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

vertex table fields: `x,y,z, u,v, r,g,b,a, nx,ny,nz,nw` (all optional except x/y/z; color defaults white, uv defaults 0, nx/ny/nz/nw default 0).

`nx,ny,nz,nw` is a generic per-vertex vec4 slot bound at shader attribute `location = 3`. By convention used for a normal (`nw` unused), but it's just raw per-vertex data — usable for anything a shader wants to read per-vertex (packed flags, weights, whatever). A shader that doesn't declare `layout(location = 3)` simply ignores it; no need to fill it in if you're not using it.

```lua
mesh = new_mesh(vertices, mode)  -- mode: "triangles" | "lines" | "line_loop" | "triangle_fan" | "triangle_strip"
mesh:setVertices(vertices)
mesh:draw(image, shader)  -- both optional
```

## shader

```lua
shader = new_shader(vert_path, frag_path)
shader:send(name, ...)     -- 1-4 numbers -> float/vec2/vec3/vec4; a 9 or 16-length table -> mat3/mat4
shader:sendTexture(name, image, unit)  -- unit optional, defaults to 1
                                        -- (unit 0 is the mesh's own implicit texture, set via mesh:draw(image, shader))
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
set_camera_look(x, y, z, yaw_deg, pitch_deg, roll_deg)  -- fly camera, angles in degrees
set_camera_lookat(eye_x, eye_y, eye_z, target_x, target_y, target_z)
-- positions the camera at (eye_x,eye_y,eye_z) looking directly at
-- (target_x,target_y,target_z). Simpler than set_camera_look when you
-- already know the point you want centered in view — e.g. a boxing-ring
-- camera tracking the midpoint between two fighters — rather than working
-- out yaw/pitch by hand.
```

## audio

```lua
snd = new_sound(path)
snd:play()
snd:stop()
snd:setLooping(bool)
snd:setVolume(0.0-1.0)
snd:setPitch(1.0)  -- 1.0 = normal
snd:setPosition(x, y, z)
snd:setVelocity(vel_x, vel_y, vel_z)
snd:setAttenuation(min_dist, max_dist)
snd:setPan(pan)

set_audio_listener_position(x, y, z)
set_audio_listener_direction(forward_x, forward_y, forward_z)
set_audio_listener_velocity(vel_x, vel_y, vel_z)
```

## input

```lua
is_key_down(name) -> bool
-- names: single letters/digits, "space", "up"/"down"/"left"/"right",
-- "ctrl"/"lctrl"/"rctrl", "shift"/"lshift"/"rshift", "alt"/"lalt"/"ralt",
-- "tab", "return", "escape", "backspace"

get_mouse_pos() -> x, y
is_mouse_down(button)  -- 1=left, 2=right, 3=middle
get_scroll() -> dx, dy  -- accumulated scroll delta since the last call, then reset
set_cursor_visible(bool)

set_mouse_relative(bool)
-- locks and hides the cursor, enabling raw motion input where the
-- platform supports it (bypasses OS pointer acceleration). While
-- enabled, get_mouse_pos() returns an unbounded virtual position, not
-- real screen coordinates — use get_mouse_delta() instead.
get_mouse_delta() -> dx, dy
-- motion since the last call. Call once per frame (e.g. from update())
-- while set_mouse_relative(true) is active.

poll_text_input() -> string
-- returns typed characters (ASCII) queued since the last call, then clears
-- the queue. Use for text input boxes; is_key_down alone can't distinguish
-- shifted/symbol characters the way this can.
```

## saving / loading

files are written to a real per-OS user data directory (not inside a fused build's zip), so save data persists across app updates and works the same in fused or loose builds.

```lua
write_file(name, data) -> true/false
read_file(name) -> string or nil
```

## modules

`require("modname")` resolves `modname.lua` or `modname/init.lua` relative to the game root (works identically in fused and loose builds).
