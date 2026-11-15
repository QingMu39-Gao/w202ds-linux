# G57 PanVK 的 Panfrost DRM 移植

实验目标是 W202DS Mali-G57（GPU_ID `0x90910010`、Valhall v9、Job Manager），
通过现有 `panfrost` DRM 内核接口提供独立 Vulkan ICD。不是 kbase 内核驱动，
不安装或替换系统 EGL、GLES、OpenGL 库，也不代表已经获得 Vulkan 一致性认证。

## 源码与补丁

- 社区源码：[FristOneRR PanVK](https://github.com/FristOneRR-Admin/FristOneRR-Panvk-Source)，
  固定提交 `efd07bab401b263a9ac114560ecdb9a2934f5d43`，保留其 v9 绘制和计算修正。
- DRM 队列与事件实现恢复自其 Mesa 基线
  [funnymdzz/mesa](https://github.com/funnymdzz/mesa/tree/6598829019c0746aa8e473b4ae1c980cbfa6ea4b)，
  保留原文件的 MIT 版权声明。
- [补丁](patches/0001-restore-panfrost-drm-jm.patch) 恢复 JM 的
  `DRM_IOCTL_PANFROST_SUBMIT`、BO handle 列表、DRM syncobj 队列/事件和信号传递；
  禁止无 kbase 后端时引用专用 host-memory import，关闭对应扩展声明，
  并为原生 DRM WSI 恢复真实设备 fd。v9 重提 malloc-vertex 任务时，恢复由 GPU 改写的顶点数组地址及步长，保留任务链和其他绘制状态，避免复用命令缓冲时触发 DATA_INVALID_FAULT。Android 的 `-llog` 仅用于 Android 构建。
  空提交和纯事件批次通过 sync-file 合并保留输入等待，不提交非法的 `jc=0`；
  提交、同步和等待错误返回 device-lost，不依赖 release 中被移除的断言。

第三方源码置于独立缓存，主仓库只维护补丁与构建入口。
已在 G57 真机验证设备枚举及 Wayland 立方体呈现。计算、完整应用兼容性与 Vulkan 一致性仍需分别验证。

## 构建

依赖 C/C++ 工具链、Python、Meson、Ninja、Mako、PyYAML、Bison、Flex、pkg-config、
libdrm、Wayland 开发包和 wayland-protocols；原生 shader 工具还需要 LLVM/Clang、
LLVM-SPIRV、libclc 和符合该 Mesa 版本要求的 SPIRV-Tools。
具体最低版本以源码 Meson 检查为准，不跳过版本检查。

准备固定提交的干净源码工作树后执行：

```sh
python3 Devices/gpu/userspace/panvk/build.py \
  --source <独立Mesa源码目录> --build-dir <固定原生构建目录> \
  --apply --host-tools
```

ARM64 交叉构建先完成上述原生工具构建，再将 `mesa_clc`、`vtn_bindgen2`、
`panfrost_compile` 所在目录加入 PATH，通过 Meson cross file 提供目标工具链、
sysroot 与目标 pkg-config。目标系统必须是 GNU/Linux ARM64，不使用 Android Bionic 库：

```sh
python3 Devices/gpu/userspace/panvk/build.py \
  --source <已应用补丁的Mesa源码目录> --build-dir <固定ARM64构建目录> \
  --cross-file <ARM64交叉配置>
```

构建只启用 Wayland、Panfrost DRM 和 PanVK；禁用 X11/GLX、kbase 与 Gallium。
完整 GLES/OpenGL 的 Wayland-only Mesa 构建需另设 Gallium panfrost、EGL/GLES/GL 选项，
不混进此独立 ICD。

## 隔离验证

保持系统图形栈不变，将 ICD JSON 的 `library_path` 指向新构建的 ARM64
`libvulkan_panfrost.so`，使用绝对路径的私有 JSON 逐进程选择：

```sh
PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 \
VK_DRIVER_FILES=<私有ICD的JSON绝对路径> vulkaninfo --summary
```

旧 Vulkan loader 可使用 `VK_ICD_FILENAMES`。使用 `tests/queue-sync.c` 同时回归 1000 次 IDVS 描述符复用及 fragment payload 保留。
先确认硬件设备是 Mali-G57，
并核对没有落入软件 Vulkan；随后验证离屏计算/图像读回、信号量与事件、
重复提交和 Wayland swapchain。诊断时可逐进程选择 ICD；板级安装通过标准 ICD 目录提供默认发现。
遇到 GPU fault、超时或 GPU reset 时保留内核日志，先定位提交/同步/描述符问题。

### 无硬件测试与设备加载检查

原生构建完成后，使用实际队列源码运行空提交 fence 单元测试，覆盖零等待、
单/多 fence、导出/合并/导入失败及 fd 清理：

```sh
python3 Devices/gpu/userspace/panvk/tests/run-queue-sync.py \
  --build-dir <原生Mesa构建目录>
```

`tests/icd-load.c` 可用 Mesa 自带的 Vulkan 头文件编译，不依赖系统 Vulkan loader：

```sh
cc -I <Mesa源码目录>/include Devices/gpu/userspace/panvk/tests/icd-load.c \
  -Wall -Wextra -Werror -ldl -o <测试输出目录>/icd-load
PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 \
  <测试输出目录>/icd-load <ICD动态库绝对路径> --require-gpu
```

ARM64 设备测试需使用 ARM64 工具链构建此程序。省略 `--require-gpu` 时允许
零设备结果，仅用于无 GPU 的链接/加载测试；不能视为硬件验收。

## 安装到根文件系统

```sh
python3 Devices/gpu/userspace/panvk/install.py \
  --library <ARM64构建的libvulkan_panfrost.so> --destdir <rootfs暂存目录>
w202-panvk-run vulkaninfo --summary
w202-panvk-run vkcube --wsi wayland
```

安装独立库、标准 Panfrost ICD JSON、私有诊断 JSON 与启动器。标准 JSON 使用发行版的 ARM64 Panfrost ICD 文件名，替换该 GPU 的旧 Vulkan 实现，避免重复枚举；不覆盖 OpenGL/EGL。

发行版集成层 打包前，将 `--destdir` 指向 `output/userspace/gpu/rootfs`。板级 `artifacts.json` 定义构建产物接口；安装了 Panfrost 内核模块的所有 desktop 配置都会导入该产物，缺少产物时打包报错。二进制输出不提交到源码仓库。目标需安装 Vulkan loader、vulkan-tools、libdrm、Wayland、zstd、udev 和 C++ 运行库。Wayland 呈现测试需先有运行中的合成器；Server 的 GPU 枚举通过不代表 KDE 或 swapchain 已通过。
