# W202DS Hall 用户态

此处维护霍尔传感器诊断和可配置的标准 input 开关桥接。内核模块在 `../source/w202-hall-input/`，默认通过 sysfs 提供 `raw_level`、`active`、`irq_count`；`active=1` 表示磁感应有效，不直接定义合盖、电源或屏幕行为。

```sh
make diagnostics OUT=/path/to/output
cat /sys/bus/platform/drivers/w202ds-hall/*/active
```

`tests/gpio-events/` 使用 GPIO-v2，仅供显式诊断；不能与占用同一 GPIO 的驱动同时运行。正常运行优先读取模块状态，`raw_level` 和 `active` 支持 `poll(POLLPRI)`，无需定时轮询 GPIO。

输入语义由系统集成选择：模块参数 `switch_code=-1` 默认不创建 input 开关。
`source/w202-hall-input.c` 编译出的 `w202-hall-input` 将 `--source` 指定的原始状态映射为
`--switch-code` 指定的标准 input 开关，依赖 libc 和 uinput，不需要 Python 运行时。
`--active-delay-ms` 默认 0；设为正数时，有效状态稳定到期后才发布，
无效状态立即发布并取消待处理的有效状态。重复有效采样不延后截止时间。

消费者可选择 `SW_LID`（0）向标准 input 接口报告盖子状态，并自行设置去抖时间。
模块保持原始输出，不同时再生成第二个盖子设备。是否息屏及恢复显示由系统集成定义，此组件不安装场景策略。

运行 `python3 -m unittest discover -s tests` 验证去抖状态机。
硬件开合、上层息屏和显示恢复需要分别验收。

上层调用约定见 [标准接口](../../INTERFACES.md)。
