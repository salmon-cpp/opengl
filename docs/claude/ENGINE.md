# How This Renderer Works

A ground-up explanation of the engine in this repository: every GL call, every
function, and the mathematics underneath each one.

This is written to be read in order the first time. Sections 1–2 build the
mathematical and conceptual vocabulary; sections 3–10 walk the actual code;
section 11 is a lookup table of every GL call; section 12 lists behaviours in
the current code that are surprising or wrong.

Notation used throughout:

```
v            a vector, e.g. v = (x, y, z)
|v|          length (magnitude) of v
v_hat        v normalized to unit length
a . b        dot product
a x b        cross product
M v          matrix M applied to vector v
M^T          transpose of M
M^-1         inverse of M
```

---

## 0. One frame, end to end

Before any detail, here is the whole program as a loop. `main()` sets up, then
repeats four steps forever:

```
while (window is open) {
    window.processInput(camera);          // read keyboard, move the camera
    render(...);                          // draw the 3D scene
    overlay.loopStart/drawUI/loopEnd();   // draw the ImGui panel on top
    window.refresh();                     // swap buffers, poll OS events
}
```

`render()` in turn does this:

1. Set polygon mode (fill / wireframe / point), clear the colour and depth buffers.
2. Ask the camera for a **view** matrix and a **projection** matrix.
3. Bind the object shader and the object VAO. Upload the uniforms shared by every
   cube (view, projection, light colour, light position).
4. For each cube: build its **model** matrix from position/rotation/scale, upload
   the per-object uniforms, and issue one `glDrawElements`.
5. Bind the lamp shader and the lamp VAO, build a small model matrix at the
   light's position, and issue one more `glDrawElements`.

Everything else in this document is an expansion of those two lists.

---

## 1. The mathematics you need

### 1.1 Vectors

A 3D vector is an ordered triple `(x, y, z)`. In this codebase a vector means one
of two different things depending on context, and conflating them causes bugs:

- A **point**: a location in space. "The cube is at `(3.2, -1.4, -12.7)`."
- A **direction**: an offset with no location. "The surface faces that way."

The distinction becomes concrete in section 1.6, where points get a 4th
coordinate `w = 1` and directions get `w = 0`. That single number is what makes
translation apply to points but not to directions — which is exactly what you
want, because moving a surface should move it, but should not change which way
it faces.

Length:

```
|v| = sqrt(x^2 + y^2 + z^2)
```

This is just Pythagoras extended to three dimensions.

### 1.2 The dot product — the "how aligned are these?" operator

```
a . b = a.x*b.x + a.y*b.y + a.z*b.z          (component form)
a . b = |a| |b| cos(theta)                   (geometric form)
```

Those two formulas are equal; the proof is the law of cosines. The geometric
form is the one that matters, because it converts an *angle* into a *number you
can multiply by*, without ever calling a trig function.

If both vectors are unit length, `|a| = |b| = 1`, so:

```
a_hat . b_hat = cos(theta)
```

Read off the behaviour:

```
theta = 0    (same direction)        cos = 1
theta = 60                           cos = 0.5
theta = 90   (perpendicular)         cos = 0
theta = 180  (opposite)              cos = -1
```

**This is why normalization matters so much in the lighting shader.** If you skip
`normalize()`, the dot product returns `|a||b|cos(theta)` — the cosine scaled by
two arbitrary lengths. Your lighting then varies with the *size* of your vectors
rather than the *angle* between them, and changes as the light moves further
away, which is not what the term is supposed to model.

### 1.3 The cross product — the "give me a perpendicular" operator

```
a x b = (a.y*b.z - a.z*b.y,
         a.z*b.x - a.x*b.z,
         a.x*b.y - a.y*b.x)

|a x b| = |a| |b| sin(theta)
```

The result is perpendicular to **both** inputs, with direction given by the
right-hand rule (point fingers along `a`, curl toward `b`, thumb points along
`a x b`). It is *anti-commutative*: `a x b = -(b x a)`. Order matters.

Two consequences used in this engine:

- The camera builds its `right` and `up` axes with cross products (section 5.2).
- If `a` and `b` are parallel, `sin(theta) = 0`, so the cross product is the zero
  vector, and normalizing it divides by zero. That is precisely the failure the
  camera's pitch clamp exists to prevent.

### 1.4 Normalization

```
v_hat = v / |v|
```

Scaling a vector to length 1 while keeping its direction. After this, the vector
carries *only* direction — which is what makes it safe to feed to a dot product
and read the result as a cosine.

### 1.5 Matrices as linear maps

A 3×3 matrix applied to a vector:

```
[ m00 m01 m02 ] [x]   [ m00*x + m01*y + m02*z ]
[ m10 m11 m12 ] [y] = [ m10*x + m11*y + m12*z ]
[ m20 m21 m22 ] [z]   [ m20*x + m21*y + m22*z ]
```

The clearest way to read a matrix: **its columns are where the basis vectors
land.** Column 0 is where `(1,0,0)` goes, column 1 is where `(0,1,0)` goes,
column 2 is where `(0,0,1)` goes. Everything else follows by linearity, since
any vector is a weighted sum of those three.

Rotation about each axis by angle `t`:

```
Rx(t) = [ 1    0        0     ]
        [ 0  cos(t)  -sin(t)  ]
        [ 0  sin(t)   cos(t)  ]

Ry(t) = [  cos(t)  0  sin(t) ]
        [    0     1    0    ]
        [ -sin(t)  0  cos(t) ]

Rz(t) = [ cos(t)  -sin(t)  0 ]
        [ sin(t)   cos(t)  0 ]
        [   0        0     1 ]
```

Scaling:

```
S(sx,sy,sz) = [ sx  0   0  ]
              [ 0   sy  0  ]
              [ 0   0   sz ]
```

### 1.6 Homogeneous coordinates — why every matrix here is 4×4

