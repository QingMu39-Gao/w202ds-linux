# W202DS 基带用户态

| 目录 | 内容 |
| --- | --- |
| `at/` | SIPC AT 查询、蜂窝数据控制和网络检查。 |
| `runtime/` | Bionic 原厂程序的属性/日志桥接、私有 NV 存储和输入契约。 |
| `rootfs/` | 运行脚本、systemd/sysusers 单元和默认配置。 |
| `ModemManager/` | ModemManager 1.24.0 的 SIPC/W202DS 插件适配。 |
| [RUNTIME.md](RUNTIME.md) | 固件/NV 私有输入与运行隔离契约。 |
| `tests/` | AT 回包、并发锁和 NV 存储回归测试。 |

内核模块、设备树和固件输入分别在 `../{source,devicetree,firmware}/`。
`ModemManager/` 是普通受控源码快照，基于上游提交 `dfa41adf391b090720fb1ea56d884f61ea7fba29` 并包含本项目适配，不是嵌套子模块。

```sh
make OUT=/path/to/output all
make OUT=/path/to/output DESTDIR=/path/to/staging install
make OUT=/path/to/native-tests check
```

基础构建需要 AArch64 GCC 和 Python 3，生成原生 C 工具及属性区；Python 仅用于构建属性区和测试；`check` 用 HOSTCC 运行主机测试。ModemManager 适配器使用 `build-modemmanager.py --cross-file <目标SDK交叉文件>` 构建，依赖 GLib、D-Bus、GUdev、PolicyKit 和 systemd 开发文件；输出固定为 `output/userspace/modem/modemmanager`，安装树为 `output/userspace/modem/rootfs`。它只构建 W202DS 插件，不启用无关的 QMI/MBIM 后端。基础 Makefile 不隐式运行该构建。

目标系统需要 systemd、libc、kmod、iproute2、util-linux/loop、匹配模块和本机合法固件/NV 输入。APN 默认空，独立启动准备默认关闭；安装不启用服务或提供私有输入。

运行桥接与 Linux 工具保持 ABI 分离，原厂 Bionic 程序和库不随源码发布。NV 镜像长度、头和校验和必须有效，损坏输入直接报错。硬件运行配置见 [RUNTIME.md](RUNTIME.md)，服务启用、权限与连接策略由系统集成者定义。SIM 适配见 [插件测试说明](ModemManager/src/plugins/w202ds/tests/README.md)。IPv6、双卡、热拔卡和长期稳定性仍需验证。

上层调用约定见 [标准接口](../../INTERFACES.md)。

## 启动顺序与服务身份

`w202-modem-ordered.conf` 禁止 udev 按设备别名抢先加载基带模块，显式服务加载仍允许。IPC 驱动须在 Trusty core 创建子设备之前注册，避免传输层在接收队列尚不存在时启动。

私有运行目录保留 root 所有权；`private-root` 按当前系统的 `w202modem` 组恢复目录遍历权限和私有设备访问权限。不能依赖归档中的数字 UID/GID，也不能将代码目录改为服务账号可写。
