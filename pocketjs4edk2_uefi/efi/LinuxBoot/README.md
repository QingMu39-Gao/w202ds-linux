# LinuxBoot

W202DS 独立 Linux EFI 加载器，读取 ESP 上的启动配置与载荷，退出 UEFI 后进入原始 ARM64 内核，不恢复或调用 LK。同一源码仍可构建 DebianServer、DebianKde 两个应用；新配置由实际 EFI 文件所在目录决定。

## ESP 配置

在 `bootaa64.efi` 同目录放置 ASCII 文件 `linuxboot.conf`：

```ini
kernel=/EFI/Linux/Image
initramfs=/EFI/Linux/initramfs.cpio.lz4
dtb=/EFI/Linux/board.dtb
metadata=/EFI/Linux/payload.bin
root-partuuid=01234567-89ab-cdef-0123-456789abcdef
options=systemd.unit=graphical.target
```

路径相对于所加载的 ESP 根目录，禁止 `.`、`..` 和空路径段。前五项必填，`options` 可省略或为空；重复项、未知项和超长配置均报错。支持 LF/CRLF、空行和整行 `#` 注释。

`payload.bin` 保留载荷长度与 CRC，更新内核、initramfs 或 DTB 时必须同步生成。更改根分区或启动目标仅需修改配置。EFI 将根分区 PARTUUID 写入 DT `/chosen/pj4edk2,root-partuuid`，将 options 追加到 DT bootargs；initramfs 挂载根分区并交接，systemd 消费 `systemd.unit=`。内核必须接受引导器命令行，不能强制覆盖它。

旧目录没有配置文件时，兼容原有 `boot.img`、`root-partuuid`、`payload.bin` 与共享 `EFI/W202DS/LINUX.DTB`。已存在但错误的配置不会静默回退。

## 构建和交接

由 `efi/PocketJsUefiPkg.dsc` 独立构建，应用不嵌入平台固件卷。所加载分区须为 GPT ESP 类型，不绑定某个固定 ESP PARTUUID。EFI 只读取文件，不修改分区表或 A/B 槽位。

加载器校验文件长度、CRC、ARM64 内核布局和 DT 保留内存；CRC 检测传输损坏，不提供签名认证。内核放到 `0xd4000000 + text_offset`，initrd 放 `0xd0000000`，DT 放 `0xd8000000`；平台须映射完整暂存区并避免与分配器、保留区冲突。

交接前关闭文件；ExitBootServices 成功后停止遗留 PMIC 看门狗，再完成缓存/MMU交接。当前显示交接验证原生 1920×1200 XRGB8888 GOP/DPU 状态，并通过公共 BootDisplay 协议写入 DT。没有当前显示请求时删除陈旧记录。

`/chosen/w202,uefi-load-us` 保存六个大端 u64 阶段耗时，供启动后分析。新版配置路径需真机验证；编译和主机解析测试不能替代上机验收。

EFI 在 boot-output 区域显示打开 ESP、读取配置、加载和校验内核/initramfs、准备交接等
真实阶段。文字调用公共 HII/GOP 适配，不改变显示模式或复位 DSI；系统字体按原始尺寸
绘制。最后一行保留到 Linux fbcon 接管，企鹅出现之前也能看出当前加载阶段。这里是 EFI
日志，Linux 自身的早期 printk 仍需等 fbcon 注册后显示。
`/chosen/w202,efi-progress-lines` 和 `w202,efi-progress-status` 记录准备 DT 时已成功显示的
行数与 EFI 状态。缺少可用字体协议时继续启动，并保留失败状态供启动后核对。

公共配置解析、根分区和显示协议位于 [common](common/readme.md)。平台地址、GOP/DPU、PMIC 和 ARM64 交接留在本目录。