Translation is **not** a linear map. There is no 3×3 matrix `M` with
`M*v = v + t` for all `v`, because any linear map must send the origin to the
origin (`M*0 = 0`), and translation does not.

The fix is to embed 3D space into 4D at the plane `w = 1`:

```
point     (x, y, z)  ->  (x, y, z, 1)
direction (x, y, z)  ->  (x, y, z, 0)
```

Now translation *is* a linear map, in 4D:

```
T(t) = [ 1  0  0  tx ]      [x]   [ x + tx ]
       [ 0  1  0  ty ]  *   [y] = [ y + ty ]
       [ 0  0  1  tz ]      [z]   [ z + tz ]
       [ 0  0  0  1  ]      [1]   [   1    ]
```

And notice what happens to a direction, with `w = 0`:

```
[ 1  0  0  tx ]   [x]   [ x ]
[ 0  1  0  ty ] * [y] = [ y ]     -- translation column multiplied by 0
[ 0  0  1  tz ]   [z]   [ z ]
[ 0  0  0  1  ]   [0]   [ 0 ]
```

Unchanged. This is why `cube.vs` writes `vec4(aPos, 1.0)` for the position but
uses a `mat3` for the normal — normals are directions and must not be
translated. Getting this wrong makes lighting slide around as objects move.

The 4th coordinate does one more job, in section 4.3: perspective.

### 1.7 Composing transformations, and reading them right-to-left

Matrix multiplication is associative but **not commutative**. `A*B != B*A`, so
the order you compose transforms is the order they happen — and the convention
trips everyone up once.

When you write `M = A * B * C` and apply it as `M * v`, the grouping is
`A * (B * (C * v))`. **`C` is applied first, `A` last. Read right to left.**

In `render()` the model matrix is built by successive post-multiplication:

```cpp
glm::mat4 model = glm::mat4(1.0f);              // identity
model = glm::translate(model, t.position);      // model = T
model = glm::rotate(model, ..., x-axis);        // model = T*Rx
model = glm::rotate(model, ..., y-axis);        // model = T*Rx*Ry
model = glm::rotate(model, ..., z-axis);        // model = T*Rx*Ry*Rz
model = glm::scale(model, t.scale);             // model = T*Rx*Ry*Rz*S
```

Each `glm::` call returns `model * (the new transform)`. The final matrix is:

```
M = T * Rx * Ry * Rz * S
```

so a vertex is **scaled first, then rotated, then translated**. That is the
order you want. Scaling last would scale the translation too, flinging distant
objects away from the origin; rotating after translating would swing objects
around the world origin instead of spinning them in place.

`glm::mat4(1.0f)` is the identity matrix — 1s on the diagonal, the transform
that does nothing. It is the multiplicative equivalent of starting a sum at 0.

**Gimbal lock.** Because this composes three separate Euler-angle rotations, at
certain orientations two of the three axes align and you lose a degree of
freedom. It is a real limitation of Euler angles, not a bug in the code;
quaternions are the usual fix, and are out of scope here.

---

## 2. The GPU pipeline

A draw call feeds vertices in one end and lit pixels come out the other. The
stages that matter to this codebase:

```
  vertex buffer (your cubeVertices array, in GPU memory)
        |
        |  the VAO says how to read it: which bytes are position,
        |  which are normal, which are texcoord
        v
  VERTEX SHADER  (cube.vs) -- runs once per vertex, in parallel
        |        writes gl_Position in *clip space*, plus any
        |        `out` variables you declare
        v
  primitive assembly -- group vertices into triangles using the index buffer
        |
        v
  clipping -- discard geometry outside the view volume
        |
        v
  perspective divide -- divide xyz by w  ->  normalized device coords (NDC)
        |
        v
  viewport transform -- NDC -> pixel coordinates
        |
        v
  RASTERIZATION -- work out which pixels each triangle covers.
        |          The vertex shader's `out` variables are *interpolated*
        |          across the triangle here.
        v
  FRAGMENT SHADER (cube.fs) -- runs once per covered pixel, in parallel
        |                      writes a colour
        v
  depth test -- is this fragment nearer than what's already there?
        |
        v
  framebuffer
```

Two properties of this pipeline explain most of the shader code:

**Vertex shaders run per-vertex; fragment shaders run per-pixel.** A 24-vertex
cube filling a 1600×900 window runs `cube.vs` 24 times and `cube.fs` potentially
over a million times. Anything you can compute per-vertex is far cheaper.

**Interpolation happens automatically between the two.** Whatever `cube.vs`
writes to `out vec3 Normal` is *blended across the triangle's surface* before
`cube.fs` reads it as `in vec3 Normal`. A pixel two-thirds of the way between
vertices A and B receives roughly two-thirds of B's value and one-third of A's.
This is why the cubes shade smoothly instead of in flat blocks, and it is also
why the fragment shader must re-normalize: interpolating between two unit
vectors generally produces a vector *shorter* than 1 (the straight line between
two points on a sphere passes through its interior).

The interpolation is *perspective-correct*: the hardware weights by `1/w` so
that values track the surface in 3D rather than in screen space.

---

## 3. Getting geometry onto the GPU

### 3.1 The vertex data

`cubeVertices` holds 24 vertices, each 8 floats:

```
position (3 floats) | normal (3 floats) | texcoord (2 floats)
   x   y   z        |   nx  ny  nz      |    u   v
```

laid out **interleaved** — all 8 floats of vertex 0, then all 8 of vertex 1, and
so on. The alternative (all positions, then all normals) is called planar layout.
Interleaving is generally faster because a vertex's attributes land in the same
cache line.

**Why 24 vertices and not 8?** A cube has 8 geometric corners. But attributes
live on *vertices*, not faces, and each corner is shared by three faces that need
three different normals — the corner at `(-0.5,-0.5,-0.5)` needs normal
`(0,0,-1)` for the back face, `(-1,0,0)` for the left, and `(0,-1,0)` for the
bottom. A single vertex cannot hold three normals, so each corner is duplicated
once per face it touches: 6 faces × 4 corners = 24. The same argument applies to
texture coordinates.

