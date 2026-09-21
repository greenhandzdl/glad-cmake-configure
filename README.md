# GLFW + GLAD CMake 图形引擎

跨平台（Windows / macOS / Linux）的 **现代 OpenGL 图形引擎 / PBR 渲染器**，源自一个用 **GLFW** 建窗、**GLAD** 加载函数的 CMake 起步模板，现已成长为一套分层的 `gfx` 引擎子系统，附两个交互式演示程序（`src/main.cpp` 的 PBR 场景、`src/voxel_main.cpp` 的可玩体素世界）。依赖通过 CMake 自动探测，内置 `build/run/clean` 脚本与 GitHub Actions 手动发布流水线。

- 语言标准：C++23；引擎以 **C++20 named module `gfx`** 交付（`main.cpp` 只需 `import gfx;`）
- OpenGL：**4.1 Core Profile**（GLSL `#version 410 core`；macOS 对应 "4.1 Metal"）
- 构建系统：CMake ≥ 3.28（配合 Ninja），需支持 named modules 的编译器（Clang ≥ 19 / GCC ≥ 14 / 最新 MSVC）
- 产物：`output/GLFW_Template` 与 `output/voxel_demo`（Windows 加 `.exe`）

**已实现的图形能力**：PBR 金属/粗糙工作流、级联阴影（CSM + PCF）、基于图像的照明（IBL：辐照度/预滤波/BRDF LUT）、HDR + MSAA + Bloom + ACES 色调映射、精灵批次 + 位图字体 HUD、实例化绘制、视锥剔除、调试线框、CPU 拾取、帧性能分析、两阶段异步资源管线，以及场景层级（`Scene`/`SceneNode`/`Transform`）+ 渲染管线（`Renderer`/`RenderPass`）。

**体素 / 世界层原语**（v1.3）：`Chunk` + `ChunkMesher`（面剔除 + 逐顶点 AO，产出 opaque / transparent 两份网格）、`BlockRegistry`、`Texture2DArray`（一方块一纹理层）、`VoxelOpaquePass` / `VoxelTransparentPass`（视距排序 + 球剔除 + 混合，`VoxelPipeline.doubleSided` 可控不透明背面剔除）、`VoxelShaders`（sampler2DArray + 平行光近似 + alpha cutoff）、`RaycastVoxel`（DDA 逐格拾取，返回命中格与进入面法向）、`Camera` 飞行接口、`Collision`（`MoveVoxelAabb`：以脚底为锚的 AABB 逐轴碰撞 + 贴墙滑行，纯 CPU）、`Noise`（Perlin + fBm，seeded 且 worker 线程安全）、`ParticleBatch`（挖掘碎屑）、`Mesh::Update`（chunk remesh 整缓冲重传）、线性距离雾。

文档按读者角色分三条路径，入口见 **[doc/](doc/README.md)**：使用者请看 [`doc/user/`](doc/user/README.md)（入门 + 一章一个 API 主题的基础系列：骨架/几何场景/资源加载/光照 UBO/相机拾取/体素世界，再加进阶与排错），开发者请看 [`doc/developer/`](doc/developer/README.md)（[design](doc/developer/design.md) + [thread-safety](doc/developer/thread-safety.md)），AI agent 速查见根目录 [AGENTS.md](AGENTS.md)。

## 目录结构

