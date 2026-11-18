#ifndef GLDX_ASSETS_MODELCACHE_H
#define GLDX_ASSETS_MODELCACHE_H

/**
 * @file ModelCache.h
 * @brief 把**已经解析好的**蒙皮模型存到磁盘，避免二次启动重新跑 assimp。
 *
 * ## 为什么需要
 *
 * 一个 12 MB 的二进制 FBX（800k 顶点 / 60 网格 / 内嵌贴图）在 assimp 里要
 * **55 秒**——实测 `importer.ReadFile` 一项就占满了整个加载时间，而我们自己的
 * 顶点处理只占 0.37 s。这不是"数据大所以慢"（每顶点 68 µs，正常应 <1 µs），
 * 是 assimp 的 FBX 解析器在这个规模下的固有代价。每次启动都付一遍 55 秒，
 * 开发循环无法接受。
 *
 * 缓存的是**我们自己的结构**（`LoadedSkinnedModelData`），不是 assimp 的场景：
 * 这样版本一改就整份失配、自动重解析，不会出现"缓存里的数据和当前代码对不上"
 * 这种最难查的问题。
 *
 * ## 失效条件
 *
 * 缓存键 = 源文件大小 + 修改时间 + 一个随结构定义变化的**格式版本号**。
 * 三者任一不同就重解析。时间戳精度只到秒是够的：改一个模型不会在 1 秒内
 * 完成保存并重新运行。
 *
 * ## 权限
 *
 * 纯 CPU、无 GL、线程安全（只读写自己算出来的文件），可以在工作线程调用。
 */

#include <string>

#include "gldx/assets/ModelLoader.h"

namespace gldx {

/**
 * 缓存文件默认落在 `output/.modelcache/`。
 *
 * 用输出目录而不是源目录：那里已经被 gitignore，而且构建目录可以整体删掉。
 */
struct ModelCacheOptions {
    std::string directory;   // 空 = 用默认
    bool        enabled = true;
};

/**
 * 尝试从缓存读取。命中返回数据；未命中/版本不符/文件损坏一律返回 `nullopt`
 * ——**缓存永远不应该让加载失败**，它只是快路径。
 */
[[nodiscard]] std::optional<LoadedSkinnedModelData>
ReadSkinnedFromCache(const std::string& sourcePath, const ModelCacheOptions& options);

/** 写入缓存。失败静默忽略（磁盘满、权限不足都不该阻止加载）。 */
void WriteSkinnedToCache(const std::string& sourcePath, const LoadedSkinnedModelData& data,
                         const ModelCacheOptions& options);

/**
 * 包一层：命中就返回缓存，否则调 `loader()` 解析并写回。
 *
 * `loader` 是一个可调用对象而不是路径，这样缓存层不需要知道 assimp 的存在，
 * 也就能在 `GLDX_NO_ASSIMP` 构建里单独测。
 */
template <typename Loader>
[[nodiscard]] std::expected<LoadedSkinnedModelData, std::string>
LoadSkinnedThroughCache(const std::string& sourcePath, const ModelCacheOptions& options,
                        Loader&& loader) {
    if (options.enabled) {
        if (auto hit = ReadSkinnedFromCache(sourcePath, options)) return std::move(*hit);
    }
    auto parsed = loader();
    if (parsed && options.enabled) WriteSkinnedToCache(sourcePath, *parsed, options);
    return parsed;
}

}  // namespace gldx

#endif  // GLDX_ASSETS_MODELCACHE_H