**Why an index buffer?** Each face is a quad, drawn as 2 triangles = 6 vertex
references, so 36 references total. Without indices you would store 36 full
vertices (288 floats). With indices you store 24 vertices (192 floats) plus 36
small integers, and — more importantly — the GPU has a *post-transform vertex
cache*: when an index repeats, the already-computed vertex shader output is
reused rather than recomputed. `cubeIndices` gives 2 triangles per face:

```
0, 1, 2,  2, 3, 0     // corners 0,1,2 then 2,3,0 -- a quad as two triangles
```

`cubeIndiceCount` is `sizeof(cubeIndices) / sizeof(cubeIndices[0])` = 36. This
`sizeof` idiom works only where the true array type is visible — it silently
becomes wrong if the array ever decays to a pointer.

### 3.2 VBO, EBO, VAO

Three objects with confusingly similar names:

- **VBO** (Vertex Buffer Object) — a block of GPU memory. Just bytes; it has no
  idea what it contains.
- **EBO** (Element Buffer Object) — also just a block of GPU memory, but bound
  to the index slot so `glDrawElements` reads indices from it.
- **VAO** (Vertex Array Object) — *not* memory. It is a saved set of
  **descriptions**: for each attribute slot, which buffer to read, at what
  offset, with what stride and type. Plus the element buffer binding.

The VAO exists so that switching geometry is one call instead of ten. Binding a
VAO restores every attribute setting you recorded into it.

### 3.3 `setupBuffers()`, call by call

```cpp
glGenVertexArrays(1, &buffers.objectVAO);
glBindVertexArray(buffers.objectVAO);
```

Create one VAO name and make it current. From here until the next
`glBindVertexArray`, attribute configuration is recorded into this VAO.

```cpp
glGenBuffers(1, &VBO);
glBindBuffer(GL_ARRAY_BUFFER, VBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
```

Create a buffer, bind it to the `GL_ARRAY_BUFFER` slot, and copy
`sizeof(cubeVertices)` bytes from CPU memory into it. `GL_STATIC_DRAW` is a
*hint*: "written once, drawn many times", letting the driver choose fast
read-only memory. It is advisory, not enforced.

```cpp
glGenBuffers(1, &EBO);
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);
```

Same, for indices. **The `GL_ELEMENT_ARRAY_BUFFER` binding is part of VAO
state** — it is recorded into the currently bound VAO. This is why the lamp VAO
below must bind the EBO again; forgetting it makes `glDrawElements` read from
nothing and draw nothing.

```cpp
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);
```

This is the call that connects buffer bytes to shader inputs. Its arguments:

```
0                 attribute location -- matches `layout (location = 0)` in cube.vs
3                 3 components (a vec3)
GL_FLOAT          component type
GL_FALSE          don't normalize integers into [0,1] -- irrelevant for floats
8 * sizeof(float) STRIDE: bytes from one vertex's attribute to the next vertex's
(void*)0          OFFSET: bytes from the start of a vertex to this attribute
```

**Stride is per-vertex, not per-attribute.** All three attributes use the same
stride of 32 bytes, because that is the size of one whole vertex. Only the offset
differs. The `(void*)` cast on the offset is a historical wart: the parameter was
once a real pointer into client memory, and the signature never changed.

Crucially, `glVertexAttribPointer` captures **whichever VBO is bound to
`GL_ARRAY_BUFFER` right now** and stores that association in the VAO. This is why
the order `bind VAO -> bind VBO -> set pointers` matters.

The remaining two attributes differ only in size and offset:

```cpp
// normal:   3 floats at byte offset 12
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
glEnableVertexAttribArray(1);

// texcoord: 2 floats at byte offset 24
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
glEnableVertexAttribArray(2);
```

`glEnableVertexAttribArray(n)` is separate and mandatory. A configured but
disabled attribute reads a constant `(0,0,0,1)` for every vertex — which for a
normal means every dot product is zero and the model renders black, with no error
reported anywhere.

The lamp VAO then reuses the same memory with a narrower view:

```cpp
glGenVertexArrays(1, &buffers.lightVAO);
glBindVertexArray(buffers.lightVAO);
glBindBuffer(GL_ARRAY_BUFFER, VBO);
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);
```

Same buffers, but only attribute 0 is configured — the lamp shader has no use
for normals or texcoords. **The stride must still be 8 floats**, because it
describes the memory layout, not the subset you care about. A stride of 3 here
would march through the buffer at the wrong rate and smear the cube across space.
The lamp keeps its own VAO so that future changes to the object layout cannot
disturb it.

```cpp
glBindVertexArray(0);
glEnable(GL_DEPTH_TEST);
glPointSize(3.0f);
```

Unbind (defensive: prevents later stray calls from editing this VAO), enable
depth testing (section 4.6), and set the pixel diameter used by `GL_POINT`
polygon mode.

---

## 4. From model space to pixels

This is the heart of the renderer. A vertex passes through four coordinate
systems, and one division.

```
local/model space  --[model matrix]-->  world space
world space        --[view matrix]-->   view (eye/camera) space
view space         --[projection]-->    clip space
clip space         --[divide by w]-->   normalized device coords (NDC)
NDC                --[viewport]-->      window pixels
```

In `cube.vs` this is:

```glsl
FragPos     = vec3(model * vec4(aPos, 1.0));
gl_Position = projection * view * vec4(FragPos, 1.0);
```

Read right to left: `aPos` (local) becomes `FragPos` (world), then view, then
projection. Writing it in two steps is deliberate — `FragPos` is needed in world
space by the fragment shader for lighting, so it is computed once and reused
rather than recomputing `model * vec4(aPos, 1.0)` twice.

### 4.1 Model matrix — local to world

