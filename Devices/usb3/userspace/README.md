# W202DS USB 用户态

`rootfs/` 提供 ACM 串口、NCM 网络复合 gadget 的脚本和 `w202-usb-gadget.service`。脚本优先保留 initramfs 已绑定的连接；未绑定时等待 UDC 并创建相同的 gadget。

```sh
make stage CC=aarch64-linux-gnu-gcc
make install DESTDIR=/path/to/staging
```

安装仅暂存文件，不绑定主机 USB 或启用服务。目标系统需要 configfs、DWC3/gadget 功能、iproute2 和 systemd。

IP 地址由发行版的 `/etc/default/w202-usb-gadget` 中 `USB_ADDRESS` 选择；未配置时只拉起接口。networkd/NetworkManager 配置、串口 getty 和服务启用由发行版管理。发行版集成层 的板型模板提供与早期 initramfs 一致的网络策略。

上层调用约定见 [标准接口](../../INTERFACES.md)。

## 角色切换

需要匹配的双角色内核、Type-C/充电模块和 DT graph。TCPM 默认自动识别并优先受电；以下命令请求角色，不绕过协议直接写 DWC3：

```sh
/usr/libexec/w202-usb-role status
sudo /usr/libexec/w202-usb-role auto
sudo /usr/libexec/w202-usb-role host
sudo /usr/libexec/w202-usb-role device
sudo /usr/libexec/w202-usb-role data host
sudo /usr/libexec/w202-usb-role data device
sudo /usr/libexec/w202-usb-role power source
sudo /usr/libexec/w202-usb-role power sink
```

`auto/host/device` 分别选择 dual/source/sink 端口类型，可能先断开再连接。`data` 和 `power` 分别请求数据/供电角色交换，需要对端及当前合同支持；失败会原样返回。source 仅提供 5 V / 500 mA，低电压、温度异常、未准备好的充电芯片或已有外部 VBUS 均可阻止供电。

host 模式会断开本端 ACM/NCM。返回 device 后，UDC 创建事件会异步重启 gadget 服务并重新绑定；角色切换期间可用其他网络连接调试。`status` 不加载驱动或修改硬件。

充电分离使用独立的 `/usr/libexec/w202-charging inhibit-charge`，恢复充电使用 `/usr/libexec/w202-charging auto`。host/source 期间禁止电池充电；返回受电状态后恢复用户先前选择的充电模式。

`make check` 使用模拟 sysfs 检查请求路由、无效/失败请求和只读状态；未触碰构建机 USB。真实外设枚举、供电及重连须目标设备测试。

## 原生控制接口与组合功能

`source/w202-usb-control.c` 提供短时 C 工具，不保存桌面策略：

```sh
/usr/libexec/w202-usb-control status
sudo /usr/libexec/w202-usb-control apply auto 1 acm,ncm
sudo /usr/libexec/w202-usb-control apply device 1 acm
sudo /usr/libexec/w202-usb-control apply device 1 ncm
sudo /usr/libexec/w202-usb-control apply auto 0 acm,ncm
```

参数依次为角色预选、Device 启用标志、预选功能。`auto` 对应 OTG 自动双角色，
`host` 暂时抑制 Device 绑定，`0` 解除绑定而保留功能目录。
`rebind` 使用同样参数，但不重新请求 Type-C 角色，用于 UDC 返回后的服务重试。
JSON 读回 `port_type/data_role/power_role`、实际绑定状态及配置中的 ACM/NCM 链接；
未知状态为 null，已配置功能不等于主机已完成枚举。
多个 UDC、多个 gadget 或陌生功能配置会拒绝写入。

底层延用 shell 初始化 gadget，使用 `USB_FUNCTIONS=acm|ncm|acm,ncm` 和
`USB_ENABLED=0|1` 接收上层配置。更改组合先解除 UDC 绑定，保留已有描述符和 MAC。
默认仍为 ACM+NCM，保留 initramfs 的恢复通道；上层负责保存设置、授权和服务重试。
部分失败可能已改变角色或配置，调用方必须重新读回，不能声称操作是原子的。
`make stage` 只生成 `output/rootfs/` 产物，不操作构建机 USB。