```
.
├── src/
│   ├── main.cpp              # PBR 演示入口：`import gfx;` + 建窗 + 组装场景 + 驱动渲染管线
│   ├── voxel_main.cpp        # 体素演示入口：chunk 流式网格化 + 方块编辑（世界层在 demo 侧，不进引擎）
│   ├── gfx/                  # 引擎子系统（分层，仅向下依赖），整体编译为 named module `gfx`
│   │   ├── gfx.cppm          #   primary interface：export { #include } 聚合全部公共头
│   │   ├── gmf.hpp           #   共享 global module fragment（GLAD/GLM/std 预包含）
│   │   ├── core/             #   Platform.h(窗口/GLFW) · RenderContext(线程亲和)/GLBuffer/VertexArray/UBO/Sampler/Framebuffer
│   │   ├── geometry/         #   Mesh / InstancedMesh / GeometryFactory
│   │   ├── texture/          #   Texture2D / Texture2DArray / TextureCubeMap / RenderTexture
│   │   ├── material/         #   PbrMaterial
│   │   ├── shader/           #   ShaderProgram + 内嵌 GLSL（ShaderLib/… Shaders.h，单一真源）
│   │   ├── camera/ light/ shadow/   # Camera(orbit+飞行) / VoxelRay(DDA) / LightBuffer(UBO) / CascadedShadowMap / EnvironmentMap
│   │   ├── voxel/            #   BlockRegistry / Chunk / ChunkMesher / VoxelMeshGpu（纯 CPU 网格化 + 上传记录）
│   │   ├── util/             #   Noise（Perlin 2D/3D + fBm，确定性、无全局状态）
│   │   ├── scene/            #   Scene / SceneNode / Transform（纯 CPU 层级）
│   │   ├── render/           #   Renderer / RenderPass / RenderFrame / 后期链 / SpriteBatch / TextRenderer / ParticleBatch / Voxel 两 pass
│   │   ├── text/ assets/ debug/     # 字体·异步资源·DebugDraw/Profiler/Picking/Frustum
│   │   └── third_party/      #   stb_image_impl.cpp（唯一第三方实现 TU，非模块接口）
│   ├── ...                   #   引擎各 .cpp 均为 `module gfx;` 实现单元
│   └── assets/               # 内容资源（不被编译）：GLSL 参考镜像 + 模型投放目录，与 gfx 代码同级
│       ├── shaders/          #   内嵌 GLSL 的只读参考镜像（不被编译/加载）
│       └── models/           #   FBX/OBJ/glTF 投放目录（运行期经 AssetManager 异步加载）
├── doc/                      # 使用者(user/) + 开发者(developer/: 设计+线程安全)；agent 速查见根 AGENTS.md
├── third_party/
│   ├── glad/                 # 子模块：glad2 生成器（OpenGL 4.1 Core 绑定）
│   ├── stb/                  # 子模块：header-only 图像/字体
│   └── assimp/               # 子模块：模型导入（源码内置构建）
├── scripts/                  # build.sh / run.sh / clean.sh（由 CMake 生成）
├── .github/workflows/        # 手动触发的三平台构建 + 发布流水线
└── CMakeLists.txt
```

## 依赖说明

| 依赖 | 来源 | 解析方式 |
|------|------|----------|
| **GLFW** | 系统包管理器 | `find_package(glfw3)`，使用导入目标 `glfw`（Homebrew / apt / vcpkg 均导出该目标） |
| **GLAD** | 系统优先，子模块回退 | 先 `find_package(glad CONFIG)`；未找到时用 `third_party/glad` 生成 OpenGL **4.1 Core** 绑定并编译为静态库 `glad_gl_core_41` |
| **GLM** | 系统包管理器 | `find_path(GLM_INCLUDE_DIR glm/glm.hpp)`，header-only |
| **STB** | git 子模块 | header-only，提供 `stb` INTERFACE 目标；由 `src/gfx/third_party/stb_image_impl.cpp` 实例化图像/字体解码 |
| **Assimp** | git 子模块 | `add_subdirectory` 源码内置构建（导入器 only），链接 `assimp::assimp`；三平台无需系统包 |

> GLAD 采用「系统优先、仓库回退」：系统装有 GLAD 时直接链接 `glad::glad`；否则现场调用子模块内的 glad2 生成器（需 Python 3 + jinja2，缺失时回退到隔离的 `uv` venv）生成绑定。无论走哪条路径，最终可执行文件只依赖 GLFW（共享库）与 GLAD（静态库，运行期动态加载 GL），因此无需再手动链接 OpenGL / X11 / Cocoa 等系统库。

### 首次克隆

```bash
git clone --recursive https://github.com/greenhandzdl/glad-cmake-configure.git
# 或已克隆后补拉子模块：
git submodule update --init --recursive
```

### 安装构建依赖

