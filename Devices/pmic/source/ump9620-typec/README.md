# UMP9620 Type-C / PD

适配 UMP9620 PHY、FIFO、CC 和中断，协议状态机使用 Linux TCPM。通过标准 regulator 管理 charging 提供的 VBUS，通过设备树 graph 连接 DWC3 的 USB role switch。

## 能力

- 数据角色：device / host；供电角色：sink / source，分别管理。
- 默认双角色、优先受电。普通 OTG 接入通过 CC 检测；已连接的 PD 设备可请求 DR_SWAP / PR_SWAP，对端可拒绝请求。
- source 仅声明 5 V / 500 mA，不声明高压输出或 VCONN。需要 VCONN 的线缆尚未支持。
- sink 默认允许固定 9 V 回退和最高 9.8 V 的受限 PPS；实际输入受充电芯片、电池温度、源合同及故障保护约束。
- 使用 USB PHY 对当前连接重新完成的 BC1.2 结果；默认 Rp 下 DCP/CDP 提供 1.5 A 预算，SDP、未知或未检测完成时保留 500 mA。明确 Rp 和 PD/PPS 合同走 TCPM 原有路径。
- 厂商 `DEBUG_CABLE`（状态 8）按双 Rp 调试附件接入，保持 sink/device 角色并触发当前连接的 BC1.2 检测。该附件不建立 PD 合同；系统输入由 telemetry 的受限 5 V 路径提供，不能因附件状态位而提高到 3 A。
- `activate=0` 仅保留只读寄存器快照。

主线 TCPM 负责角色与协议顺序。本驱动在提供 VBUS 前检查 Type-C 比较器，拒绝向已带电的连接输出；停止输出在故障锁存后仍可执行。PR_SWAP 使用厂商 CC 强制值和切换标志，完成后释放 CC 强制及临时 detach 抑制。CC 设置中途失败会请求断开并锁住供电；真实拔线后也必须确认 VBUS 关闭，才允许重新启用端口。

## 构建与集成

在板级仓库根目录构建：

```sh
make -C Devices KDIR=/path/to/kernel-output OUT=/path/to/modules w202ds-pmic-typec
```

输入为本目录源码、匹配内核输出和 AArch64 工具链，输出 `w202_ump9620_typec.ko`。需要内核启用 TYPEC/TCPM、REGULATOR、USB_ROLE_SWITCH 和 DWC3_DUAL_ROLE，以及本板 DT 的 `vbus-supply` 和双向角色 graph。

BC1.2 由内核 `drivers/usb/phy/phy-sprd-ums9620-ssphy.c` 提供，PD 与 FGU 节点的 `usb-phy` 均指向 DWC3 使用的 combo PHY。检测只在 sink 接入时运行，暂停数据路径，要求旧 DONE 清除后重新完成，随后恢复 DP/DM 与数据路径；错误、超时和拔线返回未知。PHY 内建时必须重编 Image，并同步 DTB 和配套模块。TCPM 的可选 PD profile 数组为 NULL 结尾留出空间；不修改协议状态机或 UEFI。

加载顺序为 PMIC telemetry、双路充电控制、Type-C。本驱动检查两路充电芯片的输入 OVP 是否覆盖所声明的 sink 上限，VBUS regulator 未就绪或 graph 中的 DWC3 switch 未注册时延迟探测。系统电源策略和服务启用由发行版管理。

## 接口和参数

标准接口位于 `/sys/class/typec/port*/`：`port_type`、`data_role`、`power_role`、`power_operation_mode`。板级工具见 [USB 用户态](../../../usb3/userspace/README.md)。`controller_state` 和 `register_snapshot` 提供只读诊断，不要求 debugfs。

默认参数为 `activate=1 pps=1 target_mv=9000 pps_max_mv=9800`。固定档也可选择 `pps=0 target_mv=5000`；12 V 固定档及 11 V PPS 仅是显式实验参数，不进入默认策略。

Type-C 的 CC IRQ 不覆盖所有延后的 VBUS 比较器变化。独立 worker 每 100 ms 核对实际比较器，只在状态改变时通知 TCPM；卸载先停止 worker。不能将 regulator 请求成功当作电压已经就绪，也不能省略源供电后的 VBUS 通知。

TCPM 控制电源的 `online=2` 表示 PPS 已激活，`online=1` 表示普通受电合同。`usb_type=PD_PPS` 是能力标签，不能单独证明当前 PPS。`w202-usb-c` 提供系统输入和实际 ADC 电压，内部 TCPM 电源标记为 `scope=Device`。

## 恢复与验证

保留有限 Hard Reset、稳定合同计时和真实 CC 拔线解锁；sink/source 的已脱离状态均可清理旧合同。独立复位速率上限不会因探测器抖动被清空。协议故障同时请求关闭本机 VBUS；regulator 故障通知 TCPM 重新初始化。

宿主测试直接执行生产角色和恢复代码，覆盖 CC 方向、独立供电/数据角色、已带电 VBUS、失败停止、PR_SWAP 清理、I/O 错误及复位预算。请求边界测试另验证 PD/PPS 电压、电流和源能力限制。

已验证普通 OTG 接入可自动输出约 5 V，USB hub 与 RTL8153 网卡完成枚举并绑定驱动。该次 USB 链路为 480 Mbit/s；不等于千兆吞吐验证。PD/PPS 收发、快充重连、带载充电分离、两种方向与连续切换仍需分别验收，不承诺所有 PD 线缆、USB3 或 DP Alt Mode 可用。

## 来源

W205DS-ESIM Android 13 / Linux 5.4.276 的 `drivers/usb/typec/sc27xx_typec.c`、`drivers/usb/typec/tcpm/sc27xx_pd.c`，以及 Linux 标准 TCPM、regulator 和 USB role switch 接口。厂商代码提供寄存器序列参考，不直接加载 Android 模块。
