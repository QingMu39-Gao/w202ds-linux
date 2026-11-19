# W202DS 固定内屏 DRM/KMS

本项目负责 DPU/DSI/DSC、内屏、DRM fbdev 与显示状态接口。显示接管通过 Linux 通用 aperture API 完成。

`defer_modeset=1` 为默认值。probe 只读取设备树/硬件身份、获取资源并注册 KMS；不启用显示时钟、不接管 DPU IRQ、不重置 DPU/DSI/面板、不改背光、不创建 fbdev，也不修改扫描输出。所有 atomic 提交及手动 DSI 启动请求在显示就绪前返回错误。已有启动画面继续由原持有者维护。

外层用户态在适当阶段向 `/sys/devices/platform/w202-r6p0-lk-dsi/display_enable` 写1，才启用时钟和中断并执行显示初始化。若 DT 提供有效的 `pocketjs,boot-display` 区域合同，创建原生 DRM 黑框 console；普通启动则创建全屏 fbdev。写入同步返回初始化结果，请求不能写0回退。`defer_modeset=0` 仅供明确选择立即启用行为。

启用流程先分配 XRGB8888 framebuffer、持有 DPU/DSI 时钟，再通过 Linux 通用 aperture API 移除继承的 framebuffer 所有者，复制旧画面并初始化显示。`pj4edk-boot-display` 的 aperture remove 回调同步注销旧 fbcon、等待缓存/提交 worker 后释放时钟引用。首次 KMS 提交锁存成功后开启背光。早期与原生框内日志均复用 PJ4EDK2/KernelBoot 的 fbdev/fbcon helper；字体、滚动、旋转和启动企鹅由 Linux fbcon 处理，框外保留原图。DPU 连续扫描，不逐行 atomic commit。

`boot_console_status` 提供接管、fbcon 活跃状态及 pitch；向 `boot_console` 写0注销框内 fbcon 并启用全屏 fbdev。写入 `preserve` 则只注销框内 fbcon，保留画面且不创建全屏 fbdev，供桌面 compositor 随后提交。引导 framebuffer 保留至 DRM 注销或重启，避免释放仍可能扫描的内存。登录阶段由外层集成判定，驱动提供显示状态与控制接口。以下历史自定义绘制器的计时不能代表当前 fbcon 路径。

2026-11-05 首次真机短测确认：约4.58秒原生黑框 active，旧 writer 停止更新；DSI 60 Hz、持续 vsync、无 DPU 错误或锁存超时，首次锁存约16.56 ms。首帧 CPU 绘制约1.47 ms。随后 17:00 记录确认该模块随完整 Server 常驻运行，登录 TTY 和霍尔息屏策略已接通。长期运行与 KDE 仍待验收，短测统计不等于全部面板观感验收。

2026-11-06 KDE 标准版已使用该接口启动：内核捕获有效黑框描述并完成 DRM 提交，SDDM 查询为 1920×1200 / Panfrost。KDE 开合屏幕恢复出现 vblank 等待超时，且触控异常复位后有 framebuffer 错误，尚未通过稳定性验收。

提交收尾等待本次提交的 `flip_done`，然后清理旧 framebuffer。默认的
`drm_atomic_helper_commit_tail` 在 `hw_done` 后另等一次 vblank；本驱动在层锁存
完成后已发送 pageflip 事件，此时后续 DPMS 提交可能先停掉扫描，使前一笔提交
等不到新的 vblank。专用收尾保留原有 modeset/plane 顺序，使用 DRM 的
`drm_atomic_helper_wait_for_flip_done`，同时兼容 `flip_after_latch=0` 的延后事件
和 disable 的合成事件。此修正针对这处等待竞争；完整息屏冻结及触控恢复仍需真机验收。

2026-11-08 的现有模块短测中，KDE 的正常 DPMS off/on 两轮都恢复到
`linux-active`，vsync 继续增长，触控 `quiet=0 irq=1 error=0`。未在冻结现场复测，
也未以这两轮结果确认新模块或真实手指输入已通过验收。现场恢复应先请求 KDE
正常 DPMS on；必要时再进行一轮 off/on，让显示和共享触控按既有通知顺序唤醒。
`dsi_control` 的 `start` 只用于首次初始化，初始化后的 standby 不能靠它恢复。

