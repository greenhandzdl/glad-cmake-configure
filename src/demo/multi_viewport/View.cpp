// multi_viewport demo — one context-private view (see View.h).
#include "gldx/core/Platform.h"

#include "View.h"

#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace multi_viewport {

namespace {
const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};
} // namespace

void View::Create() {
    gldx::RenderContext::MarkAsRenderThread();

    renderer = std::make_unique<gldx::Renderer>();
    renderer->Init();
    renderer->AddPass(std::make_unique<gldx::GeometryPass>());

    pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex,
                                                gldx::shaders::kPbrFragment);
    if (!*pbr) {
        std::fprintf(stderr, "view %d: PBR shader: %s\n", index, pbr->error().c_str());
        return;
    }
    (*pbr)->Use();
    (*pbr)->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);

    lights = std::make_unique<gldx::LightBuffer>();
    lights->Init();

    debug  = std::make_unique<gldx::DebugDraw>();
    sprite = std::make_unique<gldx::SpriteBatch>();
    font   = std::make_unique<gldx::Font>();
    white  = std::make_unique<gldx::Texture2D>();
    debug->Init();
    sprite->Init();
    for (const char* candidate : kFontCandidates)
        if (font->LoadFromFile(candidate, 40.0f)) break;
    gldx::Texture2DDesc solid;
    solid.width    = solid.height = 1;
    solid.channels = 4;
    solid.pixels   = {255, 255, 255, 255};
    white->Upload(solid);

    BuildScene();
}

// The Scene is pure CPU bookkeeping; clearing the items invalidates its
// non-owning mesh pointers, but Frame() early-returns once renderer is gone.
void View::Release() {
    items.clear();      // Mesh destructors delete VAO/VBO names
    spinner = nullptr;
    white.reset(); font.reset(); sprite.reset(); debug.reset();
    lights.reset();
    pbr.reset();
    renderer.reset();
}

void View::BuildScene() {
    auto add = [&](gldx::MeshData data, const gldx::PbrMaterial& m,
                   const gldx::Transform& t, gldx::SceneNode** out = nullptr) {
        auto it = std::make_unique<Item>();
        it->material = m;
        glm::vec3 c; float r;
        gldx::SceneNode::BoundsFromMeshData(data, c, r);
        it->mesh.Upload(std::move(data));
        gldx::SceneNode& n = scene.CreateRoot(t);
        n.SetRenderable(&it->mesh, &it->material);
        n.SetLocalBounds(c, r);
        if (out) *out = &n;
        items.push_back(std::move(it));
    };

    {   gldx::Transform t; t.scale = glm::vec3(20.0f, 1.0f, 20.0f);
        gldx::PbrMaterial m; m.baseColor = glm::vec4(0.55f, 0.56f, 0.6f, 1.0f); m.roughness = 0.9f;
        add(gldx::GeometryFactory::Plane(1.0f), m, t); }
    for (int i = 0; i < 5; ++i) for (int j = 0; j < 5; ++j) {
        gldx::Transform t; t.translation = glm::vec3(-3.0f + i * 1.5f, 0.5f, -3.0f + j * 1.5f);
        gldx::PbrMaterial m;
        m.metallic  = static_cast<float>(i) / 4.0f;
        m.roughness = 0.05f + 0.9f * static_cast<float>(j) / 4.0f;
        m.baseColor = glm::vec4(0.9f, 0.5f, 0.25f, 1.0f);
        add(gldx::GeometryFactory::Sphere(0.5f, 32, 24), m, t);
    }
    {   gldx::Transform t; t.translation = glm::vec3(0.0f, 2.6f, 0.0f);
        gldx::PbrMaterial m; m.baseColor = glm::vec4(0.2f, 0.75f, 0.9f, 1.0f); m.metallic = 0.1f; m.roughness = 0.35f;
        add(gldx::GeometryFactory::Cube(1.6f), m, t, &spinner); }
}

void View::Button(gldx::win::MouseButton button, gldx::win::KeyAction action) {
    if (button != gldx::win::MouseButton::Left) return;
    dragging = action == gldx::win::KeyAction::Press;
}

