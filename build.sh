#!/bin/bash
set -o pipefail

# ============================================
# IPQ 平台 U-Boot 构建脚本
# 支持平台: ipq50xx, ipq53xx, ipq60xx, ipq807x, ipq95xx
# ============================================

# 检查是否在 GitHub Actions 环境中运行
is_github_actions() {
    if [ -n "$GITHUB_ACTIONS" ] && [ "$GITHUB_ACTIONS" = "true" ]; then
        return 0
    fi
    return 1
}

# 获取脚本所在目录的绝对路径
get_script_dir() {
    if [ -n "$GITHUB_WORKSPACE" ]; then
        echo "$GITHUB_WORKSPACE"
    else
        local script_source="${BASH_SOURCE[0]:-$0}"
        cd "$(dirname "$script_source")" && pwd
    fi
}

SCRIPT_DIR=$(get_script_dir)

# ============================================
# 平台配置
# ============================================

declare -A PLATFORM_CONFIG
declare -A PLATFORM_CONFIG_ELFBIN_VERSION

# 平台配置: [平台名]="MBN版本"
PLATFORM_CONFIG["ipq50xx"]="3"
PLATFORM_CONFIG["ipq53xx"]="6"
PLATFORM_CONFIG["ipq60xx"]="6"
PLATFORM_CONFIG["ipq807x"]="3"
PLATFORM_CONFIG["ipq95xx"]="6"

# 获取平台配置
get_platform_config() {
    local platform=$1
    local key=$2
    local config="${PLATFORM_CONFIG[$platform]}"

    if [ -z "$config" ]; then
        return 1
    fi

    IFS=':' read -r mbn_version <<< "$config"

    case "$key" in
        "mbn_version") echo "$mbn_version" ;;
        *) return 1 ;;
    esac
}

# ============================================
# 设备配置
# ============================================

# 设备配置:
# 格式: "平台:设备名:配置名:友好名"
DEVICE_LIST=(
    "ipq50xx:cmcc_mr3000d-ci:ipq5018_cmcc_mr3000d_ci:CMCC MR3000D-CI"
    "ipq50xx:cmcc_pz-l8:ipq5018_cmcc_pz_l8:CMCC PZ-L8"
    "ipq50xx:cmcc_rax3000q:ipq5018_cmcc_rax3000q:CMCC RAX3000Q(Y)"
    "ipq50xx:lyw_30m:ipq5018_lyw_30m:Lingyan LYW-30M recovery"
    "ipq50xx:cucc_vs010:ipq5018_cucc_vs010:CUCC VS010"
    "ipq50xx:jdcloud_ax3000:ipq5018_jdcloud_ax3000:JDCloud AX3000"
    "ipq53xx:jdcloud_re-cs-06:ipq5332_jdcloud_re_cs_06:JDCloud BE6500"
    "ipq53xx:jdcloud_re-cs-08:ipq5332_jdcloud_re_cs_08:JDCloud ER2"
    "ipq53xx:xiaomi_be3600-pro:ipq5332_xiaomi_be3600_pro:Xiaomi BE3600 Pro (5/8 Ethernet ports)"
    "ipq60xx:anysafe_e1:ipq6018_anysafe_e1:AnySafe E1"
    "ipq60xx:cmiot_ax18:ipq6018_cmiot_ax18:CMIOT AX18"
    "ipq60xx:glinet_gl-ax1800:ipq6018_glinet_gl_ax1800:GL.iNet AX1800"
    "ipq60xx:jdcloud_re-cs-02:ipq6018_jdcloud_re_cs_02:JDCloud AX6600 (Athena)"
    "ipq60xx:jdcloud_re-cs-07:ipq6018_jdcloud_re_cs_07:JDCloud ER1"
    "ipq60xx:jdcloud_re-ss-01:ipq6018_jdcloud_re_ss_01:JDCloud AX1800 Pro (Arthur)"
    "ipq60xx:link_nn6000:ipq6018_link_nn6000:Link NN6000"
    "ipq60xx:oceanblue_s200-h:ipq6018_oceanblue_s200_h:OceanBlue Cloud S200-H"
    "ipq60xx:philips_ly1800:ipq6018_philips_ly1800:Philips LY1800"
    "ipq60xx:qihoo_360v6:ipq6018_qihoo_360v6:Qihoo 360V6"
    "ipq60xx:redmi_ax5:ipq6018_redmi_ax5:Redmi AX5"
    "ipq60xx:redmi_ax5-jdcloud:ipq6018_redmi_ax5_jdcloud:Redmi AX5 JDCloud"
    "ipq60xx:sy_y6010:ipq6018_sy_y6010:SY Y6010"
    "ipq60xx:xiaomi_ax1800:ipq6018_xiaomi_ax1800:Xiaomi AX1800"
    "ipq60xx:zn_m2:ipq6018_zn_m2:ZN M2"
    "ipq807x:aliyun_ap8220:ipq807x_aliyun_ap8220:Aliyun AP8220"
    "ipq807x:cradlepoint_e320:ipq807x_cradlepoint_e320:Cradlepoint E320"
    "ipq807x:inseego_fg2000:ipq807x_inseego_fg2000:Inseego FG2000"
    "ipq807x:oppo_ckb01:ipq807x_oppo_ckb01:OPPO CKB01 (SoftBank Air 5G)"
    "ipq807x:redmi_ax6:ipq807x_redmi_ax6:Redmi AX6"
    "ipq807x:xiaomi_ax3600:ipq807x_xiaomi_ax3600:Xiaomi AX3600"
    "ipq807x:xiaomi_ax9000:ipq807x_xiaomi_ax9000:Xiaomi AX9000"
)

