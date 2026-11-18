# Push-the-Box RL: Roadmap

Goal: train a pill-shaped agent to push a box onto a goal position, using PyTorch for
training and this OpenGL engine to watch the agent improve over time.

This file covers steps and concepts only, with no implementations. It also teaches the
reinforcement learning (RL) and OpenGL ideas you need as you go.

---

## 1. What the engine has today

It's a small OpenGL 3.3 core-profile renderer, built with GLFW, glad, GLM, ImGui and stb_image:

- **[main.cpp](../../src/main.cpp)** owns everything. The cube's vertices are hard-coded and
  uploaded into one VAO/VBO/EBO. A global `std::vector<Object>` is drawn by `render()`,
  which builds each object's model matrix (translate, then rotate X/Y/Z, then scale) and
  makes one `glDrawElements` call per object. The light marker reuses the same VBO through
  a second VAO.
- **[types.h](../../src/types.h)**: `Object` = `Transform` + `Material` + `visible`. It's pure render data.
- **[camera.cpp](../../src/camera.cpp)**: a free-fly camera. **[window.cpp](../../src/window.cpp)**
  handles the window and input. **[overlay.cpp](../../src/overlay.cpp)** is an ImGui panel that
  edits the scene directly.
- **Build:** a single g++ command in [tasks.json](../../.vscode/tasks.json), using the MSYS2 UCRT
  toolchain. There's also a stale `build/` folder with a CMake cache.

### Problems that matter for this goal

1. **There is no simulation layer.** Objects only move when you drag a slider. Nothing
   advances the world by `dt`. This is the biggest missing piece.
