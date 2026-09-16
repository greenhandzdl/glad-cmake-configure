# GLFW + GLAD CMake 模板

跨平台的现代 OpenGL CMake 模板，使用 GLFW 创建窗口、GLAD 加载 OpenGL 函数，并通过 git 子模块引入 STB 作为 header-only 图像/工具库。

## 依赖

GLAD 已内嵌在项目中（`third_party/glad/`），无需额外安装。

STB 以 git 子模块引入（`third_party/stb/`），引入命令：

```bash
git submodule add https://github.com/nothings/stb.git third_party/stb
```

首次克隆后初始化子模块：

```bash
git submodule update --init --recursive
```

**macOS：**
```bash
brew install glfw cmake
```

**Linux (Ubuntu/Debian)：**
```bash
sudo apt-get install libglfw3-dev cmake build-essential
```

**Linux (Fedora/RHEL)：**
```bash
sudo dnf install glfw-devel cmake gcc-c++
```

## CMake 集成

- **GLAD**：内嵌源码，CMake 自动编译为 `glad` 静态库
- **GLFW**：Homebrew/apt 安装，`find_package(glfw3)` 自动探测
- **GLM**：Homebrew/apt 安装，`find_path(GLM_INCLUDE_DIR glm/glm.hpp)` 自动探测
- **STB**：header-only，CMake 提供 `stb` INTERFACE 目标，仅添加 include 路径 `third_party/stb`

使用 STB 时，在**恰好一个** `.cpp` 文件中、include 对应头文件**之前**定义实现宏：

```cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
```

> 注意：实现宏（`STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION` 等）不应在 CMake 中全局定义，否则多个编译单元 include 同一头文件会导致重复符号错误。

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
