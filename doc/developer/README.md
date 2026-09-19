# 开发者文档 / Developer Docs

面向要**理解并修改 `gfx` 引擎本身**的贡献者。使用者教程（怎么跑、怎么接）在 [../user/README.md](../user/README.md)；这里讲**为什么这样设计**与**必须遵守的不变量**。

| 文档 | 讲什么 |
| --- | --- |
| [design.md](design.md) | 分层子系统与依赖方向、场景图、线性渲染管线（及为何不用完整 render graph）、材质/光照/UBO、两阶段资源管线、HDR 后期链、以 C++20 named module 交付 |
| [thread-safety.md](thread-safety.md) | 把"GL 只在渲染线程"这条不变量落到代码每一处的机制：线程亲和断言、两阶段管线、锁的划分、move-only RAII、`std::expected` 错误传播；附**新代码检查清单** |

## 贡献前的最短路径

1. 先跑通 [../user/1-getting-started.md](../user/1-getting-started.md)，确认工具链（macOS 必须 Homebrew LLVM，见 [../user/4-troubleshooting.md](../user/4-troubleshooting.md)）。
2. 读 [design.md](design.md) §1 的分层图，搞清楚你要改的东西在哪一层、依赖允许朝向哪里。
3. **只要新增/修改会拥有或调用 GL 的代码，先读 [thread-safety.md](thread-safety.md)**，对照其末尾检查清单。
4. 改 GLSL 记得只有 `src/gfx/shader/*Shaders.h` 是真源（`src/assets/shaders/` 是镜像，不加载）。

## 验证一次改动是否 OK

```bash
cmake --build build 2>&1 | grep -E 'src/(main|gfx)' | grep -iE 'warning|error'   # 期望：无（自有代码零告警，Homebrew clang 基线）
./output/GLFW_Template >/tmp/o 2>/tmp/e & p=$!; sleep 6; kill $p 2>/dev/null; wc -c /tmp/e   # 期望：stderr 0 字节
```

机器可读的命令、文件地图与硬约束速查见仓库根 [../../AGENTS.md](../../AGENTS.md)。
