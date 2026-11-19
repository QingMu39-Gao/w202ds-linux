# 传感器桌面集成

使用 Debian `iio-sensor-proxy`，通过标准 SensorProxy D-Bus 接口向桌面提供加速度方向和环境光；不另写轮询守护程序。陀螺仪、三轴磁场由 IIO 提供，不伪装成已校准罗盘。

| 文件 | 作用 |
| --- | --- |
| `rootfs/usr/lib/systemd/system/w202-sensorhub.service` | 基带运行时启动 CH 后加载模块，随基带运行时停止。 |
| `rootfs/usr/libexec/w202-sensorhub-attach` | 加载模块并等待 CH 收发环就绪，最多尝试绑定 30 次；绑定成功立即返回。整个服务启动超时为 60 秒，包含模块加载及 probe 耗时。 |
| `rootfs/usr/lib/udev/rules.d/81-w202-sensorhub.rules` | 配置 LSM6DSOQ 安装方向矩阵。 |

## 安装

```sh
make install DESTDIR=/path/to/rootfs
```

此命令仅暂存文件。将匹配的 `w202_hub_iio.ko` 安装到 `/lib/modules/$(uname -r)/updates/w202ds/`，运行 `depmod -a`，安装发行版 `iio-sensor-proxy`，启用 `w202-sensorhub.service`。CH 由现有基带运行时启动；此处不下载固件、不写 NV、不复位处理器。

安装时须同时提供现有基带运行时的服务、工具、匹配模块及固件；单独安装 Sensor Hub
服务不能启动 CH。组件选择、服务启用和桌面策略由 发行版集成层 负责，本目录只提供模板。

## 离线检查

```sh
make check OUT=/path/to/native-output HOSTCC=cc
```

此入口运行现有 CH 协议测试，检查帧向量、分片、双接收环、校验恢复和浮点转定点，
不访问硬件。从仓库根目录执行 `make -C Devices/sensorhub/userspace check` 可运行该检查。

## 上层接口

按 `/sys/bus/iio/devices/iio:device*/name` 查找设备，不固定编号：
`w202-lsm6dsoq` 提供加速度及角速度的 `raw` / `scale`；`w202-mmc5603` 提供三轴
磁场的 `raw` / `scale`；`w202-ltr578` 或 `w202-stk3x1x` 提供处理后的照度。
通道及单位见 [驱动说明](../source/w202-hub-iio/README.md)。

运动数据按新鲜度等待新样本；环境光是 on-change 上报，启用后的最后一个样本在
光照不变时仍有效。停止读取 5 秒后关闭采样，下次启用重新等待首帧。该缓存行为须保留。
键盘拆装通过独立 input 设备 `W202DS Tablet Mode` 上报 `SW_TABLET_MODE`，不依赖 CH。

消费者可通过 SensorProxy 的 `HasAccelerometer`、`HasAmbientLight` 和方向/照度属性验证接入。命令行工具 `monitor-sensor --accel --light` 受系统会话授权约束，不应为远程测试放宽访问策略。

## 方向与验证边界

物理面板为竖屏 1200×1920，但 DRM 安装补偿后的逻辑基准是横屏 1920×1200、旋转 0°。
键盘支架横屏时原始重力主分量在 +X；安装矩阵
`0, 1, 0; -1, 0, 0; 0, 0, 1` 将其映射为 -Y，向 SensorProxy 报告 normal。
矩阵只校准传感器坐标，不另加显示或触控旋转。该基准姿态已读回验证；
四个实物方向的画面、触控对应及倾斜变化仍需联合验收。

传感器数据读取、实物方向映射和上层响应应分别验收。

TODO：核对带方向标记的安装矩阵、桌面联动和挂起恢复；光强标定另验。

系统集成负责安装模板和桌面策略，应用使用此组件说明的 Linux 标准接口。