**macOS**
```bash
brew install glfw glm cmake ninja
brew install llvm        # 必需：AppleClang 没有 clang-scan-deps，编不了 C++20 modules
brew install uv          # 或 pip install jinja2（GLAD 子模块回退分支所需）
```

**Linux (Ubuntu/Debian)**
```bash
sudo apt-get install libglfw3-dev libglm-dev cmake build-essential ninja-build python3-jinja2
```

**Linux (Fedora/RHEL)**
```bash
sudo dnf install glfw-devel glm-devel cmake gcc-c++ ninja python3-jinja2
```

**Windows**
```powershell
# 建议使用 vcpkg + Visual Studio（MSVC）环境
vcpkg install glfw3 glm
# 生成 GLAD 需要 Python 3 + jinja2：pip install jinja2
```

## 构建与运行

推荐用 Ninja（单配置，产物路径固定为 `output/`）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./output/GLFW_Template        # Windows: output\GLFW_Template.exe
```

配置结束会打印依赖解析摘要，便于确认 GLAD 的来源：

```
-- GLFW_Template 1.3.1 configuration:
--   GLAD        : submodule (OpenGL 4.1 Core)   # 或 system
```

### 使用生成脚本

CMake 配置阶段会由 `scripts/*.sh.in` 生成三个可执行脚本：

```bash
./scripts/build.sh [debug|release]   # 配置并构建
./scripts/run.sh                     # 运行默认目标 GLFW_Template
./scripts/run.sh voxel_demo          # 运行体素演示（其后参数原样传给程序）
./scripts/clean.sh                   # 清理

# 等价的 CMake 目标：
cmake --build build --target clean-project
```

`build.sh` 会自动挑一个能扫模块依赖的组合：有 `ninja` 就用 `-G Ninja`；在 macOS 上若未指定
`$CXX` 且缓存里还指向 Apple 工具链，则导出 Homebrew LLVM 的 `CC/CXX`（与 CI 一致）。
生成器或编译器与既有 `CMakeCache.txt` 不符时自动加 `--fresh` 重配置，不必手动清理。
想用自己指定的工具链，照旧显式导出即可（`CXX=... ./scripts/build.sh`），脚本不覆盖。

### 命令行功能开关（无键盘自检）

两个 demo 都接受同一组开关（`src/demo_cli.h`，`--help` 打印完整列表），用途是脚本化地
逐项开关渲染功能并截图对比——回归验证与"某个开关静默失效"的唯一可靠查法：

```bash
./output/voxel_demo --off fog,water,sky,particles --quit-after 8
./output/GLFW_Template --on instances,debug,ortho --off ibl --quit-after 8
```

| 开关 | 作用 |
|------|------|
| `--off a,b` / `--on a,b` | 关闭/打开列出的功能（PBR：`shadow,ibl,bloom,debug,instances,sky,ortho`；体素：`particles,fog,water,sky,ortho,double-sided`） |
| `--quit-after SEC` | SEC 秒后自行退出 |
| `--freeze-at SEC` | 把动画/物理时钟停在启动后 SEC 秒：两次同参数运行逐像素相同，截图才可对比（并且不捕获指针，视角停在 `--yaw/--pitch` 处） |
| `--yaw/--pitch/--radius` | 设定 PBR 轨道相机朝向（实例化场在 +x 方向，默认视角看不到它，也看不到地平线） |
| `--yaw/--pitch/--rise` | 设定体素飞行相机的朝向与出生高度（默认俯角下准星射线落在交互距离之外，脚本挖掘需要更陡的俯角） |
| `--auto-break N` / `--auto-place N` | 脚本化挖 N 块 / 放 N 块，走与点击完全相同的路径（DDA 拾取 → 编辑 → remesh → 碎屑） |
| `--select ID` | 预选方块类型（`7` = 水，配合 `--auto-place` 造出可验证的水面） |

退出时体素 demo 打印一行统计（帧数、平均/最低 fps、生成与网格化的 chunk 数、破坏方块数、
存活/累计粒子数），流式收敛与粒子压力这类"截图看不出来"的性质靠它自证。

CPU 侧原语（体素 DDA、Chunk、ChunkMesher、BlockRegistry、Noise、Frustum）另跑过一轮
AddressSanitizer + UndefinedBehaviorSanitizer 的对抗输入自检（NaN / inf / 1e30 / `INT_MIN` 坐标、
未注册 id、越界访问、反向包围盒），做法记在 `AGENTS.md` 的"CPU 侧原语的对抗输入自检"一节。上表这些开关同样按这个形状喂过一遍畸形 argv（`--select inf`、`--rise nan`、`--auto-break 1e300`、拼错的功能名），两个 demo 都正常退出且不会把 `inf` 转成整数。

## 运行效果

`main.cpp` 打开一个 800×600 窗口，渲染一个交互式 PBR 演示场景：带纹理的地面与球阵、金属立方体、一个旋转的子层级（carousel，演示场景层级变换传播），配合级联阴影、IBL 环境光照、HDR + Bloom + ACES 后期、天空盒，以及精灵批次文本 HUD。

绘制不再是一大堆内联 `gl*` 调用，而是改为逐帧组装一个 `RenderFrame` 后一句 `renderer.Render(frame)`（依序执行 Shadow → Geometry → PostProcess → DebugHud 四个 pass）。

**操作**：拖拽鼠标轨道旋转 / 滚轮缩放；`A`·`D`（或 `←`·`→`）太阳方位、`W`·`S`（或 `↑`·`↓`）太阳高度；右键拾取物体（包围球射线测试，高亮）；`1` 级联阴影、`2` IBL、`3` Bloom、`4` 调试线框、`5` 实例化场、`6` 天空盒、`Tab` 透视/正交；`Esc` 退出。

**体素演示 `voxel_demo`**（`./scripts/run.sh voxel_demo`）是上述体素原语的验收场：fBm 高度场地形 + 沙滩 + 湖泊 + 树冠，worker 线程生成与网格化、渲染线程限量上传，雾随距离收掉视距边缘。世界层（chunk 网格、流式策略、地形生成、编辑规则、HUD）全部写在 `src/voxel_main.cpp`，引擎只提供原语、不含任何世界概念。

**操作**：鼠标转向（指针捕获）；`W`/`A`/`S`/`D` 飞行、`Space`/`Ctrl` 上下（按住 `Shift` 减速）；左键破坏（碎屑粒子）、右键放置；`1`–`8` 选方块；`F` 切换飞行/轨道相机；`[`/`]` 太阳方位、`-`/`=` 太阳高度；`P` 开关粒子；`Tab` 透视/正交；`X` 退出。

## CI 与发布（GitHub Actions）

流水线文件：`.github/workflows/release.yml`，**仅支持手动触发**。

触发方式：**Actions** 页面 → **Build & Release** → **Run workflow**，可填以下输入：

| 输入 | 说明 | 默认 |
|------|------|------|
| `build_type` | CMake 构建类型 | `Release` |
| `release_tag` | Release 标签；留空则自动生成 `ci-<日期>-<sha>` | 空 |
| `prerelease` | 是否标记为预发布 | `false` |
| `draft` | 是否创建为草稿 | `false` |

流水线行为：

1. **build**（矩阵：Linux / macOS / Windows）
   - 递归检出子模块；
   - 各平台安装依赖：Linux `apt`、macOS `brew`、Windows `vcpkg` + MSVC（Ninja）；
   - 统一以 **Ninja** 构建，产物打包为 zip（Windows 额外附带 `glfw3.dll` 等运行库）；
   - 上传各平台 zip。
2. **release**：汇总三平台产物，创建/更新 GitHub Release。

发布产物命名：

```
GLFW_Template-linux-x64.zip
GLFW_Template-macos-arm64.zip
GLFW_Template-windows-x64.zip
```

当前正式版：**[v1.3.1](https://github.com/greenhandzdl/glad-cmake-configure/releases/tag/v1.3.1)**

## 平台说明

- **macOS**：OpenGL 由系统框架提供，GLFW 使用 Cocoa 后端。
- **Linux**：X11，OpenGL 4.1 Core Profile。
- **Windows**：MSVC + vcpkg（GLFW 为动态库，发布包已附带对应 DLL）。

## 许可证

见 [LICENSE](LICENSE)。
