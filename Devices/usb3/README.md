# w202ds-usb3-driver（USB / Type-C 接口）

```text
usb3/
├── build.mk
├── devicetree/
├── README.md
└── source/
```

| 功能 | 可用性 | 问题描述 |
| --- | --- | --- |
| USB 串口（ACM） | 设备模式可用 | 通过标准 gadget 提供 `ttyGS0`；完整拔插与长期稳定性仍须独立验收。 |
| USB 网络（NCM） | 设备模式可用 | 与 ACM 组成复合设备；长期传输与拔插稳定性待验。 |
| USB 高速传输 | 待验证 | 尚无完整 USB 3 速率与稳定性验收结果。 |
| OTG 主机 / 设备模式切换 | 已接入，待硬件验证 | DWC3 dual-role、TCPM graph 与受保护的 5 V / 500 mA VBUS regulator；标准 Type-C 接口选择角色。 |
| Type-C / PD 协商 | 主线 TCPM 受电路径已有实现 | PMIC 提供 Type-C/Rp、固定 PD 与受限 PPS；收发稳定性和插拔恢复仍待硬件验收。 |
| DP 外接显示 | 待验证 | 尚无实际外接显示验收结果。 |
| USB 网卡与 Wi-Fi 共存 | 待验证 | 历史问题尚未闭环。 |

原厂参考为 W205DS-ESIM Android 13 / Linux 5.4.276 的 `drivers/usb/typec/sc27xx_typec.c` 与 `tcpm/sc27xx_pd.c`。该控制器驱动依赖厂商私有 `sprd_tcpm` 与端口内部字段，不能直接当作 Linux 6.18 主线 TCPM 驱动加载。当前移植控制器硬件接口到主线 Type-C / TCPM，充电策略与温度保护由 charging 负责。

UMP9620 控制器的实际移植放在同芯片的 `pmic/source/ump9620-typec/`。是否已建立合同须看实际 TCPM 状态，不能仅凭标准端口或 `PD_PPS` 能力标签判断。协议恢复限制及历史结果见 [PMIC 控制器说明](../pmic/source/ump9620-typec/README.md)。

USB 数据角色 `host/device` 与供电角色 `source/sink` 分开管理。`w202-usb-role` 请求角色并查询状态，`w202-charging` 独立选择是否给电池充电。source 启用前关闭两路充电并隔离从路，退出后恢复原输入配置。当前 DT 使用 Type-C extcon 路径，不额外重启 BC1.2；需要 VCONN 的线缆未支持。详见 [用户态接口](userspace/README.md)。
