# 充电初始化

`w202-charging` 加载电池遥测、双路充电和 Type-C/PD 驱动，等待标准 `power_supply` 接口就绪，然后设置主路充电模式并回读。从路始终保持 `inhibit-charge`。启动顺序兼容已有 `w202-power.service`，在旧初始化完成后应用发行版策略。

```sh
make install DESTDIR=/target/rootfs
sudo /usr/libexec/w202-charging auto
sudo /usr/libexec/w202-charging inhibit-charge
/usr/libexec/w202-charging status
```

基础单元默认停充。发行版通过 `/etc/default/w202-charging` 设置 `CHARGE_BEHAVIOUR=auto` 并启用 `w202-charging.service`。发行版集成层 在自己的板型配置中选择自动充电；安装模板本身不会加载模块或启用服务。

`auto` 只提交自动充电请求。PPS、固定档回退、源电流预算、温度、电压及故障保护由驱动负责，不由脚本写寄存器或固定电压。服务只在启动时应用一次策略，运行中的手动停充继续有效；`systemctl stop` 不等于停充，应使用上述 `inhibit-charge` 命令或标准 sysfs 接口。

`status` 只读，不加载模块或更改充电状态。它分别显示主路请求模式、硬件 `charge_enabled`、电池净电流/功率、系统输入和 TCPM 合同，以及可用的 Type-C 数据角色/供电角色和 DWC3 角色。

`w202-power-control status` 提供同类信息的 JSON，供上层复用。root 可请求 `charge-auto`、`charge-inhibit`、`fast-on`、`fast-off`、`usb-auto`、`usb-host`、`usb-device`。快充开关只控制许可与 PD profile，不保证电源支持快充；缺少输入电流传感器时 `input_current_ua` 为 null，电池净电流和源预算分别报告。

| 能力 | 当前接口与边界 |
| --- | --- |
| 电池充电开关 | 主路 `charge_behaviour=auto/inhibit-charge`；从路保持停充。 |
| 充电分离 | `inhibit-charge` 保留外电输入、关闭电池充电；不切 BATFET，也不承诺物理隔离电池。输入不足时电池可能补电。 |
| 实际是否给电池充电 | 联合检查硬件使能、充电状态和 `w202-battery/current_now`；电流为正表示净充入，为负表示净放出。请求 `auto` 本身不等于正在充电。 |
| 普通源识别 | 标准 `usb_type` 区分 SDP/CDP/DCP；Type-C Rp 和 PD/PPS 通过各自驱动获取。 |
| 快充 | 现有驱动支持受限 PD 固定档与 PPS，成功合同与实际输入电压须独立核对；`PD_PPS` 能力标签不等于已激活 PPS，TCPM `online=2` 才表示激活。失败可回普通充电。SFCP 等其他厂商协议未移植。 |
| USB 角色 | `w202-usb-role` 通过标准 Type-C 接口请求 auto/host/device，以及独立数据/供电交换；需要双角色内核、匹配 DT 和模块，真实切换仍须硬件验收。 |

W205 参考包中 `drivers/vendor/common/sqc_v8/sqc_misc.c` 的 `zte_sqc_set_property()` 分别将 `CHARGING_ENABLED` 投到 USB 输入限流和 `BATTERY_CHARGING_ENABLED` 投到电池充电电流；`drivers/vendor/common/power/charger_policy_service.c` 分别控制两者。这为保留外电、禁止电池充电提供实现依据。`sqc_bq2560x.c` 的 `set_powerpath()` 写入被 `#if 0` 禁用，不能据此宣称物理电池隔离可用。本板 OTG 沿该机制提供受保护的 VBUS regulator、从路临时 HIZ 和故障关断；不修改用户的充电请求。

`W202_SYSFS_ROOT` 和 `W202_POWER_SUPPLY_ROOT` 可指向离线测试树，默认分别使用 `/sys` 和其 `class/power_supply`。`make check` 使用模拟接口验证失败停留、角色读取和只读查询；不触碰构建机硬件。实际快充、插拔恢复与带载充电分离仍需目标硬件验证。

## 充电模式

```sh
sudo /usr/libexec/w202-power-control mode-pd5v
sudo /usr/libexec/w202-power-control mode-pd9v
sudo /usr/libexec/w202-power-control mode-pps
/usr/libexec/w202-power-control status
```

分别请求 PD 5 V 固定档、PD 9 V 固定档、PPS 自动调压。电流预算由电源能力与板级限制共同决定，不强制指定 2.4 A 或 2.1 A。不支持高压或 PPS 的电源可能回退到固定 5 V。切换需要协商，`charging_mode` 是请求策略，`active_mode`、`protocol` 和 `contract_*` 是当前合同。合同电流是预算，电池电流是净充电电流。

模式切换不改变充电分离设置，模式请求暂不跨重启保存。`fast-off` 等价于 PD 5 V，`fast-on` 等价于 PPS 优先。