Covered in 1.7. The cube's vertices are defined around the origin spanning
`-0.5..0.5`; the model matrix places, orients and sizes that unit cube in the
world.

### 4.2 View matrix — world to camera

The view matrix answers: "what are these world coordinates, expressed relative
to the camera?" The trick is that there is no such thing as moving the camera —
**you move the entire world in the opposite direction.** A camera 3 units back is
identical to a world pushed 3 units forward.

`glm::lookAt(eye, center, up)` builds it. Given `position`, `position + front`,
and `up`, it computes an orthonormal camera basis:

```
f = normalize(center - eye)     forward
s = normalize(f x up)           sideways (right)
u = s x f                       true up (re-derived, guaranteed perpendicular)
```

and assembles:

```
V = [  s.x   s.y   s.z   -(s . eye) ]
    [  u.x   u.y   u.z   -(u . eye) ]
    [ -f.x  -f.y  -f.z    (f . eye) ]
    [   0     0     0        1      ]
```

This is `R^T * T(-eye)`: translate the world so the camera sits at the origin,
then rotate so the camera's axes align with the world axes.

Why `R^T` and not `R`? Because `R` (whose *columns* are `s, u, -f`) is the matrix
that maps camera axes into world space, and we want the opposite. For a rotation
matrix the inverse equals the transpose — rotations are orthonormal, so
`R^T R = I`. That identity turns a costly inverse into a free row/column swap.

The `-f` row is because OpenGL's camera looks down its **negative** Z axis, a
convention inherited from right-handed coordinates.

The dot products in the last column are the translation, already expressed in the
rotated frame.

### 4.3 Projection matrix — camera to clip space

`glm::perspective(fovy, aspect, near, far)`, with `fovy = radians(FOV)`,
`aspect = width/height`, `near = 0.1`, `far = 100`.

Let `t = tan(fovy / 2)`. Then:

```
P = [ 1/(aspect*t)   0      0                      0                      ]
    [ 0              1/t    0                      0                      ]
    [ 0              0    -(far+near)/(far-near)  -2*far*near/(far-near)  ]
    [ 0              0     -1                      0                      ]
```

Where this comes from: perspective means distant things look smaller, i.e.
dividing by depth. The core insight is the bottom row `[0 0 -1 0]`, which sets

```
w_clip = -z_eye
```

so `w` carries the distance to the camera. The division that shrinks distant
objects happens later, automatically, in the perspective divide.

The `1/t` terms encode field of view. A point at the top edge of the view has
`y_eye / (-z_eye) = tan(fovy/2)`, so scaling `y` by `1/tan(fovy/2)` maps that
edge to exactly 1.0 after division — the edge of the cube OpenGL clips against.
Widening the FOV increases `t`, shrinks `1/t`, and squeezes more world into the
same screen.

Dividing `x` additionally by `aspect` compensates for a non-square window; without
it, everything stretches horizontally on a 16:9 display.

The third row maps the depth range. Working through it for `z_eye = -near`:

```
z_clip = -(f+n)/(f-n) * (-n) - 2fn/(f-n)
       = (fn + n^2 - 2fn)/(f-n)
       = (n^2 - fn)/(f-n)
       = -n(f-n)/(f-n)
       = -n

w_clip = n

z_ndc  = z_clip / w_clip = -1      -- the near plane maps to -1
```

and the same arithmetic at `z_eye = -far` gives `z_ndc = +1`. So the visible
depth range lands on `[-1, 1]`, which is what OpenGL expects.

### 4.4 The perspective divide

Between the vertex and fragment shaders the hardware performs:

```
NDC = (x_clip/w_clip, y_clip/w_clip, z_clip/w_clip)
```

Since `w_clip = -z_eye`, this divides `x` and `y` by distance — **this single
division is what perspective actually is.** Everything before it is bookkeeping
to set `w` correctly. The result is a cube spanning `[-1,1]` on all three axes;
anything outside is clipped.

Note that clipping happens *before* the divide, on the `w` form, because dividing
by a `w` at or below zero (geometry at or behind the eye) would produce garbage.

### 4.5 Viewport transform

`glViewport(0, 0, width, height)` — set in `Window::initialize()` and again in
`framebufferSizeCallback` on resize — defines the final mapping:

```
x_win = (x_ndc + 1)/2 * width  + x_origin
y_win = (y_ndc + 1)/2 * height + y_origin
z_win = (z_ndc + 1)/2                        -- into [0,1] for the depth buffer
```

A simple rescale from `[-1,1]` to pixel coordinates.

### 4.6 The depth buffer

`glEnable(GL_DEPTH_TEST)` turns on a per-pixel depth comparison. Alongside the
colour buffer the GPU keeps a depth buffer; each fragment tests its `z_win`
against the stored value and is discarded if it fails (default `GL_LESS`: keep
the nearer one). This is what lets the cubes be drawn in arbitrary order and
still occlude each other correctly.

`glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)` resets both each frame.
**Forgetting the depth bit is a classic bug**: stale depth values from the
previous frame reject almost every new fragment, and the screen appears frozen or
badly corrupted.

**Depth precision is not linear.** Because `z_ndc` is computed from `1/z_eye`,
precision concentrates near the camera:

```
z_ndc = [ (f+n)/(f-n) * z_eye + 2fn/(f-n) ] / (-z_eye)
```

With `near = 0.1` and `far = 100`, a large share of the available depth values is
spent on the first fraction of a unit. Far-apart surfaces at distance can end up
with the same stored depth and flicker against each other — *z-fighting*. The
effective fix is almost always to push `near` outward, not to pull `far` inward,
since the ratio `far/near` is what governs precision.

---

## 5. The camera

### 5.1 Euler angles and the spherical-to-Cartesian conversion

The camera stores orientation as two angles: `yaw` (rotation around the world Y
axis) and `pitch` (up/down tilt). `updateVectors()` converts them to a direction:

```cpp
direction.x = cos(radians(yaw)) * cos(radians(pitch));
direction.y = sin(radians(pitch));
direction.z = sin(radians(yaw)) * cos(radians(pitch));
```

This is the standard spherical-to-Cartesian conversion for a unit radius. Its
structure:

- `sin(pitch)` gives the vertical component directly. At `pitch = 0` it is 0
  (level); at `pitch = 90` it is 1 (straight up).
- `cos(pitch)` is the **radius of the horizontal circle** you are on. Looking
  level, that circle has radius 1; looking steeply up, it shrinks toward 0.
- `yaw` picks a point on that circle via `(cos(yaw), sin(yaw))`, scaled by the
  radius above.

Check the default `yaw = -90, pitch = 0`:

```
x = cos(-90) * cos(0) =  0 * 1 =  0
y = sin(0)            =  0
z = sin(-90) * cos(0) = -1 * 1 = -1

front = (0, 0, -1)
```

Pointing down negative Z, matching OpenGL's convention. The odd-looking `-90`
default exists because yaw is measured from the +X axis, and we want to start
looking down −Z.

### 5.2 Building the camera basis

```cpp
front = normalize(direction);
right = normalize(cross(front, worldUp));
up    = normalize(cross(right, front));
```

Three mutually perpendicular unit vectors — an orthonormal basis.

`right` comes from crossing `front` with the *world* up `(0,1,0)` rather than the
camera's own up. That is what keeps the horizon level: the camera never rolls,
because `right` is always forced to be horizontal.

`up` is then re-derived from `right x front` rather than reusing `worldUp`. When
pitched, `worldUp` is no longer perpendicular to `front`; recomputing guarantees a
genuinely orthogonal basis.

### 5.3 Why pitch is clamped to ±89 degrees

```cpp
pitch = glm::clamp(pitch, -89.0f, 89.0f);
```

At exactly ±90, `front` becomes `(0, ±1, 0)` — parallel to `worldUp`. The
cross product of parallel vectors is the zero vector (`|a x b| = |a||b|sin(0) =
0`), and `normalize()` then divides by zero, producing NaN. NaN propagates through
the view matrix and the entire scene disappears permanently, since NaN never
compares equal to anything and never recovers.

Clamping just short of the singularity avoids it entirely. This is a numerical
guard, not a stylistic choice.

### 5.4 Mouse look

```cpp
float xoffset = xpos - lastX;
float yoffset = lastY - ypos;    // note: reversed
```

Mouse deltas, converted to angle changes scaled by `sensitivity`. The Y term is
subtracted in the opposite order because window coordinates put Y=0 at the *top*,
while pitch increases *upward*.

The `firstMouse` flag suppresses one enormous jump: the first callback reports
wherever the cursor happens to be, and without it the difference against an
initial `lastX = 0` would spin the camera wildly. `resetMouse()` re-arms the same
flag after the cursor is warped by `centerCursor()`, for the same reason.

### 5.5 Movement

```cpp
const float velocity = speed * deltaTime;
position += front * velocity;   // etc.
```

Multiplying by `deltaTime` (seconds elapsed since the last frame) makes movement
**frame-rate independent** — a given speed covers the same ground per second
whether you render at 30 or 300 fps. Without it, movement speed would scale with
your GPU.

Note that `front` is the *full* 3D forward vector including pitch, so looking up
and pressing W flies upward. Restricting to horizontal movement would mean
projecting `front` onto the XZ plane first.

*(See section 12.1 — `deltaTime` is currently miscomputed, which is why `speed`
has been tuned to 200.)*

### 5.6 Zoom

```cpp
FOV -= yoffset * zoomStep;
FOV = glm::clamp(FOV, minFOV, maxFOV);
```

Scrolling narrows the field of view, which is genuine optical zoom (a longer
lens) rather than moving the camera. It feeds straight into `glm::perspective`.

---

## 6. Lighting: the Phong reflection model

`cube.fs` implements Phong shading as three additive terms:

```
result = (ambient + diffuse + specular) * surface colour
```

The model is empirical, not physical — it was chosen because it looks plausible
and is cheap, not because it derives from radiometry.

### 6.1 Ambient

```glsl
const float ambientStrength = 0.3;
vec3 ambient = ambientStrength * lightColor;
```

A constant floor. It stands in for global illumination — light that has bounced
off other surfaces before arriving. Real bounced light is enormously expensive to
compute; a flat constant is the crudest possible approximation, and its only job
is to stop unlit faces from being pure black.

### 6.2 Diffuse — Lambert's cosine law

```glsl
vec3 norm     = normalize(Normal);
vec3 lightDir = normalize(lightPos - FragPos);
vec3 diffuse  = max(dot(norm, lightDir), 0.0) * lightColor;
```

`lightDir` points *from the surface toward the light* (light position minus
fragment position). `norm` is the surface normal. Both normalized, so their dot
product is `cos(theta)` exactly.

**Why cosine is the physically correct falloff.** Consider a beam of light with
cross-sectional area `A` striking a surface at angle `theta` from the normal. It
spreads over a surface patch of area `A / cos(theta)`. Energy per unit area —
which is what brightness is — therefore scales by `cos(theta)`. A surface facing
the light head-on catches the beam at full concentration; a surface at a glancing
angle spreads the same energy thinly.

The `max(..., 0.0)` clamps the back side. Without it, faces pointing away from
the light produce a negative cosine, which subtracts from the ambient and
specular terms and can drive the final colour negative.

Lambertian surfaces are *view-independent*: matte paper looks equally bright from
any angle, because it scatters equally in all directions. Note that `diffuse`
does not reference `viewPos` at all.

### 6.3 Specular — the shiny highlight

```glsl
vec3 viewDir    = normalize(viewPos - FragPos);
vec3 reflectDir = reflect(-lightDir, norm);
float spec      = pow(max(dot(viewDir, reflectDir), 0.0), 32);
vec3 specular   = specularStrength * spec * lightColor;
```

