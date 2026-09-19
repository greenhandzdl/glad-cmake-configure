# Design

How the `gfx` layer is put together and why. Filenames and symbols referenced
here exist in `src/gfx/**`.

## 1. Layered subsystems

The engine is a set of small subsystems under `src/gfx/`, each owning one
concept. Dependencies point **downward only** — a higher layer may use a lower
one, never the reverse.

```
                 ┌───────────────────────────────────────────────┐
  application    │  src/main.cpp          (demo app, wires it up) │
                 └───────────────┬───────────────────────────────┘
                                 │ uses
                 ┌───────────────▼───────────────────────────────┐
  orchestration  │  render/   Renderer · RenderPass · RenderFrame │  CPU scene
                 │  scene/    Scene · SceneNode · Transform       │  + GL passes
                 └───────────────┬───────────────────────────────┘
                                 │ uses
                 ┌───────────────▼───────────────────────────────┐
  features       │  geometry  texture  material  shader           │
                 │  camera    light      shadow    text  debug    │
                 │  assets    (async model/texture loading)       │
                 └───────────────┬───────────────────────────────┘
                                 │ uses
                 ┌───────────────▼───────────────────────────────┐
  platform/GL    │  core/  RenderContext · GLBuffer · VertexArray │
                 │         UniformBuffer · Sampler · Framebuffer  │
                 └───────────────────────────────────────────────┘
```

- **core/** wraps raw GL handles and owns the thread-affinity guard
  (`RenderContext`). Everything above it is expressed in terms of these RAII
  objects.
- **scene/** is pure CPU (matrices + bounds), no GL, so it is naturally safe to
  reason about and test independently of a context.
- **render/** is the only place that sequences GL per frame.

## 2. Scene graph (`src/gfx/scene/`)

A lightweight transform hierarchy, deliberately *not* an ECS.

- `Transform` (header-only): `translation` · `rotation` (unit quaternion) ·
  `scale`. `LocalMatrix()` composes T·R·S (column-major GLM);
  `WorldFrom(parentWorld)` = `parentWorld * local`.
- `SceneNode`: owns a `Transform`, a **non-owning** `const Mesh*` +
  `const PbrMaterial*`, a `std::vector<std::unique_ptr<SceneNode>>` of children,
  and `visible` / `castsShadow` flags. `UpdateWorld(parentWorld)` recurses,
  caching the node's world matrix and a **world-space bounding sphere**
  (`center = world * localCenter`, `radius = localRadius * maxAxisScale(world)`).
  The local sphere is computed once from `MeshData` via
  `SceneNode::BoundsFromMeshData`.
- `Scene`: a forest of roots. `Update()` refreshes every root against identity
  and flattens the traversal into ordered lists — `Renderables()`,
  `ShadowCasters()`, `PickTargets()` — that the render passes consume without
  re-walking the tree.

Because nodes hold **non-owning** pointers, the *application* (via
`src/main.cpp`) owns the `Mesh`/`PbrMaterial` lifetime; nodes only reference
them. That keeps GL-object ownership on the render thread and out of the CPU
hierarchy. See [thread-safety.md](thread-safety.md) §4.

## 3. Render pipeline (`src/gfx/render/`)

A **linear pass chain**, not a full render graph.

- `RenderFrame` is a per-frame value struct that bundles everything the passes
  need by pointer/reference (camera, frustum, scene, post chain, light buffer,
  shadow map, env, the PBR/depth/skybox/instanced programs, toggles, the picked
  node, HUD resources, profiler, and a few output counters like
  `visibleCount`). It owns no GL resources — it is just "this frame's inputs".
- `RenderPass` is an abstract base: `virtual void Execute(RenderFrame&) = 0;`
  plus a `name`. It is non-copyable.
- Concrete passes (in `RenderPasses.h/.cpp`), executed in order:
  1. `ShadowPass`   — update CSM, render casters into the depth-array per cascade.
  2. `GeometryPass` — HDR scene into the MSAA target: bind UBOs/shadow/env, draw
     frustum-culled renderables (with a highlight material on the picked node),
     optional instanced field, then the skybox.
  3. `PostProcessPass` — resolve MSAA, optional bright/blur bloom, then the ACES
     composite to the default framebuffer.
  4. `DebugHudPass` — `DebugDraw` wireframes + `SpriteBatch`/`Font` HUD text, and
     closes the `Profiler` frame.
- `Renderer` holds the ordered `std::vector<std::unique_ptr<RenderPass>>`;
  `BuildDefaultPipeline()` installs the four passes above, `AddPass()` lets an app
  customize, and `Render(frame)` just runs them in sequence. `Init()` sets the
  one-time global GL state (depth test, face culling).

### Why a linear chain and not a render graph?

A general render graph (transient resources, automatic barrier/resource-state
management, sub-pass merging) earns its complexity when you have many passes with
non-trivial read/write dependencies across async compute or tile-based GPUs. This
pipeline is four fixed-order passes over a handful of long-lived render targets.
Hand-writing the order is shorter, easier to debug, and matches how the GL state
is actually used. `RenderFrame` + `RenderPass` already give the seams we would
need to grow toward a graph later (each pass declares its work in `Execute`, the
`Renderer` owns ordering), so we defer the abstraction until it pays for itself.

## 4. Materials, lighting and UBOs

Light and shadow state are shared through **uniform buffer blocks**:

- `LightingBlock` → binding **1**, `ShadowBlock` → binding **2** (managed by
  `LightBuffer` and `CascadedShadowMap` under `src/gfx/light` and `src/gfx/shadow`).
- Blocks use `layout(std140)`; the C++ mirror structs pack to the same layout.
- **GLSL 4.10 has no `layout(binding=N)` on uniform *blocks*** (that is a later
  GLSL feature), so the binding is assigned from the C++ side with
  `glUniformBlockBinding` / `ShaderProgram::SetBlockBinding`. Forgetting this is a
  classic "black scene with no error" trap, so the binding step is explicit at
  program-link time rather than assumed from the shader.

Materials (`PbrMaterial`) expose albedo / metallic-roughness / normal / AO maps
with scalar fallbacks and apply themselves as uniforms before a draw; the PBR
program reads lights and shadows from the bound blocks, so a material only sets
per-object texture/uniform state.

## 5. Two-stage resource pipeline (`src/gfx/assets/`)

Asset loading is split so that no GL call happens off the render thread:

- **Stage A (worker threads, no GL).** `AssetManager::RequestModel()` /
  `RequestTexture()` enqueue file IO + Assimp/STB decode onto a `ThreadPool`.
  The futures yield CPU-side descriptions (`LoadedModelData`,
  `Texture2DDesc`) wrapped in `std::expected`.
- **Stage B (render thread).** `AssetManager::ProcessUploads()`, called once per
  frame, drains finished Stage-A results and performs the GL upload
  (`Mesh::Upload`, `Texture2D` creation). Only here are GL objects touched.

Lookups return a handle / `shared_ptr<const …>` and yield `nullptr` until ready,
so callers never observe a half-built GL resource. Full guarantees and the exact
synchronisation primitives are in [thread-safety.md](thread-safety.md).

## 6. HDR post-processing chain (`src/gfx/render/` + `PostProcessShaders.h`)

The geometry/skybox/instanced draws output **linear HDR** into an RGBA16F,
MSAA-resolved target. Tone mapping and gamma are **not** in the PBR shader —
they live in the single composite pass so the whole frame (scene *and* emissive
*and* skybox) is mapped consistently:

```
linear HDR ──► exposure ──► ACES filmic ──► gamma(1/2.2) ──► default FBO
       ▲
       └── additive bloom: bright-pass(threshold) → separable blur → mix
```

Because everything stays linear until the one composite, bloom and the sun disc
behave physically; toggling bloom off just sets bloom strength to 0, the tone
map still runs. Sprite/text HUD is drawn **after** the composite, directly to the
default framebuffer, so it is not double tone-mapped.

## 7. Delivery as a C++20 named module

The whole engine is one C++20 **named module `gfx`** built as a static library
(`add_library(gfx STATIC)`). The application's only coupling to it is
`import gfx;` in `src/main.cpp` — no per-header `#include`s of `gfx/**`. This
replaced the earlier layout where `main.cpp` textually included ~27 engine
headers and everything was compiled straight into one executable.

- **Primary interface** — `src/gfx/gfx.cppm`: `export { #include "..." }` wraps
  every public engine header once, so the module re-exports the full API.
  A single interface unit (rather than one partition per subsystem) matches the
  header-centric codebase and avoids partition-ordering hazards across vendors.
- **Implementation units** — the 31 `src/gfx/**/*.cpp` are each a
  `module gfx;` unit. They see all engine declarations via the implicit import of
  the primary interface, so they carry **no** `#include "gfx/…"`.
