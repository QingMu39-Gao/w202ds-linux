# W202DS 基带运行契约

硬件运行程序和 Linux 工具保持 Bionic/glibc ABI 分离。原厂 Bionic 程序、固件和 NV 不随源码发布，必须从目标设备的可信输入准备。标准上层接口见 [INTERFACES.md](../../INTERFACES.md)；网络管理与桌面系统策略由消费者维护。

`/etc/default/w202-modem-runtime` 用 `W202_MODEM_STATE` 指定私有状态目录。新系统先创建 `w202modem` 系统账号并按目标 UID/GID 配置权限；组件提供 `usr/lib/sysusers.d/w202-modem.conf`，不能复制另一系统的数字 UID。

独立 Linux 启动所需的 AVB 输入及 boot-prepare 模块见设备 [固件说明](../firmware/README.md)。只有匹配的输入已校验并完成权限配置后，才可启用 `W202_MODEM_BOOT_PREPARE=1`；保留原验证，不直接改写原厂 NV 分区。

NV 使用完整私有镜像：`${W202_MODEM_STATE}/nv/{nr_fixnv1_a,nr_fixnv2_a,nr_runtimenv1,nr_runtimenv2}.img`。已验证布局中，前两项各 52,428,800 字节，后两项各 57,671,680 字节；有效载荷分别为 `0xec0000`、`0xf40000`。镜像目录 root:root 0700、镜像 0600。启动工具校验头、长度和校验和后分配 loop，把块节点放进私有根 `android/dev/block/by-name/`，节点只向 `w202modem` 开放。原厂 NV 服务会执行 `O_TRUNC` 及脏扇区写，不能把这些入口替换成普通文件或物理分区。

已有完整且校验有效的旧工作文件可迁移；短读或校验失败必须停服，从同一设备的可信输入恢复。辅助工具不会为修复而读取或改写物理分区。

不可变运行固件可由 `W202_MODEM_FIRMWARE_DIR` 指定，目录须 root 所有、不可被组或其他用户写入，并包含 `nr_modem_a.img`、`nr_phy_a.img`、`pm_sys_a.img`、`ch_sys_a.img`、`nr_deltanv_a.img`。启动时只读绑定到私有根 `android/firmware/`；旧名称用于兼容原厂程序，不是原厂槽位入口。完整性 manifest 必须匹配。留空时使用已有的私有固件目录。

停服后 `unmount-root` 释放绑定和本状态对应的 loop。不能在 diskserver 运行时手工拆除。可通过 `w202-modem-storage verify --state "$W202_MODEM_STATE" --check-nv` 检查私有镜像；NV、IMEI、密钥及网络凭据均不发布。
