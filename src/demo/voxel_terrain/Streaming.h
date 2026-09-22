#ifndef VOXEL_TERRAIN_STREAMING_H
#define VOXEL_TERRAIN_STREAMING_H

/**
 * @file Streaming.h
 * @brief Infinite-terrain chunk streaming for the voxel_terrain demo.
 *
 * Owns the worker pool, the two in-flight queues (generation, then meshing) and
 * the per-frame Stream() that decides what to load, drains finished work, and
 * uploads the results. Pulled out of main.cpp so the frame loop stays readable:
 * main just calls Stream(eye) and reads the counters for the HUD.
 *
 * Threading contract (unchanged from when this lived inline in main):
 *   * Generation and meshing run on pool threads; they only touch CPU world
 *     state under world.mx, never GL.
 *   * The mesh queue is drained on the render thread inside Stream(), because
 *     that drain uploads VAO/VBOs.
 */

import gldx;

#include <cstdint>
#include <deque>
#include <future>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "World.h"

namespace voxel_terrain {

class ChunkStreamer {
public:
    explicit ChunkStreamer(World& world);

    // Advance streaming around the eye: request generation for the nearest
    // missing chunks, mesh what just became complete, remesh dirtied chunks,
    // and drain finished generation/meshing (the mesh drain uploads on this,
    // the render, thread).
    void Stream(const glm::vec3& eye);

    // Stop the worker pool (joins its threads); safe to call before destruction.
    void Shutdown() { pool.shutdown(); }

    std::size_t genInFlight()  const { return genPending.size(); }
    std::size_t meshInFlight() const { return meshPending.size(); }
    // Cumulative counters for the HUD; handed out by reference so main's
    // existing "genCount"/"meshCount" reads bind straight to them.
    int& genCountRef()  { return genCount_; }
    int& meshCountRef() { return meshCount_; }

private:
    using GenResult = std::pair<std::unique_ptr<gldx::Chunk>, bool>;
    struct GenJob  { int idx; std::future<GenResult> fut; };
    struct MeshJob { int idx; std::future<gldx::VoxelChunkMesh> fut; };

    void submitMesh(int idx);
    void submitGen(int idx, glm::ivec3 g);

    World&            world;
    gldx::ThreadPool  pool{4};
    std::deque<GenJob>   genPending;
    std::deque<MeshJob>  meshPending;
    std::vector<std::pair<int, glm::ivec3>> candidates;   // scratch, reused per frame
    int genCount_ = 0, meshCount_ = 0;
    // Chunk-grid side offsets, index = Chunk::neighborDirty() side.
    const glm::ivec3 kSide[6] = { {-1,0,0}, {1,0,0}, {0,-1,0},
                                  {0,1,0}, {0,0,-1}, {0,0,1} };
};

} // namespace voxel_terrain

#endif // VOXEL_TERRAIN_STREAMING_H
