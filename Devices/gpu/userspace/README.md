# W202DS GPU 用户态

[tests/check](tests/check/README.md) 提供 EGL/GLES 离屏诊断；运行依赖目标系统的 Mesa 和 render 节点，不属于常驻服务。

[panvk](panvk/README.md) 维护 Mali-G57 PanVK 的原生 Panfrost DRM 移植补丁、
Wayland-only 独立 ICD 构建入口及板级 rootfs 产物接口。所有 desktop 配置共用该 Vulkan ICD，不替换系统 OpenGL/EGL。

`mesa/` 是忽略的可选第三方源码缓存：

| 子目录 | 来源 | 固定提交 |
| --- | --- | --- |
| `panvk-mali-jm` | [FristOneRR PanVK](https://github.com/FristOneRR-Admin/FristOneRR-Panvk-Source) | `efd07bab401b263a9ac114560ecdb9a2934f5d43` |
| `droiddeck-nonadreno-drivers` | [DroidDeck 配方](https://github.com/The412Banner/droiddeck-nonadreno-drivers) | `421d99e4ef2b2cf0e2e51728bdc28225fc38c51a` |

这些实验树保留独立 Git 历史，不作为主仓库的嵌入仓库发布。它们的 kbase 路径不能直接代替本板 Panfrost DRM 后端，聚合入口不执行社区下载/构建脚本，也不宣称 Vulkan 可用。系统 Mesa 由发行版提供。

内核 Panfrost 和调频代码在 `../source/panfrost/`。

上层调用约定见 [标准接口](../../INTERFACES.md)。

桌面会话通过板级 `environment.d` 配置开启当前 PanVK JM 驱动的显式实验性
开关，沿用标准 Vulkan loader/ICD 自动发现；不覆盖其他 ICD，也不替换
EGL/GLES/OpenGL。独立命令行诊断仍可使用 `w202-panvk-run` 指定该板级 ICD。
此开关不代表通过 Vulkan 一致性测试。
