module;

#include "gldx/gmf.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

module gldx;

namespace gldx {

namespace {

// Bump when the layout below changes. Everything (including the string fields) is
// written in order, so any structural change invalidates old files wholesale - which
// is the point: a cache that can be *half* valid is worse than no cache.
constexpr uint32_t kFormatVersion = 1;

constexpr char kMagic[8] = {'G', 'L', 'D', 'X', 'M', 'D', 'L', '1'};

template <typename T>
void WritePod(std::ofstream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool ReadPod(std::ifstream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}

void WriteString(std::ofstream& out, const std::string& s) {
    WritePod(out, static_cast<uint32_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool ReadString(std::ifstream& in, std::string& s) {
    uint32_t n = 0;
    if (!ReadPod(in, n)) return false;
    // A corrupt length would try to allocate gigabytes; 64 KiB is far more than any
    // bone/material name needs and caps the damage at "this file is rejected".
    if (n > 65536u) return false;
    s.resize(n);
    if (n) in.read(s.data(), static_cast<std::streamsize>(n));
    return static_cast<bool>(in);
}

template <typename T>
void WriteVec(std::ofstream& out, const std::vector<T>& v) {
    WritePod(out, static_cast<uint32_t>(v.size()));
    if (!v.empty()) out.write(reinterpret_cast<const char*>(v.data()),
                              static_cast<std::streamsize>(v.size() * sizeof(T)));
}

template <typename T>
bool ReadVec(std::ifstream& in, std::vector<T>& v) {
    uint32_t n = 0;
    if (!ReadPod(in, n)) return false;
    if (n > (1u << 26)) return false;   // 64M entries: refuse, do not OOM on garbage
    v.resize(n);
    if (n) in.read(reinterpret_cast<char*>(v.data()),
                   static_cast<std::streamsize>(n * sizeof(T)));
    return static_cast<bool>(in);
}

std::string CacheDirFor(const ModelCacheOptions& options) {
    if (!options.directory.empty()) return options.directory;
    return (std::filesystem::path("output") / ".modelcache").string();
}

std::string CachePathFor(const std::string& sourcePath, const ModelCacheOptions& options) {
    // Key: the file's own identity plus the format version. A hash of the path keeps
    // the filename short and avoids characters the filesystem dislikes (the assets in
    // this repo have CJK names).
    std::error_code ec;
    const auto size = std::filesystem::file_size(sourcePath, ec);
    if (ec) return {};
    const auto mtime = std::filesystem::last_write_time(sourcePath, ec);
    if (ec) return {};

    const auto stamp = static_cast<long long>(mtime.time_since_epoch().count());
    const size_t h = std::hash<std::string>{}(sourcePath);
    char name[96];
    std::snprintf(name, sizeof(name), "%016zx_v%u_%lld_%lld.gmdc", h,
                  static_cast<unsigned>(kFormatVersion),
                  static_cast<long long>(size), stamp);
    return (std::filesystem::path(CacheDirFor(options)) / name).string();
}

}  // namespace

std::optional<LoadedSkinnedModelData> ReadSkinnedFromCache(const std::string& sourcePath,
                                                           const ModelCacheOptions& options) {
    const std::string path = CachePathFor(sourcePath, options);
    if (path.empty()) return std::nullopt;

    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;

    char magic[8] = {};
    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, kMagic, sizeof(magic)) != 0) return std::nullopt;

    uint32_t version = 0;
    if (!ReadPod(in, version) || version != kFormatVersion) return std::nullopt;

    LoadedSkinnedModelData out;

    // Skeleton: bones in order (parents-first is a property of the vector, so writing
    // it in order preserves it), then the name index is rebuilt by Skeleton itself.
    uint32_t boneCount = 0;
    if (!ReadPod(in, boneCount) || boneCount > 4096u) return std::nullopt;
    for (uint32_t i = 0; i < boneCount; ++i) {
        Bone b;
        if (!ReadString(in, b.name)) return std::nullopt;
        if (!ReadPod(in, b.parent)) return std::nullopt;
        if (!ReadPod(in, b.inverseBind)) return std::nullopt;
        if (!ReadPod(in, b.restLocal)) return std::nullopt;
        out.skeleton.Add(std::move(b));
    }

    uint32_t meshCount = 0;
    if (!ReadPod(in, meshCount) || meshCount > 65536u) return std::nullopt;
    out.meshes.resize(meshCount);
    for (auto& m : out.meshes) {
        if (!ReadVec(in, m.vertices)) return std::nullopt;
        if (!ReadVec(in, m.indices)) return std::nullopt;
    }

    if (!ReadVec(in, out.materials)) return std::nullopt;
    if (!ReadVec(in, out.meshMaterial)) return std::nullopt;

    uint32_t texCount = 0;
    if (!ReadPod(in, texCount) || texCount > 65536u) return std::nullopt;
    out.textures.resize(texCount);
    for (auto& t : out.textures) {
        if (!ReadPod(in, t.desc.width)) return std::nullopt;
        if (!ReadPod(in, t.desc.height)) return std::nullopt;
        if (!ReadPod(in, t.desc.channels)) return std::nullopt;
        if (!ReadPod(in, t.desc.srgb)) return std::nullopt;
        if (!ReadVec(in, t.desc.pixels)) return std::nullopt;
        int roleInt = 0;
        if (!ReadPod(in, roleInt)) return std::nullopt;
        t.role = static_cast<SkinnedTexture::Role>(roleInt);
        if (!ReadString(in, t.name)) return std::nullopt;
    }

    uint32_t missing = 0;
    if (!ReadPod(in, missing) || missing > 65536u) return std::nullopt;
    out.missingTexturePaths.resize(missing);
    for (auto& s : out.missingTexturePaths) {
        if (!ReadString(in, s)) return std::nullopt;
    }

    if (!ReadString(in, out.sourcePath)) return std::nullopt;
    return out;
}

void WriteSkinnedToCache(const std::string& sourcePath, const LoadedSkinnedModelData& data,
                         const ModelCacheOptions& options) {
    const std::string path = CachePathFor(sourcePath, options);
    if (path.empty()) return;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) return;

