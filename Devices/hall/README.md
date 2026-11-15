# W202DS Hall 传感器

内核适配位于 `source/w202-hall-input/`，设备树位于 `devicetree/`，构建目标为 `w202ds-hall-driver`。用户态诊断见 [userspace](userspace/README.md)。

硬件层只定义 GPIO148 的磁感应状态及已验证的低有效极性。默认输出 `raw_level`、`active` 和 IRQ 计数；不固定为电源或合盖设备。集成方可以用模块参数选择标准 input 开关编码，具体解释和动作留给发行版。

GPIO 开合极性和中断此前已在实机验证。新的可配置输入映射需重新构建模块并实机验证；不能将旧版本的合盖/亮屏验收直接视为新版本通过。显示恢复与传感器状态读取独立验收。