- **Global module fragment** — GLAD, GLM and the common standard-library headers
  live in `src/gfx/gmf.hpp`, textually included at the top of every unit's
  `module;` fragment. That attaches `GLuint` / `glm::vec3` etc. to the *global
  module* (not to `gfx`), so exported signatures reference global-module types
  and `main.cpp` — which textually includes the same headers — resolves the
  identical entities rather than a conflicting module-scoped redeclaration.
- **Third-party policy** — GLAD + GLM leak into the public API so they are
  global-fragment includes (never `export`ed as entities). **STB and Assimp are
  implementation-only**: they stay inside individual units' fragments (e.g.
  `ModelLoader.cpp`, the `third_party/stb_image_impl.cpp` TU) and never surface
  on the module interface. The engine headers no longer `#include` them at all.
- **Windowing stays out of the module** — `src/gfx/core/Platform.h` (GLAD-before-
  GLFW ordering, `GLFW_PLATFORM_*` macros, the `gfx::kApp*`/`kWindow*` constants)
  is deliberately a plain text include for `main.cpp` only, because `<GLFW/glfw3.h>`
  and `#if`-visible macros cannot cross a module boundary cleanly.

Because newer standard libraries dropped transitive `<ostream>`/`<cstdint>`-style
includes, the vendored Assimp build is force-included with the few headers its
legacy contrib sources omit (see the `assimp` block in `CMakeLists.txt`) — our own
sources keep honest, explicit includes.