在 Linux 仓库根目录，本地独立构建入口是 `make -C Devices KDIR=... OUT=... w202ds-drm-driver`；研究项目统一入口为 `tools/bsp-build modules`，参数相同。`w202ds-drm-snapshot` 是单独的只读诊断目标。驱动保留原有不提供 module_exit 的行为，运行中的显示驱动不能热替换；候选版本应在下一次匹配的引导中测试。

## 可选启动显示描述

仅当 `/chosen/pocketjs,boot-display` 完整且布局有效时，使用继承画面和框内日志。
缺失、长度错误、未知版本、地址/格式/旋转不符、矩形越界均回到普通 fbdev；不因可选描述损坏阻止 DRM 激活。
资源分配或真正的硬件初始化失败仍返回错误。协议内容见 `../pj4edk-boot-display/README.md`。

构建主机测试（无硬件访问）：

```sh
mkdir -p output/tests
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  Devices/drm/source/baseline/tests/boot-display-contract.c \
  -o output/tests/boot-display-contract
output/tests/boot-display-contract
```

该测试覆盖缺失、长度、20 项非法字段、越界和 EFI 横屏布局和旧 LK 原生 DRM 布局拒绝；真机交接仍需另验。

提交与 DPMS 的主机回归测试：

```sh
python3 Devices/drm/source/baseline/tests/atomic-dpms.py \
  --kernel /path/to/matched/linux --out output/atomic-dpms-test
```

测试提取该内核的真实提交/等待函数和本驱动的提交收尾，在模拟 CRTC/IRQ 上
安排 DPMS 停扫描的时机：旧流程复现超时，新流程等待已完成的锁存事件；也覆盖
延后事件、disable、持续 inactive、wake 和正常扫描。编译启用 ASan/UBSan，
可通过 `--cc` 指定主机编译器及其链接参数。这不能替代真机硬件和桌面验收。

## DRM 旋转与用户态策略

主平面提供标准 DRM `rotation` 位掩码属性，初始值为 `rotate-0`，支持
0°、90°、180°、270°，不提供反射。默认安装补偿 `mount_rotation=270`，
在驱动内部归一化为逻辑 0°；加载时可选 90°，运行中不可修改。
触控采用相同安装坐标基准，默认内部顺时针旋转 270°。

KMS 暴露的安装后模式为 1920×1200，connector panel orientation 为 Normal。
电气时序仍为面板原生 1200×1920，驱动在模式配置时转换，刷新率不变。
DRM 请求按规范逆时针解释，转换为 DPU 顺时针编码后与安装补偿合成。
默认逻辑 0°使用 DPU 编码 1，触控转换为其逆变换。
0°/180°请求使用 1920×1200 framebuffer，90°/270°使用 1200×1920。
主平面接受完整画面；overlay 和 cursor 共享安装补偿，支持整数裁剪和偏移，无缩放。
桌面不需要额外安装补偿，用户方向选择仍通过 KDE 标准输出设置完成。

全局 framebuffer 尺寸范围为 0–8192，与 r6p0 参考驱动一致；它也用于
ADDFB2 的小型探测缓冲，不能用内屏尺寸作为最小值。实际扫描尺寸仍由
atomic check 独立限制。主平面接受 RGB565、XRGB8888 和 ARGB8888；后两者
使用相同硬件格式，当前主平面采用常量不透明 alpha，不表示新增透明叠加能力。

KDE 通过标准 KScreen/KWin 输出配置决定方向。暴露旋转属性不代表 KWin 的
所有合成帧都会改用 DPU 旋转：合成路径仍可由 GPU 变换，硬件卸载取决于
直接扫描输出、缓冲区导入及其格式是否满足要求。本驱动尚未支持任意外部
GPU DMA-BUF 导入，不能据此宣称桌面旋转已全面卸载至 DPU。

早期可选启动画面的像素布局仍按启动协议解释；这与桌面主平面的默认旋转
是独立接口，不用于推导或强制 KDE 输出方向。四角度的硬件验收需要在匹配
启动模块上分别提交并检查寄存器与画面，编译成功不等于真机验收完成。

