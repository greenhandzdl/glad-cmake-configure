# Agent 运行手册 · 环境搭建

面向 **AI coding agent**：被用户要求"帮我在这个项目上把环境搭起来 / 我编译不过"时，按本手册**幂等地**检测→补齐→验证。命令均为原文，可直接执行。速查版见仓库根 [`../../AGENTS.md`](../../AGENTS.md)；坑的人话解释见 [../user/4-troubleshooting.md](../user/4-troubleshooting.md)。

> 原则：**先检测再安装**（别重复装、别假设没装），**每步都有明确成功判据**（判据不过就停下排错，别往下冲）。

---

## 步骤 0 · 探测平台与现状

先判定 OS，再对每一项做"已满足就跳过"的检查：

```bash
uname -s                      # Darwin / Linux；Windows 用 PowerShell 另行
cmake --version               # 需 >= 3.28
ninja --version               # 建议有
# 关键：named modules 必须有依赖扫描器
# macOS：AppleClang 不带 clang-scan-deps —— 必须用 Homebrew LLVM
test -x /opt/homebrew/opt/llvm/bin/clang++ && echo "brew llvm: OK" || echo "brew llvm: MISSING"
/opt/homebrew/opt/llvm/bin/clang++ --version 2>/dev/null | grep -i clang
# 子模块是否已拉取（空行或前缀 '-' 表示未初始化）
git submodule status
test -f third_party/assimp/CMakeLists.txt && echo "submodules: OK" || echo "submodules: MISSING"
```

Linux/Windows 上把"编译器可用性"换成：`clang++ --version`（Clang ≥ 19）或 `g++ --version`（GCC ≥ 14）或 MSVC。判据：**编译器支持 named modules 且带 scan 工具**。

---

## 步骤 1 · 补齐依赖（只装缺的）

**macOS**
```bash
brew install llvm              # 必需：带 clang-scan-deps 的 clang
brew install glfw glm cmake ninja
brew install uv || pip install jinja2   # 仅当无系统 glad、要走子模块生成时需要
```

**Linux（Ubuntu/Debian）**
```bash
sudo apt-get install libglfw3-dev libglm-dev cmake build-essential ninja-build python3-jinja2
```
Linux 若默认 `g++ < 14`：装较新 GCC 或 `apt-get install clang`（≥19）并显式指定编译器（见步骤 3）。

**Fedora**
```bash
sudo dnf install glfw-devel glm-devel cmake gcc-c++ ninja python3-jinja2
```

**Windows（vcpkg + MSVC/Ninja）**
```powershell
vcpkg install glfw3 glm
pip install jinja2
```

---

## 步骤 2 · 拉子模块（若步骤 0 显示 MISSING）

```bash
git submodule update --init --recursive
```

判据：`third_party/glad`、`third_party/stb`、`third_party/assimp` 下都有源文件（如 `third_party/assimp/CMakeLists.txt`）。

---

## 步骤 3 · 首次配置 + 构建 + 运行，并验证

```bash
cmake --preset Debug                              # macOS：预设已钉 Homebrew clang，绕开 AppleClang 坑
cmake --build --preset Debug
./output/GLFW_Template >/tmp/gfx.out 2>/tmp/gfx.err &  pid=$!
sleep 6; kill $pid 2>/dev/null
wc -c /tmp/gfx.err                                # 判据：stderr 应为 0 字节（无字体/着色器/GL 报错）
```

非 macOS 或不想用预设：
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=<支持modules的clang++/g++>
cmake --build build
```

**判据（全部满足才算搭好）**：
1. 配置期摘要打印出 `GLAD : submodule (OpenGL 4.1 Core)` 或 `system package`，且无 `FATAL_ERROR`。
2. 构建 exit 0，`grep` 自有 `src/(main|gfx)` 无 `warning`/`error`。
3. 运行出现窗口、stderr 0 字节。

```bash
cmake --build build 2>&1 | grep -E 'src/(main|gfx)' | grep -iE 'warning|error'   # 期望空
```

---

## 步骤 4 ·（仅当用户用 CLion）注册 Homebrew LLVM 工具链

CLion 会自动导入 `CMakePresets.json`，但**它会用检测到的工具链在命令行注入 `-DCMAKE_CXX_COMPILER=/usr/bin/c++`，优先级高于预设的 `cacheVariables`**，把编译器覆盖回 AppleClang → named modules 配置失败、无运行配置。

修复（一次性、全局）：
1. `Settings → Build, Execution, Deployment → Toolchains → +`：C `/opt/homebrew/opt/llvm/bin/clang`、C++ `/opt/homebrew/opt/llvm/bin/clang++`，命名 `Homebrew LLVM`。
2. `... → CMake → Debug` profile：**Toolchain 选 `Homebrew LLVM`**，Generator `Ninja` → `Tools → CMake → Reset Cache and Reload Project`。
3. 要**删 `.idea` 也能复原**：把 `Homebrew LLVM` 排到工具链列表**首位**（新 profile 默认取首位）。

> 改 `workspace.xml` 等 IDE 文件前**必须先退出 CLion**，否则运行时会回写覆盖。

---

## 失败分支（配置/构建报错时对照）

| 报错关键词 | 原因 | 动作 |
| --- | --- | --- |
| `may use modules, but the compiler ... cannot scan` | AppleClang / 缺 `clang-scan-deps` | 用 `/opt/homebrew/opt/llvm/bin/clang++` 重配（`cmake --preset Debug`） |
| `GLFW3 not found` | 缺 glfw | `brew install glfw` / `apt-get install libglfw3-dev` / `vcpkg install glfw3` |
| `GLM not found` | 缺 glm | 安装 glm |
| `GLAD submodule not found` / `STB submodule not found` / `Assimp submodule not found` | 子模块未拉 | `git submodule update --init --recursive` |
| `System Python has no jinja2 and 'uv' was not found` | GLAD 生成缺依赖 | `brew install uv` 或 `pip install jinja2` |
| CLion 无运行配置 / "此文件不属于任何项目目标" | 见步骤 4 | 注册并选中 Homebrew LLVM 工具链 |

搭好之后，要替用户写/改程序 → [`author-program.md`](author-program.md)。