Specular models a mirror-like bounce, so unlike diffuse it **does** depend on
where you are standing — move around a glossy object and the highlight slides.

`reflect(I, N)` in GLSL computes `I - 2*dot(N,I)*N`, the mirror reflection of
incident direction `I` about normal `N`. It takes an *incoming* direction, but
`lightDir` points outward toward the light, hence the negation. Substituting
`I = -lightDir`:

```
reflect(-L, N) = -L - 2*dot(N, -L)*N
               = -L + 2*(N . L)*N
               = 2*(N . L)*N - L
```

which is the standard reflection formula: take the component of `L` along the
normal, double it, and subtract `L`.

The exponent controls tightness. `dot(viewDir, reflectDir)` is a cosine in
`[0,1]`; raising it to the 32nd power crushes everything except values very near
1:

```
cos = 1.00  ->  1.00^32 = 1.000     dead centre of the highlight
cos = 0.99  ->  0.99^32 = 0.725
cos = 0.95  ->  0.95^32 = 0.194
cos = 0.90  ->  0.90^32 = 0.034
cos = 0.80  ->  0.80^32 = 0.0008    effectively nothing
```

So the highlight is a small bright spot rather than a broad wash. Higher exponents
mean a smaller, sharper highlight and a shinier-looking material; lower exponents
give a broad satin sheen.

*(A common refinement is Blinn-Phong, which uses the halfway vector
`H = normalize(L + V)` and `dot(N, H)` instead. It avoids an artifact where Phong
highlights cut off abruptly at grazing angles, and is slightly cheaper. Not used
here.)*

### 6.4 The normal matrix

```glsl
Normal = mat3(transpose(inverse(model))) * aNormal;
```

Normals cannot be transformed by the model matrix directly. Under **non-uniform**
scaling — which the overlay's per-axis scale sliders produce — a normal
transformed by `M` stops being perpendicular to the surface. Squash a sphere
vertically and its normals must splay *outward*, the opposite of squashing.

The derivation. A tangent vector `t` lying in the surface transforms normally:
`t' = M t`. We need a matrix `N` such that the transformed normal stays
perpendicular:

```
given    n . t = 0                (n^T t = 0)
want     n' . t' = 0     where    n' = N n,  t' = M t

n'^T t' = (N n)^T (M t) = n^T N^T M t

For this to vanish exactly when n^T t does, for every t:

    N^T M = I
    N^T   = M^-1
    N     = (M^-1)^T
```

Hence `transpose(inverse(model))`. Taking `mat3(...)` drops the translation
column, which is correct because normals are directions (section 1.6).

Note that for a matrix with only rotation and *uniform* scale, the inverse
transpose is proportional to the original matrix, so this step would be
unnecessary. It matters precisely when scaling is non-uniform.

`inverse()` is genuinely expensive to run per vertex. The usual optimisation is
to compute the normal matrix once on the CPU per object and upload it as a
uniform. At this scale it does not matter.

### 6.5 Why `normalize()` appears so often in the fragment shader

`Normal` arrives interpolated across the triangle. Interpolating between unit
vectors shortens them (section 2), so it must be renormalized before use or the
lighting darkens toward triangle centres. `lightDir` and `viewDir` are computed
from position differences whose length is the *distance* to the light or camera,
so they must be normalized or brightness would vary with distance in an
uncontrolled way.

### 6.6 The lamp shader

`lighting.vs` / `lighting.fs` draw the small cube marking the light's position.
The fragment shader is deliberately trivial:

```glsl
FragColor = vec4(lightColor, 1.0);
```

Flat, full brightness, no lighting math. Keeping it in a separate program means
that as the lighting shader grows more elaborate, the lamp stays a plain marker
and continues to read as the *source* of light rather than an object lit by it.

---

## 7. Textures

### 7.1 Loading

```cpp
stbi_set_flip_vertically_on_load(true);
unsigned char *data = stbi_load(path, &width, &height, &nrChannels, 0);
```

Image formats put row 0 at the **top**; OpenGL texture coordinates put `v = 0` at
the **bottom**. Without the flip, every texture renders upside down.

```cpp
glGenTextures(1, &texture);
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, texture);
```

`glActiveTexture` selects which *texture unit* subsequent binds affect. Hardware
provides many units so a shader can sample several textures at once; the shader
side of this link is `lightingShader.setInt("ourTexture", 0)`, which tells the
sampler to read unit 0.

### 7.2 Wrapping and filtering

```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
```

Wrapping decides what happens outside `[0,1]`. `S` and `T` are the two texture
axes (the same things the shader calls `u` and `v`). `GL_MIRRORED_REPEAT` tiles
the image, flipping alternate copies, which hides the seam a plain repeat would
show.

```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
```

Filtering decides what happens when texels and pixels do not line up.
`GL_NEAREST` picks the closest texel (blocky); `GL_LINEAR` takes a weighted
average of the 4 surrounding texels:

```
value = (1-a)(1-b)*T00 + a(1-b)*T10 + (1-a)b*T01 + ab*T11
```

where `a` and `b` are the fractional parts of the texture coordinate. This is
*bilinear interpolation* — linear interpolation along one axis, then the other.

`MIN_FILTER` applies when the texture is shrunk on screen, `MAG_FILTER` when it
is stretched.

### 7.3 Upload and mipmaps

```cpp
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
glGenerateMipmap(GL_TEXTURE_2D);
```

`glTexImage2D` copies the pixels to the GPU. The `0` is the mipmap level (0 =
full resolution); the second `GL_RGB` and `GL_UNSIGNED_BYTE` describe the *source*
format while the first describes the *destination*. The stray `0` before them is
`border`, which must always be 0 in modern GL.