# 获取设备配置
get_device_config() {
    local device_name=$1
    local key=$2

    for device in "${DEVICE_LIST[@]}"; do
        IFS=':' read -r platform dev_name config_name friendly_name <<< "$device"
        if [ "$dev_name" = "$device_name" ]; then
            case "$key" in
                "platform") echo "$platform" ;;
                "config_name") echo "$config_name" ;;
                "friendly_name") echo "$friendly_name" ;;
                "all") echo "$platform:$dev_name:$config_name:$friendly_name" ;;
                *) return 1 ;;
            esac
            return 0
        fi
    done
    return 1
}

# 获取平台下的所有设备
get_devices_by_platform() {
    local platform=$1
    local result=()

    for device in "${DEVICE_LIST[@]}"; do
        IFS=':' read -r dev_platform dev_name config_name friendly_name <<< "$device"
        if [ "$dev_platform" = "$platform" ]; then
            result+=("$dev_name")
        fi
    done

    echo "${result[@]}"
}

# 获取所有设备名
get_all_devices() {
    local result=()

    for device in "${DEVICE_LIST[@]}"; do
        IFS=':' read -r platform dev_name config_name friendly_name <<< "$device"
        result+=("$dev_name")
    done

    echo "${result[@]}"
}

# ============================================
# Git 版本信息
# ============================================

