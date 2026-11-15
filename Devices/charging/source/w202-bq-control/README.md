# W202DS UPM6910H 充电模块

生成 `w202_bq_control.ko`，同时绑定主／从两颗 UPM6910H。主路通过标准 `power_supply` 提供自动充电、停充和电流设置；从路保持停充，不增加用户态守护程序。

```text
w202-bq-control/
├── Kbuild
├── charger.h           # 私有状态和接口
├── core.c              # 绑定、插拔通知、工作队列、睡眠与卸载
├── registers.c         # 写寄存器、停充与回读
├── policy.c            # 原厂范围、温度、电压和故障保护
├── power-supply.c      # 标准 Linux 电源接口
├── sysfs.c             # 只读诊断、兼容 stop_charge
├── pps.c               # 自动选择 PPS、升压、续约和回退
├── pps-limits.h        # 源能力与板级电压边界
├── otg.c               # 标准 VBUS regulator、双芯片锁和故障通知
├── otg-hardware.c      # 5 V 输出、从路隔离与失败回滚
├── tests/
│   ├── pps-limits.c
│   ├── pps-recovery.c    # 直接执行生产 PPS 客户端的故障回归
│   └── include/linux/jiffies.h
└── README.md
```

## 默认策略

`auto_pps=1`，`pps_supply` 默认指向本板 TCPM 电源。探测先停充、准备并回读两颗芯片的输入 OVP，待系统通过主路 `charge_behaviour=auto` 开充；加载顺序与此请求由外层系统集成管理。主路默认请求电池充电电流 **2.64 A**，仅在 18–40°C 生效，其余允许温区最多 1.5 A。

自动充电保护检查通过后优先激活 PPS，先取得源预算（最多 3 A），每次至多升 200 mV，到源能力、充电输入配置和本板 **9.8 V** 上限中的最低值。稳定后约每 4–6 秒用标准电流 setter 续约，不把旧采样电压写回。电压和预算归本策略控制，直接修改 TCPM 的设定会重新收敛。

不支持 PPS 时保留固定合同，配套控制器默认选择可用的 9 V 或 5 V。协商忙最多尝试五次；其他 PPS 失败退出固定档并锁住该次连接的 PPS 尝试，固定充电仍须通过 PG、ADC 与全部保护检查。断开或明确停充再开充后可重新尝试。退出失败只临时关闭电池充电，保留用户 auto 请求；退出状态单独记录，最多每 2 秒重试，确认回固定档或离线后才放行常规充电检查，不循环复位。GPIO extcon 的短暂断开事件会原子记录，避免同步协商阻塞时漏掉整次断开／重接。物理拔插与所有故障分支仍须实物验收。

手动 `inhibit-charge` 优先，关闭电池充电并退出 PPS；恢复 `auto` 后重新选择 PPS。PPS 临时停充有独立门控，仍先按实时源预算收敛输入限流，不能因等待协商而保留旧电源的较大输入预算。温度／故障保护不被默认高功率策略覆盖。睡眠／卸载先停充、退出 PPS，恢复后重走检查；睡眠回归未完成。

`max_input_mv=12000` 默认准备两颗芯片 REG06=0xe6 的高压 OVP，**不是请求固定 12 V**。9.8 V 已有充电实测；约 10.1 V 以上曾输入掉线，11 V 与固定 12 V 不进入自动策略。`max_input_mv=5000` 可关闭高压及 PPS 客户端；`auto_pps=0` 仅续约手动进入的 PPS，`pps_supply=` 禁用客户端。

## 充电分离与 OTG

`charge_behaviour=inhibit-charge` 关闭主路和从路的电池充电，保留外电输入，不断开 BATFET；`auto` 恢复受保护的主路自动充电。从路始终不启充电。请求模式、硬件使能与电池净电流分别可查，输入不足时电池仍可能补电。