Mipmaps are a chain of pre-shrunk copies (half size, quarter, …). When a
textured surface is far away, one pixel covers many texels; sampling just one of
them produces harsh aliasing that shimmers as the camera moves. Sampling a
pre-averaged smaller level fixes it. Total extra memory is only about 33%, since
`1/4 + 1/16 + 1/64 + … = 1/3`.

*(See section 12.3 — the mipmaps generated here are never actually used.)*

```cpp
stbi_image_free(data);
```

The CPU copy is redundant once uploaded; not freeing it leaks the whole image.

---

## 8. The `Shader` class

### 8.1 Compilation and linking

`Shader::Shader` reads both files, then for each stage:

```cpp
unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
glShaderSource(vertexShader, 1, &vShaderCode, NULL);
glCompileShader(vertexShader);
```

**GLSL is compiled at runtime, by the graphics driver.** Your C++ compiler never
sees it, so a syntax error in a shader cannot fail the build — it fails silently
at startup unless you check.

```cpp
ID = glCreateProgram();
glAttachShader(ID, vertexShader);
glAttachShader(ID, fragmentShader);
glLinkProgram(ID);
```

Linking connects the vertex shader's `out` variables to the fragment shader's
`in` variables. Mismatched names or types fail here rather than at compile time.

The error checks that follow are the most valuable code in the class:

```cpp
glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
if (!success) {
    glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
}
```

Without these, a broken shader gives you a program object that never links,
`glUseProgram` silently does nothing, and you get a black screen with no
diagnostic. Note these messages go to **stdout** — launching from an IDE build
task that hides stdout will conceal them.

```cpp
glDeleteShader(vertexShader);
glDeleteShader(fragmentShader);
```

Once linked into a program, the individual shader objects are no longer needed.
GL reference-counts them, so the program stays valid.

### 8.2 Uniforms and the location cache

Uniforms are per-draw-call constants, uniform across every vertex and fragment in
that call — as opposed to attributes, which vary per vertex.

```cpp
int Shader::location(const std::string &name) const {
    auto it = uniformCache.find(name);
    if (it != uniformCache.end())
        return it->second;

    int loc = glGetUniformLocation(ID, name.c_str());
    uniformCache[name] = loc;
    return loc;
}
```

`glGetUniformLocation` performs a **string comparison inside the driver** on every
call. With 10 cubes and several uniforms each, that is dozens of driver string
lookups per frame. Caching the integer makes each subsequent lookup a hash-map hit.

The cache is `mutable` so the `set*` methods can stay `const` — the cache is not
part of the object's logical state, only a memoization of it.

**A location of `-1` means the uniform was not found**, which happens when it does
not exist *or when the GLSL compiler optimised it away for being unused*.
`glUniform*` with location `-1` is defined to do nothing silently. This is a
frequent source of "why is my uniform ignored" confusion — and it happened in
this project's history, when `lightPos` was declared in the vertex shader but only
used in the fragment shader.

All the setters require the program to be **currently bound** via `glUseProgram`,
since `glUniform*` acts on the active program. This is why `render()` calls
`shader.use()` before any `set*`.

---

## 9. Window, context, and timing

### 9.1 Context creation

```cpp
glfwInit();
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
```

Request OpenGL 3.3 Core. **Core profile removes the fixed-function pipeline** —
no `glBegin`, no built-in matrix stack, and critically no default VAO. That last
point is why every buffer operation in this codebase happens with a VAO bound.

```cpp
glfwMakeContextCurrent(window);
```

An OpenGL context is thread-local and implicit: every `gl*` call operates on
whatever context is current on the calling thread. Nothing works before this.

```cpp
if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { ... }
```

GLAD resolves function pointers. The OS ships only an ancient OpenGL 1.1 import
library; everything newer must be looked up at runtime from the driver. That is
what GLAD automates, and it **must run after** a context is current, because the
lookup itself needs one.

```cpp
glfwSwapInterval(1);
```

V-sync: cap presentation to the monitor's refresh. This prevents tearing and
effectively pins the frame rate to the display's, which is why `deltaTime` is
needed for smooth motion.

### 9.2 Input handling

```cpp
glfwSetWindowUserPointer(window, camPtr);

static Camera& cameraFor(GLFWwindow* window) {
    return *(Camera*)glfwGetWindowUserPointer(window);
}
```

GLFW callbacks are plain C function pointers and cannot carry a `this`. The user
pointer is GLFW's escape hatch: stash an arbitrary pointer on the window, then
retrieve it inside the callback. This is the standard C-to-C++ bridge.

Mouse and scroll are handled by **callbacks** (events, delivered when they happen),
while keyboard movement is **polled** each frame with `glfwGetKey`. This split is
deliberate: movement should depend on how long a key is *held*, which polling
gives you naturally, whereas mouse motion is inherently a stream of discrete
deltas.

The `static bool f11Pressed` / `tabPressed` flags implement edge detection —
firing once on the transition from up to down, rather than every frame the key
stays down. Without them, holding F11 would toggle fullscreen every frame.

`glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED)` hides the cursor and
removes its screen boundaries, giving unlimited mouse movement — what every FPS
camera needs.

### 9.3 Frame timing

```cpp
float currentFrame = glfwGetTime();
deltaTime = currentFrame - lastFrame;
lastFrame = currentFrame;
```

`glfwGetTime()` returns seconds since init as a `double`. The difference between
consecutive frames is the per-frame timestep that makes camera speed independent
of frame rate. *(This is currently computed in two places — see 12.1.)*

```cpp
glfwSwapBuffers(window);
glfwPollEvents();
```

Double buffering: draw into a back buffer, then swap it to the front in one
operation, so the user never sees a partially drawn frame. `glfwPollEvents`
processes the OS message queue and dispatches the registered callbacks — **this
is the only point in the frame where input callbacks actually fire.**

---

## 10. The ImGui overlay

```cpp
ImGui_ImplGlfw_InitForOpenGL(window, true);
ImGui_ImplOpenGL3_Init("#version 330");
```