2. **Time is tracked in two places.** Both `render()` ([main.cpp:200-202](../../src/main.cpp#L200-L202))
   and `Window::refresh()` ([window.cpp:495-497](../../src/window.cpp#L495-L497)) write `deltaTime`
   and `lastFrame`. Both are also `float`, while `glfwGetTime()` returns `double`.
3. **The cube's geometry is built into the code.** `cubeIndiceCount` is a global and `Object`
   has no field saying which mesh it uses, so it can't draw a pill.
4. **`.gitignore` line 5 (`*.obj`) is ignoring your models.** `content/models/bean.obj`
   would never be committed. Add an exception rule for `content/models`.
5. **[cube.fs](../../shaders/cube.fs) declares `useTexture` and `ourTexture` but never samples them**,
   so textures currently do nothing.
6. **[bean.obj](../../content/models/bean.obj) will test your loader in four ways:**
   - Its faces are **quads**, not triangles.
   - Faces use `v/vt/vn` triplets.
   - It references a `bean.mtl` that doesn't exist.
   - Its Y range is −1 to 3, so the origin is at the centre of the bottom cap, not the
     middle of the model.

---

## 2. The key idea: separate simulation from rendering

Training runs millions of steps with no window, as fast as the CPU allows. Watching runs at
60 fps. So the physics and environment must be **pure C++ with no OpenGL**, and the renderer
is just one consumer of the world state.

```
        ┌──────────── sim/  (pure C++, zero GL includes) ────────────┐
        │  World + physics  ·  Env: reset(seed) / step(action)        │
        └──────────▲──────────────────────────────▲───────────────────┘
                   │ pybind11 module              │ linked as a library
          python/  (PyTorch, PPO)          apps/viewer  (OpenGL + ImGui)
                   │ writes                       ▲ reads
                   └──► runs/<run>/  metrics · checkpoints · replays ──┘
```

The training side and the viewer never have to run at the same time. Training writes files,
and the viewer reads them. That's what makes it easy to show progress over time.

### Suggested layout

```
opengl/
  CMakeLists.txt
  third_party/          glad, glm, imgui (+ implot), stb, glfw (via FetchContent)
  engine/               Shader, Mesh, ObjLoader, Camera, Window, DebugDraw, Texture
  sim/                  math types, World, Physics, Env, VecEnv     ← no GL, no ImGui
  bindings/             pybind11 module exposing Env/VecEnv
  apps/viewer/          main.cpp: run browser, replay player, plots, live mode
  python/
    pushbox/            env wrapper, networks, ppo, rollout buffer, logger, replay writer
    train.py  evaluate.py  configs/
  runs/                 (gitignored) one folder per training run
  content/  shaders/
```

Two rules keep this clean:

- `sim/` never includes anything from `engine/`.
- `engine/` knows nothing about rewards or policies.

The viewer converts a sim snapshot into draw calls.

### The Windows toolchain trap

PyTorch on Windows only ships for the normal python.org CPython, which is built with MSVC.
A pybind11 module built with MSYS2 g++ and loaded into that Python is unsupported and fragile.

**Recommended:** move the whole project to **CMake + MSVC**, using the VS Build Tools and the
VS Code "CMake Tools" extension. Fetch GLFW with `FetchContent` instead of shipping the
MinGW-only `libglfw3dll.a`.

**Fallback (stay on MinGW):** run the C++ environment as a small TCP/ZeroMQ server and step
all environments in one batched message. It's slower and has more moving parts.

Commit what you have before you start restructuring.

---

## 3. Roadmap

Each phase ends with a "done when" check. Don't start the next phase until it passes.

### Phase 0: Foundation

- [ ] Set up CMake, the folder split above, and the `.gitignore` fix.
- [ ] Use one place for frame timing in the viewer.
- [ ] Read **"Fix Your Timestep!" by Glenn Fiedler**. The sim should advance in fixed steps
      (e.g. 1/60 s). The viewer's frame time only decides *how many* fixed steps to run, and
      can interpolate between steps for display.

**Done when:** the current scene builds through CMake and looks the same as before.

### Phase 1: Meshes and OBJ import (OpenGL lesson)

**Mesh class.** A VAO doesn't hold data. It *describes how to read* buffers: attribute
layouts, which VBO each attribute comes from, and which EBO is bound. Your light VAO already
shows this, since it reuses the same VBO with a different layout. Each mesh should own its
VAO, VBO, EBO and index count, and `Object` should reference a mesh.

**The OBJ format**, which is plain text:

- `v` = positions, `vt` = texture coordinates, `vn` = normals, `f` = faces.
- Indices start at **1**, and **negative indices** mean "count back from the end".
- **The core problem:** OBJ indexes position, UV and normal *separately* (`6/1/1`), but
  OpenGL uses **one index per vertex**. Your loader has to turn each unique `(v, vt, vn)`
  combination into one GL vertex and reuse it when it appears again. A hash map keyed on the
  triplet does this.
- **Polygons** (the bean's quads) need triangulating. For convex faces, a "fan" around the
  first vertex works.
- **Missing normals:** compute them with the cross product of two triangle edges
  (see [ENGINE.md](../../ENGINE.md) §1.3).
- **Missing `.mtl`:** log a warning and carry on.
- **Bounds:** record the mesh's min and max while loading. You'll need them to recentre the
  bean and to match its visual size to its physics size.

Try writing the loader yourself. `tinyobjloader` exists if you want to compare afterwards.

**Done when:** the bean, a cube and a floor plane all render from files, and you can switch
meshes in ImGui.

### Phase 2: Simulation (physics lesson)

**Big simplification:** an upright capsule seen from above is a **circle**, and an upright
box is a **rectangle**. So the whole contact problem is **2D on the ground plane**. You
render in 3D, but Y is just cosmetic. Start with a box that only slides; add rotation later.

What to learn and build:

- **State:** position, velocity and yaw for the agent and the box, the goal position, and
  the arena walls.
- **Integration:** apply the action force, add floor friction as velocity damping, then
  integrate with a fixed `dt`.
- **Circle vs rectangle collision:** convert the circle centre into the box's local space,
  clamp it to the box's extents to get the closest point, then read off the penetration
  depth and contact normal.
- **Resolution:** push the bodies apart (positional correction), then apply a velocity
  impulse split by mass ratio. That impulse is the "push".
- **Box rotation (later):** a contact off the box's centre creates torque (r × J). Erin
  Catto's GDC physics talks and *Box2D Lite* are the best short references. Box2D v3 is the
  off-the-shelf option, but this problem is small enough that writing it yourself is worth it.
- **Determinism:** same seed + same actions must give the same trajectory. That makes bugs
  reproducible.

**Done when:** you can drive the pill with the keyboard in the viewer and push the box to the
goal yourself. If a human finds it awkward, the agent will too. Tune the physics now, not later.

### Phase 3: Environment API and Python bridge

- [ ] Use the standard interface: `reset(seed) → obs` and
      `step(action) → obs, reward, terminated, truncated, info`.
- [ ] Add a **VecEnv**: N worlds stepped in one C++ loop, exchanging NumPy arrays through
      pybind11. Batching hides Python's per-call overhead.
- [ ] Wrap it as a **Gymnasium** environment and run Gymnasium's environment checker on it.
- [ ] Run a **random policy** and record the baseline success rate and return.
- [ ] **Sanity check:** train it once with **Stable-Baselines3 PPO**. If SB3 can learn your
      environment, any later failure is in *your* PPO. If it can't, the problem is the
      environment or the reward.

Use the CPU build of PyTorch. With a tiny network and a CPU-bound environment, the GPU won't help.

### Phase 4: Your own PPO in PyTorch

See the RL primer in section 4. Suggested order:

1. Write **REINFORCE** on a trivial task, "walk to the goal" with no box.
2. Add a learned value **baseline**.
3. Add **GAE**.
4. Upgrade to **PPO**.
5. Then switch to the box task.

Log everything to `runs/<run>/`:

- `metrics.csv` (plus TensorBoard if you like)
- checkpoints every K iterations
- a few **replay episodes** per checkpoint

### Phase 5: Visualizing progress

**Replay files.** Store the state every step, not just the actions, so the viewer never needs
physics. Each step records:

- agent and box positions and yaw
- the goal
- the action taken
- the reward
- the critic's value estimate

**Viewer features**, roughly in order of value:

- **Run browser and checkpoint slider:** pick iteration 0, 50 or 500 and watch the agent at
  that point; play, pause, scrub and change speed.
- **Training curves** with **ImPlot**, an ImGui add-on: return, success rate and episode
  length, with a marker at the checkpoint you're watching.
- **Trails:** a line strip of the agent's and box's paths. You'll need a dynamic VBO that you
  re-upload each frame (`GL_DYNAMIC_DRAW`), which is your first moving geometry. Note that
  `glLineWidth > 1` is unsupported in core profile on many drivers.
- **Ghosts:** the same seed played by several checkpoints at once, semi-transparent. This is
  the most direct way to *see* learning. It teaches **alpha blending**: draw opaque objects
  first, then transparent ones back-to-front with depth writes off.
- **Value heatmap on the floor:** Python evaluates V(s) over a grid of agent positions when it
  saves a checkpoint. The viewer uploads the grid as a single-channel float texture
  (`GL_R32F`) and maps values to colours in a shader. This shows *what the critic believes*.
- **Procedural floor grid** in a fragment shader, using `fract` and `fwidth`.
- **Live mode (later):** export the MLP weights to a simple binary file and write the forward
  pass in C++ (matrix-vector multiplies plus tanh). Add **mouse picking**: a ray from the
  camera through the cursor, using the inverse of projection × view, intersected with the
  floor. Then you can drag the box or goal around while the policy reacts.
- **Many environments at once:** **instanced rendering** with `glDrawElementsInstanced`, where
  a per-instance attribute set with `glVertexAttribDivisor` supplies each copy's model matrix.

### Phase 6: Make it harder

Add these as the agent improves:

- random spawn positions
- box rotation
- obstacles
- raycast "vision" observations
- multiple boxes
- eventually pixel observations, by rendering to a framebuffer object (FBO) and reading it back

**Shadow mapping** is also worth learning at some point, because shadows make contact much
easier to judge by eye.

---

## 4. RL primer, using this task

**The loop:** the agent sees an observation, picks an action, the environment advances, and
the agent gets a reward. The math framework for this is a Markov Decision Process (MDP):
states, actions, transitions and rewards. "Markov" means the current observation must contain
everything needed to decide the next action. That's why you include **velocities**: from
positions alone the agent can't tell whether the box is moving.

**Policy** π(a|s) is your neural network. For a continuous action, it outputs a Gaussian
distribution over the action; for a discrete action, it outputs probabilities over a list of
choices.

**Return:** G = r₀ + γr₁ + γ²r₂ + …, where γ ∈ [0,1) is the **discount factor**, which weights
near-term reward more. Its rough "horizon" is 1/(1−γ) steps: γ=0.99 means about 100 steps.
Choose your step rate and episode length so a successful push fits inside that horizon.
**Action repeat** helps: the agent decides every 2–4 physics steps.

**Value function** V(s) is the expected return from state s. **Advantage** A = "how much
better this action did than expected".

**Policy gradient:** increase the probability of actions with positive advantage and decrease
it for negative ones.

- Raw returns are very noisy (that's REINFORCE).
- Subtracting V(s) as a baseline reduces the noise; this is **actor-critic**.
- **GAE(λ)** lets you trade bias against variance when estimating advantages.

**PPO:**

1. Collect a batch of experience with the current policy (it's **on-policy**: it learns only
   from data the current policy collected).
2. Run several passes of minibatch updates on that batch.
3. **Clip** the probability ratio π_new/π_old to [1−ε, 1+ε], so one batch can't wreck the policy.

The loss = policy term + c₁·value loss − c₂·entropy bonus (the entropy bonus encourages
exploration).

**Observations:**

- Use **relative** quantities: box − agent and goal − box, ideally rotated into the agent's
  frame. Never raw world coordinates.
- Normalize them (running mean and std).
- Fix and document the order of values in the observation vector.

**Actions:** start with a **discrete** set, such as 8 directions + no-op. A categorical
distribution is hard to get wrong. Move to continuous 2D force (a Gaussian, clipped to
[−1, 1]) once the pipeline works.

**Reward is the hard part:**

- **Sparse reward** (+1 only on success): a random pill almost never gets the box to the goal,
  so there's no learning signal.
- **Potential-based shaping:** add γΦ(s′) − Φ(s), where Φ combines −distance(box, goal) and
  −k·distance(agent, box). This form provably doesn't change which policy is optimal
  (Ng et al., 1999). Add a success bonus and a small per-step penalty.
- **Expect reward hacking.** Examples: the agent hugs the box without pushing, or jiggles it
  to farm shaping reward. **This is exactly why you build the replay viewer:** you can't
  diagnose this from a reward curve.

**Termination vs truncation:**

- **Terminated** = the episode really ended (success).
- **Truncated** = timeout. Here you must **bootstrap** the remaining return with V(s′), which
  you skip on true termination.
- Mixing these up is the most common PPO bug.

**Curriculum:** start with the box close to and in line with the goal, then widen the
randomization as the success rate goes up.

**What to watch while training:**

- episode return and success rate
- policy entropy (it should fall slowly, not collapse)
- approximate KL and clip fraction (if they're large, updates are too aggressive)
- explained variance of the critic

RL results vary a lot between seeds, so run at least 3 seeds before concluding anything.

**Typical starting PPO settings:**

- learning rate 3e-4
- γ 0.99, λ 0.95
- clip 0.2
- a 2×64 tanh MLP
- about 10 epochs per batch

---

## 5. Reading list

- **RL:** OpenAI *Spinning Up* (concepts plus a PPO walkthrough). *"The 37 Implementation
  Details of PPO"* (an ICLR blog-track post). CleanRL's single-file PPO, as a reference to
  compare against *after* your own attempt. Sutton & Barto, chapters 3, 6 and 13.
- **OpenGL:** LearnOpenGL's Model Loading, Instancing, Blending, Framebuffers and Shadow
  Mapping chapters. Their Model Loading chapter uses the Assimp library, but you'll write
  your own loader.
- **Physics:** "Fix Your Timestep!" (Fiedler), Box2D Lite, and Erin Catto's GDC slides.
