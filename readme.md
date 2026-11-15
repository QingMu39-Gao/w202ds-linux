# W202DS Linux 6.18.35

W202DS 的 Linux 内核、设备树、板级驱动和用户态适配。启动固件由独立 boot 工程提供，发行版由 REIMUKS 组装。

| 目录 | 内容 |
| --- | --- |
| `kernel/` | Linux stable 子模块及 W202DS 内核适配。 |
| `Devices/` | 按硬件维护驱动、共享接口头、DTSI、固件输入和用户态配套。 |
| `devicetree/` | 板级设备树入口。 |
| `config/` | desktop/debug 内核配置模型及片段。 |
| `scripts/` | 内核及模块的构建、导入和导出工具。 |

构建见 [BUILD.md](BUILD.md)，用户态布局和安装见 [Devices/USERSPACE.md](Devices/USERSPACE.md)，硬件实现及验收范围见 [Devices/README.md](Devices/README.md)。

每个硬件目录用 `source/` 维护内核实现，用 `userspace/` 维护用户态程序、库适配和服务。基础 systemd 单元放在组件的 `userspace/rootfs/usr/lib/systemd/system/`，脚本放在 `userspace/rootfs/usr/libexec/`。构建只暂存文件，发行版负责依赖安装、配置覆盖和服务启用。

源码、构建入口、服务模板及测试随仓库发布。编译输出、第三方实验检出缓存、设备私有 NV、账号和完整 rootfs 不进入源码交付。GPU/PanVK 和 VPU 的第三方实验范围见各组件说明，不能将源码已就位视为硬件验收通过。

构建输出统一放在仓库级 `output/` 或显式指定的外部目录；不在设备源码目录生成模块包、运行证据或 `SOURCES*.json`。历史集成包和研究资料独立保存。

## 内核 Git

`kernel/` 基于官方 `v6.18.35`，主仓库用子模块固定适配提交；内核版本沿用 Kbuild 的 Git 提交标识。查看适配差异：

```sh
git -C kernel diff --stat v6.18.35..HEAD
git -C kernel log --oneline v6.18.35..HEAD
git submodule status kernel
```

初始化和恢复方式见 [KERNEL-GIT.md](KERNEL-GIT.md)。复制工程时保留 `kernel/` 与 `Devices/` 的相对位置及内部链接。

## 当前板级集成状态

`Devices/` 同时维护板级内核模块和各设备的 `userspace/` 实现；发行版桌面策略由消费工程定义。当前改动包括 GPU DVFS、Type-C/充电/OTG、显示与触控旋转、霍尔标准输入、基带有序初始化，以及原生 V4L2 Request 和 VA-API 构建接入。旧 Android VPU worker 已移除并禁用；基带所需的独立运行环境不属于该删除范围。

桌面配置启用挂起基础能力、启动标志和 printk 时间；配置存在不保证设备能可靠休眠或出现企鹅。基带完整注册、浏览器硬件视频和显示边界仍需配套系统实机验收。当前 DRM 最小 framebuffer 宽高仍为 1200，会拒绝宽度 64 的小缓冲区；这是已定位、尚未修复的问题，不能把本提交作为该问题的修复。

可复现构建应固定内核子模块、公共显示协议子模块、工具链版本、配置片段和 Kbuild 构建身份；按 `BUILD.md` 从固定 `output/` 入口生成。比较 Image、DTB、模块内容及运行包清单，不混用不同内核 release 的模块。测试源码和文档包含在本仓库，构建输出不提交。
