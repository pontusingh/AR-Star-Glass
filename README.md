# 观星AR眼镜（AR-Star-Glass）

> 一款独立自足的 DIY AR 眼镜：佩戴者抬头时，将可见恒星、行星和星座的实时标签直接叠加在视野中。
> 全部天文计算（Astronomy Engine C 移植）与星表都在设备本地运行，**核心操作无需手机**。

**AI 使用说明**：本项目在开发过程中使用了 AI 辅助编码，AI 主要参与部分驱动与行为层代码的编写、调试及文档整理；核心天文算法移植自第三方开源库 [Astronomy Engine](https://github.com/cosinekitty/astronomy)（MIT），未经修改。

---

## 目录

- [观星AR眼镜（AR-Star-Glass）](#观星ar眼镜ar-star-glass)
  - [目录](#目录)
  - [功能特性](#功能特性)
  - [硬件](#硬件)
  - [软件架构](#软件架构)
  - [项目结构](#项目结构)
  - [快速开始](#快速开始)
  - [按键操作](#按键操作)
  - [串口控制台](#串口控制台)
  - [验证](#验证)
  - [第三方依赖与许可](#第三方依赖与许可)

---

## 功能特性

- 基于头部朝向（BNO085 IMU + 磁力计融合）的实时天体标签叠加
- 最佳可见目标智能推荐，按高度角 + 亮度排序，物理按键循环浏览
- 转向引导：对选中目标以方向箭头指示应转的方位 / 高度
- 时空旅行模式：通过按键设定不同日期 / 时间查看星空
- 单点验证校准：瞄准已知亮星比对计算朝向与实际方向，残差持久化到 NVS
- GPS 提供位置与 UTC 时间，一次定位后由 MCU 时钟推进，无需持续占用

## 硬件

| 组件 | 型号 / 说明 |
|---|---|
| MCU | ESP32-C3 SuperMini（4MB Flash，无 PSRAM） |
| IMU | BNO085（SH-2 协议，旋转矢量 @50Hz） |
| 显示 | SSD1306 OLED（I2C 0x3C）+ 棱镜光学 |
| 定位 | GPS 模块（UART NMEA，9600） |
| 输入 | 4 个物理按键 |

**引脚分配**（ESP32-C3）：

| 信号 | GPIO |
|---|---|
| OLED / IMU SDA | 8 |
| OLED / IMU SCL | 9 |
| IMU INT | 6 |
| IMU RST | 5 |
| GPS RX / TX | 4 / 3 |
| 按键 A / B / 上 / 下 | 0 / 1 / 10 / 7 |

> OLED（0x3C）与 BNO085（0x4A）共享 ESP32-C3 唯一一条 I2C 总线（`Wire`），总线由 `oledInit()` 初始化唯一一次。

## 软件架构

- **驱动层**：`ImuDriver`、`DisplayDriver`、`GpsDriver`、`ButtonDriver`（纯硬件封装，无逻辑）
- **数据层**：`StarCatalog`（闪存驻留星表）、`AstronomyEngine`（移植的 C 引擎薄封装）
- **行为层**：`OrientationManager`、`VisibilityRanker`、`CalibrationManager`、`TimeTravelManager`、`RenderManager`、`InputManager`（模式状态机）
- **主程序**：`main.cpp` 仅作胶水层，`setup()` 初始化驱动，`loop()` 轮询按键并刷新显示

## 项目结构

```
include/           头文件（驱动 / 数据 / 行为层接口）
src/
  main.cpp         胶水层：setup + loop
  drivers/         硬件驱动实现
  data/            天文引擎封装 + 星表数据
  behaviors/       行为 / 应用逻辑
  serial/          串口控制台
lib/AstronomyEngine/  移植的 Astronomy Engine C 源码（第三方，MIT）
tools/
  gen_star_catalog.py 星表生成脚本（勿手改生成的 StarCatalogData.cpp）
  host_verify/     PC 端数值验证（无需硬件）
plan.md           路线图与架构蓝图（源码真相，先读它）
```

## 快速开始

使用 [PlatformIO](https://platformio.org/)：

```bash
pio run              # 编译
pio run -t upload    # 烧录
pio device monitor   # 串口监视（115200）
```

> 首次上电后 USB-Serial-JTAG 会重新枚举，固件会等待至多 1s 的 USB-CDC 连接，以保证开机横幅可见。

## 按键操作

| 按键 | 功能 |
|---|---|
| A | 确认（进入选项 / 生效设置） |
| B | 返回 / 取消（逐级返回） |
| 上 | 光标上移 / 数值加 |
| 下 | 光标下移 / 数值减 |

开机进入模式选择主菜单：**定位 / 校准 / 观星 / 时空旅行**。

## 串口控制台

串口 115200，命令以换行结尾：

| 命令 | 说明 |
|---|---|
| `H` | 帮助 |
| `P <纬度> <经度> [高度米]` | 设置观察点覆盖 |
| `T <年 月 日 时 分 秒>` | 设置当前 UTC 时刻 |
| `TT <年 月 日 时 分 秒>` / `TT OFF` | 开启 / 取消时空旅行覆盖 |
| `O <yaw> <pitch> <roll>` / `O OFF` | 注入朝向（覆盖真实 IMU） |
| `I` | 打印当前朝向 |
| `R` | 打印一次可见 Top10（含高度 / 方位角） |

## 验证

- 固件验证：一次干净的 `pio run`（`test/` 目录无 Unity 测试，勿跑 `pio test`）
- 数值交叉验证（无需硬件）：在仓库根目录执行

  ```bash
  gcc -DASTRONOMY_ENGINE_NO_CURRENT_TIME -Ilib/AstronomyEngine/src \
      tools/host_verify/host_verify.c src/data/AstronomyEngine.cpp \
      -o tools/host_verify/host_verify -lm
  tools/host_verify/host_verify.exe
  ```

  并与官方 `astronomy-engine` Python 包输出对照。

## 第三方依赖与许可

- [Astronomy Engine](https://github.com/cosinekitty/astronomy)（C 实现，MIT，版权 2019–2025 Don Cross）—— 见 `lib/AstronomyEngine/LICENSE`
- [U8g2](https://github.com/olikraus/u8g2)（OLED 显示，`olikraus/U8g2@^2.36.0`）
- [Adafruit BNO08x](https://github.com/adafruit/Adafruit_BNO08x)（IMU，`adafruit/Adafruit BNO08x@^1.2.0`）
