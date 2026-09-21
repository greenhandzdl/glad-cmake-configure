/**
 * @file main.cpp
 * @brief camera_picking - frustum culling and mouse picking, with a live HUD.
 *
 * Two independent CPU features that both hang off the camera's matrices:
 *
 *   Frustum::Extract(viewProj) lifts the six inward planes from the matrix
 *   (Gribb-Hartmann) and SphereVisible rejects nodes whose world bounding sphere
 *   lies wholly beyond one plane. GeometryPass consults `f.frustum` per node, so
 *   wiring it in is enough to cull - and it reports the outcome back through
 *   `f.visibleCount` / `f.totalNodes`, which the HUD echoes.
 *
 *   Picking is pure math in Picking.h: PickRay unprojects a framebuffer pixel at
 *   the near and far planes through camera.InverseViewProjection() to build a
 *   world ray (projection-agnostic, so it is correct in ortho too), and
 *   PickNearest front-to-back-tests the ray against the scene's world bounding
 *   spheres (scene.PickTargets()). The chosen node goes into `f.selected`, which
 *   GeometryPass draws with a gold override.
 *
 * The pixel must be in *framebuffer* space, not GLFW's window coordinates: on a
 * Retina display they differ by the content scale, so the cursor position is
 * normalised to [0,1] at click time and multiplied by the framebuffer size when
 * the ray is built - the same convention pbr_showcase uses.
 *
 * A ring of spheres, half behind the camera at rest, makes the culling count move
 * as you orbit. Controls: right-click picks, Esc quits, drag orbits,
 * --quit-after SECONDS for headless.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

struct View {
    float yaw = 0.7f, pitch = 0.32f, radius = 11.0f;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    bool pickPending = false;
    float pickX = 0.0f, pickY = 0.0f;   // window-normalised [0..1]
};

void OnMouse(GLFWwindow* w, double x, double y) {
    auto* v = static_cast<View*>(glfwGetWindowUserPointer(w));
    if (!v || !v->dragging) return;
    v->yaw   -= static_cast<float>(x - v->lastX) * 0.006f;
    v->pitch  = std::min(std::max(v->pitch + static_cast<float>(y - v->lastY) * 0.006f, -1.45f), 1.45f);
    v->lastX = x; v->lastY = y;
}
void OnButton(GLFWwindow* w, int b, int a, int) {
    auto* v = static_cast<View*>(glfwGetWindowUserPointer(w));
    if (!v) return;
    double cx = 0, cy = 0;
    glfwGetCursorPos(w, &cx, &cy);
    if (b == GLFW_MOUSE_BUTTON_RIGHT) {
        int ww = 0, wh = 0;
        glfwGetWindowSize(w, &ww, &wh);
        if (a == GLFW_PRESS && ww > 0 && wh > 0) {
            v->pickPending = true;
            v->pickX = static_cast<float>(cx / ww);
            v->pickY = static_cast<float>(cy / wh);
        }
        return;
    }
    if (b != GLFW_MOUSE_BUTTON_LEFT) return;
    v->dragging = (a == GLFW_PRESS);
    v->lastX = cx; v->lastY = cy;
}

struct Item { gldx::Mesh mesh; gldx::PbrMaterial material; };

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};
gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1; d.channels = 4; d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

// HUD strip that reads the culling + selection results GeometryPass wrote into the
// shared frame this same pass order (added after GeometryPass, so it sees them).
class PickHudPass : public gldx::RenderPass {
public:
    PickHudPass() : RenderPass("PickHud") {
        if (!sprite_.Init()) std::fprintf(stderr, "SpriteBatch init failed\n");
        for (const char* c : kFontCandidates) { if (font_.LoadFromFile(c, 48.0f)) break; }
        white_.Upload(MakeSolidDesc());
    }
    void Execute(gldx::RenderFrame& f) override {
        if (!font_.loaded() || !white_.valid()) return;
        char line[128];
        std::snprintf(line, sizeof(line),
                      "right-click = pick   visible %d/%d   selected #%d",
                      f.visibleCount, f.totalNodes, f.selected ? f.selected->id : -1);
        const float w = gldx::TextRenderer::Measure(font_, line, 20.0f) + 24.0f;
        sprite_.Begin(white_, f.fbWidth, f.fbHeight);
        sprite_.Draw(white_, 14.0f, 14.0f, w, 38.0f, 0, 0, 1, 1, glm::vec4(0, 0, 0, 0.4f));
        gldx::TextRenderer::Draw(sprite_, font_, line, 26.0f, 22.0f, 20.0f,
                                glm::vec4(0.8f, 0.9f, 1.0f, 1.0f));
        sprite_.End();
    }
private:
    gldx::SpriteBatch sprite_;
    gldx::Font        font_;
    gldx::Texture2D   white_;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"yaw", "pitch", "radius"}, "camera_picking");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - camera_picking (frustum cull + mouse pick)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    GLFWwindow* const native = window.Handle();
    gldx::Renderer renderer;
    renderer.Init();

    View view;
    view.yaw = flags.real("yaw", view.yaw);
    view.pitch = flags.real("pitch", view.pitch);
    view.radius = flags.real("radius", view.radius, 2.0f, 40.0f);
    glfwSetWindowUserPointer(native, &view);
    glfwSetCursorPosCallback(native, OnMouse);
    glfwSetMouseButtonCallback(native, OnButton);

    renderer.AddPass(std::make_unique<gldx::GeometryPass>());
    renderer.AddPass(std::make_unique<PickHudPass>());

    auto pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex, gldx::shaders::kPbrFragment);
    if (!pbr) { std::fprintf(stderr, "PBR shader: %s\n", pbr.error().c_str()); return 1; }
    pbr->Use();
    pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);

    gldx::LightBuffer lights; lights.Init();
    gldx::Camera camera; camera.SetPerspective(55.0f, 1.0f, 0.1f, 100.0f);

    std::vector<std::unique_ptr<Item>> items;
    gldx::Scene scene;
    int nextId = 0;
    auto add = [&](gldx::MeshData data, const gldx::PbrMaterial& m, const gldx::Transform& t) {
        auto it = std::make_unique<Item>(); it->material = m;
        glm::vec3 c; float r; gldx::SceneNode::BoundsFromMeshData(data, c, r);
        it->mesh.Upload(std::move(data));
        gldx::SceneNode& n = scene.CreateRoot(t);
        n.SetRenderable(&it->mesh, &it->material); n.SetLocalBounds(c, r);
        n.id = nextId++;
        items.push_back(std::move(it));
    };
    { gldx::Transform t; t.scale = glm::vec3(20.0f, 1.0f, 20.0f);
      gldx::PbrMaterial m; m.baseColor = glm::vec4(0.4f, 0.42f, 0.46f, 1.0f); m.roughness = 0.95f;
      add(gldx::GeometryFactory::Plane(1.0f), m, t); }
    for (int i = 0; i < 16; ++i) {   // a full ring: half is behind the eye at rest
        const float a = static_cast<float>(i) / 16.0f * 2.0f * 3.14159265f;
        gldx::Transform t; t.translation = glm::vec3(std::cos(a) * 6.0f, 0.55f, std::sin(a) * 6.0f);
        gldx::PbrMaterial m; m.metallic = 0.3f; m.roughness = 0.5f;
        m.baseColor = glm::vec4(0.3f + 0.5f * std::cos(a) * std::cos(a),
                                0.5f, 0.4f + 0.5f * std::sin(a) * std::sin(a), 1.0f);
        add(gldx::GeometryFactory::Sphere(0.55f, 32, 24), m, t);
    }

    gldx::SceneNode* selected = nullptr;
    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        const glm::vec3 target(0.0f, 0.5f, 0.0f);
        const float cp = std::cos(view.pitch);
        const glm::vec3 eye(target.x + view.radius * cp * std::sin(view.yaw),
                            target.y + view.radius * std::sin(view.pitch),
                            target.z + view.radius * cp * std::cos(view.yaw));
        camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
        camera.LookAt(eye, target, glm::vec3(0, 1, 0));

        gldx::LightSetup setup;
        setup.sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        setup.sun.color = glm::vec3(1.0f);
        setup.sun.intensity = 3.0f;
        setup.ambient = glm::vec3(0.08f);
        lights.Update(setup, camera.Position());
        scene.Update();

        const glm::mat4 viewProj = camera.ViewProjection();
        gldx::Frustum frustum;
        frustum.Extract(viewProj);

        if (view.pickPending) {
            view.pickPending = false;
            const auto& targets = scene.PickTargets();
            std::vector<std::pair<glm::vec3, float>> spheres;
            spheres.reserve(targets.size());
            for (const auto& t : targets) spheres.emplace_back(t.center, t.radius);
            const gldx::Ray ray = gldx::PickRay(
                view.pickX * static_cast<float>(info.fbWidth),
                view.pickY * static_cast<float>(info.fbHeight),
                info.fbWidth, info.fbHeight, camera.InverseViewProjection());
            const int idx = gldx::PickNearest(ray, spheres);
            selected = (idx >= 0) ? targets[idx].node : nullptr;
        }

        f.camera = &camera; f.frustum = &frustum; f.scene = &scene;
        f.lights = &lights; f.pbr = &*pbr;
        f.viewProj = viewProj; f.lightSetup = setup;
        f.selected = selected;

        renderer.Render(f);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
