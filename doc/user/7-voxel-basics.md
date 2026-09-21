# 🟢 ⑥ 体素世界：从噪声地形到挖方块

前提：已完成 [⑤ 相机与拾取](6-camera-picking.md)。这一章走一遍体素子系统的最小闭环：**方块表 → 程序化地形 → 成块网格 → 上传 → 射线交互**。完整可运行实现是整个 `src/demo/voxel_terrain/`（`main.cpp` 只装配窗口/帧循环，世界层在 `World.{h,cpp}`、输入在 `Input.{h,cpp}`、HUD 在 `Hud.{h,cpp}`；`./scripts/run.sh voxel_terrain`）。

---

## 1. 方块表：`BlockRegistry`

```cpp
gldx::BlockRegistry blocks;                  // 内置 9 种：kAir/kGrass/kDirt/kStone/kSand/kWood/kLeaves/kWater/kSnow
std::uint16_t gravel = blocks.Add({"gravel", 9, true, false, false, false});  // BlockDef{name, texLayer, solid, cutout, transparent, emissive}
```

- id 是 `uint16_t`，**0 永远是 air**（chunk 零初始化即空），上限 `kMaxTypes = 65535`。
- 未注册的 id 一律读成 air（`IsAir`），旧存档/卸载的 mod 不会画出"幽灵面片"。
- `texLayer` 是贴图数组的切片号，与你的 `Texture2DArray` 内容对应。

## 2. 数据：`Chunk` + `Noise`

```cpp
gldx::Chunk chunk({0, 0, 0});                       // 16^3 cells（kChunkSize=16），origin 为世界坐标
gldx::Noise terrain(20260919u);                     // 固定种子 => 同一 seed 永远同一地形（回归截图依赖这点）
for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z) {
    double h = terrain.ToUnit(terrain.Fbm2(x * 0.12, z * 0.12, 4)) * 8.0;   // [-1,1]→[0,1] 后拉伸
    for (int y = 0; y <= static_cast<int>(h); ++y)
        chunk.Set(x, y, z, y == static_cast<int>(h) ? gldx::BlockRegistry::kGrass : gldx::BlockRegistry::kStone);
}
```

`Get/Set` 用局部坐标（越界读返回 air、写被丢弃），`Sample(worldCell)` 实现 `IVoxelSource`——只回答本 chunk 内的格子，跨 chunk 查询由上层聚合（见 `src/demo/voxel_terrain/World.h` 的 `World`）。`Set` 自动打脏标记，边界编辑还会连带标脏邻接方向。

## 3. 成块网格：`ChunkMesher`（纯 CPU，可并行）

```cpp
gldx::ChunkMesher mesher(blocks);
gldx::VoxelChunkMesh cm = mesher.Build(chunkCellsPtr, chunkOrigin, worldSource);  // opaque + transparent 两份 MeshData
```

- 只输出"与不透明邻居相邻"的面（面剔除），水/叶等透明与 cutout 几何分在 `cm.transparent`。
- **Stage A 身份**：全程不碰 GL，可以扔进 `ThreadPool` 并行（`src/demo/voxel_terrain/main.cpp` 就是这么干的），结果 `std::future<VoxelChunkMesh>` 回渲染线程。

## 4. 上传与绘制（渲染线程）

```cpp
gldx::Texture2DArray atlas;                          // 所有方块切片的图集
atlas.Upload({.width = 16, .height = 16, .layers = n, .channels = 4, .srgb = true, .pixels = rgbaBytes});

gldx::VoxelMesh opaque;                              // 每个 chunk 一对；重mesh用 Update() 复用缓冲
opaque.Upload(std::move(cm.opaque));

gldx::VoxelPipeline vx;                              // 把"这一帧要画什么"打包成一个结构
vx.prog = &voxelProg;  vx.atlas = &atlas;  vx.chunks = &chunkList;   // ready() 三缺一不画
auto opaquePass = std::make_unique<gldx::VoxelOpaquePass>();
opaquePass->SetPipeline(vx);                        // VoxelTransparentPass 同款，多按视距从远到近排序
```

