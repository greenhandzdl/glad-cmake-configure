// multi_window_levels demo — one graded, context-private window (see View.h).
#include "gldx/core/Platform.h"

#include "View.h"

#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace multi_window_levels {

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

// Unit-cube corners + the 12 edges, used to draw the island's rotating
// wireframe. DebugDraw.PushBox is axis-aligned only, so a spinning box has to
// rotate its own corners on the CPU and push the edges between them.
const glm::vec3 kCubeCorners[8] = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1},
};
const int kCubeEdges[12][2] = {
    {0,1},{1,2},{2,3},{3,0},   // back face
    {4,5},{5,6},{6,7},{7,4},   // front face
    {0,4},{1,5},{2,6},{3,7},   // connecting edges
};

} // namespace

void View::Create() {
    gldx::RenderContext::MarkAsRenderThread();

    // Every window, island included, needs the line overlay + text HUD.
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

    // The island window deliberately stops here: no renderer, no PBR program,
    // no lights, no scene - it shares none of the field windows' GPU world.
    if (profile.independent) return;

    renderer = std::make_unique<gldx::Renderer>();
    renderer->Init();
    renderer->AddPass(std::make_unique<gldx::GeometryPass>());

    pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex,
                                                gldx::shaders::kPbrFragment);
    if (!*pbr) {
        std::fprintf(stderr, "%s: PBR shader: %s\n", profile.name, pbr->error().c_str());
        return;
    }
    (*pbr)->Use();
    (*pbr)->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);

    lights = std::make_unique<gldx::LightBuffer>();
    lights->Init();

    BuildField();
}

void View::Release() {
    items.clear();      // Mesh destructors delete this context's VAO/VBO names
    spinner = nullptr;
    white.reset(); font.reset(); sprite.reset(); debug.reset();
    lights.reset();
    pbr.reset();
    renderer.reset();
}

// The shared object field: one ground plane plus a spread grid of spheres and a
// central spinner cube. The layout is identical (deterministic) in every field
// window - only the LOD tessellation follows this window's tier, so it is the
// per-window camera frustum, not the content, that changes the culled count.
void View::BuildField() {
    const int seg   = profile.lodSeg;
    const int rings = seg > 4 ? seg / 2 : 3;

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
        n.id = static_cast<int>(items.size());
        if (out) *out = &n;
        items.push_back(std::move(it));
    };

    {   gldx::Transform t; t.scale = glm::vec3(30.0f, 1.0f, 30.0f);
        gldx::PbrMaterial m; m.baseColor = glm::vec4(0.30f, 0.32f, 0.36f, 1.0f); m.roughness = 0.95f;
        add(gldx::GeometryFactory::Plane(1.0f), m, t); }

    // A 6x6 grid spread across X and Z, spaced wide enough that a short far
    // plane or a 180-degree eye swing changes which columns survive culling.
    for (int i = 0; i < 6; ++i) for (int j = 0; j < 6; ++j) {
        const float x = -12.5f + i * 5.0f;
        const float z = -12.5f + j * 5.0f;
        gldx::Transform t; t.translation = glm::vec3(x, 0.6f, z);
        gldx::PbrMaterial m;
        m.metallic  = static_cast<float>(i) / 5.0f;
        m.roughness = 0.10f + 0.85f * static_cast<float>(j) / 5.0f;
        m.baseColor = glm::vec4(profile.tint, 1.0f);
        add(gldx::GeometryFactory::Sphere(0.6f, seg, rings), m, t);
    }

    {   gldx::Transform t; t.translation = glm::vec3(0.0f, 3.0f, 0.0f);
        gldx::PbrMaterial m; m.baseColor = glm::vec4(0.20f, 0.75f, 0.90f, 1.0f); m.metallic = 0.1f; m.roughness = 0.35f;
        add(gldx::GeometryFactory::Cube(1.8f), m, t, &spinner); }
}

