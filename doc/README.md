# Documentation

Design and safety notes for the `gfx` engine layer. Code is the ultimate source
of truth; these documents explain the *why* behind the structure and point at
the implementations.

| Document                                     | What it covers                                                                       |
| -------------------------------------------- | ------------------------------------------------------------------------------------ |
| [design.md](design.md)                       | Subsystem layering, the two-stage asset pipeline, HDR post chain, scene graph + linear render pipeline (and why not a full render graph), UBO binding. |
| [thread-safety.md](thread-safety.md)         | The concrete mechanisms that keep the multi-threaded design correct, each mapped to code. |

## Reading order

1. Start with [design.md](design.md) for the big picture and module boundaries.
2. Read [thread-safety.md](thread-safety.md) before touching anything that owns
   a GL object or crosses a thread boundary.
3. See [`../assets/README.md`](../assets/README.md) for how content and code are
   isolated on disk.

## Scope / roadmap

This is a graphics/engine **core**, not a full game engine. Deliberately out of
scope today (candidates for later phases):

- an Entity-Component system (the current scene is a transform hierarchy of
  `SceneNode`s, not an ECS),
- skeletal animation / skinning,
- physics, audio, and a general input-action mapping layer,
- texture compression (KTX/BC/ASTC) pipelines,
- a data-driven material / shader-graph system.