get_git_version() {
    local srctree="${SCRIPT_DIR}/u-boot-2016"
    local git_hash="unknown"
    local git_dirty="0"

    # 获取 Git Hash
    for try in . .. ../..; do
        if git_hash=$(git -C "$srctree/$try" rev-parse --short=7 HEAD 2>/dev/null); then
            break
        fi
    done

    # 获取 Dirty 状态
    for try in . .. ../..; do
        if git -C "$srctree/$try" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
            if [ -n "$(git -C "$srctree/$try" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
                git_dirty="1"
            else
                git_dirty="0"
            fi
            break
        fi
    done

    local dirty_flag=""
    if [ "$git_dirty" = "1" ]; then
        dirty_flag="-dirty"
    fi

    echo "${git_hash}${dirty_flag}"
}

# ============================================
# 日志系统
# ============================================

LOG_FILE=""
BUILD_RESULTS=()

# 初始化日志
init_logging() {
    local log_dir=$1
    local release_version=$2

    if [ ! -d "$log_dir" ]; then
        mkdir -p "$log_dir"
    fi

    LOG_FILE="${log_dir}/log-${release_version}.txt"

    echo "==========================================" > "$LOG_FILE"
    echo "编译开始时间: $(TZ=UTC-8 date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
    echo "==========================================" >> "$LOG_FILE"
}

# 日志输出函数（带时间戳）
log_message() {
    local message="$*"
    local timestamp=$(TZ=UTC-8 date '+%Y-%m-%d %H:%M:%S')

    echo "$message"

    if [ -n "$LOG_FILE" ]; then
        echo "[$timestamp] $message" >> "$LOG_FILE"
    fi
}

# 日志 echo 函数（不带时间戳）
log_echo() {
    local message="$*"

    echo "$message"

    if [ -n "$LOG_FILE" ]; then
        echo "$message" >> "$LOG_FILE"
    fi
}

# 日志 printf 函数（不带时间戳）
log_printf() {
    if [ $# -eq 0 ]; then
        return
    fi

    printf "$@"

    if [ -n "$LOG_FILE" ]; then
        printf "$@" >> "$LOG_FILE"
    fi
}

# ============================================
# 编译环境设置
# ============================================

setup_build_env() {
    log_message "设置编译环境"

    export ARCH=arm
    export CROSS_COMPILE=arm-openwrt-linux-
    export TARGETCC="${CROSS_COMPILE}gcc"
    export STAGING_DIR="${SCRIPT_DIR}/staging_dir"
    export HOSTLDFLAGS="-L${STAGING_DIR}/usr/lib -znow -zrelro -pie"
    export PATH="${STAGING_DIR}/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin:$PATH"

    return 0
}

# 设置编译环境（供外部 source 使用）
setup_env() {
    if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
        echo "要在当前 shell 中设置编译环境，请执行: source ${BASH_SOURCE[0]} setup_env"
        return 1
    fi

    setup_build_env

    echo ""
    echo "ARCH=$ARCH"
    echo "CROSS_COMPILE=$CROSS_COMPILE"
    echo "TARGETCC=$TARGETCC"
    echo "STAGING_DIR=$STAGING_DIR"
    echo "HOSTLDFLAGS=$HOSTLDFLAGS"
    echo ""
    echo "编译环境设置完成"

    return 0
}

# ============================================
# 缓存清理
# ============================================

# 清理编译缓存
clean_cache() {
    cd "${SCRIPT_DIR}/u-boot-2016/" || return 1

    # TODO: 参考 .gitignore 更新清理规则
    # 清理编译产物
    find . -type f \
        \( \
            -name '.*.cmd' -o \
            -name '.*.tmp' -o \
            -name '*.o' -o \
            -name '*.o.*' -o \
            -name '*.a' -o \
            -name '*.s' -o \
            -name '*.su' -o \
            -name '*.mod.c' -o \
            -name '*.i' -o \
            -name '*.lst' -o \
            -name '*.order' -o \
            -name '*.elf' -o \
            -name '*.swp' -o \
            -name '*.bin' -o \
            -name '*.patch' -o \
            -name '*.cfgtmp' -o \
            -name '*.exe' -o \
            -name 'MLO*' -o \
            -name 'SPL' -o \
            -name 'System.map' -o \
            -name 'LOG' -o \
            -name '*.orig' -o \
            -name '*~' -o \
            -name '#*#' -o \
            -name 'cscope.*' -o \
            -name 'tags' -o \
            -name 'ctags' -o \
            -name 'etags' -o \
            -name 'GPATH' -o \
            -name 'GRTAGS' -o \
            -name 'GSYMS' -o \
            -name 'GTAGS' \
        \) -delete 2>/dev/null

    rm -rf \
        .config \
        .gdb_history \
        .stgit-edit.txt \
        .u-boot* \
        arch/arm/dts/dtbtable.S \
        dtb_combined* \
        include/config \
        include/generated \
        scripts/kconfig/conf \
        scripts/basic/fixdep \
        tools/gen_eth_addr \
        tools/img2srec \
        tools/proftool \
        tools/fdtgrep \
        tools/envcrc \
        tools/dumpimage \
        u-boot* 2>/dev/null

    return 0
}

# ============================================
# 依赖管理
# ============================================

# 检查编译依赖
check_dependencies() {
    local missing_deps=0
    local optional_missing=0

    setup_build_env

    echo "检查编译依赖"

    # 必需的系统工具
    local required_tools=(
        "make"
        "gcc"
        "g++"
        "arm-openwrt-linux-gcc"
        "arm-openwrt-linux-strip"
        "dtc"
        "python3"
        "perl"
        "truncate"
        "stat"
        "find"
        "rm"
        "mkdir"
        "cp"
        "mv"
        "zip"
        "tar"
    )

    # 可选工具（用于网页文件压缩）
    local optional_tools=(
        "html-minifier-terser"
        "cleancss"
        "terser"
    )

    # Perl 模块依赖
    local perl_modules=(
        "IO::Compress::Gzip"
    )

    # 获取工具版本号的函数
    get_version() {
        local tool=$1
        local version=""

        case $tool in
            "make")
                version=$(make --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "gcc")
                version=$(gcc --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "g++")
                version=$(g++ --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "arm-openwrt-linux-gcc")
                version=$($tool --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                [ -z "$version" ] && version=$($tool -dumpversion 2>/dev/null)
                ;;
            "arm-openwrt-linux-strip")
                version=$($tool --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "dtc")
                version=$(dtc --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "python3")
                version=$(python3 --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "perl")
                version=$(perl --version 2>/dev/null | grep -oE 'v[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1 | sed 's/v//')
                ;;
            "truncate")
                version=$(truncate --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                [ -z "$version" ] && version=$(coreutils --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "stat")
                version=$(stat --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                [ -z "$version" ] && version=$(coreutils --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "find")
                version=$(find --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                [ -z "$version" ] && version=$(findutils --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "rm"|"mkdir"|"cp"|"mv")
                version=$($tool --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                [ -z "$version" ] && version=$(coreutils --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "zip")
                version=$(zip --version 2>/dev/null | head -n2 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "tar")
                version=$(tar --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            "html-minifier-terser")
                version=$(html-minifier-terser --version 2>/dev/null)
                ;;
            "cleancss")
                version=$(cleancss --version 2>/dev/null)
                ;;
            "terser")
                version=$(terser --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
                ;;
            *)
                version=""
                ;;
        esac

        if [ -n "$version" ]; then
            echo "$version"
        else
            echo "未知版本"
        fi
    }

    # 检查必需工具
    echo "检查必需工具:"
    for tool in "${required_tools[@]}"; do
        if command -v "$tool" >/dev/null 2>&1; then
            local version=$(get_version "$tool")
            echo "  ✓ $tool (版本: $version)"
        else
            echo "  ✗ $tool (缺失)"
            missing_deps=$((missing_deps + 1))
        fi
    done

    # 检查可选工具
    echo -e "\n检查可选工具（用于网页文件压缩）:"
    for tool in "${optional_tools[@]}"; do
        if command -v "$tool" >/dev/null 2>&1; then
            local version=$(get_version "$tool")
            echo "  ✓ $tool (版本: $version)"
        else
            echo "  ✗ $tool (缺失 - 可选)"
            optional_missing=$((optional_missing + 1))
        fi
    done

    # 检查 Perl 模块
    echo -e "\n检查 Perl 模块:"
    for module in "${perl_modules[@]}"; do
        if perl -M"$module" -e "1" 2>/dev/null; then
            local version=$(perl -M"$module" -e "print \$${module}::VERSION" 2>/dev/null)
            if [ -n "$version" ]; then
                echo "  ✓ $module (版本: $version)"
            else
                echo "  ✓ $module (版本未知)"
            fi
        else
            echo "  ✗ $module (缺失)"
            missing_deps=$((missing_deps + 1))
        fi
    done

    # 检查 Python3 模块
    echo -e "\n检查 Python3 模块:"
    if command -v python3 >/dev/null 2>&1; then
        local py_version=$(python3 --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1)
        echo "  ✓ Python3 (版本: $py_version)"

        local py_modules=("os" "sys" "struct")
        for py_mod in "${py_modules[@]}"; do
            if python3 -c "import $py_mod" 2>/dev/null; then
                echo "    ✓ $py_mod"
            else
                echo "    ✗ $py_mod (缺失)"
                missing_deps=$((missing_deps + 1))
            fi
        done
    else
        echo "  ✗ Python3 (无法运行)"
        missing_deps=$((missing_deps + 1))
    fi

    # 输出总结
    echo -e "\n=== 依赖检查总结 ==="
    if [ $missing_deps -eq 0 ]; then
        echo "所有必需依赖已满足 ✓"
        if [ $optional_missing -gt 0 ]; then
            echo "注意: 有 $optional_missing 个可选工具未安装"
            echo "      这将影响网页文件的压缩效果"
        fi
        return 0
    else
        echo "错误: 缺少 $missing_deps 个必需依赖 ✗"
        echo "请运行 'sudo ./build.sh install_deps' 安装缺失的依赖"
        return 1
    fi
}

# 安装编译依赖
install_dependencies() {
    # 检查是否以 root 权限运行
    if [ "$EUID" -ne 0 ]; then
        echo "错误: 安装依赖需要 root 权限"
        echo "请使用 sudo 运行此命令:"
        echo "  sudo ${BASH_SOURCE[0]} install_deps"
        return 1
    fi

    setup_build_env

    echo "开始安装编译依赖"

    # 检测包管理器
    local pkg_manager=""
    local install_cmd=""
    local update_cmd=""

    if command -v apt >/dev/null 2>&1; then
        pkg_manager="apt"
        install_cmd="apt install -y"
        update_cmd="apt update"
    elif command -v yum >/dev/null 2>&1; then
        pkg_manager="yum"
        install_cmd="yum install -y"
        update_cmd="yum check-update"
    elif command -v dnf >/dev/null 2>&1; then
        pkg_manager="dnf"
        install_cmd="dnf install -y"
        update_cmd="dnf check-update"
    elif command -v pacman >/dev/null 2>&1; then
        pkg_manager="pacman"
        install_cmd="pacman -S --noconfirm"
        update_cmd="pacman -Sy"
    elif command -v apk >/dev/null 2>&1; then
        pkg_manager="apk"
        install_cmd="apk add"
        update_cmd="apk update"
    else
        echo "错误: 未找到支持的包管理器 (apt, yum, dnf, pacman, apk)"
        return 1
    fi

    echo "检测到包管理器: $pkg_manager"

    # 更新包索引
    echo "更新包索引"
    eval "$update_cmd"

    # 安装基础开发工具
    echo "安装基础开发工具"
    case $pkg_manager in
        apt)
            $install_cmd build-essential
            $install_cmd device-tree-compiler
            $install_cmd python3
            $install_cmd perl
            $install_cmd coreutils
            $install_cmd nodejs npm
            $install_cmd git
            $install_cmd zip
            $install_cmd tar
            ;;
        yum|dnf)
            $install_cmd gcc gcc-c++ make
            $install_cmd dtc
            $install_cmd python3
            $install_cmd perl
            $install_cmd coreutils
            $install_cmd nodejs npm
            $install_cmd git
            $install_cmd zip
            $install_cmd tar
            ;;
        pacman)
            $install_cmd base-devel
            $install_cmd dtc
            $install_cmd python
            $install_cmd perl
            $install_cmd coreutils
            $install_cmd nodejs npm
            $install_cmd git
            $install_cmd zip
            $install_cmd tar
            ;;
        apk)
            $install_cmd build-base
            $install_cmd dtc
            $install_cmd python3
            $install_cmd perl
            $install_cmd coreutils
            $install_cmd nodejs npm
            $install_cmd git
            $install_cmd zip
            $install_cmd tar
            ;;
    esac

    # 安装 Perl 模块
    echo "安装 Perl 模块"
    if command -v cpan >/dev/null 2>&1; then
        # 尝试使用包管理器安装 Perl 模块
        case $pkg_manager in
            apt)
                $install_cmd libio-compress-perl
                ;;
            yum|dnf)
                $install_cmd perl-IO-Compress
                ;;
            pacman)
                $install_cmd perl-io-compress
                ;;
            apk)
                $install_cmd perl-io-compress
                ;;
        esac

        # 如果包管理器安装失败，使用 cpan
        if ! perl -MIO::Compress::Gzip -e 1 2>/dev/null; then
            echo "尝试使用 cpan 安装 IO::Compress::Gzip"
            cpan -i IO::Compress::Gzip
        fi
    else
        echo "警告: cpan 未安装，无法自动安装 Perl 模块"
        echo "请手动安装 Perl 模块 IO::Compress::Gzip"
    fi

    # 安装 Node.js 工具（用于网页文件压缩）
    echo "安装 Node.js 工具（用于网页文件压缩）"
    if command -v npm >/dev/null 2>&1; then
        echo "安装 html-minifier-terser"
        npm install -g html-minifier-terser

        echo "安装 clean-css-cli"
        npm install -g clean-css-cli

        echo "安装 terser"
        npm install -g terser
    else
        echo "警告: npm 未安装，无法安装 Node.js 工具"
        echo "如需网页文件压缩功能，请手动安装:"
        echo "  npm install -g html-minifier-terser clean-css-cli terser"
    fi

    # 检查工具链
    echo -e "\n检查工具链"
    local toolchains=(
        "arm-openwrt-linux-gcc"
        "arm-openwrt-linux-strip"
    )

    local missing_toolchain=0
    for tool in "${toolchains[@]}"; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "  注意: 未找到 $tool"
            missing_toolchain=$((missing_toolchain + 1))
        fi
    done

    if [ $missing_toolchain -gt 0 ]; then
        echo -e "\n注意: 部分工具链未找到"
        echo "请确保工具链已安装并在 PATH 环境变量中"
        echo "工具链通常位于:"
        echo "  ${SCRIPT_DIR}/staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin/"
    fi

    echo -e "\n依赖安装完成！"
    echo "建议运行 '${BASH_SOURCE[0]} check_deps' 验证安装结果"
}

