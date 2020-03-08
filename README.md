# ucore-mips-with-comment

- 编译生成FPGA镜像: `make`
- 编译生成模拟镜像: `make ON_FPGA=n`
- 使用qemu启动镜像: `qemu-system-mipsel -M mipssim -m 32M -kernel obj/ucore-kernel-initrd`
- 使用qemu启动loader:  `qemu-system-mipsel -M mipssim -m 32M -serial stdio -bios boot/loader.bin`
- make所需的gcc: `mipsel-linux-gnu-gcc`
- Debug: `qemu-system-mipsel -M mipssim -m 32M -serial stdio -bios boot/loader.bin -S -s ; sleep 1 ; gnome-terminal -e "mips-sde-elf-gdb"`



```sh
qemu-system-mipsel \ # qemu程序
   -M mipssim \ # mips simulation
   -m 32M \ # 32MB 内存
   -monitor stdio \ # 将qemu窗口绑定到stdio
   -kernel obj/ucore-kernel-initrd # kernel模式
```
