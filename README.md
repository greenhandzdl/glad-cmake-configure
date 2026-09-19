# GLFW + GLAD CMake 图形引擎

跨平台（Windows / macOS / Linux）的 **现代 OpenGL 图形引擎 / PBR 渲染器**，源自一个用 **GLFW** 建窗、**GLAD** 加载函数的 CMake 起步模板，现已成长为一套分层的 `gfx` 引擎子系统，附一个交互式演示程序（`src/main.cpp`）。依赖通过 CMake 自动探测，内置 `build/run/clean` 脚本与 GitHub Actions 手动发布流水线。

- 语言标准：C++23；引擎以 **C++20 named module `gfx`** 交付（`main.cpp` 只需 `import gfx;`）
- OpenGL：**4.1 Core Profile**（GLSL `#version 410 core`；macOS 对应 "4.1 Metal"）
- 构建系统：CMake ≥ 3.28（配合 Ninja），需支持 named modules 的编译器（Clang ≥ 19 / GCC ≥ 14 / 最新 MSVC）
- 产物：`output/GLFW_Template`（Windows 为 `output/GLFW_Template.exe`）

**已实现的图形能力**：PBR 金属/粗糙工作流、级联阴影（CSM + PCF）、基于图像的照明（IBL：辐照度/预滤波/BRDF LUT）、HDR + MSAA + Bloom + ACES 色调映射、精灵批次 + 位图字体 HUD、实例化绘制、视锥剔除、调试线框、CPU 拾取、帧性能分析、两阶段异步资源管线，以及场景层级（`Scene`/`SceneNode`/`Transform`）+ 渲染管线（`Renderer`/`RenderPass`）。

设计与线程安全说明见 **[doc/](doc/README.md)**。

## 目录结构

```
.
├── src/
│   ├── main.cpp              # 应用入口：`import gfx;` + 建窗 + 组装场景 + 驱动渲染管线
│   ├── gfx/                  # 引擎子系统（分层，仅向下依赖），整体编译为 named module `gfx`
│   │   ├── gfx.cppm          #   primary interface：export { #include } 聚合全部公共头
│   │   ├── gmf.hpp           #   共享 global module fragment（GLAD/GLM/std 预包含）
│   │   ├── core/             #   Platform.h(窗口/GLFW) · RenderContext(线程亲和)/GLBuffer/VertexArray/UBO/Sampler/Framebuffer
│   │   ├── geometry/         #   Mesh / InstancedMesh / GeometryFactory
│   │   ├── texture/          #   Texture2D / TextureCubeMap / RenderTexture
│   │   ├── material/         #   PbrMaterial
│   │   ├── shader/           #   ShaderProgram + 内嵌 GLSL（ShaderLib/… Shaders.h，单一真源）
│   │   ├── camera/ light/ shadow/   # Camera / LightBuffer(UBO) / CascadedShadowMap / EnvironmentMap
│   │   ├── scene/            #   Scene / SceneNode / Transform（纯 CPU 层级）
│   │   ├── render/           #   Renderer / RenderPass / RenderFrame / 后期链 / SpriteBatch / TextRenderer
│   │   ├── text/ assets/ debug/     # 字体·异步资源·DebugDraw/Profiler/Picking/Frustum
│   │   └── third_party/      #   stb_image_impl.cpp（唯一第三方实现 TU，非模块接口）
│   └── ...                   #   引擎各 .cpp 均为 `module gfx;` 实现单元
├── assets/
│   ├── shaders/              # 内嵌 GLSL 的只读参考镜像（不被编译/加载）
│   └── models/              # FBX/OBJ/glTF 投放目录（运行期经 AssetManager 异步加载）
├── doc/                      # 设计思路 + 线程安全说明
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
-- GLFW_Template 1.0.0 configuration:
--   GLAD        : submodule (OpenGL 4.1 Core)   # 或 system
```

### 使用生成脚本

CMake 配置阶段会由 `scripts/*.sh.in` 生成三个可执行脚本：

```bash
./scripts/build.sh [debug|release]   # 配置并构建
./scripts/run.sh                     # 运行 output 下的可执行文件
./scripts/clean.sh                   # 清理

# 等价的 CMake 目标：
cmake --build build --target clean-project
```

## 运行效果

`main.cpp` 打开一个 800×600 窗口，渲染一个交互式 PBR 演示场景：带纹理的地面与球阵、金属立方体、一个旋转的子层级（carousel，演示场景层级变换传播），配合级联阴影、IBL 环境光照、HDR + Bloom + ACES 后期、天空盒，以及精灵批次文本 HUD。

绘制不再是一大堆内联 `gl*` 调用，而是改为逐帧组装一个 `RenderFrame` 后一句 `renderer.Render(frame)`（依序执行 Shadow → Geometry → PostProcess → DebugHud 四个 pass）。

**操作**：拖拽鼠标轨道旋转 / 滚轮缩放；A·D（或←→）太阳方位、W·S（或↑↓）太阳高度；右键拾取物体（包围球射线测试，高亮）；`1` 级联阴影、`2` IBL、`3` Bloom、`4` 调试线框、`5` 实例化场（开关）；`Esc` 退出。

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

当前正式版：**[v1.0.0](https://github.com/greenhandzdl/glad-cmake-configure/releases/tag/v1.0.0)**

## 平台说明

- **macOS**：OpenGL 由系统框架提供，GLFW 使用 Cocoa 后端。
- **Linux**：X11，OpenGL 4.1 Core Profile。
- **Windows**：MSVC + vcpkg（GLFW 为动态库，发布包已附带对应 DLL）。

## 许可证

见 [LICENSE](LICENSE)。
