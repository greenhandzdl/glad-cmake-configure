# 🟢 入门：从最初搭建到跑起来

目标：在一台干净的机器上，把项目克隆下来、装好依赖、构建、运行，看到演示窗口。全程不需要读引擎源码。

> 下一篇：跑起来之后想基于它写程序，看 [🟡 基础用法](2-basic-usage.md)。中途报错先翻 [🧰 排错](4-troubleshooting.md)。

---

## 1. 前置要求

| 项 | 要求 |
| --- | --- |
| 编译器 | 支持 **C++20 named modules**：Clang ≥ 19 / GCC ≥ 14 / 较新 MSVC |
| 构建 | CMake ≥ **3.28**，建议配 **Ninja** |
| 语言标准 | 工程按 **C++23** 编译（`CMAKE_CXX_STANDARD 23`） |
| 运行库 | OpenGL **4.1 Core**（macOS 即系统 "4.1 Metal"） |

> ⚠️ **macOS 唯一的关键前提**：Apple 自带的 `/usr/bin/clang` **不带 `clang-scan-deps`**，无法配置本工程的 named module。必须用 **Homebrew LLVM**（`brew install llvm`）。用 CLion 还要额外设一次工具链。细节见 [🧰 排错 §1](4-troubleshooting.md#1-macos--clion必须用-homebrew-llvm-工具链)。命令行按下面第 4 步用 `cmake --preset` 即可自动避开。

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

依赖解析策略：GLFW 走系统包；GLAD **系统优先、子模块回退**；GLM header-only；STB/Assimp 由子模块内置构建。最终可执行文件只依赖 GLFW（共享库）+ GLAD（静态、运行期动态加载 GL），无需手动链接 OpenGL/X11/Cocoa。

---

## 4. 构建与运行

四种方式任选其一。产物固定在 `output/GLFW_Template`（Windows 加 `.exe`）。

### 4.1 命令行（跨平台通用）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./output/GLFW_Template        # Windows: output\GLFW_Template.exe
```

配置结束会打印依赖摘要，例如 `GLAD : submodule (OpenGL 4.1 Core)`。

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
./scripts/build.sh [debug|release]   # 配置并构建
./scripts/run.sh                     # 运行 output 下的可执行文件
./scripts/clean.sh                   # 清理构建产物
# 等价的 CMake 目标：
cmake --build build --target clean-project
```

### 4.4 用 CLion 打开

CLion 会自动导入 `CMakePresets.json`。**macOS 上唯一要做的**：在 `Settings → Build, Execution, Deployment → Toolchains` 里加一个 Homebrew LLVM 工具链，并让 `Debug` profile 用它，否则会用回 AppleClang 而配置失败。完整图文步骤见 [🧰 排错 §1](4-troubleshooting.md#1-macos--clion必须用-homebrew-llvm-工具链)。

---

## 5. 运行效果与操作

`main.cpp` 打开 800×600 窗口，渲染交互式 PBR 演示：纹理地面 + 5×5 金属/粗糙球阵、纹理立方体、一个旋转的子层级（carousel，演示场景变换传播），配合级联阴影、IBL 环境光、HDR + Bloom + ACES 后期、天空盒，以及精灵批次文本 HUD。

| 操作 | 键 |
| --- | --- |
| 轨道旋转 / 缩放 | 拖拽鼠标 / 滚轮 |
| 太阳方位 / 高度 | `A·D`（或 ←→） / `W·S`（或 ↑↓） |
| 拾取物体（包围球射线，高亮） | 右键 |
| 级联阴影 / IBL / Bloom / 调试线框 / 实例化场 开关 | `1` `2` `3` `4` `5` |
| 天空盒开关 / 透视↔正交 | `6` / `Tab` |
| 退出 | `Esc` |

窗口能出来、能看到球阵和阴影、按 `1..6` 与 `Tab` 有明显画面变化，就说明**整条工具链 + 运行库都通了**。

第二个演示 `voxel_demo`（`./scripts/run.sh voxel_demo`）是可玩的体素世界，验收引擎的体素原语；操作键位与可脚本化的命令行开关（`--off/--on/--freeze-at/--quit-after`，`--help` 看全表）见 [根 README](../../README.md#命令行功能开关无键盘自检)。

---

## 下一步

- 想在自己的程序里用 `gfx` → [🟡 基础用法](2-basic-usage.md)
- 想知道"为什么全黑 / 为什么 CLion 没配置" → [🧰 排错](4-troubleshooting.md)
- 想理解引擎为什么这样设计 → [../developer/design.md](../developer/design.md)