# ============================================
# 文件填充
# ============================================

# 获取目标文件大小（U-Boot 文件将被填充到此大小）
get_target_file_size() {
    local device_name=$1

    # TODO: 根据不同设备设置目标大小（从 DEVICE_LIST 中获取目标大小）
    # 目前暂时统一使用 640 KiB
    case "$device_name" in
        *)
            target_size=655360
            ;;
    esac

    echo "$target_size"
}

# ============================================
# 核心编译函数
# ============================================

compile_device() {
    local dev_name=$1
    local uboot_version=$2
    local output_dir=$3

    # 获取设备配置
    local device_config=$(get_device_config "$dev_name" "all")
    if [ -z "$device_config" ]; then
        log_message "错误: 未知设备 '$dev_name'"
        return 1
    fi

    IFS=':' read -r platform device_name config_name friendly_name <<< "$device_config"

    log_message "=========================================="
    log_message "开始编译: $friendly_name ($device_name)"
    log_message "目标平台: $platform"
    log_message "=========================================="

    # 设置编译环境
    setup_build_env

    # 清理编译缓存
    log_message "清理编译缓存"
    clean_cache

    # 进入编译目录
    cd "${SCRIPT_DIR}/u-boot-2016/" || {
        log_message "错误: 无法进入编译目录: ${SCRIPT_DIR}/u-boot-2016/"
        return 1
    }

    # 配置
    log_message "构建配置: ${config_name}_defconfig"
    if ! make ${config_name}_defconfig 2>&1 | tee -a "$LOG_FILE"; then
        log_message "错误: 配置失败"
        cd "$SCRIPT_DIR"
        return 1
    fi

    # 编译
    log_message "开始编译"
    local make_exit=0
    make V=s 2>&1 | tee -a "$LOG_FILE" || make_exit=$?

    if [ $make_exit -ne 0 ]; then
        log_message "错误: 编译失败 (退出码: $make_exit)"
        cd "$SCRIPT_DIR"
        return 1
    fi

    # Strip
    log_message "Strip ELF"
    local strip_cmd="${CROSS_COMPILE}strip u-boot -o u-boot.strip"
    if ! $strip_cmd 2>&1 | tee -a "$LOG_FILE"; then
        log_message "错误: Strip 失败"
        cd "$SCRIPT_DIR"
        return 1
    fi

    # 转换为 MBN
    log_message "转换 ELF 为 MBN"
    local mbn_version=$(get_platform_config "$platform" "mbn_version")
    local python_cmd="python3 -B tools/elftombn.py -f ./u-boot.strip -o ./u-boot.mbn -v ${mbn_version} -p ${platform}"
    if ! eval "$python_cmd" 2>&1 | tee -a "$LOG_FILE"; then
        log_message "错误: ELF 转 MBN 失败"
        cd "$SCRIPT_DIR"
        return 1
    fi

    # 复制并重命名文件
    log_message "复制 u-boot.mbn 到输出目录并重命名"
    local output_file="${output_dir}/uboot-${platform}-${device_name}-${uboot_version}.bin"
    if [ ! -f "./u-boot.mbn" ]; then
        log_message "错误: 文件不存在: ./u-boot.mbn"
        cd "$SCRIPT_DIR"
        return 1
    fi
    cp ./u-boot.mbn "$output_file" || {
        log_message "错误: 复制失败"
        cd "$SCRIPT_DIR"
        return 1
    }

    # 检查并填充文件
    if [ ! -f "$output_file" ]; then
        log_message "错误: 文件不存在: $output_file"
        cd "$SCRIPT_DIR"
        return 1
    fi

    local original_size=$(stat -c%s "$output_file" 2>/dev/null || stat -f%z "$output_file" 2>/dev/null)
    local target_size=$(get_target_file_size "$device_name")
    local exceeded=0

    if [ $original_size -lt $target_size ]; then
        log_message "填充文件: $original_size -> $target_size 字节"
        truncate -s $target_size "$output_file" || dd if=/dev/zero of="$output_file" bs=1 count=0 seek=$target_size
    elif [ $original_size -gt $target_size ]; then
        exceeded=1
        log_message "警告: 文件大小 ($original_size) 超过目标大小 ($target_size)!"
    else
        log_message "文件大小等于目标大小，无需填充"
    fi

    # 记录结果
    local status="成功"
    local final_size=$(stat -c%s "$output_file" 2>/dev/null || stat -f%z "$output_file" 2>/dev/null)

    if [ "$exceeded" = "1" ]; then
        status="警告"
    fi

    BUILD_RESULTS+=("$platform:$device_name:$friendly_name:$output_file:$original_size:$final_size:$target_size:$exceeded:$status")

    log_message "编译完成: $friendly_name ($device_name)"
    log_message "输出目录: $output_dir/"
    log_message "输出文件: $(basename "$output_file")"
    log_message "文件大小: $final_size 字节"

    cd "$SCRIPT_DIR"
    return 0
}

