#include "gldx/core/Platform.h"

import gldx;

#include "Scene.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

namespace pbr_showcase {

gldx::Texture2DDesc MakeCheckerDesc(int size, int cells) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 3;
    d.srgb = true;
    d.pixels.resize(static_cast<std::size_t>(size) * size * 3);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool on = ((x / cell) + (y / cell)) % 2 == 0;
            const glm::vec3 c = on ? glm::vec3(0.82f, 0.80f, 0.76f) : glm::vec3(0.16f, 0.18f, 0.22f);
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            d.pixels[i + 0] = static_cast<std::uint8_t>(c.r * 255.0f);
            d.pixels[i + 1] = static_cast<std::uint8_t>(c.g * 255.0f);
            d.pixels[i + 2] = static_cast<std::uint8_t>(c.b * 255.0f);
        }
    }
    return d;
}

gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1;
    d.channels = 4;
    d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

gldx::SceneNode& ShowcaseScene::AddObject(gldx::MeshData data, const gldx::PbrMaterial& matCfg,
                                          const gldx::Transform& xf, gldx::SceneNode* parent) {
    auto o = std::make_unique<Owned>();
    o->material = matCfg;
    if (!o->material.placeholder) o->material.placeholder = &checker_;
    glm::vec3 c;
    float r;
    gldx::SceneNode::BoundsFromMeshData(data, c, r);
    o->mesh.Upload(std::move(data));
    Owned* raw = o.get();
    owned_.push_back(std::move(o));
    gldx::SceneNode& node = parent ? parent->AddChild(xf) : scene_.CreateRoot(xf);
    node.SetRenderable(&raw->mesh, &raw->material);
    node.SetLocalBounds(c, r);
    node.id = nextId_++;
    return node;
}

void ShowcaseScene::Build() {
    checker_.Upload(MakeCheckerDesc());

    // Ground plane (large scale, never casts).
    {
        gldx::Transform t;
        t.scale = glm::vec3(24.0f, 1.0f, 24.0f);
        gldx::PbrMaterial m;
        m.baseColor = glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
        m.roughness = 0.85f;
        m.albedo = &checker_;
        AddObject(gldx::GeometryFactory::Plane(1.0f), m, t).castsShadow = false;
    }

    // PBR test grid (metallic x roughness).
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            gldx::Transform t;
            t.translation = glm::vec3(-3.0f + i * 1.5f, 0.5f, -3.0f + j * 1.5f);
            gldx::PbrMaterial m;
            m.metallic  = static_cast<float>(i) / 4.0f;
            m.roughness = 0.05f + 0.9f * static_cast<float>(j) / 4.0f;
            m.baseColor = glm::vec4(0.9f, 0.5f, 0.25f, 1.0f);
            AddObject(gldx::GeometryFactory::Sphere(0.5f, 48, 32), m, t);
        }
    }

    // Textured cubes (checker albedo).
    for (int k = 0; k < 3; ++k) {
        gldx::Transform t;
        t.translation = glm::vec3(-1.6f + k * 1.6f, 0.5f, 3.6f);
        t.SetAxisAngle(glm::vec3(0, 1, 0), 0.5f * k);
        gldx::PbrMaterial m;
        m.baseColor = glm::vec4(1.0f);
        m.roughness = 0.45f;
        m.albedo = &checker_;
        AddObject(gldx::GeometryFactory::Cube(1.0f), m, t);
    }

    // Hierarchy demo: an empty pivot carrying orbiting children. The pivot is
    // spun every frame; the children ride along via world-matrix propagation.
    carousel_ = &scene_.CreateRoot();
    for (int c = 0; c < 3; ++c) {
        const float a = c * 2.0f * 3.14159265f / 3.0f;
        gldx::Transform t;
        t.translation = glm::vec3(std::cos(a) * 1.2f, 1.2f, std::sin(a) * 1.2f);
        gldx::PbrMaterial m;
        m.metallic = 0.9f;
        m.roughness = 0.2f;
        m.baseColor = glm::vec4(0.2f + 0.4f * c, 0.6f, 0.9f - 0.3f * c, 1.0f);
        AddObject(gldx::GeometryFactory::Cube(0.5f), m, t, carousel_);
    }
}

void BuildInstancedField(gldx::InstancedMesh& instField) {
    gldx::MeshData geo = gldx::GeometryFactory::Cube(1.0f);
    std::vector<gldx::Instance> insts;
    constexpr int n = 8;
    for (int ix = 0; ix < n; ++ix) {
        for (int iz = 0; iz < n; ++iz) {
            const float x = 7.5f + ix * 1.4f;
            const float z = -4.9f + iz * 1.4f;
            const float h = 0.5f + 0.5f * static_cast<float>((ix * 3 + iz * 5) % 6);
            const glm::mat4 m =
                glm::translate(glm::mat4(1.0f), glm::vec3(x, h * 0.5f, z)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, h, 0.4f));
            gldx::Instance in;
            in.model = m;
            const float t = static_cast<float>((ix + iz) % 5) / 4.0f;
            in.color = glm::vec4(0.3f + 0.6f * t, 0.4f, 0.85f - 0.5f * t, 1.0f);
            insts.push_back(in);
        }
    }
    instField.Create(std::move(geo), std::move(insts));
}

} // namespace pbr_showcase
