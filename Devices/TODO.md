# 硬件验证索引

2026-11-04 更新。以各硬件 README 的实机证据为准；源码导入、独立编译通过不等于功能可用。
此表是早期验证范围的历史索引，当前能力与缺陷须以对应组件说明和本次实测为准；不作为任务授权或开发顺序。

| 归属 | 已验证基础 | 尚未完成 / TODO |
| --- | --- | --- |
| WCN | 单独 STA；单独 AP WPA2/DHCP/DNS/蜂窝 HTTPS | AP＋STA 并发断流、GO客户端 reason9；蓝牙 HCI/配对/音频；GNSS 定位；吞吐/漫游 |
| modem | 注册/拨号/DNS/HTTPS及自动启动 | CAICT 全球网测实际速率、500 Mbit/s 证明、IPv6、长时运行和物理断电上电 |
| GPU | Panfrost/GLES 着色器与像素回读 | KDE 合成、Firefox B站/Shadertoy、VS Code；Panfrost DRM Vulkan 移植；长时/休眠 |
| DRM / [handoff](drm/source/pj4edk-boot-display/README.md) | 内屏/背光、Server10秒息屏；独立模块构建与非活跃 console release | 新 DRM 与独立 handoff 的匹配 rootfs 安装、完整冷启动/第一帧交接、失败恢复、休眠、自动亮度 |
| VPU | H.264 450帧逐帧匹配及共享缓冲/电源管理 | V4L2/普通播放器/浏览器接口、编码和其他格式、动态分辨率、长时/系统休眠；去除专有 AVC 库依赖 |
| PMIC | 电压/有符号电流/温度/估算SOC、库仑计数 | 可靠 SOC 基线与容量/温度补偿；Linux 马达实物和可见呼吸灯验收；PMIC音频 |
| charging | 双 UPM6910H 身份、停充和外电负载实验 | 电池物理隔离/独立电荷泵尚无证明；输入电流、充停转换与恢复、快充协商和温控联动 |
| Sensor Hub | 固件版本/身份、时钟同步、10个 IIO 直接通道 | 开盖光照/实物动作、轴向/单位/时间戳与校准；服务/bundle冷启动；缓冲采样/频率档位/电源管理 |
| Hall | 11次实际开合与IRQ/输入事件一致、自启 | 固定 bundle/新 rootfs 冷启动、确切器件身份、长期与休眠策略 |
| 键盘/触控板 | 历史 evdev 按键、移动/点击/双指 | 当前 rootfs 复验、Fn/修饰键、Caps LED与稳定TTY/serdev接口 |
| 音频 | 保留 PCM/功放/原厂固件参考 | VBC/codec/AUDCP、功放配置与校准、ALSA路由；扬声器播放、麦克风录音和休眠 |
| 相机 / 闪光灯 | 原厂型号线索、DT与模块 | 芯片ID/前后映射、核心驱动/ISP/调校、Linux拍摄；闪光/手电筒与超时 |
| USB3 | USB调试/网络已有记录 | SuperSpeed速率、OTG/VBUS、Type-C方向/PD、DP altmode视频输出、USB网卡与Wi-Fi共存 |
| thermal | 参考源码；电池NTC由PMIC读通 | SoC/PMIC温度全链路、阈值/节流、与充电/CPU/GPU联动 |
| 触控 | 独立模块/原厂固件参考 | Linux/UEFI触控实机可靠性、独立 EFI 测试；按用户要求暂缓研发 |
| 串口 | USB ACM历史调试、当前USB/Wi-Fi双SSH | 物理UART连接/波特率与分阶段日志，避免占用键盘UART |
| CPU / NPU / 调度策略 | 内核运行，CPU多核在线有历史证据 | CPU/GPU/VPU/NPU实际频率档位、NPU驱动与推理、紫光调度/功耗策略仍需独立审计；用户要求后置 |
| 固定交付与引导 | 17目录、50硬件ko＋独立handoff构建；B正式5MiB EDK2已读回 | 最新模块/服务汇入匹配bundle/rootfs并冷启动；正式EDK2普通冷启动与OS入口完整验收 |

AP＋STA 的本轮定位与无效“通过”标记纠正见 历史 r65 记录；当前实现与限制见 [WCN 说明](wcn/README.md)。
后续进展写回对应硬件目录；本页只作总索引，不复制源码或固件。
