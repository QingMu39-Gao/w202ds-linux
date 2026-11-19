# W202DS 扬声器固件

功放型号为 AW88166。AUDCP A/B 完整原厂容器已有归档；本机 ACF 和 speaker XML
需由设备持有者从自己的 factory 镜像提供；默认放在本目录的 vendor/ 下。

`prepare.py` 仅接受已知 SHA256 输入，输出：

| 安装路径（相对 /usr/lib/firmware） | 处理 |
| --- | --- |
| `w202ds/agdsp.bin` | 按 AVB footer 裁到原始 5 MiB，保留 0x80 固件头 |
| `aw88166_acf.bin` | 原厂 `aw883xx_acf.bin` 原样更名，不修改 ACF |
| `w202ds/vbc-speaker.bin` | 16 字节格式头及 1732 字节 speaker 参数 |

```sh
python3 Devices/audio/firmware/prepare.py --help
python3 -m unittest discover -s Devices/audio/firmware -p test_prepare.py
```

DSP selector 由原厂实际 HAL 表确认：XML mode 64、logical ID 75、DSP case 0，
合成 **0x404b0000**。不能用 XML common 排序 68 替代 logical ID。
参数序列化会拒绝越界、重叠和未知空洞；仅两个已知 XML 控制的 reserved bits17:18
按零填充。脚本不会烧写分区或自动加载驱动。

完整容器 `stock-partitions/l_agdsp_{a,b}.img` 各 6,291,456 字节，包括 AVB 元数据和填充，
不能不经裁剪直接当作加载输入。上游 AW88166 不请求独立 monitor 文件。
输入及输出哈希、来源和已有归档来历保留于 [manifest.json](manifest.json)。
原厂固件未获得新的再分发许可。

`make -C Devices/audio/userspace install DESTDIR=<staging>` 自动验证原厂输入并准备固件，
再安装到板级包。中间结果只写入 `output/userspace/audio/firmware`；删除构建输出后
可以从相同的外部原厂输入重新生成。可通过 `AUDIO_AUDCP`、`AUDIO_ACF`、
`AUDIO_DSP_XML` 和 `AUDIO_FIRMWARE` 显式指定输入与输出位置。