void View::Drag(gldx::win::Vec2d pos) {
    if (!state) return;
    if (!dragging) {            // press edge: reseat the anchor, no jump
        lastX = pos.x; lastY = pos.y;
        return;
    }
    state->orbitYaw.fetch_add(static_cast<float>(pos.x - lastX) * -0.006f,
                              std::memory_order_relaxed);
    state->orbitPitch.store(
        Clamp(state->orbitPitch.load(std::memory_order_relaxed)
                  + static_cast<float>(pos.y - lastY) * 0.006f,
              -1.45f, 1.45f),
        std::memory_order_relaxed);
    lastX = pos.x; lastY = pos.y;
}

void View::Frame(const gldx::win::FrameInfo& info) {
    // The per-frame contract gldxwin now guarantees: this window's context
    // is the current one, and we are still the one render thread.
    if (!info.window->ContextIsCurrent())
        std::fprintf(stderr, "view %d: context mismatch during OnFrame\n", index);
    gldx::RenderContext::AssertRenderThread("multi_viewport::View::Frame");
    if (!renderer || !pbr || !*pbr) return;

    const float yaw   = state->orbitYaw.load(std::memory_order_relaxed) + yawOffset;
    const float pitch = Clamp(state->orbitPitch.load(std::memory_order_relaxed) + pitchOffset,
                              -1.45f, 1.45f);
    const glm::vec3 target(0.0f, 1.2f, 0.0f);
    const float cp = std::cos(pitch);
    const glm::vec3 eye(target.x + 11.0f * cp * std::sin(yaw),
                        target.y + 11.0f * std::sin(pitch),
                        target.z + 11.0f * cp * std::cos(yaw));
    camera.SetPerspective(45.0f, 1.0f, 0.1f, 100.0f);
    camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
    camera.LookAt(eye, target, glm::vec3(0, 1, 0));

    // Sample the shared clock once per frame; every view samples the same
    // atomic, so the cube's orientation is identical across windows.
    const float phase = static_cast<float>(state->phase.load(std::memory_order_relaxed));
    if (spinner) {
        spinner->local().rotation = glm::angleAxis(phase, glm::normalize(glm::vec3(0.3f, 1.0f, 0.15f)));
        spinner->local().translation = glm::vec3(0.0f, 2.6f + 0.4f * std::sin(phase * 0.5f), 0.0f);
    }

    gldx::LightSetup setup;
    setup.sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    setup.sun.color     = glm::vec3(1.0f);
    setup.sun.intensity = 3.0f;
    setup.ambient       = glm::vec3(0.08f);
    lights->Update(setup, camera.Position());

    scene.Update();

    gldx::RenderFrame f;
    f.fbWidth     = info.fbWidth;
    f.fbHeight    = info.fbHeight;
    f.smoothedFps = info.smoothedFps;
    f.camera      = &camera;
    f.scene       = &scene;
    f.lights      = lights.get();
    f.pbr         = &**pbr;
    f.viewProj    = camera.ViewProjection();
    f.lightSetup  = setup;
    renderer->Render(f);

    DrawHud(info);
}

// Overlay straight onto the default framebuffer after the geometry pass:
// the axes make the per-window camera offset visible, the numbers line
// makes the shared phase/ticks readable in a screenshot.
void View::DrawHud(const gldx::win::FrameInfo& info) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, info.fbWidth, info.fbHeight);

    debug->Clear();
    debug->PushAxes(glm::vec3(0.0f), 2.0f);
    debug->Draw(camera.ViewProjection());

    if (font->loaded() && white->valid()) {
        char line[192];
        std::snprintf(line, sizeof(line),
                      "view %d/%d  orbit %+4d deg  phase %6.2f  ticks %6u  %3.0f fps",
                      index + 1, total,
                      static_cast<int>(state->orbitYaw.load() * 180.0f / kPi)
                          + static_cast<int>(yawOffset * 180.0f / kPi),
                      state->phase.load(), state->ticks.load(), info.smoothedFps);
        sprite->Begin(*white, info.fbWidth, info.fbHeight);
        sprite->Draw(*white, 10.0f, 10.0f, 620.0f, 34.0f,
                     0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));
        gldx::TextRenderer::Draw(*sprite, *font, line, 22.0f, 18.0f, 16.0f,
                                 glm::vec4(0.75f, 0.9f, 1.0f, 1.0f));
        sprite->End();
    }
}

} // namespace multi_viewport
