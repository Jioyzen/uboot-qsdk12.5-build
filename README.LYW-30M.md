# LYW-30M 专用恢复 U-Boot

系统源码独立维护于 [Jioyzen/openwrt-nss-edma](https://github.com/Jioyzen/openwrt-nss-edma/tree/lyw-30m-nss125-validation)。本仓库只维护引导程序。

## 当前实测状态（2026-10-20）

已在本机跑通：干净原厂系统的隐藏Web管理页开启SSH → 原厂SSH写主APPSBL → 专用不死U-Boot恢复Web → 用户Web刷专用factory UBI → OpenWrt NAND23正常启动。此次SSH开启没有使用TTL；TTL用于恢复准备和被动记录。

已验证运行镜像：`uboot-ipq50xx-lyw_30m-261020_185707_f687f0a-dirty.bin`，655360字节；启动版本 `U-Boot 2016.01-f687f0a-dirty (2026-10-20 18:57:10 +0800)`，SHA256 `5e37b3faa67c816971b2f60400ef0849b3a59f8893e743b87eecfcb6bd071643`。对应功能源码已整理为提交 `d7daf9fc276b0f910b65ccff168fa3c64dbe7363`，尚未推送；二进制在提交前构建，保留实际f687f0a-dirty版本字符串。源码文件校验值和实测清单见 `builds/lyw-30m-stock-web-20261020.json`。

上游为 [chenxin527/uboot-qsdk12.5-build](https://github.com/chenxin527/uboot-qsdk12.5-build)，固定基线 `9ce315735ac88fd0a7c7697bc527a8616ed0a972`；本地维护分支 `lyw-30m-recovery`。源码包含构建脚本及完整 `u-boot-2016/`，不是只有下载器。

## 硬件与布局

- LYW-30M / AX3000_Q06_V2.0_0223，IPQ5018 MP03.3，board ID `0x8040002`，256MiB RAM。
- Winbond W25N01GW，128MiB NAND，2048字节页、64字节OOB、ECC4。
- Reset GPIO27、WPS GPIO38、红灯GPIO23、绿灯GPIO28。
- LAN3恢复Web已验证，默认 `192.168.1.1`；其他恢复网口未逐个验收。
- 原厂MIBIB、SBL1、ART及其他保护区保持原有区间。原厂已预留主/备用APPSBL，各1.25MiB；本机原始及当前APPSBL_1全0xff，没有可用备用引导程序。全区CRC32 `5f3b230d` 是空区的CRC，早期记录曾误称原厂备用程序，现已纠正。不能依赖备用区自动回退。
- OpenWrt单系统UBI：0x00900000起，长度0x07700000（119MiB）。原厂MIBIB未改，由lyw_mtd和Linux板级DTS使用单系统布局。
- NAND23运行时是16个物理MTD：前15个9MiB保护区Linux只读；mtd15/rootfs119MiB可写。UBI卷kernel32LEB、rootfs75LEB、rootfs_data821LEB（自动扩展），初始可写空间约85MiB。

## 编译及回归

在本地维护工作区执行：

```sh
./build.sh check_deps
./build.sh build lyw_30m
```

工具链为ARM32 `staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/`，defconfig为 `ipq5018_lyw_30m_defconfig`。系统固件的AArch64工具链不能直接替代。输出在 `bin/<版本>/`；时间戳/Git描述影响二进制哈希。保留上游脚本串行make，直接并行曾触发combined DTB生成/链接竞态。

实际C校验函数的主机回归：

```sh
python3 tests/lyw-factory-validator-host.py /path/to/lyw-factory.ubi
```

需主机C编译器、zlib及libfdt.so.1，不访问路由器。当前9项用例覆盖256MiB HTTP真实地址、另一加载地址、EC/VID CRC、卷表、错误板型、缺少NAND标志、截断及缺失kernel LEB。

## 支持的系统镜像

“固件更新”用于本机单系统布局的 **factory UBI**。本轮验证文件 `lyw-nss618-nand23-defaults-squashfs-factory.ubi`，14286848字节，SHA256 `66c1a308a1ed99ad901e1cb6e78606de4b76970164265da727ba76718687213b`。它不是其他高通AX3000设备的通用刷机入口；不能直接用官方未适配本机的镜像。Web NAND刷写器不支持sysupgrade tar，系统内升级使用本机专用sysupgrade。

验证器检查UBI EC/VID/卷表CRC、2048/4096几何、kernel/rootfs/rootfs_data卷结构与容量、config@mp03.3及本机NAND DTB标志；上传和实际擦写前均验证。异常响应包含reason。并非所有可能的损坏形式都已穷尽测试。

首次原厂安装时，只有factory写入成功后才保存lyw_mtd、lyw_args、lyw_boot和bootcmd=run lyw_boot，不依赖开发机预先设置启动脚本。身份相关环境字段保留，不同步/修改BOOTCONFIG。Web更新U-Boot只写主APPSBL，不写APPSBL_1。

18:15候选曾把0x41000000误设为FIT工作区，与256MiB设备HTTP上传区重叠，正常factory被安全拒绝、没有擦除。18:57候选改为平台检查允许的HLOS高端工作区，保留64KiB边界保护和输入重叠检查；未扩大malloc池或关闭板型检查。

## 已完成与待验收

本轮已完成：

- 原厂隐藏 `/itms/set/telnet` GET/错误密码拒绝/正确user密码开启SSH、root登录及重启后持久性。
- 原厂SSH写主APPSBL、整个1.25MiB读回一致；其他14个保护区未变。
- Web U-Boot自更新、主分区读回、13个保护区不变，环境仅有意设置一次性Web；软件重启后LAN3千兆Web正常。
- 真实HTTP上传损坏CRC、错误板型被拒；正常factory接受；上传测试前后119MiB系统区散列一致。
- 用户实际Web factory擦写、自动环境迁移、NAND Linux6.18.55启动。kernel/rootfs整个预留卷与factory字节一致；13个保护区未变，ART及本机BDF校验通过；环境CRC有效，仅4个启动键改变。
- NAND952个好PEB、0坏块/损坏块，完整读取后ECC失败和纠正计数均0。双CPU、qca8k/DSA、双频AP及NSS12.5/16MiB初始化正常。

仍待独立验收：物理按住Reset上电进入Web、真正断电后的正常启动、新安装配置下的双频长期负载及WAN/NAT/PPPoE实测。软件reset不能称为断电冷启动。NAND23尚有PSCI电源协调与ath11k额外QMI请求的兼容提示；首次启动、WAN未接等日志已另作分类，不能宣称零警告或已完成长期稳定性验证。

TTL可执行httpd；也已验证failsafe一次性环境进入Web。默认lyw_boot当前没有Linux启动失败自动转入HTTP的完整兜底。恢复Web依赖完整引导链，不能恢复被破坏的SBL1/MIBIB/ART；“不死”不等于任何故障自动恢复。

其他同型号机器仍必须逐台核对板号、NAND/ECC、MIBIB区间、主APPSBL实际启动选择、环境及目标容量。从分区名称定位mtd设备，再核对区间；不照抄本机mtd编号、不刷本机原始保护区备份到另一台。当前不发布未经跨机验证的一键转换脚本，系统升级也不应顺带更新U-Boot。

## 安装与验收文档

- [原厂Web开启SSH和安装步骤](docs/lyw-30m-stock-install.zh-CN.md)
- [Web刷写、分区、启动环境和驱动验收](docs/lyw-30m-validation.zh-CN.md)

用户已确认当前系统稳定、初始化和参数正常。上述文档保留尚无独立日志记录的物理按键及断电测试边界；这不影响提交已验证的源码修复。仓库不包含单台设备的密码、密钥、ART/BDF及完整NAND备份。
