#include "gfx/assets/AssetManager.h"

#include <chrono>
#include <cstdio>
#include <utility>

#include "gfx/assets/ImageLoader.h"
#include "gfx/core/RenderContext.h"
#include "gfx/geometry/Mesh.h"

namespace gfx {

AssetManager::AssetManager(std::size_t workerThreads) : pool_(workerThreads) {}

AssetManager::~AssetManager() {
    pool_.shutdown();
    // GPU resources are released here. By contract the manager is destroyed
    // while the GL context is current on the render thread, so ~Texture2D and
    // ~Mesh (which assert render-thread affinity) are safe.
    std::unique_lock<std::shared_mutex> lk(storeMutex_);
    textures_.clear();
    models_.clear();
}

void AssetManager::RequestTexture(const std::string& key, const std::string& filePath, bool srgb) {
    std::lock_guard<std::mutex> lk(queueMutex_);
    if (requestedKeys_.count(key)) return;
    requestedKeys_.insert(key);
    PendingTexture pt;
    pt.key = key;
    pt.fut = pool_.enqueue([filePath, srgb]() {
        auto d = LoadImageToDesc(filePath);
        if (d) d->srgb = srgb;
        return d;
    });
    pendingTextures_.push_back(std::move(pt));
}

void AssetManager::RequestModel(const std::string& key, const std::string& filePath) {
    std::lock_guard<std::mutex> lk(queueMutex_);
    if (requestedKeys_.count(key)) return;
    requestedKeys_.insert(key);
    PendingModel pm;
    pm.key = key;
    pm.stage = ModelStage::AwaitingModel;
    pm.modelFut = pool_.enqueue([filePath]() { return ModelLoader::Load(filePath); });
    pendingModels_.push_back(std::move(pm));
}

void AssetManager::ProcessUploads() {
    RenderContext::AssertRenderThread("AssetManager::ProcessUploads");

    // ---- textures ----
    std::vector<PendingTexture> readyTex;
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        for (auto it = pendingTextures_.begin(); it != pendingTextures_.end();) {
            if (it->fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                readyTex.push_back(std::move(*it));
                it = pendingTextures_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& pt : readyTex) {
        auto res = pt.fut.get();
        if (res) {
            auto tex = std::make_shared<Texture2D>();
            tex->Upload(res.value());
            std::unique_lock<std::shared_mutex> slk(storeMutex_);
            textures_[pt.key] = std::move(tex);
        } else {
            std::fprintf(stderr, "[AssetManager] texture '%s' failed: %s\n",
                         pt.key.c_str(), res.error().c_str());
            std::lock_guard<std::mutex> qlk(queueMutex_);
            requestedKeys_.erase(pt.key);
        }
    }

    // ---- models: advance the state machine, gather ones ready to finalize ----
    std::vector<PendingModel> readyModel;
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        for (auto it = pendingModels_.begin(); it != pendingModels_.end();) {
            PendingModel& pm = *it;
            if (pm.stage == ModelStage::AwaitingModel) {
                if (pm.modelFut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    ++it; continue;
                }
                auto res = pm.modelFut.get();
                if (!res) {
                    std::fprintf(stderr, "[AssetManager] model '%s' failed: %s\n",
                                 pm.key.c_str(), res.error().c_str());
                    requestedKeys_.erase(pm.key);
                    it = pendingModels_.erase(it);
                    continue;
                }
                pm.staged = std::move(res.value());
                for (const auto& texPath : pm.staged.texturePaths) {
                    pm.textureFuts.push_back(
                        pool_.enqueue([texPath]() { return LoadImageToDesc(texPath); }));
                }
                pm.stage = ModelStage::AwaitingTextures;
            }
            // AwaitingTextures: are all dependent decodes done?
            bool allReady = true;
            for (auto& f : pm.textureFuts) {
                if (f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    allReady = false; break;
                }
            }
            if (allReady) {
                readyModel.push_back(std::move(pm));
                it = pendingModels_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ---- models: upload GPU resources (render thread) ----
    for (auto& pm : readyModel) {
        auto model = std::make_shared<Model>();
        model->name = pm.key;
        model->scale = pm.staged.scale;

        model->textures.reserve(pm.textureFuts.size());
        for (auto& f : pm.textureFuts) {
            auto res = f.get();
            if (res) {
                auto tex = std::make_shared<Texture2D>();
                tex->Upload(res.value());
                model->textures.push_back(std::move(tex));
            } else {
                model->textures.push_back(nullptr);  // keep index alignment
                std::fprintf(stderr, "[AssetManager] model texture failed: %s\n",
                             res.error().c_str());
            }
        }

        model->meshes.reserve(pm.staged.meshes.size());
        for (auto& meshData : pm.staged.meshes) {
            auto mesh = std::make_shared<Mesh>();
            mesh->Upload(std::move(meshData));
            model->meshes.push_back(std::move(mesh));
        }

        std::unique_lock<std::shared_mutex> slk(storeMutex_);
        models_[pm.key] = std::move(model);
    }
}

std::shared_ptr<Texture2D> AssetManager::GetTexture(const std::string& key) {
    std::shared_lock<std::shared_mutex> lk(storeMutex_);
    auto it = textures_.find(key);
    return it == textures_.end() ? nullptr : it->second;
}

std::shared_ptr<Model> AssetManager::GetModel(const std::string& key) {
    std::shared_lock<std::shared_mutex> lk(storeMutex_);
    auto it = models_.find(key);
    return it == models_.end() ? nullptr : it->second;
}

std::size_t AssetManager::PendingCount() const {
    std::lock_guard<std::mutex> lk(queueMutex_);
    return pendingTextures_.size() + pendingModels_.size();
}

} // namespace gfx
