# w202ds-boot

W202DS 引导固件仓库。当前布局把可构建的组合工程放在
[`pocketjs4edk2_uefi/`](pocketjs4edk2_uefi/README.md)，仓库根只保留板级 LK 材料。

```text
w202ds-boot/
├── pocketjs4edk2_uefi/   # 组合构建：EDK II + PocketJS + ESP + LK 容器
└── lk/                   # LK 桥接、原厂 LK 与 LK 参考代码
```

## 快速开始

远端克隆 `w202ds-linux` 的 `boot` 分支并初始化组件子模块：

```sh
git clone --branch boot --recurse-submodules https://github.com/Yeamika/w202ds-linux.git w202ds-boot
```

完整构建说明、组件查找顺序、ESP 源码布局和产物列表见
[`pocketjs4edk2_uefi/README.md`](pocketjs4edk2_uefi/README.md)。

```sh
python3 pocketjs4edk2_uefi/build.py --output /path/to/build/w202ds \
  --lk-baseline /path/to/inputs/lk-jump-rmk.img
```

## 目录职责

- `components/`：以子模块固定 `edk2-ums9620` 与 `pj4edk2`，本地开发可在同一路径使用链接。
- `pocketjs4edk2_uefi/`：组合层源码、EFI 启动器、测试和构建工具。
- `pocketjs4edk2_uefi/tools/verify_firmware.py`：W202DS 固件格式、LK 分区布局、嵌入数据和尾部校验；两个构建入口完成构建后自动运行。REIMUKS 只调度构建、收集并核对产物身份。
- `lk/`：当前使用的 LK 桥接和 RAMBoot 源码，也是后续原厂 LK 及板级 LK
  参考代码的存放位置。本目录不参与 ESP 独立编译，由组合构建通过链接使用。

历史验证记录在 [`pocketjs4edk2_uefi/VALIDATION.md`](pocketjs4edk2_uefi/VALIDATION.md)。

## 本地二进制输入

RAMLK 从 `lk/firmware/uboot-mod.bin` 自动生成；设置由运行时读写 ESP 上的 `EFI/BOOT/pj4edk/settings0.bin` 和 `settings1.bin`。两个构建入口均使用
本仓库内的 LK 底包，无需 REIMUKS 或 Linux 工作区提供二进制。
例如只需指定新输出目录：

```sh
python3 pocketjs4edk2_uefi/build.py --output /path/to/build/w202ds
```

二进制当前由 Git 忽略；复制工程时需携带 `lk/firmware/uboot-mod.bin`。
仅克隆 Git 仓库不会自动得到它们。Git 源码依赖及编译工具仍需正常准备。

## 离线校验

```sh
python3 pocketjs4edk2_uefi/tools/verify_firmware.py /path/to/build/w202ds \
  --manifest firmware-manifest.json
python3 -m unittest discover -s pocketjs4edk2_uefi/tests -p test_boot_partition.py -v
```

`build-firmware.py` 生成 `firmware-manifest.json`；独立 `build.py` 的清单为 `manifest.json`，校验时按对应入口选择。

分区校验测试默认使用确定的文件夹具；设置 `W202_FIRMWARE_BUILD=/path/to/build/w202ds` 可改用完整构建产物。测试只读输入，在临时副本上做损坏检查，不刷写设备。

## 当前 Linux 启动交接

LinuxBoot 使用公共 `linux-efi` 配置解析器，启动目标由 ESP 中各启动项的 `linuxboot.conf` 指定；多个启动项可以共享内核并选择不同参数和根分区。显示交接与触控诊断使用相同的旋转约定，平台不再要求启动子程序前恢复旧显示模式。

重建时固定 `components/` 和 LinuxBoot 公共代码的子模块提交，使用相同工具链及输入镜像。分别比较 EFI 应用、UEFI payload 与最终 LK 分区镜像的 SHA-256；最终镜像还依赖 LK 底包和构建选项，不能用不同输入验证一致性。模型测试不替代设备启动验收，固件构建不会自动刷写设备。
