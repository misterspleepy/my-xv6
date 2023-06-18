# my-xv6
this is a unix-like operating system, which refer to a lot of [xv6-riscv](https://github.com/mit-pdos/xv6-riscv) source code

## Get started
```bash
export my-xv6=path_to_my_xv6
cd $my-xv6
docker run -it --rm -v `pwd`:/home/xv6/xv6-riscv  wtakuo/xv6-env
make qemu
```