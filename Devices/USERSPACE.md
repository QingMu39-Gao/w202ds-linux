# W202DS 用户态

用户空间源码随硬件放在 `Devices/<设备>/userspace/`，与同设备的 `source/`、`devicetree/` 和 `firmware/` 并列。USB 归 `usb3/userspace/`。用户态构建不触发内核编译。上层调用方式见 [标准接口](INTERFACES.md)。

| 组件 | 内容 |
| --- | --- |
| [audio](audio/userspace/README.md) | 扬声器初始化脚本与音频服务。 |
| [charging](charging/userspace/README.md) | 双路充电初始化和标准充电模式控制，默认行为由发行版选择。 |
| [drm](drm/userspace/README.md) | 原生显示初始化与输入诊断。 |
| [gpu](gpu/userspace/README.md) | EGL/GLES 诊断和可选 Mesa/PanVK 构建资料。 |
| [hall](hall/userspace/README.md) | 霍尔状态读取约定与 GPIO 诊断。 |
| [keyboard](keyboard/userspace/README.md) | UART attach 与协议测试。 |
| [modem](modem/userspace/README.md) | AT、蜂窝运行桥接和 ModemManager 适配。 |
| [sensorhub](sensorhub/userspace/README.md) | Sensor Hub attach 服务及 udev 方向配置。 |
| [touchscreen](touchscreen/userspace/README.md) | Wayland 触摸诊断。 |
| [usb3](usb3/userspace/README.md) | ACM/NCM USB gadget 脚本与基础服务。 |
| [vpu](vpu/userspace/README.md) | 解码接口、码流和范围检查工具。 |
| [wcn](wcn/userspace/README.md) | 蓝牙初始化、显式 WCN 恢复与 GNSS。 |

## 构建和安装

需要 GNU make、AArch64 GCC/binutils、Python 3 和常见 POSIX 构建工具。从仓库根执行：

```sh
make -C Devices -f userspace.mk OUT="$PWD/output/userspace/arm64" all
make -C Devices -f userspace.mk OUT="$PWD/output/userspace/arm64" \
  DESTDIR="$PWD/output/userspace/rootfs" install
make -C Devices -f userspace.mk OUT="$PWD/output/userspace/tests" check
```

默认构建键盘、基带和蓝牙工具，并安装这些组件及 audio、charging、drm、sensorhub、usb3 的运行模板。可用 `COMPONENTS="audio usb3"` 选择子集；`CROSS_COMPILE`、`CC`、`CPPFLAGS`、`CFLAGS`、`LDFLAGS` 和 `OUT` 可覆盖。`check` 使用 `HOSTCC` 构建主机测试，输出与目标程序分开，不访问硬件。`diagnostics` 仅编译 DRM、Hall、UART 诊断，不运行或默认安装它们。

GPU、VPU、ModemManager 的第三方库和 Bionic ABI 构建有独立依赖，见各组件说明；聚合入口不隐式下载或构建这些依赖。

统一入口为 `Devices/userspace.mk`。直接在组件目录运行 make 也可以，默认输出到仓库级 `output/userspace/<设备>/`；显式 `OUT` 应选择独立输出目录。模块包、运行证据、研究参考和本地来源清单不放回设备源码目录。

## systemd 与发行版的分工

组件内的安装模板采用目标文件系统布局：

```text
Devices/<设备>/userspace/
├── Makefile
├── source/ 或组件自己的源码目录
├── rootfs/
│   └── usr/
│       ├── lib/systemd/system/  # 基础单元
│       ├── lib/udev/rules.d/    # 设备规则
│       └── libexec/            # 运行脚本
└── tests/
```

`make install DESTDIR=...` 只生成离线暂存树，不运行 systemctl、modprobe 或设备脚本，也不创建服务启用链接。系统集成从组件导出的暂存树读取模板，选择服务、维护登录与网络覆盖项并创建启用链接；本项目不要求消费者采用特定目录布局。

硬件默认状态不等于系统行为。霍尔模块默认只提供磁感应状态；消费者选择是否映射成 `SW_LID`。登录、合盖、空闲、电源键、虚拟键盘、热点和自动恢复触发均由系统集成定义。组件仅提供机制、标准接口和基础单元，不保存场景策略或历史构建记录。