ImGui is *immediate mode*: there are no widget objects that persist. Every frame
you re-declare the entire UI, and the library rebuilds vertex buffers from
scratch. `ImGui::SliderFloat("FOV", &camera.FOV, …)` is handed a raw pointer and
writes through it directly, which is why the UI needs no synchronisation code.

The `true` argument installs ImGui's own GLFW callbacks, chaining to the ones
already registered. This is how ImGui captures mouse input when the cursor is over
a panel.

Order matters in the main loop: the 3D scene renders first, then the overlay draws
on top. The overlay is 2D and unaffected by the depth buffer.

Worth knowing: ImGui's backend saves and restores GL state around its own
rendering, including polygon mode. That is why setting wireframe mode does not
turn the UI panel into wireframe.

---

## 11. Every GL call in this codebase

| Call | Purpose |
|---|---|
| `glGenVertexArrays` | Create VAO name(s) |
| `glBindVertexArray` | Make a VAO current; 0 unbinds |
| `glGenBuffers` | Create buffer name(s) |
| `glBindBuffer` | Bind a buffer to a target slot (`GL_ARRAY_BUFFER`, `GL_ELEMENT_ARRAY_BUFFER`) |
| `glBufferData` | Allocate and upload buffer contents |
| `glVertexAttribPointer` | Describe how to read one attribute out of the bound VBO; records into the bound VAO |
| `glEnableVertexAttribArray` | Turn that attribute on (mandatory) |
| `glDrawElements` | Draw indexed primitives from the bound VAO's element buffer |
| `glEnable(GL_DEPTH_TEST)` | Enable per-pixel depth comparison |
| `glClearColor` | Set the colour the framebuffer is cleared to |
| `glClear` | Clear colour and/or depth buffers |
| `glPolygonMode` | Render as filled / wireframe / points |
| `glPointSize` | Pixel diameter for `GL_POINT` mode |
| `glViewport` | Map NDC to a pixel rectangle |
| `glGenTextures` | Create texture name(s) |
| `glActiveTexture` | Select the texture unit for subsequent binds |
| `glBindTexture` | Bind a texture to the active unit |
| `glTexParameteri` | Set wrap / filter behaviour |
| `glTexImage2D` | Upload pixel data |
| `glGenerateMipmap` | Build the mipmap chain |
| `glCreateShader` | Create a shader object |
| `glShaderSource` | Give it GLSL source |
| `glCompileShader` | Compile it (at runtime, in the driver) |
| `glCreateProgram` | Create a program object |
| `glAttachShader` | Attach a compiled shader |
| `glLinkProgram` | Link stages into a usable program |
| `glGetShaderiv` / `glGetShaderInfoLog` | Query compile status / error text |
| `glGetProgramiv` / `glGetProgramInfoLog` | Query link status / error text |
| `glDeleteShader` | Release a shader object after linking |
| `glUseProgram` | Make a program current |
| `glGetUniformLocation` | Look up a uniform's location by name |
| `glUniform1i` / `1f` / `3fv` / `Matrix4fv` | Upload uniform values to the current program |

---

## 12. Known quirks in the current code

These are real behaviours of the code as it stands, listed so the document
matches reality rather than intent.

### 12.1 `deltaTime` is computed twice per frame

`render()` contains:

```cpp
double now = glfwGetTime();
window.deltaTime = now - window.lastFrame;
window.lastFrame = now;
```

and `Window::refresh()` contains the same computation. Because the loop runs
`processInput -> render -> overlay -> refresh`, the value `refresh()` leaves
behind is *only the time from `render()` to `refresh()`* — the overlay pass —
not the full frame. That much smaller number is what `processInput` reads on the
next iteration.

This is almost certainly why `Camera::speed` has had to be raised to `200.0f`
with a slider up to `500`, when a true per-frame delta would want something far
smaller. Removing the block from `render()` restores a correct full-frame delta,
at which point `speed` should be retuned back down.

### 12.2 The texture is sampled and then discarded

`cube.fs` computes:

```glsl
vec3 surface = objectColor;
if (useTexture)
    surface *= texture(ourTexture, TexCoord).rgb;
```

but the final line multiplies by `objectColor`, not `surface`:

```glsl
vec3 result = (ambient + diffuse + specular) * objectColor;
```

So `surface` is computed and thrown away, and the overlay's "use texture"
checkbox has no visible effect. Changing `objectColor` to `surface` on that line
is the fix.

### 12.3 Mipmaps are generated but never used

`glGenerateMipmap` builds the chain, but the minification filter is set to
`GL_LINEAR`, which samples only level 0. To actually use the mipmaps the filter
needs to be a mipmap variant, e.g. `GL_LINEAR_MIPMAP_LINEAR` (trilinear).
As written, you pay the memory cost without the anti-aliasing benefit.

### 12.4 Triangle winding is inconsistent

Face culling is not enabled, so this is currently harmless — but the index order
does not wind consistently. Checking each face's first triangle against its
normal:

```
Back    CW    (opposite its normal)
Front   CCW
Left    CW    (opposite its normal)
Right   CCW
Bottom  CCW
Top     CW    (opposite its normal)
```

If `glEnable(GL_CULL_FACE)` were ever switched on with the default `GL_CCW`
front-face convention, the back, left and top faces would vanish. Fixing it means
swapping the index order for those three faces.

### 12.5 Smaller things

- `viewPos` is uploaded inside the per-object loop in `render()`, though it is the
  same for every object. It belongs with the other shared uniforms above the loop.
- The `PI` macro in `main.cpp` is no longer referenced.
- `Window` has a `monitor` member that is shadowed by local variables of the same
  name in `initialize()` and `toggleFullscreen()`; the member is never used.
- Shader file naming is inverted relative to their roles: `cube.vs`/`cube.fs` are
  the full lighting shaders, while `lighting.vs`/`lighting.fs` draw the flat lamp
  marker.
