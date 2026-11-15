# 硬件与原厂设备树的对应关系

此表依据原厂 `board-0.dts`，SHA256
`ce5a2252e350f8d1d70987f614ec0da44b4494153a42e7a13b7a29e307605dc1`。这是原厂基础 DTS 的分析证据，不是 Linux 当前最终运行 DT；实际启用状态、内核绑定及驱动效果须结合运行 DT 和对应系统的实机日志。

顶层目录代表已确认芯片或共同接口，不要求一个目录恰好一个 DT 节点。UMP9620 的 FGU/ADC/codec/振动/灯是同芯片子功能；Wi-Fi/蓝牙可经 PCIe 枚举；CH 代理的外部传感器也可能没有 AP 可见的独立节点。IMU、磁力计和光线统一在 sensorhub 设备目录维护，保留不同器件身份；共享通道和固件只留一份。

| 硬件/接口 | 原厂基础 DTS 节点 | 对应关系与限制 |
| --- | --- | --- |
| <a id="audio"></a>audio | `/soc/agcp/vbc@56510000`<br>`/soc/agcp/audio-codec@56360000`<br>`/soc/aon/spi@400000/pmic@0/audio-codec@1000`<br>`/sprd-pcm-audio`<br>`/sound@0`<br>`/soc/ap-ahb/i2c@22c0000/aw883xx_smartpa@34` | 多节点音频接口：VBC、SoC 数字 codec、UMP9620 模拟 codec、声卡与 I²C 功放协同；AW883xx 节点数不能作为实体扬声器数量。 |
| <a id="camera"></a>camera | `/soc/ap-ahb/i2c@2240000/sensor-main@20`<br>`/soc/ap-ahb/i2c@2250000/sensor-sub@5a`<br>`/soc/ap-ahb/i2c@2280000/sensor-main2@5a` | sensor-main/main2/sub 是平台候选节点，不能据此认定安装三个摄像头，也不能按名字直接对应前后镜头。 |
| <a id="charging"></a>charging | `/soc/ap-ahb/i2c@22a0000/charger@6b`<br>`/soc/ap-ahb/i2c@2260000/charger2@6b` | 两个独立 I²C 充电控制器：原厂 ti,bq2560x_chg/chg2 是兼容名称，实际型号已由原厂识别分支、Android 日志和 Linux 读数确认 UPM6910H。 |
| <a id="drm"></a>drm | `/soc/dpuvsp/dpu@31000000`<br>`/soc/dpuvsp/dsi@31300000`<br>`/soc/dpuvsp/dsi@31300000/panel` | 内屏是 DPU → DSI → panel 链路，背光和偏压分别控制；USB-C DPU1/DPTX 由 usb3 维护。 |
| <a id="flash"></a>flash | `/soc/ap-ahb/i2c@22a0000/flash-ic@63` | 原厂 flash-ic@63 指向 AW36518A GPIO 兼容实现；固件包中的多种兼容模块不代表多颗实体灯。 |
| <a id="gpu"></a>gpu | `/soc/mm/gpu@23140000` | 原厂 GPU 节点的 compatible 为 sprd,mali-natt；本板主线驱动与运行 DT 的适配另行维护，不能直接把原厂绑定当主线绑定。 |
| <a id="hall"></a>hall | `/soc/hall_sensor` | 独立霍尔输入平台节点 ah,hall_ic，GPIO148；名字 ah1898/hall_ic 不等于读取过芯片 ID。 |
| <a id="keyboard"></a>keyboard | `/zsk_keyboard`<br>`/soc/ap-apb/serial@20000` | zsk_keyboard 平台节点提供连接/供电 GPIO，数据通过 UART2（/dev/ttyS2，115200/8N1，无流控），不是 I²C 键盘。 |
| <a id="modem"></a>modem | `/sipc-virt/core@5`<br>`/sipc-virt/core@7`<br>`/modem@2`<br>`/modem@3`<br>`/soc/ipa-apb/sipa@25220000` | 基带是 SIPC、mailbox、loader、SIPA/网络和安全服务组成的多节点子系统；共享 WCN 兼容头文件不表示基带就是 WCN 芯片。 |
| <a id="pmic"></a>pmic | `/soc/aon/spi@400000/pmic@0`<br>`/soc/aon/spi@400000/pmic@0/fgu@800`<br>`/soc/aon/spi@400000/pmic@0/adc@504`<br>`/soc/aon/spi@400000/pmic@0/audio-codec@1000`<br>`/soc/aon/spi@400000/pmic@0/vibrator@2390` | 单个 UMP9620 MFD 下有 FGU、ADC、音频 codec、Type-C/PD、振动和 BLTC 子功能；UMP9622 温度传感器是另一 PMIC，归 thermal。 |
| <a id="sensorhub"></a>sensorhub | `/sprd-sensorhub`<br>`/sipc-virt/core@9`<br>`/sipc-virt/core@9/channel@4` | 根节点 /sprd-sensorhub 由 CHSYS（SIPC dst9）承载；channel4 是双环通道，独立 IMU/磁力计/光线可能没有 AP 可见总线子节点。 |
| <a id="serial"></a>serial | `/soc/ap-apb/serial@0`<br>`/soc/ap-apb/serial@10000`<br>`/soc/ap-apb/serial@20000`<br>`/soc/ap-apb/serial@30000` | SoC 的 serial0..3 为 UART 控制器；USB ACM 由 gadget 枚举，不需要另造一个固定 UART DT 节点。 |
| <a id="thermal"></a>thermal | `/soc/aon/thermal@270000`<br>`/soc/aon/thermal@280000`<br>`/soc/aon/thermal@290000`<br>`/soc/aon/thermal@2A0000`<br>`/soc/aon/spi@400000/pmic@2/tsensor@20f8` | 包含多个 SoC 热区与另一实体 UMP9622 PMIC 的 tsensor；UMP9620 电池 ADC、环境温度和芯片结温不能混作同一量。 |
| <a id="touchscreen"></a>touchscreen | `/soc/ap-apb/spi@70000/zte-ts@0` | SPI 下一个 zte-ts@0 节点包含多种兼容候选；并不代表安装 Chipone、Himax、Focaltech 三颗触控芯片。 |
| <a id="usb3"></a>usb3 | `/soc/ipa-apb/usb3@25100000`<br>`/soc/ipa-apb/usb3@25100000/dwc3@25100000`<br>`/soc/ipa-apb/ssphy@25310000`<br>`/soc/ipa-apb/dpu1@31880000`<br>`/soc/ipa-apb/dptx@31890000`<br>`/soc/aon/usb@a00000`<br>`/soc/aon/spi@400000/pmic@0/typec@380`<br>`/soc/aon/spi@400000/pmic@0/pd@e00` | 这是跨芯片接口链：DWC3/共享 USB-DP PHY、DPU1/DPTX 和 UMP9620 Type-C/PD 协同。基础 DT 的 disabled/okay 不是当前运行状态。 |
| <a id="vpu"></a>vpu | `/soc/mm/video-codec@32000000`<br>`/soc/mm/enc-core1@32100000`<br>`/soc/mm/video-codec@32200000`<br>`/soc/mm/power-domain@0`<br>`/soc/mm/power-domain@1`<br>`/soc/mm/power-domain@2`<br>`/soc/mm/power-domain@3` | 三个 codec 节点及各自子电源域组成同一视频编解码接口；USB-C 视频输出由 usb3 的 DPU1/DPTX 负责。 |
| <a id="wcn"></a>wcn | `/soc/pcie@26100000`<br>`/sipc-virt/core@9/channel@7` | Wi-Fi/BT 随 PCIe 总线枚举，不要求各有独立 AP DT 节点；GNSS 的 SIPC data0_gnss 是通道证据，不是卫星定位成功。 |

历史来源清单、设备树摘录和原厂 DTS 已独立保存，源码文档保留来源哈希。硬件集成关系不作为构建拓扑或模块加载顺序。

<a id="imu"></a><a id="magnetometer"></a><a id="light"></a>
原 IMU、磁力计、光线入口现统一对应 sensorhub：LSM6DSOQ、MMC5603、LTR578 都经 `/sprd-sensorhub` 与 CHSYS 代理，未确认独立 AP 总线地址；器件不同，源码、模块与固件统一维护。
