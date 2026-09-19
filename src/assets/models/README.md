# Model drop-in directory

Place mesh assets here — **FBX / OBJ / glTF / GLB** (anything [Assimp][assimp]
v5.4.x can import). This folder is intentionally kept empty in the repository
via `.gitkeep`; add real files through your normal workflow (they are large, so
consider Git LFS or the ignore rules noted below).

[assimp]: https://assimp.org

## How models reach the GPU

Models are loaded through `gfx::AssetManager::RequestModel()` (see
[`src/gfx/assets`](../../src/gfx/assets)), which runs a **two-stage pipeline** so
no GL call ever happens off the render thread:

1. **Stage A (background thread, no GL):** Assimp parses the file and the
   importer builds CPU-side `MeshData` (positions, normals, UVs, tangents) and
   material descriptions. Results are returned as immutable, shared data.
2. **Stage B (render thread):** the uploaded `MeshData` is turned into GL
   buffers / vertex arrays inside `Mesh::Upload(...)`.

`AssetManager` is safe to call from any thread: it owns a worker queue
(`std::mutex` + `std::condition_variable`) and a result cache guarded by a
`std::shared_mutex`; callers get handles / `shared_ptr<const …>`, never mutable
GL objects. See [`doc/thread-safety.md`](../../doc/thread-safety.md) for the full
invariant list.

## Textures / materials

Model-embedded and standalone textures are referenced by path and decoded with
STB (`src/utils/stb.cpp`). Keep texture files alongside models here (e.g. a
`models/<name>/` subfolder) so relative material paths resolve.

## Repo hygiene

Large binaries are not committed by default. If you add heavy assets, extend
`.gitignore` (or enable Git LFS) rather than checking them in — the engine and
`main.cpp` must stay clone-and-build for CI.
