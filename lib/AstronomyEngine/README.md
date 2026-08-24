# AstronomyEngine（第三方 C 库）

移植自 [Astronomy Engine](https://github.com/cosinekitty/astronomy) v2.1.19 的 **C 实现**
（MIT 许可证，版权 2019-2025 Don Cross），按 plan.md 第 0/1 阶段移植。

## 目录结构

- `src/astronomy.c` / `src/astronomy.h` —— 上游原始源码原样保留（未做任何修改），
  位于标准 PlatformIO 私有库布局的 `src/` 下，由 LDF 在被引用时自动编译
- `LICENSE` —— 上游 MIT 许可证原文
- `library.json` —— 库清单；通过构建标志定义 `ASTRONOMY_ENGINE_NO_CURRENT_TIME`
  （ESP32 无上游支持的平台时钟接口，且固件时间一律来自 GPS，故按上游文档
  排除 `Astronomy_CurrentTime()`；其余功能不受影响）

## 使用约定

- 固件其余部分**一律**通过 `src/data/AstronomyEngine.cpp` 的薄封装访问
  （声明在 `include/AstronomyEngine.h`），不直接 include `astronomy.h`
- 仅保留了 C 实现；上游仓库中的 C#/JavaScript/Python/Kotlin 绑定、demo、
  测试数据生成器（generate/）、CI 配置等均已删除
