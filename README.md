# GLFW + GLAD CMake 模板

跨平台的现代 OpenGL CMake 模板，使用 GLFW 创建窗口、GLAD 加载 OpenGL 函数，并通过 git 子模块引入 GLAD、STB 作为依赖。

## 依赖

GLAD 采用**系统优先、仓库回退**策略：系统包管理器能找到 GLAD 时直接使用系统版本；找不到时回退到仓库子模块（`third_party/glad/`，来源 https://github.com/Dav1dde/glad.git ）编译链接。

- **系统 GLAD（可选）**：`brew install glad`（或 apt/dnf 对应包）。安装后 CMake 会通过 `find_package(glad CONFIG)` 找到并链接 `glad::glad`。
- **子模块 GLAD（回退）**：`third_party/glad/` 以 git 子模块引入，构建时调用仓库内的 glad2 生成器（需 Python 3 + jinja2）现场生成 OpenGL 3.3 Core 绑定源码并编译为静态库。

STB 以 git 子模块引入（`third_party/stb/`），引入命令：

```bash
git submodule add https://github.com/nothings/stb.git third_party/stb
```

首次克隆后初始化全部子模块：

```bash
git submodule update --init --recursive
```

> 提示：若未初始化子模块且系统没有 GLAD，CMake 配置会报错并提示先执行上述初始化命令。

**macOS：**
```bash
brew install glfw cmake
pip install jinja2        # 回退分支生成 glad2 绑定所需
```

**Linux (Ubuntu/Debian)：**
```bash
sudo apt-get install libglfw3-dev cmake build-essential python3-jinja2
```

**Linux (Fedora/RHEL)：**
```bash
sudo dnf install glfw-devel cmake gcc-c++ python3-jinja2
```

## CMake 集成

- **GLAD**：优先 `find_package(glad CONFIG)` 使用系统包（`glad::glad`）；未找到时回退到子模块 `third_party/glad/`，现场生成并编译 `glad_gl_core_33` 静态库（glad2，OpenGL 3.3 Core）
- **GLFW**：Homebrew/apt 安装，`find_package(glfw3)` 自动探测
- **GLM**：Homebrew/apt 安装，`find_path(GLM_INCLUDE_DIR glm/glm.hpp)` 自动探测
- **STB**：header-only，CMake 提供 `stb` INTERFACE 目标，仅添加 include 路径 `third_party/stb`

构建时可通过配置摘要确认 GLAD 来源：

```
--   GLAD:            system                       # 使用系统 glad
--   GLAD:            submodule (OpenGL 3.3 Core)  # 回退到子模块
```

STB 的实现宏（`STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION`、`STB_TRUETYPE_IMPLEMENTATION`）已内置在 `src/utils/stb.cpp`（implementation file）中，且该文件已加入 CMake 编译。其他源文件直接包含 stb 头文件即可使用，**无需自行定义任何 `STB_xxx_IMPLEMENTATION` 宏**：

```cpp
#include "stb_image.h"        // stbi_load / stbi_free 等
#include "stb_image_write.h"  // stbi_write_png 等
#include "stb_truetype.h"     // stbtt_* 字体光栅化
```

> 注意：请勿在 CMake 中全局定义实现宏，也不要再在其他 `.cpp` 文件中定义它们，否则多个编译单元 include 同一头文件会导致重复符号错误。如需新增 stb 库（如 `stb_image_resize2.h`），只需在 `src/utils/stb.cpp` 中按同样方式补一行实现宏 + include 即可。

## 构建 & 运行

```bash
mkdir build && cd build
cmake ..
cmake --build .
./output/GLFW_Template
```

## 操作

- **R** — 开关旋转
- **空格** — 重置旋转
- **ESC** — 退出

## 清理

```bash
cmake --build build --target clean-project
```

删除 `build/`、`output/` 及生成的脚本。

## 平台说明

- **macOS**：Cocoa 框架，OpenGL 4.1 Metal 后端
- **Linux**：X11，OpenGL 3.3+ Core Profile
