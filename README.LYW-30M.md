# LYW-30M 专用恢复 U-Boot

系统固件独立维护于 [Jioyzen/openwrt-nss-edma](https://github.com/Jioyzen/openwrt-nss-edma/tree/lyw-30m-nss125-validation)。本仓库只维护引导程序。

## 来源

- 上游：[chenxin527/uboot-qsdk12.5-build](https://github.com/chenxin527/uboot-qsdk12.5-build)。仓库同时包含构建脚本和 `u-boot-2016/` 源码，不只是下载器。
- 固定上游基线：`9ce315735ac88fd0a7c7697bc527a8616ed0a972`。
- LYW-30M 开发分支：`lyw-30m-recovery`。
- 当前设备已安装版本：`8bcf93b41dd79760f907d47a7df9d399ee3ee17d`，U-Boot 2016.01。
- 已安装镜像大小：655360 字节，SHA256：`b14b935654e7332abc2af677fcceddf54cf915670b7ddab022769afe47a4684d`。

本分支后续提交补充维护说明，并修正 Web 自更新中的备用 APPSBL 判断：LYW-30M 不将新镜像同步写入 `0:APPSBL_1`，保留现存备用副本；其他板型保持上游行为。LYW-30M 的 Web 系统刷写入口也已取消通用 `bootconfig set firmware 0` 操作：本机单系统使用 `lyw_boot`，刷系统不应改动或同步原厂 BOOTCONFIG。这些源码修正尚未重新刷入设备。当前安装版本在本机有备用分区时会跳过备用写入，但原判断错误地在分区不存在时尝试写入，不能推广到其他分区布局。

## 硬件及布局

- LYW-30M / AX3000_Q06_V2.0_0223，IPQ5018 MP03.3，board ID `0x8040002`。
- Winbond W25N01GW，128MiB NAND，2048 字节页、64 字节 OOB、ECC4。
- reset GPIO27，WPS GPIO38，红灯 GPIO23，绿灯 GPIO28。
- 恢复 Web：已验证外壳 LAN3，默认 `192.168.1.1`。其他网口未逐个验收。
- 单系统 UBI：起点 `0x00900000`，长度 `0x07700000`（119MiB）。引导链、ART 等不属于该区域。
- 默认启动：读取 UBI 的 `kernel` 卷，以 `config@mp03.3` 启动；Linux 使用 `rootfs` 的 squashfs/ubiblock 和可写数据卷。

## 编译

```sh
git clone --branch lyw-30m-recovery --single-branch https://github.com/Jioyzen/uboot-qsdk12.5-build.git
cd uboot-qsdk12.5-build
./build.sh check_deps
./build.sh build lyw_30m
```

按上游说明安装缺少的依赖。脚本使用 `staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/` 的 ARM32 工具链，目标 defconfig 为 `ipq5018_lyw_30m_defconfig`；输出位于 `bin/<版本>/`。固件系统本身的 AArch64 工具链不能直接替代这里的 ARM32 工具链。时间戳和 Git 描述会影响镜像哈希。保留上游脚本的串行 make：直接并行构建会触发 combined DTB 生成/链接竞态，本次检查也复现了该问题。

## 支持的镜像与测试范围

Web 的固件入口面向**本机单系统布局的 UBI factory 镜像**，不是所有高通 AX3000 固件的通用刷机入口。当前 NAND Web 刷写器不支持 OpenWrt sysupgrade tar；系统内升级请使用系统仓库的专用 sysupgrade。原厂升级包和其他机型镜像没有兼容性承诺。

目前 UBI 上传校验只检查格式标志与尺寸，不能证明内部 kernel、rootfs、板型、ECC 和布局正确。测试过的较大文件上传仅证明接收和尺寸检查，不等于实际刷写或启动验证。完整 factory UBI 的 Web 擦写/恢复验收仍待完成。

已完成：RAM 链式启动、NAND 安装后 Linux 启动、LAN3 HTTP、默认环境在 RAM 中恢复后的启动、Web U-Boot 自更新及备用 APPSBL 保留检查。这些重启主要为软件 reset；物理断电冷启动及按住 reset 上电进入 Web 尚未完成独立验收。

TTL 下可执行 `httpd`；上游还提供 failsafe 环境变量和 GPIO 按键入口。本机默认 `lyw_boot` 脚本尚无启动失败自动转入 HTTP 的兜底，不能宣称任何损坏都能自动恢复。“不死”仅指恢复功能，不代表覆盖引导链后仍可恢复。

## 原厂机器安装

原厂 SSH root 可以作为读取、备份和写入分区的操作通道，但不代表任意原厂 LYW-30M 都已通过完整免 TTL 安装验证。安装前必须逐台确认板型、NAND/ECC、MIBIB、APPSBL 及备用分区、启动环境、镜像尺寸和恢复路径。当前仓库不提供未经验证的一键原厂转换脚本，也不将备用 APPSBL 的存在等同于必然自动回退。

维护时先 RAM chainload，再验证读 NAND、HTTP、Linux 启动、备用引导副本及物理按键；完成这些验证后再发布对应二进制。系统升级不应顺带更新 U-Boot。
