# CI 与发布

三平台构建与 GitHub Release 由一条**仅手动触发**的流水线完成；版本号则由 `scripts/release.sh` 单源同步。本文从根 [README](../../README.md) 拆出。

## 1. 触发方式

流水线文件：`.github/workflows/release.yml`。**没有 push / PR 触发**——图形 demo 的构建昂贵，且本仓库不需要每次提交都出制品。

触发路径：**Actions** 页面 → **Build & Release** → **Run workflow**，可填：

| 输入 | 说明 | 默认 |
|------|------|------|
| `build_type` | CMake 构建类型 | `Release` |
| `release_tag` | Release 标签；留空则自动生成 `ci-<日期>-<sha>` | 空 |
| `prerelease` | 是否标记为预发布 | `false` |
| `draft` | 是否创建为草稿 | `false` |

## 2. 流水线行为

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

制品里含 `GLFW_Template` 与**全部** demo 可执行，无外部资源文件依赖（贴图与模型都是程序化生成或投放目录）。

## 3. 平台差异

- **macOS**：OpenGL 由系统框架提供，GLFW 使用 Cocoa 后端；本地构建必须用 Homebrew LLVM（AppleClang 无 `clang-scan-deps`），见 [使用者排错 §1](../user/9-troubleshooting.md)。
- **Linux**：X11，OpenGL 4.1 Core Profile；注意 libstdc++ 不透出 `<mutex>`，用到 `std::unique_lock` 的文件要显式 include。
- **Windows**：MSVC + vcpkg（GLFW 为动态库，发布包已附带对应 DLL）。当前 job **钉死 `windows-2022` 镜像**：更新镜像带来的 MSVC 版本会在 C++20 named modules 的 `import` 侧触发 STL 重定义编译风暴，解除条件与该决策记录见仓库根 [AGENTS.md](../../AGENTS.md) 的“CI / 发布”一节。

## 4. 版本号同步（`scripts/release.sh`）

版本串同时写在源码、构建配置与文档里，手工发版很容易漂移（打了 tag 但 HUD 版本与“当前正式版”链接还停在旧值）。`scripts/release.sh <X.Y.Z>` 一次性重写全部锚点：

| 文件 | 锚点 |
| --- | --- |
| `src/demo/pbr_showcase/main.cpp` | `kAppVersion = "X.Y.Z"`（权威源） |
| `src/demo/voxel_terrain/main.cpp` | `kAppVersion = "X.Y.Z"` |
| `CMakeLists.txt` | `project(GLFW_Template VERSION X.Y.Z ...)` |
| `README.md` | 配置摘要样例行 `GLFW_Template X.Y.Z configuration:` 与“当前正式版 `[vX.Y.Z](.../releases/tag/vX.Y.Z)`”链接 |
| `AGENTS.md` | “当前正式版 tag：`vX.Y.Z`” |

脚本自带校验：改写后若在受管文件里还能搜到旧版本号就打 WARNING 并列出行。**它刻意不 push、不创建 GitHub Release**——那两步是人的决定。可选 `--commit` 顺带提交版本 bump，再加 `--tag` 打 annotated tag `vX.Y.Z`。

> 因为版本锚点只写在 `README.md` 与 `AGENTS.md`，文档正文（含本文与 [入门](../user/1-getting-started.md)）一律用 `<版本>` 占位描述那行配置摘要，避免出现第六处需要同步的地方。

发版后的手动步骤：

```bash
git push origin master --follow-tags
gh workflow run "Build & Release" -f build_type=Release -f release_tag=vX.Y.Z
```
