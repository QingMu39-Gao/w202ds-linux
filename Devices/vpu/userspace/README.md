# W202DS VPU 用户态

只维护原生 Linux V4L2 Media Request 解码路径。旧 Bionic worker、私有运行库打包器和旧解码命令已删除；`w202-vpu.service` 永久屏蔽，不参与原生驱动初始化。

原生驱动通过模块及设备探测注册 `/dev/video*` 和 `/dev/media*`，不依赖 GPU 服务或 Android 运行库。接口注册不等于完整视频播放支持；应使用 Request 测试客户端对照独立软件参考逐帧验证。

```sh
make check
make CPPFLAGS="-I/path/to/drm-uapi" diagnostics
make DESTDIR=/path/to/staging install
```

`source/` 保存原生诊断客户端、测试码流生成器和软件参考工具。`make-avc-fixture.py` 需要 PyAV 和 NumPy；生成的参考不能替代硬件测试。标准接口和当前限制见 [v4l2.md](../source/v4l2.md)。

宿主头文件缺少 HEVC Request 类型时，先从配套的 6.18.35 内核运行 headers_install，再通过 CPPFLAGS=-I<导出目录>/include 运行 make check。

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
`output/userspace/vpu`，安装 `v4l2_request_drv_video.so`。启用 H.264 8-bit
Baseline/Main/High 和受限 HEVC Main，不广告 H.264 High10 或 HEVC Main10。
桥接通过标准 Media Request 调用驱动，不恢复 Android/Bionic worker。

构建副本依次应用 `libva-v4l2-request-w202-profile.patch` 和
`libva-v4l2-request-w202-layout.patch`。前者收窄 profile，并把 HEVC 的 VA RBSP
头部字节偏移转换为提交到 V4L2 的 EBSP 偏移，计入 emulation-prevention 与起始码。
后者处理应用在 codec／CTU 布局明确前就导出 surface 的情况：布局相同直接导入
DMA-BUF；不同则保留原生 CAPTURE 和参考辅助区，成功解码后同步并复制可见 NV12
到已导出的稳定 backing，再报告完成。这条兼容路径有 CPU 复制开销，不能称零拷贝。

`make check` 包括独立范围 oracle 和 H.264／HEVC 参数边界回归；诊断目标包括
`w202-vpu-request-hevc`，支持显式 MMAP／DMA-BUF 测试。离线检查不能代替硬件像素验证。
HEVC 支持完整单 slice 的 WPP，前端校验当前 Request 的入口数量和长度。
tiles、多 slice、scaling lists、PCM、10-bit 和 CTU16+SAO 仍拒绝，
具体资格范围见 [V4L2 记录](../source/v4l2.md)。
