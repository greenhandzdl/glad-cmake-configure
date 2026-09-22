// voxel_terrain demo — chunk streaming (see Streaming.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Streaming.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <shared_mutex>

#include <glm/gtc/matrix_transform.hpp>

namespace voxel_terrain {

ChunkStreamer::ChunkStreamer(World& world) : world(world) {}

void ChunkStreamer::submitMesh(int idx) {
    {
        // Clear the flag *before* the job runs. An edit that lands
        // while the worker is copying then re-dirties the chunk and
        // earns a second remesh, so no change can be swallowed by a
        // mesh that was built from an older snapshot.
        std::unique_lock<std::shared_mutex> lk(world.mx);
        world.cpu[idx]->ClearDirty();
    }
    world.state[idx].phase = kMeshing;
    try {
        auto fut = pool.enqueue([idx, this]() -> gldx::VoxelChunkMesh {
            std::shared_lock<std::shared_mutex> lk(world.mx);
            const gldx::Chunk& ch = *world.cpu[idx];
            // Private 8 KB snapshot of the cells, taken under the lock:
            // the mesher never sees a torn view of a concurrent edit.
            const std::array<std::uint16_t,
                             static_cast<std::size_t>(gldx::kChunkSize)
                                 * gldx::kChunkSize * gldx::kChunkSize> cells = ch.blocks();
            return world.mesher.Build(cells.data(), ch.origin(), world);
        });
        meshPending.push_back({idx, std::move(fut)});
    } catch (const std::future_error&) {
        world.state[idx].phase = kGenerated;   // pool gone; retry later
    }
}

void ChunkStreamer::submitGen(int idx, glm::ivec3 g) {
    world.state[idx].phase = kGenerating;
    try {
        auto fut = pool.enqueue([g, this]() -> GenResult {
            auto ch = std::make_unique<gldx::Chunk>(g * kChunk);
            const bool any = GenerateChunk(*ch, world);
            return {std::move(ch), any};
        });
        genPending.push_back({idx, std::move(fut)});
    } catch (const std::future_error&) {
        world.state[idx].phase = kEmpty;
    }
}

void ChunkStreamer::Stream(const glm::vec3& eye) {
    const int ccx = std::clamp(static_cast<int>(eye.x) / kChunk, 0, kGridX - 1);
    const int ccy = std::clamp(static_cast<int>(eye.y) / kChunk, 0, kGridY - 1);
    const int ccz = std::clamp(static_cast<int>(eye.z) / kChunk, 0, kGridZ - 1);
    // Publish the ring centre before anything asks who is ready.
    world.camChunk = {ccx, ccz};

    candidates.clear();
    for (int dz = -kRenderDistance; dz <= kRenderDistance; ++dz) {
        for (int dx = -kRenderDistance; dx <= kRenderDistance; ++dx) {
            const int rr = dx * dx + dz * dz;
            if (rr > kRenderDistance * kRenderDistance) continue;
            const int cz = ccz + dz, cx = ccx + dx;
            if (cx < 0 || cz < 0 || cx >= kGridX || cz >= kGridZ) continue;
            for (int cy = 0; cy < kGridY; ++cy) {
                const int idx = World::Index(cx, cy, cz);
                std::uint8_t& phase = world.state[idx].phase;
                // The height test costs a few noise lookups; running it
                // once and remembering the answer as kVoid keeps the
                // per-frame cost at a comparison.
                if (phase == kEmpty && !world.ColumnRelevant(cx, cy, cz))
                    phase = kVoid;
                if (phase == kVoid) continue;
                candidates.emplace_back(rr + std::abs(cy - ccy),
                                        glm::ivec3{cx, cy, cz});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Nearest-first: request generation, then mesh what just became
    // complete (its 3x3x3 neighbourhood must exist first), then remesh
    // whatever an edit dirtied.
    for (const auto& cand : candidates) {
        const glm::ivec3 g = cand.second;
        const int idx = World::Index(g.x, g.y, g.z);
        const std::uint8_t phase = world.state[idx].phase;
        if (phase == kEmpty) {
            if (genPending.size() < kMaxGenInFlight) submitGen(idx, g);
        } else if (phase == kGenerated) {
            if (meshPending.size() >= kMaxMeshInFlight) continue;
            std::shared_lock<std::shared_mutex> lk(world.mx);
            if (world.NeighborReady(g.x, g.y, g.z)) {
                lk.unlock();
                submitMesh(idx);
            }
        } else if (phase == kReady) {
            std::shared_lock<std::shared_mutex> lk(world.mx);
            const gldx::Chunk* ch = world.cpu[idx].get();
            const bool dirty = ch && ch->dirty();
            lk.unlock();
            if (dirty && meshPending.size() < kMaxMeshInFlight) submitMesh(idx);
        }
    }

    // Drain finished generation: install the chunk and wake up any
    // Ready neighbour whose border now disagrees with it.
    for (auto it = genPending.begin(); it != genPending.end();) {
        if (it->fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        const int idx = it->idx;
        GenResult result = it->fut.get();
        std::unique_ptr<gldx::Chunk> ch = std::move(result.first);
        const glm::ivec3 g = ch->origin() / kChunk;
        if (!result.second) {
            // Pure air after all: cache the verdict instead of the
            // data. Neighbours stop waiting on it (NeighborReady) and
            // the streaming loop stops proposing it.
            world.state[idx].phase = kVoid;
            it = genPending.erase(it);
            continue;
        }
        {
            std::unique_lock<std::shared_mutex> lk(world.mx);
            world.cpu[idx] = std::move(ch);
            for (const glm::ivec3& off : kSide) {
                const glm::ivec3 nb = g + off;
                if (!World::InRange(nb.x, nb.y, nb.z)) continue;
                const int nidx = World::Index(nb.x, nb.y, nb.z);
                if (world.state[nidx].phase == kReady && world.cpu[nidx])
                    world.cpu[nidx]->MarkDirty();
            }
        }
        world.state[idx].phase = kGenerated;
        ++genCount_;
        it = genPending.erase(it);
    }

    // Drain finished meshing and upload (render thread only, that is
    // why the queue is drained here rather than in the worker). Buffer
    // swaps are the expensive part, so a frame takes a fixed few.
    std::size_t uploads = 0;
    for (auto it = meshPending.begin(); it != meshPending.end();) {
        if (uploads >= kMaxUploadsPerFrame) break;
        if (it->fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        const int idx = it->idx;
        gldx::VoxelChunkMesh mesh = it->fut.get();
        ++uploads;
        gldx::Chunk& ch = *world.cpu[idx];
        gldx::VoxelChunkGpu& rec = world.gpu[idx];
        const glm::ivec3 g = ch.origin() / kChunk;

        // A live opaque VAO is also the record's "built" flag: the
        // first build is where the transform and bounds get filled.
        if (rec.opaque.valid()) {
            rec.opaque.Update(std::move(mesh.opaque));
        } else {
            rec.origin = g * kChunk;
            rec.model = glm::translate(glm::mat4(1.0f), glm::vec3(rec.origin));
            rec.center = glm::vec3(rec.origin) + glm::vec3(kChunk * 0.5f);
            rec.radius = gldx::Chunk::circumRadius();
            rec.opaque.Upload(std::move(mesh.opaque));
        }
        // Water is optional per chunk. An empty mesh leaves the buffer
        // alone instead of asking GL for a zero-size upload; the
        // indexCount() check below is what actually gates the pass.
        if (!mesh.transparent.indices.empty()) {
            if (rec.transparent.valid()) rec.transparent.Update(std::move(mesh.transparent));
            else rec.transparent.Upload(std::move(mesh.transparent));
        }
        rec.hasTransparent = rec.transparent.indexCount() > 0;

        {
            std::unique_lock<std::shared_mutex> lk(world.mx);
            for (int s = 0; s < 6; ++s) {
                if (!ch.neighborDirty(s)) continue;
                const glm::ivec3 nb = g + kSide[s];
                if (!World::InRange(nb.x, nb.y, nb.z)) continue;
                const int nidx = World::Index(nb.x, nb.y, nb.z);
                if (world.state[nidx].phase == kReady && world.cpu[nidx])
                    world.cpu[nidx]->MarkDirty();
            }
            ch.ClearNeighborDirty();
        }
        world.state[idx].phase = kReady;
        ++meshCount_;
        it = meshPending.erase(it);
    }
}

} // namespace voxel_terrain