主路同时提供标准 `otg-vbus` regulator，供 Type-C 控制器消费。OTG 输出限为 5 V / 500 mA；启用前检查两颗芯片身份、关机/挂起/故障状态、电池 3.6–4.43 V、10–43°C、外部输入和两路 watchdog。两路停充并确认从路 HIZ 后才启 boost，失败执行回滚。关断未确认时保持从路隔离；成功关断后恢复原输入隔离和 boost 配置。用户的 auto/inhibit 请求独立保存，退出 OTG 后再应用。

沿用现有策略 worker 监测源模式的电压、温度和故障，失败关断并通知 regulator 消费方，不循环重启 boost。REG09 先读历史再读当前故障；仅历史中的充电输入丢失不终止正常 OTG，仍保留到 history。所有当前故障、其他历史保护位和读写失败仍会关断。OTG 供电期间暂阻止系统挂起。`charging_policy` 同时报告 `otg_active` 与 `otg_restore_pending`。已验证 USB hub 与网卡枚举，持续带载及角色交换仍需验收。

## 限制与接口

- 主路电流标准接口：`constant_charge_current`，60 mA 步进，60000–2640000 µA；用户可降低，超界拒绝。设定电流不是 FGU 实测电流。
- 输入限流由源合同和原厂 DT 限制共同确定：5 V 最多 2 A，高压按 1.25 A 向下取档为 1.2 A，未知源回到 500 mA。3 A PPS 只是协议预算。
- 保留电池 10–43°C、恢复 13–40°C、4.4 V 向下取档、420 mA 终止、10 小时定时器、90°C 芯片热调节、电池／充电器故障保护；严重故障要求明确停充／恢复。从路禁止开启。
- 标准 `charge_behaviour` 与兼容 `stop_charge` 共用入口；内部电源为 `scope=Device`，UPower / KDE 通过系统电池和唯一外电接口读取状态与净充放电功率。
- `*_locked()` 持本芯片锁；TCPM 同步协商不持该锁。PPS 截止时间在续约与退出 setter 前更新，防止标准源通知引起连续自触发；状态仅由原 2 秒 worker 管理，notifier 仅记录断开标志并调度。
- REG09 仅充电/OTG 策略读取，保存历史后读取当前故障；诊断不清故障。`charging_policy` 可查看目标电流、保护和 PPS 尝试锁存。

## 构建与验证

板级仓库根目录：`make -C Devices KDIR=/path/to/kernel-output OUT=/path/to/modules w202ds-charging-control`。输入为本工程、内核配置／符号表和 AArch64 工具链；输出为一个 `.ko`、日志和 `build.json`。依赖 PMIC telemetry，与 Type-C 控制器通过标准接口协作。USB host/device 切换另需 DWC3_DUAL_ROLE 内核及匹配的 DT graph；充电分离本身不要求重编内核。

PPS 边界测试：用宿主 C 编译器构建 `tests/pps-limits.c`，输出放 `output/`。当前 73,184 次决策覆盖 3.3 V／5 V 起报源的区间交集、源上限、板级上限、步进、无效范围和最终收敛。`tests/pps-recovery.c` 直接包含生产 pps.c，模拟 TCPM 忙、输入丢失、短暂断开和协商中用户停充；检查退出限频、自动恢复、用户请求优先、锁与引用，使用 `-I tests/include` 编译。历史手动 PPS 9.8 V 两分钟实测约 8.99 W；此值不是恒定功率，电池接近充电电压时会自然降流。

`tests/otg-hardware.c` 直接执行生产寄存器/OTG 序列，逐一注入所有启用 I/O 失败，检查 boost-off 未确认时保持从路隔离、原 HIZ/配置恢复及用户停充请求保留。

## TODO / Experimental

实物拔插自动重建、完整重启、睡眠、长时间充满及故障边界；完整 JEITA、百分比阈值与双路协作。自动策略当前轮次的真机结果和交付位置见上级 README。

## 来源

本板原厂 `sqc_bq2560x.{c,h}`、B 槽 DT 与电池解析器；Linux 标准电源 ABI。原厂快充电池／输入电流 2.65 A／1.25 A 按芯片步进向下取档；不依据兼容芯片资料取消保护。
