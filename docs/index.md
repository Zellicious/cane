# engine API reference

api is currently very limited.

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
img:getWidth() / img:getHeight()
img:setFilter(min, mag)  -- "nearest" | "linear"
```

## canvas (render target)

```lua
canvas = new_canvas(w, h, msaa)       -- msaa optional, defaults to 0
set_canvas(canvas)            -- nil to draw to screen
get_active_canvas() -> canvas -- returns active Canvas userdata or nil if screen targeted
canvas:draw(x, y, w, h)
canvas:getWidth() / canvas:getHeight()
canvas:setFilter(min, mag)
```

## mesh (3d)

meshes support two vertex formats: a legacy table-of-tables format, and a custom layout format using flat arrays. the engine automatically detects which format you are using based on the first argument.

### legacy format
if the first argument is a table of vertex tables, it uses the default built-in layout. 
vertex fields: `x,y,z, u,v, r,g,b,a, nx,ny,nz,nw` (all optional except x/y/z; color defaults white, uv defaults 0, normals default 0).

`nx,ny,nz,nw` is a generic per-vertex vec4 slot bound at shader attribute `location = 3`.

```lua
local verts = {
    {x = -1, y = -1, z = 0, u = 0, v = 0, r = 1, g = 0, b = 0, a = 1},
    {x =  1, y = -1, z = 0, u = 1, v = 0, r = 0, g = 1, b = 0, a = 1},
    {x =  0, y =  1, z = 0, u = 0, v = 1, r = 0, g = 0, b = 1, a = 1}
}
local mesh = new_mesh(verts, "triangles")
```

### custom layout format
if the first argument is a table containing layout definitions (objects with a `loc` field), it uses the custom format. this allows you to define arbitrary vertex attributes and pass a single flat array of numbers, which is much faster for the engine to parse and upload to the gpu. (i personally recommend this one)

supported layout types: `"float"` (1), `"vec2"` (2), `"vec3"` (3), `"vec4"` (4).

```lua
-- 1. define the layout
local my_layout = {
    {loc = 0, type = "vec3"}, -- position
    {loc = 1, type = "vec3"}, -- normal
    {loc = 2, type = "float"} -- custom heat data
}

-- 2. provide a flat array of numbers matching the layout stride 
-- (3 + 3 + 1 = 7 floats per vertex)
local flat_verts = {
    -1, -1, -1,   0, 0, 1,   0.5,
     1, -1, -1,   0, 0, 1,   0.8,
     1,  1, -1,   0, 0, 1,   0.2
}

local mesh = new_mesh(my_layout, flat_verts, "triangles")
```

### methods

```lua
mesh = new_mesh(vertices, mode) 
-- or
mesh = new_mesh(layout, flat_vertices, mode)
-- mode: "triangles" | "lines" | "line_loop" | "triangle_fan" | "triangle_strip"

mesh:setVertices(vertices) 
-- accepts either the legacy table-of-tables or the flat array. 
-- it automatically detects the format and matches the mesh's original layout.

mesh:draw(image, shader)  -- both optional
```
## shader

```lua
shader = new_shader(vert_path, frag_path)
shader:send(name, ...)     -- 1-4 numbers -> float/vec2/vec3/vec4; a 9 or 16-length table -> mat3/mat4
shader:sendTexture(name, image, unit)  -- unit optional, defaults to 1
                                        -- (unit 0 is the meshes own implicit texture, set via mesh:draw(image, shader))
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
set_camera_look(x, y, z, yaw_deg, pitch_deg, roll_deg)  -- interesting rotation layout
set_camera_lookat(eye_x, eye_y, eye_z, target_x, target_y, target_z) -- name explains itself
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

get_mouse_pos() -> x, y
is_mouse_down(button)  -- 1=left, 2=right, 3=middle
get_scroll() -> dx, dy  -- accumulated scroll delta since the last call, then reset
set_cursor_visible(bool)

set_mouse_locked(bool)
get_mouse_locked() -> bool

set_mouse_relative(bool)
get_mouse_delta() -> dx, dy

poll_text_input() -> string
```

## saving / loading

files are written to a real per-OS user data directory (not inside a fused build's zip), so save data persists across app updates and works the same in fused or loose builds.

```lua
write_file(name, data) -> true/false
read_file(name) -> string or nil
```

## modules

`require("modname")` resolves `modname.lua` or `modname/init.lua` relative to the game root (works identically in fused and loose builds).

## threads

workers run in their own pthread with their own `lua_State`. they cannot touch graphics, audio, input, or the window - those are main-thread only. use them for heavy cpu work. 
threads communicate by passing serialized strings through thread-safe queues; never share lua tables across threads directly.

```lua

worker = new_thread(script_path)
-- spawns a new thread that loads and runs script_path via the VFS.
-- returns a Thread userdata.

worker:send(string) -> bool
-- pushes a string into the worker's input queue. returns false if the worker has already exited.

worker:receive() -> string or nil
-- pops a string from the worker's output queue (non-blocking). returns nil if the queue is empty OR the worker has exited.

worker:isRunning() -> bool
-- false once the worker script returns or crashes.

worker:stop()
-- closes the workers input queue, which unblocks a worker stuck in thread.receive(). the worker will then see nil from receive() and can exit cleanly.

```

inside the worker script, a global `thread` table is available:

```lua

thread.send(string)
-- send a message back to the main thread.

thread.receive(block) -> string or nil
-- if block is true/omitted, waits for a message. if false, returns immediately with nil if the queue is empty. returns nil when the main thread has called worker:stop() or the main state is closing.

```

typical pattern:

```lua
-- main.lua
local worker = new_thread("worker.lua")
function update(dt)
    local msg = worker:receive()
    if msg then handle_result(msg) end
    if need_work then worker:send(serialize.pack("job", data)) end
end

-- worker.lua
while true do
    local msg = thread.receive()
    if not msg then break end
    local kind, data = serialize.unpack(msg)
    -- do expensive work...
    thread.send(serialize.pack("result", result))
end

```

workers have access to: `math`, `string`, `table`, `os`, `require` (via VFS), `read_file`, `write_file`. 
they do not have access to: `new_image`, `new_mesh`, `draw_*`, `new_sound`, `get_mouse_*`, `is_key_down`, window functions, or anything else that touches OpenGL / GLFW / miniaudio.
