# Server GPU 着色器检查

`w202-gpu-check.c` 用 EGL surfaceless 创建 GLES 上下文，只接受 Mali/Panfrost renderer。
它编译顶点/片元着色器、用 VBO 绘制三角形，100 次改变颜色，每次回读并校验整个
64×64 RGBA 图像，失败返回非零。它使用 render 节点，不接管显示或桌面。

提供目标系统的 EGL/GLES2/KHR 开发头文件后构建：

```sh
aarch64-linux-gnu-gcc -O2 -g0 -Wall -Wextra -Werror -std=gnu11 \
  -I /path/to/target/include w202-gpu-check.c -ldl -o /absolute/task/output/w202-gpu-check.bin
```

在目标系统直接运行生成的程序，动态加载匹配的 EGL/GLES 库；产物不放回源码目录。
计时包含同步回读和 CPU 校验，不是显示 FPS。
