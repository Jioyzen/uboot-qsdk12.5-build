

# uBootKit - Qualcomm IPQ Platform U-Boot

**LYW-30M 专用分支：[镜像下载与本机说明](README.LYW-30M.md) · [Releases](https://github.com/Jioyzen/uboot-qsdk12.5-build/releases/tag/lyw-30m-web-20261020) · [原厂免拆机刷 OpenWrt 教程](https://github.com/Jioyzen/openwrt-nss-edma/blob/lyw-30m-nss125-validation/README.md)。本机支持范围和实测状态以专用说明为准。**

## 项目简介

- 支持通过 DHCP 为客户端分配 IP，无需手动固定 IP。
- 自动过滤来自上级路由的 DHCP 报文（OFFER / ACK 等），防止干扰 U-Boot DHCP（IPQ50xx 暂不支持）。
- 类 Argon 风格的 Web 界面，针对移动端也进行了优化。

## 浏览器兼容性

由于 Web 前端使用了很多新特性（gzip 压缩等），所以建议使用新版浏览器访问，过于古老的浏览器可能无法正常渲染。

| 浏览器 | 最低版本 | 推荐版本 | 兼容状态 |
|:-------|:--------|:--------|:---------|
| **Google Chrome** | 80+ | 最新版 | ✅ **兼容** |
| **Microsoft Edge** | 80+ | 最新版 | ✅ **兼容** |
| **Mozilla Firefox** | 75+ | 最新版 | ✅ **兼容** |
| **Apple Safari** | 14.1+ | 最新版 | ✅ **兼容** |
| **Opera** | 67+ | 最新版 | ✅ **兼容** |
| **Internet Explorer** | - | - | ❌ **不兼容** |

## 进 Web 界面

默认后台 IP 为 192.168.1.1（进入 Web 后可在 “系统 -> 网络设置” 页面下自定义，建议修改为与上级路由不同网段的 IP）。

| 方法                                                   | 说明                                                         |
| :----------------------------------------------------- | :----------------------------------------------------------- |
| 按键                                                   | 有什么键就按什么键，Reset、WPS、Mesh、Joy、Screen、Sync 等等，除电源开关（部分机型有）外都能按。按住按键后给机器通电，观察到 LED 闪烁 3 次后常亮说明 httpd 已经启动。 |
| 环境变量                                               | 设置 failsafe 环境变量：1. 设为 "always"，U-Boot 每次启动都会进入 Web，直到手动删除 failsafe 环境变量（测试用，日常使用一般用不上）；2. 设为其他任意合法非空值，U-Boot 会进入一次 Web 并删掉 failsafe 环境变量，防止下次启动重复进入 Web。 |
| [uBootEnter](https://github.com/chenxin527/uBootEnter) | U-Boot 启动中断工具，可用于中断 U-Boot 自动启动流程，中断成功后会自动打开系统默认浏览器并跳转到 U-Boot Web 界面。IPQ50xx 暂不支持。 |
| 其他                                                   | 通过 9008 启动时自动进入 Web；bootipq 失败后自动进入 Web；TTL 下手动执行 httpd 进入 Web。 |

## 支持设备

| 平台    | 设备                        | 型号              | 备注                         |
| :------ | :-------------------------- | :---------------- | :--------------------------- |
| IPQ50xx | CMCC MR3000D-CI             | cmcc_mr3000d-ci   |                              |
| IPQ50xx | CMCC PZ-L8                  | cmcc_pz-l8        |                              |
| IPQ50xx | CMCC RAX3000Q(Y)            | cmcc_rax3000q     | LAN3 口（靠近 Reset 键）不通 |
| IPQ50xx | CUCC VS010                  | cucc_vs010        |                              |
| IPQ50xx | JDCloud AX3000              | jdcloud_ax3000    | 所有网口都可以使用；无法启动 6.18 内核的固件 ([pmyy-wt/jdc_re-cs-03](https://github.com/pmyy-wt/jdc_re-cs-03/releases) 仓库 2026-07-27 及之后的固件) |
| IPQ53xx | JDCloud BE6500              | jdcloud_re-cs-06  |                              |
| IPQ53xx | JDCloud ER2                 | jdcloud_re-cs-08  | 待测试；MAC 位置未明确       |
| IPQ53xx | Xiaomi BE3600 Pro (5/8 Ethernet ports) | xiaomi_be3600-pro | 该机型正式版可能有锁，若有锁则不要刷写此 U-Boot |
| IPQ60xx | AnySafe E1                  | anysafe_e1        | 待测试                        |
| IPQ60xx | CMIOT AX18                  | cmiot_ax18        |                              |
| IPQ60xx | GL.iNet AX1800              | glinet_gl-ax1800  | 待测试                        |
| IPQ60xx | JDCloud AX6600 (Athena)     | jdcloud_re-cs-02  |                              |
| IPQ60xx | JDCloud ER1                 | jdcloud_re-cs-07  |                              |
| IPQ60xx | JDCloud AX1800 Pro (Arthur) | jdcloud_re-ss-01  |                              |
| IPQ60xx | Link NN6000                 | link_nn6000       |                              |
| IPQ60xx | OceanBlue Cloud S200-H      | oceanblue_s200-h  | 待测试                        |
| IPQ60xx | Philips LY1800              | philips_ly1800    |                              |
| IPQ60xx | Qihoo 360V6                 | qihoo_360v6       |                              |
| IPQ60xx | Redmi AX5                   | redmi_ax5         | 待测试                        |
| IPQ60xx | Redmi AX5 JDCloud           | redmi_ax5-jdcloud |                              |
| IPQ60xx | SY Y6010                    | sy_y6010          |                              |
| IPQ60xx | Xiaomi AX1800               | xiaomi_ax1800     | 待测试                        |
| IPQ60xx | ZN M2                       | zn_m2             |                              |
| IPQ807x | Aliyun AP8220               | aliyun_ap8220     |                              |
| IPQ807x | Cradlepoint E320            | cradlepoint_e320  | 待测试；原机 CPU 有锁，需更换无锁 CPU 才能刷写此 U-Boot |
| IPQ807x | Inseego FG2000              | inseego_fg2000    | 5G 网口未驱动                |
| IPQ807x | OPPO CKB01 (SoftBank Air 5G) | oppo_ckb01       |                              |
| IPQ807x | Redmi AX6                   | redmi_ax6         |                              |
| IPQ807x | Xiaomi AX3600               | xiaomi_ax3600     |                              |
| IPQ807x | Xiaomi AX9000               | xiaomi_ax9000     | 待测试                        |

## 支持固件

### eMMC 固件

| 类型                                                | 支持 | 说明                                                         |
| :-------------------------------------------------- | :--- | :----------------------------------------------------------- |
| OpenWrt Factory                                     | 是   | 由 kernel 和 rootfs 简单拼接生成，其中 kernel 会被填充到固定大小（目前常见 6 MiB 和 12 MiB）。只要 kernel 大小为 2 MiB 的整数倍，U-Boot 均可自动识别。 |
| OpenWrt Sysupgrade                                  | 是   | Tar 格式镜像，包含控制信息（板子型号）、kernel 和  rootfs。该格式固件由 untar 动态解析，对 kernel 大小无特殊要求。 |
| 京东云官方固件                                      | 是   | 支持哪吒/后羿、赵云、亚瑟、雅典娜、太乙的官方固件；太乙 Plus 理论上和赵云一样（待验证）。 |
| [ASUSWRT](https://github.com/N-wrt/asuswrt-ipq6018) | 是   | 去掉 64 字节的 Legacy Image 头后即为 Tar 格式镜像，与 OpenWrt Sysupgrade 类似，同样由 untar 动态解析。注意，U-Boot 仅完成刷写部分（将 kernel 与 rootfs 刷写到对应分区），前置工作请自行参考相关教程完成。 |

### NAND 固件

| 类型                                                | 支持 | 说明                                                         |
| :-------------------------------------------------- | :--- | :----------------------------------------------------------- |
| OpenWrt Factory                                     | 是   | 一般为 UBI 固件。整个固件都被刷写到 rootfs 分区，无需区分 kernel 大小。 |
| OpenWrt Sysupgrade                                  | 否   | 格式同 eMMC 固件。暂不支持刷写。                             |
| GL.iNet 官方固件                                    | 是   | 3.x 和 4.x 版本的固件均支持。                                |
| [ASUSWRT](https://github.com/N-wrt/asuswrt-ipq6018) | 否   | 暂不支持刷写。                                               |

### NOR 固件

分区表参考高通 [meta-tools](https://github.com/chenxin527/meta-tools) 中的 nor-partition.xml。

| 类型               | 支持 | 说明                             |
| :----------------- | :--- | :------------------------------- |
| OpenWrt Factory    | 是   | 格式及刷写方式与 eMMC 固件一致。 |
| OpenWrt Sysupgrade | 是   | 格式及刷写方式与 eMMC 固件一致。 |

## 快速开始

### 下载编译好的文件

不想自己编译的直接到 Release 中下载编译好的文件即可：[点击前往最新 Release](https://github.com/chenxin527/uboot-qsdk12.5-build/releases/latest)。

### 自行编译

#### 本地编译

> 建议使用 Ubuntu 进行编译。

##### 1. 克隆项目到本地

```bash
git clone https://github.com/chenxin527/uboot-qsdk12.5-build.git
cd uboot-qsdk12.5-build
```

##### 2. 安装编译依赖

```bash
sudo ./build.sh install_deps
```

##### 3. 检查编译依赖

```bash
./build.sh check_deps
```

##### 4. 查看帮助文档

```bash
用法: ./build.sh [命令] [参数]

命令:
  build <目标>            编译指定的目标
  setup_env               在当前 Shell 中设置编译环境 (需使用 source 执行)
  check_deps              检查编译所需的依赖
  install_deps            安装编译所需的依赖 (需要 root 权限)
  clean_cache             清理编译缓存
  help                    显示此帮助信息

编译目标:
  all                     编译所有设备
  <平台名>                编译指定平台下的所有设备
  <设备名>                编译指定的单个设备

支持的平台:
  ipq50xx, ipq53xx, ipq60xx, ipq807x

支持的设备:
  ipq50xx:
    cmcc_mr3000d-ci           CMCC MR3000D-CI
    cmcc_pz-l8                CMCC PZ-L8
    cmcc_rax3000q             CMCC RAX3000Q(Y)
    cucc_vs010                CUCC VS010
    jdcloud_ax3000            JDCloud AX3000

  ipq53xx:
    jdcloud_re-cs-06          JDCloud BE6500
    jdcloud_re-cs-08          JDCloud ER2
    xiaomi_be3600-pro         Xiaomi BE3600 Pro (5/8 Ethernet ports)

  ipq60xx:
    anysafe_e1                AnySafe E1
    cmiot_ax18                CMIOT AX18
    glinet_gl-ax1800          GL.iNet AX1800
    jdcloud_re-cs-02          JDCloud AX6600 (Athena)
    jdcloud_re-cs-07          JDCloud ER1
    jdcloud_re-ss-01          JDCloud AX1800 Pro (Arthur)
    link_nn6000               Link NN6000
    oceanblue_s200-h          OceanBlue Cloud S200-H
    philips_ly1800            Philips LY1800
    qihoo_360v6               Qihoo 360V6
    redmi_ax5                 Redmi AX5
    redmi_ax5-jdcloud         Redmi AX5 JDCloud
    sy_y6010                  SY Y6010
    xiaomi_ax1800             Xiaomi AX1800
    zn_m2                     ZN M2

  ipq807x:
    aliyun_ap8220             Aliyun AP8220
    cradlepoint_e320          Cradlepoint E320
    inseego_fg2000            Inseego FG2000
    oppo_ckb01                OPPO CKB01 (SoftBank Air 5G)
    redmi_ax6                 Redmi AX6
    xiaomi_ax3600             Xiaomi AX3600
    xiaomi_ax9000             Xiaomi AX9000


示例:
  ./build.sh build all                     编译所有设备
  ./build.sh build ipq60xx                 编译 IPQ60xx 平台下的所有设备
  ./build.sh build jdcloud_re-ss-01        编译 JDCloud AX1800 Pro (Arthur)
  source ./build.sh setup_env              在当前 Shell 中设置编译环境
  ./build.sh check_deps                    检查编译所需的依赖
  sudo ./build.sh install_deps             安装编译所需的依赖
```

##### 5. 开始编译

根据所需设备按需编译。

#### 云编译

Fork 本仓库后，在 GitHub Actions 中手动触发“构建并发布”工作流进行云编译。

## 相关项目

- [uBootEnter](https://github.com/chenxin527/uBootEnter): U-Boot 启动中断工具

## 鸣谢

- [qsdk/u-boot-2016](https://git.codelinaro.org/clo/qsdk/oss/boot/u-boot-2016/-/tree/NHSS.QSDK.12.5?ref_type=heads)
- [Yuzhii0718/bl-mt798x-dhcpd](https://github.com/Yuzhii0718/bl-mt798x-dhcpd)
- [1980490718/u-boot-2016](https://github.com/1980490718/u-boot-2016)
