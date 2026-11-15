# 上层标准接口

用户态配套放在各设备的 `userspace/`。应用优先使用 Linux 和桌面已有的接口；硬件初始化脚本由系统集成调用，应用无需按具体寄存器或 GPIO 控制硬件。

| 设备 | 内核接口 | 上层调用 | 当前边界 |
| --- | --- | --- | --- |
| [Hall](hall/userspace/README.md) | sysfs `raw_level/active`，可选 evdev 映射 | 发行版集成层 映射为 `SW_LID` 后使用 libinput、logind/UPower D-Bus | 驱动默认只报告霍尔状态；初始状态和双边沿 IRQ 保留，输入含义及动作由集成方定义。 |
| [键盘](keyboard/userspace/README.md) | evdev、`SW_TABLET_MODE` | libinput、KWin/Qt 输入与平板模式 | UART attach 是底层准备，桌面使用 input 事件。 |
| [触摸](touchscreen/userspace/README.md) | Linux 多点触摸 input | libinput、Wayland/Qt 输入 | 具体内核的触摸和固件重载仍须验证。 |
| [显示](drm/userspace/README.md) | DRM/KMS、backlight | libdrm、Wayland compositor、桌面亮度管理 | 板级初始化保留硬件控制；挂起恢复仍有已知问题。 |
| [GPU](gpu/userspace/README.md) | DRM render node | Mesa EGL/OpenGL ES | Panfrost 路径；Vulkan 实验缓存不代表支持。 |
| [音频](audio/userspace/README.md) | ALSA PCM/control | alsa-lib、PipeWire/WirePlumber | 已有扬声器初始化；录音等能力独立验收。 |
| [传感器](sensorhub/userspace/README.md) | IIO | iio-sensor-proxy 的 `net.hadess.SensorProxy` | 方向/环境光供桌面消费，其他通道可读 IIO；策略由桌面决定。 |
| [WCN](wcn/userspace/README.md) | cfg80211/nl80211、rfkill、Bluetooth HCI | NetworkManager、BlueZ D-Bus | 厂商初始化留在组件内；应用走网络和蓝牙服务。 |
| [基带](modem/userspace/README.md) | SIPC AT/数据设备 | ModemManager、NetworkManager D-Bus | 需要 W202DS 插件、匹配固件及本设备私有输入；原始 AT 为底层工具。 |
| [USB](usb3/userspace/README.md) | configfs gadget、`ttyGS0`、NCM 网卡 | 标准串口、socket/网络管理器 | `w202-usb-role` 通过 Type-C 请求数据/供电角色；地址、登录和远程服务由发行版配置。 |
| [VPU](vpu/userspace/README.md) | V4L2/Media Request、DMA-BUF | libva 桥和支持的解码应用 | 现有标准路线仍属适配范围，按实际格式能力协商；Bionic worker 独立维护。 |
| [电池/充电](charging/userspace/README.md) | `power_supply` sysfs | UPower D-Bus、桌面电量界面；主路 `charge_behaviour` | 自动充电、停充供电与真实电流分别可读；PD/PPS 成功与恢复需实测，USB host/source 路径已接入，待硬件验收。 |

## Hall：KDE 与普通应用

霍尔驱动默认只提供 `raw_level`、`active` 和 `irq_count`；`active=1` 表示磁感应有效，
不预设电源或合盖含义，`active` 和 `raw_level` 支持 `poll(POLLPRI)`。
`switch_code=-1` 默认不创建 input 开关。集成方可选择原生映射，或使用
`hall/userspace/source/w202-hall-input.c` 选择编码、设备名称和有效状态延迟。
发行版集成层 desktop/kde 使用后者：原生映射关闭，用户态将稳定两秒的有效状态映射为
标准 `EV_SW/SW_LID`，无效状态立即映射为开盖。udev 分类为标准 input switch 和
power-switch；应用不应固定 event 节点编号，也不应绕过用户态去抖读取 raw 状态做桌面动作。

启用上述 发行版集成层 映射且 logind 已发现 input 设备后，可读取标准 D-Bus 属性：

```sh
busctl --system get-property org.freedesktop.login1 \
  /org/freedesktop/login1 org.freedesktop.login1.Manager LidClosed
```

返回 `b true` 表示合盖。KDE/Qt 或其他 D-Bus 应用可对同一对象使用 `org.freedesktop.DBus.Properties.Get` 读取 `LidClosed`，订阅 `PropertiesChanged` 跟踪变化。先订阅变化，再读取初始状态，避免首次读取与订阅之间漏掉事件。

安装 UPower 时也可读取：

```sh
busctl --system get-property org.freedesktop.UPower \
  /org/freedesktop/UPower org.freedesktop.UPower LidIsClosed
busctl --system get-property org.freedesktop.UPower \
  /org/freedesktop/UPower org.freedesktop.UPower LidIsPresent
```

底层程序使用 evdev 的 `EVIOCGSW` 获取当前开关位，读取 `EV_SW/SW_LID` 事件并按 `SYN_REPORT` 成帧；遇到 `SYN_DROPPED` 时丢弃至下一个 `SYN_REPORT`，再用 ioctl 重读状态。桌面输入程序也可使用 libinput。input 节点访问遵循 seat/会话权限；普通状态查询优先使用系统 D-Bus。

合盖状态、是否息屏、是否挂起是不同层的职责。KDE 的具体动作由 PowerDevil/桌面配置选择，Server 可选择保持运行。发行版集成层 的 `desktop/server` 使用自己的 logind 忽略策略以保持合盖运行；`desktop/kde` 交给 KDE/PowerDevil。板级目录不保存这些策略。应用读取状态不会修改系统动作。新的可配置映射经过离线构建后仍需实机验收。

## 接口约定

每个组件说明自己的标准接口、依赖、权限和验收范围。已有标准接口由其系统服务维护；板级脚本只补足硬件准备。私有诊断接口明确标记为诊断，不作为上层稳定 API。

参考：[Linux input 事件规范](https://www.kernel.org/doc/html/latest/input/event-codes.html)、[logind D-Bus](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html)、[UPower D-Bus](https://upower.freedesktop.org/docs/UPower.html)。
