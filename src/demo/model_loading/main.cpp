/**
 * @file main.cpp
 * @brief model_loading - the async asset pipeline, degrading gracefully.
 *
 * AssetManager is the two-stage loader: RequestModel(key, path) enqueues the
 * CPU-side Assimp parse (ModelLoader::Load -> MeshData + texture paths, no GL) on
 * a worker thread, and ProcessUploads(), called once per frame on the render
 * thread, drains finished work and performs the GL uploads, after which GetModel
 * hands back a shared_ptr<Model> of already-uploaded gldx::Mesh values. That split
 * is the whole point: file IO and parsing never block the render thread; only
 * Stage B touches GL.
 *
 * Model import is a single optional dependency (assimp) gated by the CMake
 * option GLDX_ENABLE_ASSIMP. When the engine is built with it OFF, ModelLoader::Load
 * returns std::unexpected("gldx built without Assimp: model import unavailable");
 * the request/lookup signatures are unchanged, the model simply never arrives, and
 * this demo surfaces that on its HUD instead of crashing. The same code runs under
 * both builds - only the reported status differs - which is exactly what the
 * ON/OFF verification matrix checks.
 *
 * A one-shot synchronous Load on the render thread is used only to obtain a
 * human-readable status string (the async API reports no error text); the actual
 * load still goes through AssetManager. Pass a model with --model PATH.
 *
 * Controls: Esc quits, drag orbits, --model PATH, --quit-after SECONDS headless.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

struct View {
    float yaw = 0.7f, pitch = 0.3f, radius = 4.0f;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
};
void OnMouse(GLFWwindow* w, double x, double y) {
    auto* v = static_cast<View*>(glfwGetWindowUserPointer(w));
    if (!v || !v->dragging) return;
    v->yaw   -= static_cast<float>(x - v->lastX) * 0.006f;
    v->pitch  = std::min(std::max(v->pitch + static_cast<float>(y - v->lastY) * 0.006f, -1.45f), 1.45f);
    v->lastX = x; v->lastY = y;
}
void OnButton(GLFWwindow* w, int b, int a, int) {
    if (b != GLFW_MOUSE_BUTTON_LEFT) return;
    if (auto* v = static_cast<View*>(glfwGetWindowUserPointer(w))) {
        v->dragging = (a == GLFW_PRESS); glfwGetCursorPos(w, &v->lastX, &v->lastY);
    }
}

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Helvetica.ttc",
    "C:/Windows/Fonts/consola.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
};
gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1; d.channels = 4; d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

// Reports the async pipeline's state on a strip; the app callback pushes a fresh
// status string every frame (it knows pending vs loaded vs failed).
class StatusHudPass : public gldx::RenderPass {
public:
    StatusHudPass() : RenderPass("StatusHud") {
        if (!sprite_.Init()) std::fprintf(stderr, "SpriteBatch init failed\n");
        for (const char* c : kFontCandidates) { if (font_.LoadFromFile(c, 48.0f)) break; }
        white_.Upload(MakeSolidDesc());
    }
    void Execute(gldx::RenderFrame& f) override {
        if (!font_.loaded() || !white_.valid()) return;
        sprite_.Begin(white_, f.fbWidth, f.fbHeight);
        const float w = gldx::TextRenderer::Measure(font_, status_, 20.0f) + 24.0f;
        sprite_.Draw(white_, 14.0f, 14.0f, w, 38.0f, 0, 0, 1, 1, glm::vec4(0, 0, 0, 0.4f));
        gldx::TextRenderer::Draw(sprite_, font_, status_, 26.0f, 22.0f, 20.0f,
                                glm::vec4(0.8f, 0.9f, 1.0f, 1.0f));
        sprite_.End();
    }
    void setStatus(std::string s) { status_ = std::move(s); }
private:
    gldx::SpriteBatch sprite_;
    gldx::Font        font_;
    gldx::Texture2D   white_;
    std::string      status_ = "initialising...";
};

// Owns the GPU mesh + material for one scene object built from a loaded model.
struct Item { gldx::PbrMaterial material; };

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"yaw", "pitch", "radius", "model"}, "model_loading");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const std::string modelPath = flags.string("model");

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - model_loading (async AssetManager + graceful OFF)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    GLFWwindow* const native = window.Handle();
    gldx::Renderer renderer;
    renderer.Init();

    View view;
    view.yaw = flags.real("yaw", view.yaw);
    view.pitch = flags.real("pitch", view.pitch);
    view.radius = flags.real("radius", view.radius, 0.5f, 200.0f);
    glfwSetWindowUserPointer(native, &view);
    glfwSetCursorPosCallback(native, OnMouse);
    glfwSetMouseButtonCallback(native, OnButton);

    auto hud = std::make_unique<StatusHudPass>();
    StatusHudPass* hudRaw = hud.get();
    renderer.AddPass(std::make_unique<gldx::GeometryPass>());
    renderer.AddPass(std::move(hud));

    auto pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex, gldx::shaders::kPbrFragment);
    if (!pbr) { std::fprintf(stderr, "PBR shader: %s\n", pbr.error().c_str()); return 1; }
    pbr->Use();
    pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);

    gldx::LightBuffer lights; lights.Init();
    gldx::Camera camera; camera.SetPerspective(45.0f, 1.0f, 0.1f, 500.0f);

    std::vector<std::unique_ptr<Item>> mats;
    gldx::Scene scene;

    // Kick the async load (or decide the status up front) with a single CPU
    // probe for a readable reason; the real geometry still arrives via AssetManager.
    gldx::AssetManager assets(2);
    bool requested = false;
    if (modelPath.empty()) {
        hudRaw->setStatus("no model: pass --model PATH (e.g. a .obj/.gltf)");
    } else if (auto probe = gldx::ModelLoader::Load(modelPath); !probe) {
        hudRaw->setStatus("model unavailable: " + probe.error());
    } else {
        assets.RequestModel("m", modelPath);
        requested = true;
        hudRaw->setStatus("async loading: " + modelPath);
    }

    std::shared_ptr<gldx::Model> heldModel;   // keep meshes alive once uploaded

    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        if (requested && !heldModel) {
            assets.ProcessUploads();
            if (auto m = assets.GetModel("m")) {
                heldModel = m;
                for (const auto& mesh : m->meshes) {
                    if (!mesh || !mesh->valid()) continue;
                    auto it = std::make_unique<Item>();
                    it->material.baseColor = glm::vec4(0.85f, 0.82f, 0.78f, 1.0f);
                    it->material.roughness = 0.55f;
                    it->material.metallic = 0.0f;
                    gldx::SceneNode& n = scene.CreateRoot();
                    n.SetRenderable(mesh.get(), &it->material);
                    mats.push_back(std::move(it));
                }
                hudRaw->setStatus("loaded: " + std::to_string(m->meshes.size()) + " mesh(es), drag to orbit");
            } else if (assets.PendingCount() == 0) {
                hudRaw->setStatus("load finished with no model (see stderr)");
            }
        }

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
        setup.sun.intensity = 3.2f;
        setup.ambient = glm::vec3(0.12f);
        lights.Update(setup, camera.Position());
        scene.Update();

        f.camera = &camera; f.scene = &scene; f.lights = &lights; f.pbr = &*pbr;
        f.viewProj = camera.ViewProjection(); f.lightSetup = setup;

        renderer.Render(f);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
