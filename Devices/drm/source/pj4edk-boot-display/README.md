# pj4edk-boot-display

DRM 前的可选启动日志实现，源码属于 Linux；没有导入 EFI、LK 或 PocketJS 运行时。
`CONFIG_PJ4EDK_BOOT_DISPLAY=y` 时内建，真实启用还要求 `/chosen/pocketjs,boot-display`
通过长度、格式、几何和已保留显存校验。缺少或无效描述时不注册此 fbdev。

共享 PJ4EDK2/KernelBoot fbcon helper 在日志框内注册标准 fbdev。字体、字符、滚动、
旋转和企鹅均由 Linux fbcon 处理，不再注册私有 printk console。damage 回调只排队，
workqueue 清理脏行缓存并提交继承的 DPU 帧。硬件忙时延后重试，没有固定每行延迟。
fbdev 在 device init 阶段注册，早于 logo initdata 释放；需要内建 LOGO 配置和 tty1 console。
持有显示所需的 6 个时钟引用，
防止共享 VPU 电源管理关掉还在扫描的时钟。

DRM 使用标准 aperture API 移除旧显示 owner；回调先同步注销 fbcon，再等待缓存/提交工作退出后释放时钟。
DRM 必须先取得自己的时钟引用。本组件不控制 DRM 的私有入口。

本地板级策略消费原生横屏版本 2 描述。UEFI、ESP EFI、内核与 DRM 模块必须配套更新；
旧竖屏 RGB565 描述不会作为新显示布局使用。

## 规范与自定义接口

当前启动器是 EFI 应用，退出 Boot Services 后按 ARM64 Linux 原生协议进入 Image。
内核配置关闭 `CONFIG_EFI`，不接收 EFI Runtime Services。ARM64 的 DTB 地址、
CPU/MMU/缓存状态、内存与 initramfs 布局属于启动协议；黑框继承是本项目可选扩展。

显示描述在 DTB `/chosen/pocketjs,boot-display`，80 字节、20 个大端 u32。
EFI 内部发布的 BootDisplay 配置表使用另一份 36 字节矩形描述，启动器读取它，
核对 GOP/显示寄存器/保留内存后转换成这 80 字节；Linux 不使用 EFI 配置表中的原指针。

| 索引 | 当前约定 |
| --- | --- |
| 0～1 | 固定协议标记 `2, 2`，当前消费者严格校验 |
| 2～3 | 64 位显存物理地址，当前 `0xb0000000` |
| 4～5 | 保留区大小 `0x9e4000`；有效画面大小 9,216,000 字节 |
| 6～9 | 内存宽 1920、高 1200、stride 7680 字节、格式编号 2（字节顺序 BGRX，XRGB8888） |
| 10～11 | 逻辑画布；当前 EFI 为 1920×1200 |
| 12～15 | 日志框 x、y、宽、高 |
| 16～19 | 内存旋转 0、DPU 源旋转 90、每像素 4 字节、固定标记 1 |

画面像素保留在 RAM，DPU/DSI/面板维持交接前状态；这些不是由 80 字节保存的寄存器快照。
`w202,uefi-profile` 和 `w202,uefi-load-us` 是另外两项自定义启动信息，不由本组件消费。

原生 DRM 的可选框内 console 也读同一份 DT 描述，但没有本组件的私有符号依赖。
描述缺失或格式错误时走普通全屏 fbdev；有效时继承画面，使用自己的 DRM buffer 继续输出。
旧 W202DS 发送端曾把 BGRX 标为格式 3；灰色文字没有暴露通道错误。fbcon 的彩色
企鹅要求正确的通道定义，因此本次 EFI 发送端与内核接收端统一使用公共协议格式 2，
Linux EFI、内核和 DRM 模块需配套更新。协议版本仍是 2；格式 3 保持公共 RGBX 含义。
标准协议分别见 [ARM64 Linux booting](https://docs.kernel.org/arch/arm64/booting.html)、
[UEFI System Table](https://uefi.org/specs/UEFI/2.10/04_EFI_System_Table.html)、
[Linux EFI](https://docs.kernel.org/arch/arm/uefi.html)。

## 验证与 TODO

完整内核构建检查内建接入；DRM 的 `source/baseline/tests/boot-display-contract.c`
覆盖缺失、损坏、越界和原生横屏布局与旧布局拒绝。测试说明见该目录 README。
分离后重新上机验证时钟交接、显示接管和异常退出；编译通过不等于真机通过。

## 公共层归属

本目录继续作为唯一 W202DS 启动显示模块。内部的 [common](common/readme.md)
固定 pj4edk2 `kernel-boot-display` 分支；远程通过 gitlink 固定提交，本地为相对符号链接。
该公共依赖不在顶层 components 下另建并行驱动。

公共协议与实现见 [common/readme.md](common/readme.md)。硬件接受策略、显存授权、
缓存、DPU、时钟和所有权留在本模块。原生 DRM 的框内日志复用同一套 fbcon helper，
不依赖本组件的私有符号。窗口几何、标准 framebuffer 操作和文字策略由公共层及 fbcon 负责。
