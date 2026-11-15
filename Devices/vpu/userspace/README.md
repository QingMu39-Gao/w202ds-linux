# W202DS VPU 用户态

只维护原生 Linux V4L2 Media Request 解码路径。旧 Bionic worker、私有运行库打包器和旧解码命令已删除；`w202-vpu.service` 永久屏蔽，不参与原生驱动初始化。

原生驱动通过模块及设备探测注册 `/dev/video*` 和 `/dev/media*`，不依赖 GPU 服务或 Android 运行库。接口注册不等于完整视频播放支持；应使用 Request 测试客户端对照独立软件参考逐帧验证。

```sh
make check
make CPPFLAGS="-I/path/to/drm-uapi" diagnostics
make DESTDIR=/path/to/staging install
```

`source/` 保存原生诊断客户端、测试码流生成器和软件参考工具。`make-avc-fixture.py` 需要 PyAV 和 NumPy；生成的参考不能替代硬件测试。标准接口和当前限制见 [v4l2.md](../source/v4l2.md)。

## 解码器使用率

驱动提供只读 sysfs 属性：

```sh
cat /sys/bus/platform/drivers/w202-vpu-codec/*/utilization
```

`version=1` 输出 `generation`、`total_ns`、`busy_ns`、`untracked_ns`、
`healthy` 和 `frequency_hz`。同一 generation 内，两次采样的
`100 × Δbusy_ns / Δtotal_ns` 是原生解码任务的忙碌百分比。
若 generation 变化、healthy 为 0、采样无时间增量，或该区间
`untracked_ns` 增长，应显示未知／不可用，不能把私有 MMIO 路径当成空闲。
计数读取不会唤醒硬件；频率是配置时钟值，不代表空闲时仍在供电。
该接口统计当前解码核心，不代表编码器、JPEG 或 GPU 使用率。
通用监控程序可按上述计数计算使用率；桌面监视器需要相应的数据源适配。

## VA-API

`build-vaapi.py` 从固定提交的 `libva-v4l2_request` 编译原生 VA-API 驱动，
由板级 userspace producer 调用。显式提供目标 SDK、宿主 SDK 和 PRoot：

```sh
python3 build-vaapi.py --sdk /path/to/target-sdk \
  --host-root /path/to/build-host-root --proot /path/to/proot
```

宿主 SDK 需要 Python、Meson、Ninja、pkg-config 和 ARM64 交叉编译器；目标
SDK 需要 libc、Linux UAPI、libva 和 libdrm 开发文件。输出统一在工程
`output/userspace/vpu`，安装 `v4l2_request_drv_video.so`。仅启用已验证的
H.264 8-bit Baseline/Main/High，不广告 High10 或尚未接入的其他解码器。
桥接通过标准 Media Request 调用驱动，不恢复 Android/Bionic worker。