体素 demo 不用 `BuildPbrPipeline()`，而是手工 `AddPass` 拼链（顺序：`... → VoxelOpaque → Skybox → VoxelTransparent → PostProcess → HUD`）：`VoxelOpaquePass` 像 `GeometryPass` 一样开启场景目标，独立的 `SkyboxPass` 排在 opaque 与 transparent 之间（即天空盒原先内联的位置），因此不想要天空盒直接从链里去掉它即可。`waterAlpha`/`leafCutoff`/`time`（水面 uv 流动）也在 `VoxelPipeline` 上调。`doubleSided`（默认 `false`）关掉不透明 pass 的背面剔除：站在挖开的腔体里从下往上看时，被剔掉的底面会“看穿”，打开它就能看到腔壁。HUD 字体是 demo 侧同步 `Font::LoadFromFile` 加载的系统 TTF，找不到只禁用文字叠加。

## 5. 交互：`RaycastVoxel`（DDA，纯 CPU）

```cpp
gldx::Ray ray = gldx::PickRay(cursorX * xscale, cursorY * yscale, fbW, fbH, cam.InverseViewProjection());
if (auto hit = gldx::RaycastVoxel(ray, {0, 0, 0}, worldMaxCell,
        [&](glm::ivec3 c) { return world.Pickable(c); }, /*maxDist=*/6.0f)) {
    world.Edit(hit->position, airId);                  // 挖掉命中格（世界格坐标；demo 的 World::Edit，内部负责落到对应 chunk）
    world.Edit(hit->position + hit->normal, placeId);  // 或沿命中面法线在相邻格放置
}
```

命中返回 `{position, normal, distance}`；谓词自己定（demo 用"可见即 solid"）。范围外的格子视为虚空——射线一出 `[min,max]` 就报 miss，chunk 边界编辑不会误中邻块的幻影方块。射线含 NaN/inf 分量时返回 miss 而不是崩溃（与 [⑤](6-camera-picking.md) 的相机钳位互为双保险）。

## 6. 点缀：`ParticleBatch`

```cpp
gldx::ParticleBatch particles;   // Init() 渲染线程；容量 2048 硬上限（粒子是装饰，满了 Spawn 返回 false）
particles.Spawn(pos, vel, {1, 0.8f, 0.4f, 1}, 0.9f /*life 秒*/, 0.12f /*世界直径*/, 9.8f /*重力*/);
particles.Update(dt);           // 每帧一次
particles.Draw(frame.viewProj, cam.Position(), pixelScale);   // 透明地形之后画：深度测试开、写入关、互不遮挡
```

## 7. 站立与滑行：`MoveVoxelAabb`（纯 CPU）

射线回答“眼睛看到了哪个格子”，碰撞回答“身体允许走到哪”。把玩家建模成以**脚底中心**为锚的轴对齐盒，逐帧提出位移让它落地、贴墙滑行：

```cpp
const gldx::VoxelBody player{0.3f, 1.9f};        // 半径(XZ半宽) + 身高(脚→头)
glm::vec3 feet = cam.Position(); feet.y -= kEyeHeight;   // 眼睛在脚上方
gldx::VoxelMoveResult r = gldx::MoveVoxelAabb(feet, player, delta,
        [&](glm::ivec3 c){ return world.Pickable(c); }); // 谓词=什么格挡得住人
// r.grounded 告诉你这帧踩到了地面（下落被拦），交给重力/跳跃决定
```

- `pos` 传的是**脚底**（不是中心），盒子占 `[pos.x±r] × [pos.y, pos.y+height] × [pos.z±r]`；`VoxelAabbSolid` 单列出来给“出生点是否卡进几何体”这类纯查询用。
- 逐轴解算：撞墙只取消被挡那一轴，另一轴照常推进，这就是贴着墙走能滑行的由来。
- **单帧位移别超过一格**：快速移动/传送要调用方自己子步进（demo 把每帧位移按 ≤ 0.5 格切开再解），否则可能停在薄墙前而非穿过去。

---

## 下一步

- 把这个世界加上雾、阴影、后期 → [🔴 进阶](8-advanced.md)
- 地形"没出现"/只有水没方块 → [🧰 排错](9-troubleshooting.md)（先查 `VoxelPipeline::ready()` 三要素）
