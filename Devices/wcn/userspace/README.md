# W202DS WCN 用户态

Wi-Fi、蓝牙、GNSS 和共芯片恢复的硬件适配集中在这里。

- `bt-init/`：SC2355/Marlin3 厂商初始化程序，随后交给标准 BlueZ。
- `rootfs/`：蓝牙初始化、显式 WCN 恢复工具及基础单元。
- [gnss/](gnss/README.md)：SIPC/NMEA 工具，独立安装，不自动请求 GNSS 上电。
- `tests/`：PTY/H4 协议模拟，不访问实际无线接口。

初始化程序基于 [mu300-linux](https://github.com/dikeckaan/mu300-linux) 的 `833616a6c03a`，保留 MIT 版权。配置输入来自 `../firmware/bt_configure_*.ini`，不是可执行固件。

```sh
make CC=cc OUT=/path/to/native-tests test
make OUT=/path/to/arm64 DESTDIR=/path/to/staging install
make DESTDIR=/path/to/staging install-gnss
```

目标系统需要 BlueZ/btattach、kmod、systemd、util-linux 和匹配的 WCN/BT 模块。设备地址优先来自本设备状态或 prodnv，无有效地址时由 machine-id 派生，不复制其他设备的地址或 NV。

恢复工具保留无线接口原状态并限制重试次数；调用会短暂中断 Wi-Fi/蓝牙。是否因故障自动调用、挂起恢复时如何协调服务以及热点行为，均由系统集成定义。这里不安装自动触发的 udev 或休眠钩子。策略测试由相应消费者维护。

安装仅暂存文件；服务启用和网络连接由发行版负责。当前没有在此补造完整的 `w202-wifi.service`；蓝牙单元的排序不代表依赖 Wi-Fi 已联网。

上层调用约定见 [标准接口](../../INTERFACES.md)。
