# W202DS 显示用户态

- `rootfs/`：`w202-display-prepare` 与 `w202-bsp.service`，验证模块版本、等待并启用原生 DRM 显示。
- `tests/test-input.c`：需显式运行的输入诊断。

```sh
make OUT=/path/to/output diagnostics
make DESTDIR=/path/to/staging install
```

安装只暂存原生显示准备脚本和基础单元，不启用服务。全屏登录、普通日志显示、空闲息屏、合盖和电源键动作由系统集成定义。消费者通过 DRM/KMS 和 backlight 标准接口定义显示策略，此组件不维护桌面或登录场景。

上层调用约定见 [标准接口](../../INTERFACES.md)。