    // Write to a temporary and rename: a process killed mid-write must not leave a
    // truncated file that later parses as "corrupt cache" forever. Rename is atomic
    // within one filesystem, which a temp in the same directory guarantees.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(kMagic, sizeof(kMagic));
        WritePod(out, kFormatVersion);

        WritePod(out, static_cast<uint32_t>(data.skeleton.size()));
        for (std::size_t i = 0; i < data.skeleton.size(); ++i) {
            const Bone& b = *data.skeleton.bone(static_cast<int>(i));
            WriteString(out, b.name);
            WritePod(out, b.parent);
            WritePod(out, b.inverseBind);
            WritePod(out, b.restLocal);
        }

        WritePod(out, static_cast<uint32_t>(data.meshes.size()));
        for (const auto& m : data.meshes) {
            WriteVec(out, m.vertices);
            WriteVec(out, m.indices);
        }
        WriteVec(out, data.materials);
        WriteVec(out, data.meshMaterial);

        WritePod(out, static_cast<uint32_t>(data.textures.size()));
        for (const auto& t : data.textures) {
            WritePod(out, t.desc.width);
            WritePod(out, t.desc.height);
            WritePod(out, t.desc.channels);
            WritePod(out, t.desc.srgb);
            WriteVec(out, t.desc.pixels);
            WritePod(out, static_cast<int>(t.role));
            WriteString(out, t.name);
        }

        WritePod(out, static_cast<uint32_t>(data.missingTexturePaths.size()));
        for (const auto& s : data.missingTexturePaths) WriteString(out, s);
        WriteString(out, data.sourcePath);
    }
    std::filesystem::rename(tmp, path, ec);
}

}  // namespace gldx