void View::Frame(const gldx::win::FrameInfo& info) {
    if (!info.window->ContextIsCurrent())
        std::fprintf(stderr, "%s: context mismatch during OnFrame\n", profile.name);
    gldx::RenderContext::AssertRenderThread("multi_window_levels::View::Frame");

    // --- advance or sample the animation phase -----------------------------
    // Sync windows read the one shared atomic (identical number across them);
    // async and island windows integrate their own clock from this window's
    // dt, so they drift from the shared clock and from each other.
    const bool sync = !profile.independent && profile.clock == Clock::Sync;
    if (sync) phase = state ? state->syncPhase.load(std::memory_order_relaxed)
                            : 0.0;
    else      phase += info.dt * kSpinSpeed * profile.rate;

    const float aspect = info.fbHeight > 0
        ? static_cast<float>(info.fbWidth) / static_cast<float>(info.fbHeight) : 1.0f;

    // --- island window: a wholly unrelated line-overlay scene --------------
    if (profile.independent) {
        glViewport(0, 0, info.fbWidth, info.fbHeight);
        glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        camera.SetPerspective(profile.fov, aspect, 0.1f, profile.far);
        camera.LookAt(glm::vec3(0.0f, 2.5f, 8.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));

        debug->Clear();
        debug->PushAxes(glm::vec3(0.0f), 1.6f);
        const glm::quat q = glm::angleAxis(static_cast<float>(phase),
                                           glm::normalize(glm::vec3(0.3f, 1.0f, 0.15f)));
        const glm::vec4 col(profile.tint, 1.0f);
        for (const auto& e : kCubeEdges) {
            const glm::vec3 a = q * (kCubeCorners[e[0]] * 2.0f);
            const glm::vec3 b = q * (kCubeCorners[e[1]] * 2.0f);
            debug->PushLine(a, b, col);
        }
        debug->Draw(camera.ViewProjection());
        DrawHud(info, -1, -1);
        return;
    }

    if (!renderer || !pbr || !*pbr) return;

    // --- field window: per-window camera culls the shared field ------------
    const glm::vec3 target(0.0f, 1.5f, 0.0f);
    const float cp = std::cos(profile.pitch);
    const glm::vec3 eye(target.x + profile.radius * cp * std::sin(profile.yaw),
                        target.y + profile.radius * std::sin(profile.pitch),
                        target.z + profile.radius * cp * std::cos(profile.yaw));
    camera.SetPerspective(profile.fov, aspect, 0.1f, profile.far);
    camera.LookAt(eye, target, glm::vec3(0, 1, 0));

    if (spinner) {
        spinner->local().rotation = glm::angleAxis(static_cast<float>(phase),
                                                   glm::normalize(glm::vec3(0.3f, 1.0f, 0.15f)));
        spinner->local().translation = glm::vec3(0.0f, 3.0f + 0.5f * std::sin(phase * 0.5f), 0.0f);
    }

    gldx::LightSetup setup;
    setup.sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    setup.sun.color     = glm::vec3(1.0f);
    setup.sun.intensity = 3.0f;
    setup.ambient       = glm::vec3(0.08f);
    lights->Update(setup, camera.Position());

    scene.Update();

    const glm::mat4 viewProj = camera.ViewProjection();
    gldx::Frustum frustum;
    frustum.Extract(viewProj);

    gldx::RenderFrame f;
    f.fbWidth     = info.fbWidth;
    f.fbHeight    = info.fbHeight;
    f.smoothedFps = info.smoothedFps;
    f.camera      = &camera;
    f.frustum     = &frustum;
    f.scene       = &scene;
    f.lights      = lights.get();
    f.pbr         = &**pbr;
    f.viewProj    = viewProj;
    f.lightSetup  = setup;
    renderer->Render(f);

    DrawHud(info, f.visibleCount, f.totalNodes);
}

// One HUD line naming this window's tier and the live evidence for each
// dimension: the clock mode + phase value (sync vs async), the culled visible
// count out of the shared total, the LOD segment count, and the far plane.
void View::DrawHud(const gldx::win::FrameInfo& info, int visible, int totalNodes) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, info.fbWidth, info.fbHeight);

    if (!font->loaded() || !white->valid()) return;

    char line[224];
    if (profile.independent) {
        std::snprintf(line, sizeof(line),
                      "island %d/%d  INDEP-CONTEXT  async phase %6.2f  "
                      "no field / no pipeline shared  %3.0f fps",
                      index + 1, total, phase, info.smoothedFps);
    } else {
        std::snprintf(line, sizeof(line),
                      "%-10s %d/%d  %s phase %6.2f  visible %2d/%2d  lod %2d  far %5.0f  %3.0f fps",
                      profile.name, index + 1, total,
                      profile.clock == Clock::Sync ? "SYNC " : "ASYNC",
                      phase, visible, totalNodes, profile.lodSeg, profile.far,
                      info.smoothedFps);
    }

    sprite->Begin(*white, info.fbWidth, info.fbHeight);
    sprite->Draw(*white, 8.0f, 8.0f, 640.0f, 32.0f,
                 0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
    gldx::TextRenderer::Draw(*sprite, *font, line, 18.0f, 15.0f, 15.0f,
                             glm::vec4(0.78f, 0.92f, 1.0f, 1.0f));
    sprite->End();
}

} // namespace multi_window_levels
