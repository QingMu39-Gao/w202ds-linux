# w202ds-gpu-driver（图形处理器）

```text
gpu/
├── build.mk
├── devicetree/
├── README.md
└── source/
```

用户态程序、服务模板和诊断见 [userspace](userspace/README.md)。

| 功能 | 可用性 | 问题描述 |
| --- | --- | --- |
| GLES 3D 绘制 | 已验证 | Panfrost / GLES 3.1：819 MHz 和 845 MHz 扩展配置的各五档均通过 100 帧像素校验（2026-11-13）；最终 845 MHz 模块复测通过。 |
| 3D 负载运行 | 短测通过 | 845 MHz 配置下，Wayland glmark2 800×600 离屏 build、terrain 各运行 8 秒；simple_ondemand 负载采样达到 845 MHz / 750 mV。尚未验证长时间稳定性，离屏结果不代表屏幕帧率。 |
| KDE 桌面加速 | 短测通过，开合恢复待修复 | 重启后 KWin 6.3.6 正常启动，OpenGL 合成为 Mali-G57 / Panfrost（Mesa 25.0.7）；前一次 CMA 分配阻塞未复现，根因尚未确定。此前开合后的 framebuffer 错误与恢复问题仍待修复。 |
| 浏览器图形加速 | Chrome 已验证 | 官方 ARM64 Chrome 的 GPU 查询为 ANGLE / Panfrost，合成与 WebGL 已启用；已播放 B 站视频，尚未完成 Shadertoy 和长时间稳定性验收。 |
| Vulkan | 待完成 | 尚无可用性验收结果。 |
| 动态调频 | 已验证 | 默认 T760 配置为 153.6/384/512/655.2 MHz；可显式选择 819 MHz 或 845 MHz 扩展配置，使用 simple_ondemand 调速。两种扩展配置各通过 40 次高低频切换。 |
| 频档准入与电压协调 | 短测通过 | 按 T760 标识、efuse `gpu_bin`、时钟映射和稳压器能力开放频档；升频前确认电压，降频后降压，同时同步硬件 DVFS 电压槽。扩展配置另须原始 index 6 指向 750 mV 电压槽。 |
| 温控限频 | 部分验证 | 读取 `gpu-thmzone`，达到 75°C 时通过 PM QoS 限制到 384 MHz，降到 65°C 解除；限频/解除日志已在调低阈值时观察到，默认阈值下长期热验证待完成。 |
| 温控提供者重绑 | 短测通过 | managed device link 保证传感器移除前先停止 GPU；真机完成传感器解绑、重绑和 GPU 重新绑定。高频配置下传感器未就绪会延迟 GPU probe。 |
| 温度读取失败处理 | 本地用例通过 | 首次有效冷读数前和读取失败时限制到 384 MHz；恢复后沿用 65°C 解除阈值。错误与恢复路径覆盖在 12 个状态转换用例中，未在真机注入读故障。 |
| GPU 运行时省电恢复 | 短测通过 | 819 MHz 和 845 MHz 扩展配置各通过三轮空闲断电、唤醒绘制和再次断电，每轮 100 帧像素校验通过；断电后恢复继承电压，唤醒前建立目标电压。整机休眠另待验证。 |

更新日期：2026-11-13

## 构建和测试

输入为本目录 `source/panfrost/` 与匹配目标内核的构建目录（含生成头文件和 `Module.symvers`）。不需要修改内核源码；输出为 `panfrost.ko`、`w202_gpu_support.ko` 和符号表。

```sh
make -C "$KDIR" M="$PWD/Devices/gpu/source/panfrost" \
  MO="$PWD/output/gpu" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  CONFIG_DRM_PANFROST=m W=1 modules
cc -Wall -Wextra -Werror -O2 Devices/gpu/source/panfrost/tests/thermal-policy.c \
  -o output/thermal-policy
./output/thermal-policy
cc -Wall -Wextra -Werror -O2 Devices/gpu/source/panfrost/tests/dvfs-transition.c \
  -o output/dvfs-transition
./output/dvfs-transition
```

运行时使用标准 devfreq 的 `governor`、`available_frequencies` 和 `trans_stat`。桌面启动策略也须选择 `simple_ondemand`；系统若另行强制 `performance`，将不会按负载自动降档，但 PM QoS 温控上限仍有效。

## T760 频率配置

支持模块的只读参数 `ums9620_max_freq` 选择配置，单位为 Hz；须在加载 `panfrost` 之前设置。默认值为 `655200000`。

| 参数值 | 可用频率（MHz） | 本次验证板的对应电压（mV） |
| --- | --- | --- |
| `655200000` | 153.6 / 384 / 512 / 655.2 | 650 / 650 / 650 / 700 |
| `819000000` | 153.6 / 384 / 512 / 655.2 / 819 | 650 / 650 / 650 / 700 / 750 |
| `845000000` | 153.6 / 384 / 512 / 676 / 845 | 650 / 650 / 650 / 700 / 750 |

例如在模块尚未加载时：

```sh
modprobe w202_gpu_support ums9620_max_freq=845000000
modprobe panfrost
```

持久选择可在目标系统的 `/etc/modprobe.d/w202-gpu-profile.conf` 中加入：

```text
options w202_gpu_support ums9620_max_freq=845000000
```

实际电压还受芯片 bin 和继承电压下限约束，以上电压不作为其他板子的固定值。扩展配置要求可读的有效 bin、完整的原始 index-6 时钟/电压映射、750 mV 电压槽与稳压器能力，以及匹配的 GPLL 分频读回；不满足条件时仅保留通过检查的较低频档。

W205 参考设备树中最高档标称 850 MHz。当前 GPLL 驱动配置采用 26 MHz 参考时钟的整数倍：1.638 GHz 对应 655.2/819 MHz，1.690 GHz 对应 676/845 MHz，因此本驱动将这两组配置按实际频率命名。819/845 MHz 均已通过 GPU 周期计数器测量；这不代表 W205 零售固件的实际运行频率，也不排除其他 PLL 配置实现 850 MHz。

电压转换测试覆盖 400 次转换及六种故障/重试情形；最终 845 MHz 模块通过五档绘制和调低阈值的温控限频/解除测试。这些结果属于短测，默认 75°C/65°C 阈值下的长时间热稳定性仍待验证。

## TODO

- 移植 Vulkan（PanVK），完成 W202DS 真机验证。参考 [DroidDeck 非 Adreno 驱动](https://github.com/The412Banner/droiddeck-nonadreno-drivers) 和 [FristOneRR PanVK](https://github.com/FristOneRR-Admin/FristOneRR-Panvk-Source)。
- 在默认温控阈值下完成长时间高负载热验证。
- 排查 KDE 弹窗卡顿：2026-11-07 列表创建约 43–70 ms，扫描调用 0–2 ms，但展开到 Qt 窗口首帧约 150–1254 ms；一次线程采样中渲染线程约 800 ms 等待 DRM syncobj，随后还等待 DMA fence。DPU 锁存无超时／错误，不能直接归因为 DPU 或确认是 GPU 驱动缺陷。关闭模糊／缩短动画未稳定消除延迟；Qt 渲染循环候选未形成有效对照。相关临时配置已撤回，标准桌面已恢复，尚无已验证的性能修复。

## 板级用户态默认配置

`userspace/rootfs/etc/modprobe.d/w202-gpu-profile.conf` 选择 845 MHz 配置。
这是板级 GPU 运行配置，由发行版集成时安装；驱动未提供该覆盖时仍使用 655.2 MHz 默认值。
频率、电压和热保护由驱动实现，桌面控件不维护 OPP 表。`.ko` 随独立内核模块包部署。
