# LYW-30M 原厂转换和 NAND23 验收

2026-10-20已实测完整原厂Web开SSH、原厂SSH刷专用U-Boot、恢复Web刷OpenWrt的路径。用户确认工作稳定、初始化及参数正常。原厂启动链恢复准备可用TTL；本轮SSH开启和U-Boot安装不依赖TTL命令。

## 写入和启动证据

- 干净原厂UBI使用原始kernel、wifi_fw、bt_fw、ubi_rootfs，rootfs_data清空，排除历史SSH配置。
- 原厂22/23最初关闭，错误user密码被拒，正确密码仅开启22；root登录和软件重启后持久性通过。
- 原厂SSH写主APPSBL后整个1.25MiB读回一致，其他14个保护区不变，ECC失败和坏块为0。
- 修正上传工作区后，恢复Web自更新主U-Boot、整分区读回和软件重启Web通过；13个保护区不变，环境仅有意用于一次性Web。
- 真实HTTP上传损坏CRC及错误板型被拒，NAND23 factory接受；只测试上传前后，119MiB系统区散列相同。
- 用户随后手动执行factory擦写及重启，Linux6.18.55、双CPU、LAN3千兆、DSA、双频AP和NSS12.5/16MiB正常初始化。
- kernel和rootfs整个预留卷与factory逐字节一致；ART和本机BDF验证通过。NAND952个好PEB，0坏块、0损坏块，完整读取后ECC失败/纠正计数均0；坏块预留20PEB。
- 原厂环境CRC有效，仅bootcmd、lyw_mtd、lyw_args、lyw_boot四个键改变，其他身份字段未变。SBL1、MIBIB、两份BOOTCONFIG等13个保护区未变。

## 实际布局

原厂前15个物理区间合计9MiB原样保留，Linux只读；mtd15/rootfs从0x900000到0x8000000，119MiB单系统UBI读写。

| 卷 | 名称 | LEB数 | 容量 |
|---|---|---:|---:|
| ubi0_0 | kernel | 32 | 3.875MiB |
| ubi0_1 | rootfs | 75 | 9.082MiB |
| ubi0_2 | rootfs_data | 821 | 99.418MiB |

数据卷由9LEB自动扩到821LEB，扣除文件系统开销后初始可写余量约85MiB。UBI available=0表示可用LEB已经分配给数据卷，不代表磁盘已满；/rom为只读squashfs，显示100%正常。

APPSBL_1原始及当前均全0xff。预留备用分区不是预装了备用引导程序，不依赖它进行自动回退。

## 驱动和日志范围

本轮未见OOM、panic、无线固件崩溃、主机NSS分配失败、坏描述符或无线RX/TX描述符分配失败。NSS及两路ath11k卸载启用，256M内存配置生效，TX描述符4096、CE5为256。

日志不是零警告：

- PSCI电源协调模式调用返回-1，来自原厂安全固件接口；双CPU已启动，尚未为此改写TZ/QSEE。
- ath11k忽略额外QMI内存请求type10，随后双频初始化成功；其固件请求含义与长期负载仍需跟踪。
- 首次启动的seed缺失随后恢复，/init探测及overlay UUID提示未妨碍NAND启动。
- ART裸NAND读取的mtdblock建议不代表校准损坏，不应为消除它而格式化ART。
- WAN未接，DNS无上游及IPv6无默认路由提示符合环境；本次没有验证WAN/NAT/PPPoE实际负载。
- U-Boot探测无SPI/MMC、交换机CPU口PHY及旧QPIC节点fixup的提示，未妨碍当前LAN3恢复Web和Linux NAND/DSA运行；其余恢复网口未逐个验收。

MemTotal163.37MiB。大批SSH读回后MemAvailable一度约31MiB，停止大批读取后的45秒轻量采样为33.55–33.77MiB，并观察到SKB和Slab回收；不将此当作长时间无泄漏的证明。本轮新默认2.4GHz是HE20、5GHz是HE80，不能与旧160MHz测速直接比较。

物理按住Reset上电及真正断电启动仍无独立日志证据；用户的稳定运行确认与已采集检查分别记录。本次源码整理不重启路由、不更换已运行镜像。二进制编译时的f687f0a-dirty版本字符串与提交后的源码SHA不同，由清单的源码文件SHA256建立对应，不伪称已重编或重刷。
