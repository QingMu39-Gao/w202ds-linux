# W202DS 触摸用户态

`tests/wayland-m21/` 保存 Wayland 触摸检查和压力工具，需显式构建和运行，不是系统触摸输入需要的常驻服务。

内核驱动及固件由 `../` 维护。板级校准和 udev 配置可归本组件，libinput 与 Wayland compositor 由发行版提供。

上层调用约定见 [标准接口](../../INTERFACES.md)。
