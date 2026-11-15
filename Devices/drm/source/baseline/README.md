# W202DS 固定内屏 DRM/KMS

本项目负责 DPU/DSI/DSC、内屏、DRM fbdev 与显示状态接口。显示接管通过 Linux 通用 aperture API 完成。

`defer_modeset=1` 为默认值。probe 只读取设备树/硬件身份、获取资源并注册 KMS；不启用显示时钟、不接管 DPU IRQ、不重置 DPU/DSI/面板、不改背光、不创建 fbdev，也不修改扫描输出。所有 atomic 提交及手动 DSI 启动请求在显示就绪前返回错误。已有启动画面继续由原持有者维护。

外层用户态在适当阶段向 `/sys/devices/platform/w202-r6p0-lk-dsi/display_enable` 写1，才启用时钟和中断并执行显示初始化。若 DT 提供有效的 `pocketjs,boot-display` 区域合同，创建原生 DRM 黑框 console；普通启动则创建全屏 fbdev。写入同步返回初始化结果，请求不能写0回退。`defer_modeset=0` 仅供明确选择立即启用行为。

启用流程先分配 RGB565 framebuffer、持有 DPU/DSI 时钟，再通过 Linux 通用 aperture API 移除继承的 framebuffer 所有者，复制旧画面并初始化显示。`pj4edk-boot-display` 的 aperture remove 回调同步停止写屏后释放其时钟引用。首次 KMS 提交锁存成功后开启背光，异步 console 只改黑框内变化的字形，框外保留原图。DPU 连续扫描，不逐行 atomic commit。

`boot_console_status` 提供接管、帧数、字形数及 CPU 绘制计时；向 `boot_console` 写0停止黑框 writer 并启用全屏 fbdev。写入 `preserve` 则只停 writer，保留画面且不创建全屏 fbdev，供桌面 compositor 随后提交。引导 framebuffer 保留至 DRM 注销或重启，避免释放仍可能扫描的内存。登录阶段由外层集成判定，驱动提供显示状态与控制接口。

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
  6.18.35/Devices/drm/source/baseline/tests/boot-display-contract.c \
  -o output/tests/boot-display-contract
output/tests/boot-display-contract
```

该测试覆盖缺失、长度、20 项非法字段、越界和 EFI/旧 LK 两种有效布局；真机交接仍需另验。

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
当前仅接受完整画面，无裁剪、缩放或目标偏移；硬件 pitch 与内存范围由驱动校验。
桌面不需要额外安装补偿，用户方向选择仍通过 KDE 标准输出设置完成。

KDE 通过标准 KScreen/KWin 输出配置决定方向。暴露旋转属性不代表 KWin 的
所有合成帧都会改用 DPU 旋转：合成路径仍可由 GPU 变换，硬件卸载取决于
直接扫描输出、缓冲区导入及其格式是否满足要求。本驱动尚未支持任意外部
GPU DMA-BUF 导入，不能据此宣称桌面旋转已全面卸载至 DPU。

早期可选启动画面的像素布局仍按启动协议解释；这与桌面主平面的默认旋转
是独立接口，不用于推导或强制 KDE 输出方向。四角度的硬件验收需要在匹配
启动模块上分别提交并检查寄存器与画面，编译成功不等于真机验收完成。
