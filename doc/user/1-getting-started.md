# 🟢 入门：从最初搭建到跑起来

目标：在一台干净的机器上，把项目克隆下来、装好依赖、构建、运行，看到演示窗口。全程不需要读引擎源码。

> 下一篇：跑起来之后想基于它写程序，看 [🟢 ① 核心骨架](2-core-setup.md)。中途报错先翻 [🧰 排错](9-troubleshooting.md)。

---

## 1. 前置要求

| 项 | 要求 |
| --- | --- |
| 编译器 | 支持 **C++20 named modules**：Clang ≥ 19 / GCC ≥ 14 / 较新 MSVC |
| 构建 | CMake ≥ **3.28**，建议配 **Ninja** |
| 语言标准 | 工程按 **C++23** 编译（`CMAKE_CXX_STANDARD 23`） |
| 运行库 | OpenGL **4.1 Core**（macOS 即系统 "4.1 Metal"） |

> ⚠️ **macOS 唯一的关键前提**：Apple 自带的 `/usr/bin/clang` **不带 `clang-scan-deps`**，无法配置本工程的 named module。必须用 **Homebrew LLVM**（`brew install llvm`）。用 CLion 还要额外设一次工具链。细节见 [🧰 排错 §1](9-troubleshooting.md#1-macos--clion必须用-homebrew-llvm-工具链)。命令行按下面第 4 步用 `cmake --preset` 即可自动避开。

---

## 2. 克隆（含子模块）

依赖 `glad / stb / assimp` 以 git 子模块形式内置，**必须一起拉下来**：

```bash
git clone --recursive https://github.com/greenhandzdl/glad-cmake-configure.git
cd glad-cmake-configure
# 若忘了 --recursive：
git submodule update --init --recursive
```

---

## 3. 安装系统依赖

**macOS**
```bash
brew install glfw glm cmake ninja
brew install llvm          # 提供带 clang-scan-deps 的 clang++（本工程必需）
brew install uv            # 或 pip install jinja2 —— 仅 GLAD 子模块回退分支需要
```

**Linux（Ubuntu/Debian）**
```bash
sudo apt-get install libglfw3-dev libglm-dev cmake build-essential ninja-build python3-jinja2
```

**Linux（Fedora/RHEL）**
```bash
sudo dnf install glfw-devel glm-devel cmake gcc-c++ ninja python3-jinja2
```

**Windows**（建议 vcpkg + MSVC）
```powershell
vcpkg install glfw3 glm
pip install jinja2         # 生成 GLAD 需要
```

依赖解析策略一览：

| 依赖 | 来源 | 解析方式 |
|------|------|----------|
| **GLFW** | 系统包管理器 | `find_package(glfw3)`，使用导入目标 `glfw`（Homebrew / apt / vcpkg 均导出该目标） |
| **GLAD** | 系统优先，子模块回退 | 先 `find_package(glad CONFIG)`；未找到时用 `third_party/glad` 生成 OpenGL **4.1 Core** 绑定并编译为静态库 `glad_gl_core_41` |
| **GLM** | 系统包管理器 | `find_path(GLM_INCLUDE_DIR glm/glm.hpp)`，header-only |
| **STB** | git 子模块 | header-only，提供 `stb` INTERFACE 目标；由 `src/gldx/third_party/stb_image_impl.cpp` 实例化图像/字体解码 |
| **Assimp** | git 子模块（可选） | 由 `GLDX_ENABLE_ASSIMP`（默认 `ON`）控制：`ON` 时 `add_subdirectory` 源码内置构建（导入器 only）、链接 `assimp::assimp`；`OFF` 时整个 assimp 依赖不构建、`ModelLoader::Load` 降级为“模型导入不可用”，体素/程序化几何/PBR 不受影响（`model_loading` demo 在 OFF 下会优雅提示不可用而非崩溃） |

> GLAD 采用「系统优先、仓库回退」：系统装有 GLAD 时直接链接 `glad::glad`；否则现场调用子模块内的 glad2 生成器（需 Python 3 + jinja2，缺失时回退到隔离的 `uv` venv）生成绑定。无论走哪条路径，最终可执行文件只依赖 GLFW（共享库）与 GLAD（静态库，运行期动态加载 GL），因此无需再手动链接 OpenGL / X11 / Cocoa 等系统库。

---

## 4. 构建与运行

四种方式任选其一。产物固定在 `output/`：`GLFW_Template`（`src/main.cpp` 的 hello-triangle 最小演示）与 `src/demo/` 下每个 feature demo 一个可执行（`pbr_showcase`、`voxel_terrain`…共 26 个）（Windows 加 `.exe`）。

### 4.1 命令行（跨平台通用）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./output/GLFW_Template        # 最裸 hello-triangle（Windows: output\GLFW_Template.exe）
./output/pbr_showcase         # 任一功能 demo：./output/<demo>
```

配置结束会打印依赖解析摘要，便于确认 GLAD 与 Assimp 的来源（形状为 `GLFW_Template <版本> configuration:` 加两行 `GLAD : submodule (OpenGL 4.1 Core)` / `Assimp : submodule (static, bundled zlib)`，各项都可替为 `system` 或 `DISABLED`）。

不需要模型导入时，可用 `-DGLDX_ENABLE_ASSIMP=OFF` 去掉这个重依赖（连子模块都不必拉）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGLDX_ENABLE_ASSIMP=OFF
```

### 4.2 CMake Presets（macOS 便捷路径，推荐）

仓库根带 `CMakePresets.json`，已把编译器钉到 Homebrew LLVM，且**仅在 macOS 生效**（`condition: Darwin`，不影响 Linux/Windows 与 CI）：

```bash
cmake --preset Debug            # 或 Release
cmake --build --preset Debug
./output/GLFW_Template
```

这一步会自动用对编译器，绕开上面 ⚠️ 提到的 AppleClang 坑。

### 4.3 一键脚本

配置阶段会由 `scripts/*.sh.in` 生成：

```bash
./scripts/build.sh [debug|release]   # 配置并构建（全部 demo）
./scripts/run.sh                     # 运行默认目标 GLFW_Template（hello-triangle）
./scripts/run.sh pbr_showcase        # 运行指定 demo（其后参数原样传给程序）
./scripts/run.sh pbr_lighting --quit-after 3   # 无头自检：3 秒后自行退出
./scripts/clean.sh                   # 清理构建产物
# 等价的 CMake 目标：
cmake --build build --target clean-project
```

`build.sh` 会自动挑一个能扫模块依赖的组合：有 `ninja` 就用 `-G Ninja`；在 macOS 上若未指定
`$CXX` 且缓存里还指向 Apple 工具链，则导出 Homebrew LLVM 的 `CC/CXX`（与 CI 一致）。
生成器或编译器与既有 `CMakeCache.txt` 不符时自动加 `--fresh` 重配置，不必手动清理。
想用自己指定的工具链，照旧显式导出即可（`CXX=... ./scripts/build.sh`），脚本不覆盖。

### 4.4 用 CLion 打开

CLion 会自动导入 `CMakePresets.json`。**macOS 上唯一要做的**：在 `Settings → Build, Execution, Deployment → Toolchains` 里加一个 Homebrew LLVM 工具链，并让 `Debug` profile 用它，否则会用回 AppleClang 而配置失败。完整图文步骤见 [🧰 排错 §1](9-troubleshooting.md#1-macos--clion必须用-homebrew-llvm-工具链)。

---

## 5. 运行效果与操作

`src/main.cpp`（目标 `GLFW_Template`）是最小实现基线：一个 800×600 窗口里只画一个三角形——自定义 `RenderPass` + 内联 GLSL + `glDrawArrays(3)`，不建任何引擎子系统。看到它，就说明工具链、GL 4.1 上下文与 `import gldx;` 全通了。

完整的交互式 PBR 演示现在在 `pbr_showcase`（`./scripts/run.sh pbr_showcase`）：纹理地面 + 5×5 金属/粗糙球阵、纹理立方体、一个旋转的子层级（carousel，演示场景变换传播），配合级联阴影、IBL 环境光、HDR + Bloom + ACES 后期、天空盒，以及精灵批次文本 HUD。下表是其键位：

| 操作 | 键 |
| --- | --- |
| 轨道旋转 / 缩放 | 拖拽鼠标 / 滚轮 |
| 太阳方位 / 高度 | `A·D`（或 ←→） / `W·S`（或 ↑↓） |
| 拾取物体（包围球射线，高亮） | 右键 |
| 级联阴影 / IBL / Bloom / 调试线框 / 实例化场 开关 | `1` `2` `3` `4` `5` |
| 天空盒开关 / 透视↔正交 | `6` / `Tab` |
| 退出 | `Esc` |

在 `pbr_showcase` 里能看到球阵和阴影、按 `1..6` 与 `Tab` 有明显画面变化，就说明**整条工具链 + 运行库都通了**。

另两个成品演示：`voxel_terrain`（`./scripts/run.sh voxel_terrain`）是可玩的体素世界，验收引擎的体素原语。此外 `src/demo/` 下还有一批单功能入门 demo（`pbr_lighting`、`shadow_csm`、`ibl_environment`、`postprocess_bloom`、`skybox`、`instancing`、`particles`、`text_hud`、`camera_picking`、`debug_draw`、`geometry_upload`、`texture_samplers`、`model_loading`），每个只装配一个子系统。着色器装配与多窗口专项：`shader_file`（从磁盘加载 GLSL、两文件版）、`geometry_shader_file`（多阶段从磁盘加载：V+Geom+F 全走 `CreateFromFiles({...})`）、`geometry_shader`（选择性挂几何阶段）、`shader_stages`（一次集成全 5 个图形阶段、画 GL_PATCHES 细分线框）、`multi_viewport`（N 窗独立 context 同步重渲同一场景）、`render_passes`（N 窗 + 一个 for 循环装配 Clear/Triangle/Quad/Line/Point 多个不同 `RenderPass` 子类、共设一个原子时钟）。操作键位与可脚本化的命令行开关（`--off/--on/--freeze-at/--quit-after`，`--help` 看全表）见 [①·补 §8.1 共享开关总表](2a-window-input-cli.md)，逐 demo 的画面叙述、键位与索引表在 [Demo 画廊](10-demos.md)。

---

## 下一步

- 想在自己的程序里用 `gldx` → [🟢 ① 核心骨架](2-core-setup.md)，紧接着看 [🟢 ①·补 窗口·输入·命令行](2a-window-input-cli.md)（键盘/鼠标/多窗口/截图/`--开关`）；后续 API 章（几何/资源/光照/相机/体素/进阶）见 [user 目录](README.md)
- 想知道某个 demo 到底演示什么、按什么键 → [🟢 Demo 画廊](10-demos.md)
- 想知道"为什么全黑 / 为什么 CLion 没配置" → [🧰 排错](9-troubleshooting.md)
- 想理解引擎为什么这样设计 → [../developer/design.md](../developer/design.md)