# ============================================
# 校验和计算
# ============================================

# 计算输出目录下所有 .bin 文件的 MD5 和 SHA256 校验和
calculate_checksums() {
    local output_dir=$1
    local release_version=$2

    log_message "计算校验和"

    # 检查输出目录是否存在
    if [ ! -d "$output_dir" ]; then
        log_message "错误: 输出目录不存在: $output_dir"
        return 1
    fi

    # 切换到输出目录
    cd "$output_dir" || {
        log_message "错误: 无法进入输出目录: $output_dir"
        return 1
    }

    # 检查是否有 .bin 文件
    local bin_files=($(find . -maxdepth 1 -type f -name "*.bin" 2>/dev/null | sed 's/^\.\///' | sort))
    if [ ${#bin_files[@]} -eq 0 ]; then
        log_message "警告: 输出目录中没有找到 .bin 文件"
        cd "$SCRIPT_DIR" || return 1
        return 0
    fi

    # 生成 MD5 校验和文件
    local md5_file="${output_dir}/md5-${release_version}.txt"
    log_message "MD5 校验和文件: $(basename "$md5_file")"

    # 清空或创建文件
    > "$md5_file"

    # 计算每个文件的 MD5
    for bin_file in "${bin_files[@]}"; do
        local file_name=$(basename "$bin_file")
        if command -v md5sum >/dev/null 2>&1; then
            md5sum "$bin_file" 2>/dev/null >> "$md5_file"
        else
            log_message "警告: 未找到 md5sum 命令，跳过 MD5 计算"
            echo "ERROR: md5 command not found" >> "$md5_file"
            break
        fi
    done

    # 生成 SHA256 校验和文件
    local sha256_file="${output_dir}/sha256-${release_version}.txt"
    log_message "SHA256 校验和文件: $(basename "$sha256_file")"

    # 清空或创建文件
    > "$sha256_file"

    # 计算每个文件的 SHA256
    for bin_file in "${bin_files[@]}"; do
        local file_name=$(basename "$bin_file")
        if command -v sha256sum >/dev/null 2>&1; then
            sha256sum "$bin_file" 2>/dev/null >> "$sha256_file"
        else
            log_message "警告: 未找到 sha256sum 命令，跳过 SHA256 计算"
            echo "ERROR: sha256sum command not found" >> "$sha256_file"
            break
        fi
    done

    cd "$SCRIPT_DIR" || return 1

    return 0
}

# ============================================
# Release Notes 生成
# ============================================

# 生成 Release Notes
generate_release_notes() {
    local output_dir=$1
    local release_version=$2

    # 检查是否在 GitHub Actions 环境中
    if ! is_github_actions; then
        return 0
    fi

    log_message "生成 Release Notes"

    # 文件路径
    local source_file="${SCRIPT_DIR}/doc/RELEASE.md"
    local target_file="${output_dir}/release-${release_version}.md"

    # 复制源文件内容到目标文件
    if [ -f "$source_file" ]; then
        cat "$source_file" > "$target_file"
    fi

    # 检查 BUILD_RESULTS 是否有数据
    if [ ${#BUILD_RESULTS[@]} -eq 0 ]; then
        log_message "警告: 没有编译结果可添加到 Release Notes"
        return 0
    fi

    # 获取 GitHub 仓库信息
    local github_repo="${GITHUB_REPOSITORY:-unknown/unknown}"
    local github_tag="${release_version}"

    # 表格标题
    cat >> "$target_file" << 'EOF'

## 文件下载

| 平台名称 | 设备名称 | 设备型号 | 下载链接 |
|:--------|:--------|:--------|:--------|
EOF

    # 添加表格行
    for result in "${BUILD_RESULTS[@]}"; do
        IFS=':' read -r platform device_name friendly_name output_file original_size final_size target_size exceeded status <<< "$result"

        # 构建下载链接
        local file_name=$(basename "$output_file")
        local download_url="https://github.com/${github_repo}/releases/download/${github_tag}/${file_name}"
        local download_link="[点击下载](${download_url})"

        # 转义表格中的特殊字符（如管道符）
        local platform_safe=$(echo "$platform" | sed 's/|/\\|/g')
        local friendly_name_safe=$(echo "$friendly_name" | sed 's/|/\\|/g')
        local device_name_safe=$(echo "$device_name" | sed 's/|/\\|/g')

        # 添加到表格
        printf "| %s | %s | %s | %s |\n" \
            "$platform_safe" \
            "$friendly_name_safe" \
            "$device_name_safe" \
            "$download_link" >> "$target_file"
    done

    log_message "Release Notes 文件: $(basename "$target_file")"
    return 0
}

# ============================================
# 编译总结
# ============================================

show_build_summary() {
    local output_dir=$1

    log_echo ""
    log_echo "=========================================="
    log_echo "编译总结"
    log_echo "=========================================="

    log_echo ""
    log_echo "输出目录: ${output_dir}/"

    local total=${#BUILD_RESULTS[@]}
    local success=0
    local warning=0
    local failed=0
    local separator="------------------------------------------------------------------------------------------------------------------------------------"

    log_echo ""
    log_echo "$separator"
    log_printf "%-33s %-21s %-21s %-22s %-10s %-s\n" "设备" "原始大小" "最终大小" "目标大小" "状态" "输出文件"
    log_echo "$separator"

    for result in "${BUILD_RESULTS[@]}"; do
        IFS=':' read -r platform device_name friendly_name output_file original_size final_size target_size exceeded status <<< "$result"

        local status_icon="✓"

        if [ "$status" = "成功" ]; then
            success=$((success + 1))
        elif [ "$status" = "警告" ]; then
            status_icon="⚠"
            warning=$((warning + 1))
        else
            status_icon="x"
            status="失败"
            failed=$((failed + 1))
        fi

        log_printf "%-30s %-16s %-16s %-15s %-13s %-s\n" \
            "$friendly_name" \
            "${original_size} Bytes" \
            "${final_size} Bytes" \
            "${target_size} Bytes" \
            "$status_icon $status" \
            "$(basename "$output_file")"
    done

    log_echo "$separator"
    log_echo "总计: $total"
    log_echo "成功: $success"
    log_echo "警告: $warning"
    log_echo "失败: $failed"

    if [ $failed -gt 0 ]; then
        log_echo ""
        log_echo "失败的设备:"
        for result in "${BUILD_RESULTS[@]}"; do
            IFS=':' read -r platform device_name friendly_name output_file original_size final_size target_size exceeded status <<< "$result"
            if [ "$status" != "成功" ] && [ "$status" != "警告" ]; then
                log_echo "  - $friendly_name ($device_name)"
            fi
        done
    fi

    echo "" >> "$LOG_FILE"
    echo "==========================================" >> "$LOG_FILE"
    echo "编译结束时间: $(TZ=UTC-8 date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
    echo "==========================================" >> "$LOG_FILE"
}

# ============================================
# 压缩输出目录
# ============================================

# 在 GitHub Actions 环境下压缩输出目录
compress_output_directory() {
    local output_dir=$1
    local release_version=$2

    # 检查是否在 GitHub Actions 环境中
    if ! is_github_actions; then
        return 0
    fi

    echo ""
    echo "压缩输出目录"

    # 检查输出目录是否存在
    if [ ! -d "$output_dir" ]; then
        echo "错误: 输出目录不存在: $output_dir"
        return 1
    fi

    # 进入输出目录的父目录
    local parent_dir=$(dirname "$output_dir")
    local dir_name=$(basename "$output_dir")

    if command -v zip >/dev/null 2>&1; then
        # 使用 zip 命令压缩（保留目录结构）
        echo "使用 zip 压缩: ${dir_name}/"
        local temp_zip="${parent_dir}/uboot-all-${release_version}.zip.tmp"
        local final_zip="${output_dir}/uboot-all-${release_version}.zip"

        cd "$parent_dir" || {
            echo "错误: 无法进入目录: $parent_dir"
            return 1
        }

        # 在父目录生成压缩文件，排除已存在的压缩文件
        local zip_output
        zip_output=$(zip -r "$temp_zip" "$dir_name" -x "*.zip" "*.tar.gz" 2>&1)
        local zip_exit_code=$?

        if [ $zip_exit_code -eq 0 ] && [ -f "$temp_zip" ]; then
            # 移动压缩文件到输出目录
            if mv "$temp_zip" "$final_zip" 2>/dev/null; then
                local zip_size=$(stat -c%s "$final_zip" 2>/dev/null || stat -f%z "$final_zip" 2>/dev/null)
                echo "压缩完成: $(basename "$final_zip") ($(numfmt --to=iec $zip_size 2>/dev/null || echo "$zip_size bytes"))"
                cd "$SCRIPT_DIR" || return 1
                return 0
            else
                echo "错误: 无法移动压缩文件到输出目录"
                rm -f "$temp_zip" 2>/dev/null
                cd "$SCRIPT_DIR" || return 1
                return 1
            fi
        else
            echo "错误: zip 压缩失败 (退出码: $zip_exit_code)"
            if [ -n "$zip_output" ]; then
                echo "zip 错误信息: $zip_output"
            fi
            rm -f "$temp_zip" 2>/dev/null
            cd "$SCRIPT_DIR" || return 1
            return 1
        fi
    else
        # 如果 zip 命令不可用，尝试使用 tar
        echo "使用 tar 压缩: ${dir_name}/"
        local temp_tar="${parent_dir}/uboot-all-${release_version}.tar.gz.tmp"
        local final_tar="${output_dir}/uboot-all-${release_version}.tar.gz"

        cd "$parent_dir" || {
            echo "错误: 无法进入目录: $parent_dir"
            return 1
        }

        # 在父目录生成压缩文件，排除已存在的压缩文件
        local tar_output
        tar_output=$(tar -czf "$temp_tar" --exclude="*.tar.gz" --exclude="*.zip" "$dir_name" 2>&1)
        local tar_exit_code=$?

        if [ $tar_exit_code -eq 0 ] && [ -f "$temp_tar" ]; then
            # 移动压缩文件到输出目录
            if mv "$temp_tar" "$final_tar" 2>/dev/null; then
                local tar_size=$(stat -c%s "$final_tar" 2>/dev/null || stat -f%z "$final_tar" 2>/dev/null)
                echo "压缩完成: $(basename "$final_tar") ($(numfmt --to=iec $tar_size 2>/dev/null || echo "$tar_size bytes"))"
                cd "$SCRIPT_DIR" || return 1
                return 0
            else
                echo "错误: 无法移动压缩文件到输出目录"
                rm -f "$temp_tar" 2>/dev/null
                cd "$SCRIPT_DIR" || return 1
                return 1
            fi
        else
            echo "错误: tar 压缩失败 (退出码: $tar_exit_code)"
            if [ -n "$tar_output" ]; then
                echo "tar 错误信息: $tar_output"
            fi
            rm -f "$temp_tar" 2>/dev/null
            cd "$SCRIPT_DIR" || return 1
            return 1
        fi
    fi
}

# ============================================
# 多设备编译控制
# ============================================

build_targets() {
    local target=$1

    local git_version=$(get_git_version)
    local unified_time=$(TZ=UTC-8 date +"%s")
    local compile_date=$(TZ=UTC-8 date -d "@$unified_time" +"%y.%m.%d-%H.%M.%S")
    local uboot_date=$(TZ=UTC-8 date -d "@$unified_time" +"%y%m%d_%H%M%S")
    local uboot_version="${uboot_date}_${git_version}"
    local release_version="${compile_date}-${git_version}"
    local output_dir="${SCRIPT_DIR}/bin/${release_version}"

    export _RELEASE_VERSION_="$release_version"

    if [ ! -d "$output_dir" ]; then
        mkdir -p "$output_dir"
    fi

    # 初始化日志
    init_logging "$output_dir" "$release_version"

    log_echo "版本信息: $release_version"

    # 确定要编译的设备列表
    local devices=()

    case "$target" in
        "all")
            log_echo "编译目标: 所有设备"
            devices=($(get_all_devices))
            ;;
        *)
            # 检查是否为平台名
            if [ -n "$(get_platform_config "$target" "mbn_version")" ]; then
                log_echo "编译目标: $target 平台下的所有设备"
                devices=($(get_devices_by_platform "$target"))
            else
                # 单设备编译
                if [ -n "$(get_device_config "$target" "platform")" ]; then
                    log_echo "编译目标: 单个设备"
                    devices=("$target")
                else
                    log_echo "错误: 未知目标 '$target'"
                    return 1
                fi
            fi
            ;;
    esac

    if [ ${#devices[@]} -eq 0 ]; then
        log_echo "错误: 没有找到任何设备"
        return 1
    fi

    log_echo "设备数量: ${#devices[@]}"
    log_echo ""

    # 清空结果记录
    BUILD_RESULTS=()

    # 编译每个设备
    local success_count=0
    local fail_count=0

    for device in "${devices[@]}"; do
        if compile_device "$device" "$uboot_version" "$output_dir"; then
            success_count=$((success_count + 1))
        else
            local device_config=$(get_device_config "$device" "all")
            IFS=':' read -r platform device_name config_name friendly_name <<< "$device_config"
            BUILD_RESULTS+=("$platform:$device_name:$friendly_name:-:-:-:-:-:失败")
            fail_count=$((fail_count + 1))
            log_message "设备 $device 编译失败"
        fi
        log_message ""
    done

    # 计算校验和
    calculate_checksums "$output_dir" "$release_version"

    # 生成 Release Notes（仅在 GitHub Actions 环境下）
    generate_release_notes "$output_dir" "$release_version"

    # 显示总结
    show_build_summary "$output_dir"

    # 压缩输出目录（仅在 GitHub Actions 环境下）
    compress_output_directory "$output_dir" "$release_version"

    return 0
}

# ============================================
# 帮助信息
# ============================================

# 获取所有平台列表（去重）
get_platform_list() {
    local platforms=()
    local seen_platforms=""

    for device in "${DEVICE_LIST[@]}"; do
        IFS=':' read -r platform dev_name config_name friendly_name <<< "$device"
        # 检查是否已存在
        if [[ ! "$seen_platforms" =~ (^|:)$platform(:|$) ]]; then
            platforms+=("$platform")
            seen_platforms="${seen_platforms}:${platform}:"
        fi
    done

    echo "${platforms[@]}"
}

# 打印设备列表（格式化）
print_device_list() {
    echo "支持的设备:"

    # 获取所有平台并排序
    local platforms=($(get_platform_list))

    # 按字母顺序排序
    IFS=$'\n' platforms=($(sort <<<"${platforms[*]}"))
    unset IFS

    for platform in "${platforms[@]}"; do
        echo "  $platform:"

        # 获取该平台下的所有设备
        local devices=($(get_devices_by_platform "$platform"))

        # 按字母顺序排序设备
        IFS=$'\n' devices=($(sort <<<"${devices[*]}"))
        unset IFS

        for device in "${devices[@]}"; do
            # 获取设备的友好名
            local friendly_name=$(get_device_config "$device" "friendly_name")
            printf "    %-25s %-s\n" "$device" "$friendly_name"
        done
        echo ""
    done
}

show_help() {
    echo "用法: ${BASH_SOURCE[0]} [命令] [参数]"
    echo ""
    echo "命令:"
    echo "  build <目标>            编译指定的目标"
    echo "  setup_env               在当前 Shell 中设置编译环境 (需使用 source 执行)"
    echo "  check_deps              检查编译所需的依赖"
    echo "  install_deps            安装编译所需的依赖 (需要 root 权限)"
    echo "  clean_cache             清理编译缓存"
    echo "  help                    显示此帮助信息"
    echo ""
    echo "编译目标:"
    echo "  all                     编译所有设备"
    echo "  <平台名>                编译指定平台下的所有设备"
    echo "  <设备名>                编译指定的单个设备"
    echo ""
    echo "支持的平台:"
    echo "  ipq50xx, ipq53xx, ipq60xx, ipq807x"
    echo ""

    print_device_list

    echo ""
    echo "示例:"
    echo "  ${BASH_SOURCE[0]} build all                     编译所有设备"
    echo "  ${BASH_SOURCE[0]} build ipq60xx                 编译 IPQ60xx 平台下的所有设备"
    echo "  ${BASH_SOURCE[0]} build jdcloud_re-ss-01        编译 JDCloud AX1800 Pro (Arthur)"
    echo "  source ${BASH_SOURCE[0]} setup_env              在当前 Shell 中设置编译环境"
    echo "  ${BASH_SOURCE[0]} check_deps                    检查编译所需的依赖"
    echo "  sudo ${BASH_SOURCE[0]} install_deps             安装编译所需的依赖"
}

# ============================================
# 主程序入口
# ============================================

main() {
    local cmd="${1:-help}"
    shift || true

    case "$cmd" in
        "build")
            if [ -z "$1" ]; then
                echo "错误: 缺少编译目标"
                echo "使用 '${BASH_SOURCE[0]} help' 查看帮助"
                return 1
            fi
            build_targets "$1"
            ;;
        "setup_env")
            setup_env "$@"
            ;;
        "check_deps"|"check_dependencies")
            check_dependencies
            ;;
        "install_deps"|"install_dependencies")
            install_dependencies
            ;;
        "clean_cache")
            clean_cache
            ;;
        "help"|"")
            show_help
            ;;
        *)
            echo "错误: 未知命令 '$cmd'"
            echo "使用 '${BASH_SOURCE[0]} help' 查看帮助"
            return 1
            ;;
    esac
}

# 脚本入口
main "$@"
