#ifndef GFX_ASSETS_ASSETMANAGER_H
#define GFX_ASSETS_ASSETMANAGER_H

/**
 * @file AssetManager.h
 * @brief Thread-safe asset pipeline: CPU staging on worker threads, GPU upload
 *        on the render thread (plan sections 1.2 and "assets").
 *
 * Request* calls are made from the render thread and enqueue Stage-A work on
 * the ThreadPool (file IO + decode/parse, no GL). ProcessUploads() must be
 * called once per frame on the render thread: it drains completed CPU results
 * and performs the GL uploads, then makes assets available through Get*.
 *
 * Ownership: all GPU resources are created (and destroyed, in ~AssetManager)
 * on the render thread, satisfying GL context affinity.
 */

#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gfx/assets/ModelLoader.h"
#include "gfx/assets/ThreadPool.h"
#include "gfx/geometry/Model.h"
#include "gfx/texture/Texture2D.h"

namespace gfx {

class AssetManager {
public:
    explicit AssetManager(std::size_t workerThreads = 2);
    ~AssetManager();

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Request async loads (render thread). Duplicate keys are ignored.
    void RequestTexture(const std::string& key, const std::string& filePath, bool srgb = true);
    void RequestModel(const std::string& key, const std::string& filePath);

    // Drain finished CPU work and upload to GPU. Render thread, once per frame.
    void ProcessUploads();

    // Lookups (render thread). Return null until the asset is ready.
    std::shared_ptr<Texture2D> GetTexture(const std::string& key);
    std::shared_ptr<Model>     GetModel(const std::string& key);

    [[nodiscard]] std::size_t PendingCount() const;

private:
    struct PendingTexture {
        std::string key;
        std::future<std::expected<Texture2DDesc, std::string>> fut;
    };

    enum class ModelStage { AwaitingModel, AwaitingTextures };

    struct PendingModel {
        std::string key;
        ModelStage  stage = ModelStage::AwaitingModel;
        std::future<std::expected<LoadedModelData, std::string>> modelFut;
        LoadedModelData staged;                                     // after modelFut ready
        std::vector<std::future<std::expected<Texture2DDesc, std::string>>> textureFuts;
    };

    ThreadPool pool_;

    mutable std::mutex queueMutex_;                       // guards the vectors below
    std::vector<PendingTexture> pendingTextures_;
    std::vector<PendingModel>   pendingModels_;
    std::unordered_set<std::string> requestedKeys_;

    mutable std::shared_mutex storeMutex_;                // guards the asset maps
    std::unordered_map<std::string, std::shared_ptr<Texture2D>> textures_;
    std::unordered_map<std::string, std::shared_ptr<Model>>     models_;
};

} // namespace gfx

#endif // GFX_ASSETS_ASSETMANAGER_H
