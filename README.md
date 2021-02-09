# ucore-mips-with-comment

### Intro
基于 uCore 的 MIPS 版本，手动添加注释
基于 2020 年 4 月份的 uCore 源码完成
龙芯杯 2020 已结束，此仓库仅供参考，不建议直接编译
既因为在添加注释的过程中不排除改坏了代码
又因为 uCore 也在不断更新中，建议编译最新版 uCore
uCore 仓库地址：[https://github.com/chyyuu/ucore_os_plus](https://github.com/chyyuu/ucore_os_plus)
清华大学龙芯杯参赛使用的 uCore for MIPS ：[https://github.com/chyh1990/ucore-thumips](https://github.com/chyh1990/ucore-thumips)

### Usage

- 编译生成FPGA镜像: `make`
- 编译生成模拟镜像: `make ON_FPGA=n`
- 使用qemu启动镜像: `qemu-system-mipsel -M mipssim -m 32M -kernel obj/ucore-kernel-initrd`
- 使用qemu启动loader:  `qemu-system-mipsel -M mipssim -m 32M -serial stdio -bios boot/loader.bin`
- make所需的gcc: `mipsel-linux-gnu-gcc`
- Debug: `qemu-system-mipsel -M mipssim -m 32M -serial stdio -bios boot/loader.bin -S -s ; sleep 1 ; gnome-terminal -e "mips-sde-elf-gdb"`



```sh
qemu-system-mipsel -M mipssim -m 32M -monitor stdio -kernel obj/ucore-kernel-initrd

qemu-system-mipsel \ # qemu程序
   -M mipssim \ # mips simulation
   -m 32M \ # 32MB 内存
   -monitor stdio \ # 将qemu窗口绑定到stdio
   -kernel obj/ucore-kernel-initrd # kernel模式
```