## DPU 加速能力与尚未接入的接口

W205DS-ESIM Android 13 / Linux 5.4.276 的
[厂商开源包](https://download.ztedevices.com/device/global/support/opensource/2/20250611_01/W205DS-ESIM_Android13_kernel%285.4.276%29.tar.gz)
中，UMS9620 参考代码通过
`sprd,qogirn6pro-dpu` 选择 `dpu_r6p0_core_ops`。厂商的
`drivers/gpu/drm/sprd/dpu/dpu_r6p0.c` 提供以下实现；这些能力不等于本驱动已经支持。

| 能力 | 厂商参考实现 | 本驱动 |
| --- | --- | --- |
| 多图层与透明叠加 | `dpu_capability()` 暴露 6 层；`dpu_img_ctrl()` 支持 coverage/premultiplied alpha | 全屏 primary、一个 overlay 和一个 ARGB8888 cursor；辅助层支持预乘 alpha |
| 裁剪与缩放 | `dpu_layer()` 设置 crop、src/dst size 和缩放使能 | 辅助层支持裁剪和偏移，无缩放 |
| 旋转与镜像 | 0/90/180/270°及镜像 | 主层支持四个直角旋转，辅助层共享安装补偿，无镜像 |
| YUV 与压缩格式 | YUV→RGB、XFBC | RGB565/XRGB8888/ARGB8888 线性缓冲 |
| 外部 DMA-BUF | PRIME 导入后由 VAU 映射散页 | VAU translation 关闭，拒绝外部分配的缓冲 |

硬件存在 8 组层寄存器，参考驱动仅向用户态提供 6 层，另有安全层及
writeback 路径；不能直接把全部寄存器暴露为通用 overlay。参考驱动还将这些
层全部标为 primary，并使用 Android/ION 相关接口，移植时应改为标准的
primary/cursor/overlay 和 DMA-BUF 同步、映射模型。

UMS9620 另有独立的 GSP r9p0 和 GSP lite r4p0，用于内存到内存的二维处理。
厂商通过专用 GSP ioctl 提交任务；本驱动未接入，不能由 DPU 扫描输出或
GL/GLES/Vulkan 的启用状态推导为已经可用。DPU 的 XFBC 也不能直接当作
GPU 的 ARM AFBC modifier 使用。

待接入 VAU、外部 DMA-BUF 导入及缩放。[KWin 6.3.6 的 DRM 输出路径](https://raw.githubusercontent.com/KDE/kwin/v6.3.6/src/backends/drm/drm_pipeline.cpp)使用 primary 和 cursor，
新增 overlay 不会自动将任意桌面窗口分配给硬件层。GSP 还需要独立的驱动、
同步和用户态提交接口。DPU 核心时钟已配置为参考表的 614.4 MHz 最高档，
新增这些接口仍需分别验证正确性、延迟和功耗。

## Retaining an already running display

With `adopt_display=1` (default), initial activation checks the DPU framebuffer,
timing, VAU bypass, DSC profile, DSI video mode, reset GPIO, PHY lock and clock
rates. Two fresh VSYNC interrupts must arrive within 100 ms. Compatible scanout
is retained without blanking the backlight, resetting DPU/DSI/DPHY/the panel or
resending panel initialization commands. Both 60 Hz and 90 Hz are accepted; the
inherited mode remains preferred for the first native framebuffer commit.

An incompatible or stopped display falls back to the complete initialization
path and records the reason. `adopt_display=0` explicitly selects that path.
`dsi_control` reports `adopted`, `host_resets`, `panel_resets` and `soft_resets`,
in addition to the panel command count. These are Linux-side operations since
driver load; they do not claim to count actions performed earlier by firmware.
The initial retained-console and later native-DRM handoffs are distinct: this
check affects native DRM acquisition, while the early fbcon adapter already
uses inherited scanout and sends no DSI reset or panel commands.

## 内屏背光

通过 DRM connector 的 sysfs 目录发现同一个 PWM 背光设备；udev 保留原有标识以恢复保存的亮度。
