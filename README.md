# GLFW + GLAD CMake 模板

一个跨平台（Windows / macOS / Linux）的现代 OpenGL 起步模板：用 **GLFW** 创建窗口、**GLAD** 加载 OpenGL 函数，附带 header-only 的 **GLM**（数学）与 **STB**（图像/字体），全部依赖通过 CMake 自动探测，并内置一套 `build/run/clean` 脚本与 GitHub Actions 手动发布流水线。

- 语言标准：C++23
- OpenGL：3.3 Core Profile
- 构建系统：CMake ≥ 3.16（推荐配合 Ninja）
- 产物：`output/GLFW_Template`（Windows 为 `output/GLFW_Template.exe`）

## 目录结构

```
.
├── src/
│   ├── main.cpp              # 入口：创建窗口 + 初始化 GLAD + 渲染循环
│   ├── headers/              # common.h / Shader.h
│   └── utils/                # Shader.cpp / stb.cpp / utils.cpp
├── third_party/
│   ├── glad/                 # git 子模块：Dav1dde/glad（glad2 生成器）
│   └── stb/                  # git 子模块：nothings/stb（header-only）
├── scripts/                  # build.sh / run.sh / clean.sh（由 CMake 生成）
├── .github/workflows/        # 手动触发的三平台构建 + 发布流水线
└── CMakeLists.txt
```

## 依赖说明

| 依赖 | 来源 | 解析方式 |
|------|------|----------|
| **GLFW** | 系统包管理器 | `find_package(glfw3)`，使用导入目标 `glfw`（Homebrew / apt / vcpkg 均导出该目标） |
| **GLAD** | 系统优先，子模块回退 | 先 `find_package(glad CONFIG)`；未找到时用 `third_party/glad` 生成 OpenGL 3.3 Core 绑定并编译为静态库 `glad_gl_core_33` |
| **GLM** | 系统包管理器 | `find_path(GLM_INCLUDE_DIR glm/glm.hpp)`，header-only |
| **STB** | git 子模块 | header-only，提供 `stb` INTERFACE 目标（仅加 include 路径） |

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
--   GLAD        : submodule (OpenGL 3.3 Core)   # 或 system
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

`main.cpp` 会打开一个 800×600 窗口，清屏为深灰背景，并在终端打印应用名与运行时 OpenGL 版本。它是最小可运行的起点骨架——`Shader`（基于 `std::expected` 的 RAII 着色器封装）、GLM、STB 等工具已就位，可在其基础上扩展渲染逻辑。

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
- **Linux**：X11，OpenGL 3.3+ Core Profile。
- **Windows**：MSVC + vcpkg（GLFW 为动态库，发布包已附带对应 DLL）。

## 许可证

见 [LICENSE](LICENSE)。
