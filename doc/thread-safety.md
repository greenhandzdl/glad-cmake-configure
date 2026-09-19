# Thread safety

The engine touches GL from **exactly one thread** while still using background
threads for slow CPU work. This document lists the concrete mechanisms that make
that correct, each mapped to the code that enforces it. The load-bearing
invariant:

> **Invariant.** Every OpenGL call (creation, upload, draw, *and* deletion)
> happens on the render thread. Worker threads only ever do CPU work (file IO,
> decode, parse, math) on data that is not yet — and may never be — a GL object.

Everything below is a way of holding that line.

## 1. GL context thread affinity (enforced, not assumed)

`RenderContext` (`src/gfx/core/RenderContext.{h,cpp}`) is the gatekeeper:

- `MarkAsRenderThread()` sets a `thread_local bool g_isRenderThread = true`.
  `src/main.cpp:225` calls it once, on the thread that owns the GL context, right
  after context creation.
- `IsRenderThread()` reports the flag.
- `AssertRenderThread(where)` prints a `FATAL` line to stderr and `std::abort()`s
  if the flag is not set on the current thread.

Every class that owns or drives GL calls `AssertRenderThread(...)` at the entry
of each public method and in its destructor. Confirmed call sites include:
`GLBuffer`, `UniformBuffer`, `VertexArray`, `Sampler`, `Framebuffer`,
`Texture2D`, `TextureCubeMap`, `RenderTexture`, `ShaderProgram`, `Mesh`,
`InstancedMesh`, `EnvironmentMap`, `CascadedShadowMap`, `PostProcessChain`,
`SkyboxRenderer`, `SpriteBatch`, the `RenderPass::Execute` methods in
`RenderPasses.cpp`, and `Renderer::Render/Init/AddPass`. The check is cheap and
fail-fast: a wrong-thread GL call crashes immediately with a named location
instead of corrupting the context silently.

## 2. Two-stage asset pipeline (CPU off-thread, GL on-thread)

Split at the GL boundary — see `AssetManager` (`src/gfx/assets/`):

- **Stage A** — `RequestModel()` / `RequestTexture()` hand the *path* to a
  `ThreadPool`; workers run Assimp/STB decode and build plain CPU structs
  (`LoadedModelData`, `Texture2DDesc`). No GL types exist yet, so there is
  nothing to misuse off-thread.
- **Stage B** — `ProcessUploads()` runs **on the render thread** once per frame,
  drains finished futures, and only then creates GL objects (`Mesh::Upload`,
  texture creation) — the one place decoded data becomes GPU state.

Because Stage A returns immutable descriptions by value/`shared_ptr<const>` and
Stage B is render-thread-only, the two stages never race on a GL object.

## 3. Synchronisation primitives in `AssetManager`

The manager guards its two distinct shared structures with two distinct locks
(`src/gfx/assets/AssetManager.h`):

- `mutable std::mutex queueMutex_` — guards the in-flight future vectors
  (work queue). Short critical sections around enqueue / drain.
- `mutable std::shared_mutex storeMutex_` — guards the finished-asset maps.
  Lookups take a **shared** (reader) lock; publishing a finished asset takes the
  **unique** (writer) lock. Many readers, rare writers — which is exactly the
  frame-loop access pattern.
- Stage A results are carried as `std::future<std::expected<T, std::string>>`,
  so completion is observed via the future (no shared mutable decode state).

## 4. Ownership: expose handles, never mutable GL objects

- `AssetManager` is non-copyable, and asset lookups return `shared_ptr<const …>`
  / value handles; callers cannot obtain a writable GL wrapper from the cache.
- Scene nodes (`SceneNode`) hold **non-owning** `const Mesh*` / `const
  PbrMaterial*`. The owning objects live in the application's render-thread
  resource scope (`src/main.cpp`), so GL-object lifetime stays pinned to the
  render thread and is not spread through the CPU graph.
- `RenderFrame` (per-frame inputs) holds subsystems by pointer/reference and
  owns **no** GL resource; it is a data bundle, so passing it to passes cannot
  duplicate or move an owner.

## 5. Destroy-on-render-thread (by ownership + destructor asserts)

GL deletion is as thread-affine as creation. Each GL wrapper's destructor calls
`AssertRenderThread` (e.g. `~Texture2D`), so freeing a resource off-thread aborts
just like a wrong-thread draw would. Correctness is therefore guaranteed by
*keeping ownership on the render thread* (see §4) so teardown naturally lands
there, and verified by the destructor assertions.

> Roadmap note: there is currently **no automatic deferred-destruction queue**
> (handing a doomed GL id to the render thread to free later). Ownership design
> makes one unnecessary today; if assets ever need to be released from a
> non-render thread, that queue is the intended extension.

## 6. Move-only RAII (no accidental handle duplication)

Every GL wrapper and the manager/passes delete copy construction and assignment
(`X(const X&) = delete; X& operator=(const X&) = delete;`) — see `GLBuffer`,
`VertexArray`, `UniformBuffer`, `Sampler`, `Framebuffer`, `Texture2D`,
`TextureCubeMap`, `RenderTexture`, `ShaderProgram`, `Mesh`, `InstancedMesh`,
`SceneNode`, `RenderPass`, `ThreadPool`, `AssetManager`. A GL id therefore has a
single owner; it cannot be silently copied and double-freed or driven from two
places. Movement is by `std::move`/`unique_ptr` only, so ownership transfers stay
explicit.

## 7. Error propagation without exceptions crossing threads

Fallible operations return `std::expected<T, E>` rather than throwing, at the GL
boundary (`ShaderProgram` compile/link), in asset decoding (`ModelLoader`,
`ImageLoader`, `AssetManager` futures), and in environment generation
(`EnvironmentMap`). This keeps failure handling explicit and avoids throwing
across a `std::thread`/future boundary, where an uncaught exception would call
`std::terminate`. Callers inspect `has_value()` / `error()` and decide policy.

## 8. Pure-CPU objects are inherently shareable

Some types hold no GL and no mutable shared state, so they are safe regardless of
thread: the math/utilities in `Frustum`, `Picking`, `Transform`, and the read side
of `SceneNode` (cached world matrices / bounding spheres). They are computed on
the render thread each frame (`Scene::Update`) and read by passes; because they
are value-semantic and GL-free, they carry no affinity constraint and can be
inspected or tested without a context. The scene graph was placed at this same
level on purpose.

---

### Quick checklist for new code

- Owning or calling GL? → run it on the render thread and add an
  `AssertRenderThread` at each public entry (and the destructor).
- Shared cache? → separate reader (`shared_mutex`) from writer (`mutex`) state;
  keep critical sections short.
- Public resource type? → delete copies, prefer move + `unique_ptr`, expose
  `const` handles.
- Possible failure? → return `std::expected`, don't throw across threads.
