# LYW-30M 原厂系统免拆机安装

已于2026-10-20在 LYW-30M / AX3000_Q06_V2.0_0223、IPQ5018 + QCN6102 + QCA8337、256MiB RAM、W25N01GW 128MiB NAND 上跑通完整流程。原厂内核为4.4.60、原厂U-Boot为2023-02-24版本。相同外壳型号不能代替硬件和分区核对。

## 原厂 Web 开启 SSH

1. 网线连接LAN3，确认设备管理地址。干净原厂系统固定地址为192.168.10.1，也可能经LAN DHCP获得另一地址；如果改过LAN地址，使用实际地址。
2. 浏览器打开 `http://设备地址/itms/set/telnet`，页面标题为“Telnet_SSH管理”。路径不是 `/set/dGVsbmV0`，也不是OpenWrt的 `/cgi-bin/luci/`。
3. Telnet保持关闭，SSH开启；省份保持页面当前值，本机是XIN。
4. 输入本机普通 `user` 管理账号的密码，提交。页面自己做标准Base64编码，不要预先编码浏览器输入框中的密码。
5. 页面显示“操作成功”，22端口开放。用 `ssh root@设备地址` 登录；SSH用该原厂固件的root口令，与user密码不同。原厂固件初始化脚本 `/etc/uci-defaults/z_ctcapd` 中的 `PASSWORD_ROOT` 可在解包原厂rootfs后核实；不要假定其他版本的root口令相同。本仓库不保存本机管理密码、原厂口令和shadow数据。
6. `id` 应返回uid=0(root)。本机实际验证了错误user密码拒绝、正确密码开启、root登录，以及重启后SSH继续开启；不需要TTL来开启SSH。

较新OpenSSH如明确报告ssh-rsa协商失败，可加 `-o HostKeyAlgorithms=+ssh-rsa`。重刷导致主机密钥改变时，应核对目标设备后更新对应known_hosts。

后端POST同一路径，字段为：

| 字段 | 开启SSH、关闭Telnet |
|---|---|
| telnet_1 | no |
| telnet_2 | yes |
| tel_password | 普通user管理密码的标准Base64 |
| prov | 当前省份，本机XIN |

不要省略prov，原厂后端省略时默认SHA，可能改变省份。该入口先于普通LuCI会话认证，但自己校验user密码，是带密码校验的原厂隐藏管理功能，不能称为无密码解锁。明文后端在原厂 `usr/lib/lua/luci/dispatcher.lua` 的 `/set/telnet` 分支，页面在 `usr/lib/lua/luci/view/telnet.htm`；通过原生 `ctcapd.telnetssh set` 开启Dropbear。

## 原厂 SSH 安装专用 U-Boot

先确认板号、NAND页/OOB/ECC、MIBIB区间、BOOTCONFIG实际选择的APPSBL、目标容量、坏块状态，保存本机引导链/环境/ART等备份到电脑并校验。当前实测目标从主APPSBL启动。本机保护区编号如下，其他机器必须重新从名称定位：

| 本机编号 | 名称 | 大小 |
|---|---|---|
| mtd10 | 0:APPSBLENV | 0x80000 |
| mtd11 | 0:APPSBL | 0x140000 |
| mtd12 | 0:APPSBL_1 | 0x140000 |

可用下列只读命令查找主分区，不把设备编号当成型号常量：

```sh
cat /proc/mtd
awk -F '"' '$2 == "0:APPSBL" { split($1, p, ":"); print p[1] }' /proc/mtd
```

本轮先将已验证655360字节镜像以0xff补到实际主分区大小，在电脑端及上传后分别校验SHA256，再通过原厂 `mtd -f write 镜像路径 /dev/已核验的mtd编号` 写主APPSBL。`-f`不替代上述检查。不要把 `0:APPSBL` 直接传给该版mtd作为设备名：它把冒号当多设备分隔符，会尝试打开错误的 `0`。写后读回整个主分区与补齐镜像逐字节比对，核对其他保护区未变及ECC无错，然后才能重启。

不写SBL1、MIBIB、ART或备用APPSBL；不使用其他机器的原始保护区备份。本机APPSBL_1原厂就存在，但全0xff，没有可用备用U-Boot。其1.25MiB全区CRC32 `5f3b230d` 是空区的CRC，不能据此认为存在原厂备用程序。

通过原厂SSH运行 `fw_setenv failsafe 1`，保留 `bootcmd=bootipq`，同步并重启。新U-Boot消费一次性failsafe后进入Web。这样能验证首次安装自动迁移，无需预先用TTL写入lyw_boot变量。本轮流程只用SSH写引导，TTL仅用于恢复准备和被动日志。

## 恢复 Web 安装 OpenWrt

1. 电脑连接LAN3，配置能访问192.168.1.0/24的地址，打开 `http://192.168.1.1/firmware.html`。
2. 在“固件更新”选择本机单系统factory UBI；不要选sysupgrade.bin、initramfs或U-Boot本体。
3. 上传校验通过后执行刷写，再由页面自动或手动重启。本轮用户手动完成了该操作。
4. 本机factory写入0x900000起的119MiB系统区，成功后保存lyw_mtd、lyw_args、lyw_boot和bootcmd，保留其他原厂身份字段；不修改BOOTCONFIG和MIBIB。
5. NAND23默认LAN192.168.2.1/24，root/password，HTTP80，WAN DHCP，三个LAN及Wi-Fi桥接并提供DHCP；开放SSID LYW-2.4G / LYW-5G，CN、自动信道、HE20/HE80，未固定发射功率。

本轮factory为 `lyw-nss618-nand23-defaults-squashfs-factory.ubi`，14286848字节，SHA256 `66c1a308a1ed99ad901e1cb6e78606de4b76970164265da727ba76718687213b`。系统仓库与U-Boot仓库独立维护，更新系统不需要顺带更新U-Boot。

完整实测结果见 [验收记录](lyw-30m-validation.zh-CN.md) 和 `builds/lyw-30m-stock-web-20261020.json`。物理按住Reset上电和真正断电后的恢复入口仍没有独立测量记录；软件重启成功不等于已经验证所有故障恢复情况。本仓库不分发单台设备绑定的演练写入脚本。
