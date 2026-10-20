// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 chenxin527. All Rights Reserved.
 *
 * This file is part of the project uboot-qsdk12.5-build
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <common.h>
#include <cli.h>
#include <errno.h>
#include <ipq_api.h>
#include <fdtdec.h>
#include <dt-bindings/gpio/gpio.h>
#include <part.h>
#include <mmc.h>
#include <sdhci.h>
#include <spi.h>
#include <spi_flash.h>
#include <linux/sizes.h>
#include <asm/arch-qca-common/smem.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <ubi_uboot.h>
#include <failsafe/fw_dec.h>
#include <mapmem.h>
#include <flashrw.h>

#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern struct sdhci_host mmc_host;
#endif

DECLARE_GLOBAL_DATA_PTR;

// =============================================================================
// GPIO 帮助函数
// =============================================================================

void ipq_gpio_init(void)
{
	int node;
	const char *node_paths[] = {"/leds", "/keys", "/gpio-export"};

	for (int i = 0; i < ARRAY_SIZE(node_paths); i++) {
		node = fdt_path_offset(gd->fdt_blob, node_paths[i]);
		if (node >= 0)
			qca_gpio_init(node);
	}
}

// =============================================================================
// httpd 触发检测（按键、环境变量、9008 模式）
// =============================================================================

typedef struct {
	const char *name;
	unsigned int gpio;
	unsigned int active_level;
} button_info_t;

static bool is_button_pressed(unsigned int button_gpio, unsigned int active_level)
{
	unsigned int button_pressed = (active_level == GPIO_ACTIVE_LOW) ? GPIO_OUT_LOW : GPIO_OUT_HIGH;

	if (gpio_get_value(button_gpio) != button_pressed)
		return false;

	udelay(10000);

	return (gpio_get_value(button_gpio) == button_pressed) ? true : false;
}

static bool is_any_button_pressed(button_info_t *button_info)
{
	int parent, node;
	unsigned int gpio, active_level;

	parent = fdt_path_offset(gd->fdt_blob, "/keys");
	if (parent < 0)
		return false;

	fdt_for_each_subnode(gd->fdt_blob, node, parent) {
		gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", GPIO_NOT_FOUND);
		active_level = fdtdec_get_uint(gd->fdt_blob, node, "active_level", GPIO_ACTIVE_LOW);
		if (gpio != GPIO_NOT_FOUND && is_button_pressed(gpio, active_level)) {
			button_info->name = fdt_get_name(gd->fdt_blob, node, NULL);
			button_info->gpio = gpio;
			button_info->active_level = active_level;
			return true;
		}
	}

	return false;
}

static bool is_any_button_pressed_for_enough_time(void)
{
	int counter = 3;
	ulong ts;
	bool led_state_on, still_pressed = true;
	button_info_t button;

    if (!is_any_button_pressed(&button))
		return false;

	printf("%s button pressed, enter web failsafe mode after: %-2d", button.name, counter);

	while (counter > 0 && still_pressed) {
		counter--;
		ts = get_timer(0);
		led_state_on = false;
		led_off("power_led");
		do {
			still_pressed = is_button_pressed(button.gpio, button.active_level);

			if (!led_state_on && get_timer(ts) >= 500) {
				led_state_on = true;
				led_on("power_led");
			}

			udelay(10000);
		} while (still_pressed && get_timer(ts) < 1000);

		printf("\b\b%-2d", counter);
	}

	putc('\n');
	led_on("power_led");

	return still_pressed;
}

static bool failsafe_env_exists(void)
{
	const char *failsafe_start_mode = getenv("failsafe");

	if (!failsafe_start_mode)
		return false;

	if (strcmp(failsafe_start_mode, "always")) {
		/* 非 always 模式，删除 failsafe 环境变量，防止重启后再次启动 httpd */
		setenv("failsafe", NULL);
		saveenv();
	}

	return true;
}

static bool check_9008_mode_and_failsafe_env(void)
{
	ulong ts;
	int counter = 3;
	bool led_state_on, abort = false;

	if (!is_9008_mode() && !failsafe_env_exists())
		return false;

	puts(is_9008_mode() ? "currently in 9008 mode" : "failsafe env variable defined");
	printf(", enter web failsafe mode after: %-2d", counter);

	/* Wait 3s for phy link to settle down */
	while (!abort && counter > 0) {
		counter--;
		ts = get_timer(0);
		led_state_on = false;
		led_off("power_led");

		do {
			if (tstc()) { /* we got a key press	*/
				abort = true;
				counter = 0;
				(void) getc(); /* consume input	*/
				break;;
			}
			if (!led_state_on && get_timer(ts) >= 500) {
				led_state_on = true;
				led_on("power_led");
			}
			udelay(10000);
		} while (!abort && get_timer(ts) < 1000);

		printf("\b\b%-2d", counter);
	}

	putc('\n');
	led_on("power_led");

	/* 用户手动打断，直接进入命令行模式，不返回 */
	if (abort)
		cli_loop();

	return true;
}

void do_httpd_check(void)
{
	if (check_9008_mode_and_failsafe_env() ||
		is_any_button_pressed_for_enough_time()) {
		run_command("httpd", 0);
		cli_loop();
	}
}

// =============================================================================
// 网络参数检查与修改 (ipaddr、netmask、serverip)
// =============================================================================

/**
 * 每次启动都会检查环境变量：ipaddr、netmask 和 serverip，并将其重置为默认值。
 * 若想要自定义这三个环境变量，需添加 custom_network 环境变量（任意合法非空值即可）。
 */
void do_network_check(void)
{
	if (getenv("custom_network"))
		return;

	int modified = 0;
	const char *current_ipaddr, *current_netmask, *current_serverip;
	const char *default_ipaddr, *default_netmask, *default_serverip;

#ifdef CONFIG_IPADDR
	default_ipaddr = __stringify(CONFIG_IPADDR);
#else
	default_ipaddr = "192.168.1.1";
#endif /* CONFIG_IPADDR */
#ifdef CONFIG_NETMASK
	default_netmask = __stringify(CONFIG_NETMASK);
#else
	default_netmask = "255.255.255.0";
#endif /* CONFIG_NETMASK */
#ifdef CONFIG_SERVERIP
	default_serverip = __stringify(CONFIG_SERVERIP);
#else
	default_serverip = "192.168.1.2";
#endif /* CONFIG_SERVERIP */

	current_ipaddr = getenv("ipaddr");
	if (!current_ipaddr || strcmp(current_ipaddr, default_ipaddr)) {
		setenv("ipaddr", default_ipaddr);
		net_ip = string_to_ip(default_ipaddr);
		modified++;
	}

	current_netmask = getenv("netmask");
	if (!current_netmask || strcmp(current_netmask, default_netmask)) {
		setenv("netmask", default_netmask);
		net_netmask = string_to_ip(default_netmask);
		modified++;
	}

	current_serverip = getenv("serverip");
	if (!current_serverip || strcmp(current_serverip, default_serverip)) {
		setenv("serverip", default_serverip);
		net_server_ip = string_to_ip(default_serverip);
		modified++;
	}

	if (modified) {
		puts("\"custom_network\" env variable not defined, reset network settings to default values:\n");
		printf("    ipaddr: %s\n", default_ipaddr);
		printf("    netmask: %s\n", default_netmask);
		printf("    serverip: %s\n", default_serverip);
#ifndef CONFIG_TARGET_IPQ5018_LYW_30M
		saveenv();
#endif
	}
}

// =============================================================================
// 闪存设备检测
// =============================================================================

typedef struct {
	bool spi;
	bool nand;
	bool mmc;
} detected_flash_device_t;

static detected_flash_device_t detected_flash_device;

void detect_flash_device(void)
{
	int len = 0;
	char flash_list[25];
	struct spi_flash *spi;
	block_dev_desc_t *mmc_dev;
	nand_info_t *nand;
	detected_flash_device_t *dfd = &detected_flash_device;

	dfd->spi = false;
	dfd->nand = false;
	dfd->mmc = false;

	spi = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
			CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
	if (spi && spi->size > 0) {
		len += strlcpy(flash_list + len, "SPI", sizeof(flash_list));
		dfd->spi = true;
	}

	nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	if (nand->name && nand->size > 0 && nand->writesize > 0) {
		len += sprintf(flash_list + len, "%sNAND", len ? ", " : "");
		dfd->nand = true;
	}

	mmc_dev = mmc_get_dev(mmc_host.dev_num);
	if (mmc_dev && mmc_dev->type != DEV_TYPE_UNKNOWN) {
		len += sprintf(flash_list + len, "%sMMC", len ? ", " : "");
		dfd->mmc = true;
	}

	flash_list[len] = '\0';

	printf("FLASH(S): %s\n", len ? flash_list : "NONE");
}

bool has_spi(void)
{
	return detected_flash_device.spi;
}

bool has_nand(void)
{
	return detected_flash_device.nand;
}

bool has_mmc(void)
{
	return detected_flash_device.mmc;
}

// =============================================================================
// flash_type enum 与 string 转换
// =============================================================================

const char *flash_type_to_string(uint32_t flash_type)
{
    switch (flash_type) {
    case SMEM_BOOT_NO_FLASH: return NO_FLASH_STR;
    case SMEM_BOOT_NOR_FLASH: return NOR_FLASH_STR;
    case SMEM_BOOT_NAND_FLASH: return NAND_FLASH_STR;
    case SMEM_BOOT_ONENAND_FLASH: return ONENAND_FLASH_STR;
    case SMEM_BOOT_SDC_FLASH: return SDC_FLASH_STR;
    case SMEM_BOOT_MMC_FLASH: return MMC_FLASH_STR;
    case SMEM_BOOT_SPI_FLASH: return SPI_FLASH_STR;
    case SMEM_BOOT_NORPLUSNAND: return NORPLUSNAND_STR;
    case SMEM_BOOT_NORPLUSEMMC: return NORPLUSEMMC_STR;
    case SMEM_BOOT_QSPI_NAND_FLASH: return QSPI_NAND_FLASH_STR;
    default: return UNKNOWN_FLASH_STR;
    }
}

int string_to_flash_type(const char *str)
{
    if (!strcasecmp(str, NO_FLASH_STR))
        return SMEM_BOOT_NO_FLASH;
    else if (!strcasecmp(str, NOR_FLASH_STR))
        return SMEM_BOOT_NOR_FLASH;
    else if (!strcasecmp(str, NAND_FLASH_STR))
        return SMEM_BOOT_NAND_FLASH;
    else if (!strcasecmp(str, ONENAND_FLASH_STR))
        return SMEM_BOOT_ONENAND_FLASH;
    else if (!strcasecmp(str, SDC_FLASH_STR))
        return SMEM_BOOT_SDC_FLASH;
    else if (!strcasecmp(str, MMC_FLASH_STR))
        return SMEM_BOOT_MMC_FLASH;
    else if (!strcasecmp(str, SPI_FLASH_STR))
        return SMEM_BOOT_SPI_FLASH;
    else if (!strcasecmp(str, NORPLUSNAND_STR))
        return SMEM_BOOT_NORPLUSNAND;
    else if (!strcasecmp(str, NORPLUSEMMC_STR))
        return SMEM_BOOT_NORPLUSEMMC;
    else if (!strcasecmp(str, QSPI_NAND_FLASH_STR))
        return SMEM_BOOT_QSPI_NAND_FLASH;
    else
        return -1;
}

// =============================================================================
// 9008 模式相关 (MIBIB 重载、默认 flash_type 设置)
// =============================================================================

typedef struct {
	size_t read_size;
	size_t flash_block_size;
	size_t flash_size;
	uint32_t flash_type;
	const char *flash_type_str;
	mibib_type_t mibib_type;
	int (*read_data_from_flash)(ulong offset, size_t size, void *buf, size_t buf_size);
} mibib_reload_info_t;

int get_mibib_and_ptable_addr(const void *addr, size_t limit,
		const void **mibib_addr, const void **ptable_addr, mibib_type_t mibib_type)
{
	const void *p = addr;
	const u64 magic_mibib_start = HEADER_MAGIC_MBN;
	const u64 magic_ptable_start = HEADER_MAGIC_PTABLE;
	const u64 magic_ptable_end = FOOTER_MAGIC_MBN;
	const size_t magic_len = sizeof(u64);
	uint32_t ptable_start_in_mibib, ptable_end_in_mibib;

	if (!p)
		return -EINVAL;

	switch (mibib_type) {
	case MIBIB_TYPE_NAND:
		ptable_start_in_mibib = PTABLE_START_IN_MIBIB_NAND;
		ptable_end_in_mibib = PTABLE_END_IN_MIBIB_NAND;
		break;
	case MIBIB_TYPE_NOR:
		ptable_start_in_mibib = PTABLE_START_IN_MIBIB_NOR;
		ptable_end_in_mibib = PTABLE_END_IN_MIBIB_NOR;
		break;
	default:
		return -EINVAL;
	}

	while (limit >= ptable_end_in_mibib + magic_len) {
		limit--;
		if (!memcmp(p, &magic_mibib_start, magic_len) &&
			!memcmp(p + ptable_start_in_mibib, &magic_ptable_start, magic_len) &&
			!memcmp(p + ptable_end_in_mibib, &magic_ptable_end, magic_len)) {
			if (mibib_addr)
				*mibib_addr = p;
			if (ptable_addr)
				*ptable_addr = p + ptable_start_in_mibib;
			return 0;
		}
		p++;
	}

	return -ENOENT;
}

static int check_smem_flash_block_size(const void *load_addr,
		const void *mibib_addr, mibib_type_t mibib_type)
{
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
	uint32_t offset_blocks, size_blocks;
	uint32_t smem_offset_bytes, acutal_offset_bytes;
	uint32_t flash_block_size;
	int ret;

	ret = smem_getpart("0:MIBIB", &offset_blocks, &size_blocks);
	if (ret)
		return ret;

	smem_offset_bytes = offset_blocks * sfi->flash_block_size;
	acutal_offset_bytes = mibib_addr - load_addr;
	if (acutal_offset_bytes && smem_offset_bytes != acutal_offset_bytes) {
		if (offset_blocks && acutal_offset_bytes % offset_blocks == 0) {
			flash_block_size = acutal_offset_bytes / offset_blocks;
			printf("change smem flash block size from 0x%x to 0x%x\n",
				sfi->flash_block_size, flash_block_size);
			sfi->flash_block_size = flash_block_size;
		}
	}

	return 0;
}

static int reload_mibib_from_flash(mibib_reload_info_t *info)
{
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
	size_t read_size;
	void *load_addr;
	const void *mibib_addr, *ptable_addr;
	int ret;

	read_size = min_t(size_t, info->read_size, info->flash_size);

	load_addr = map_sysmem(CONFIG_SYS_LOAD_ADDR, read_size);
	if (!load_addr)
		return -ENOMEM;

	ret = info->read_data_from_flash(0, read_size, load_addr, read_size);
	if (ret)
		goto done;

	ret = get_mibib_and_ptable_addr(load_addr, read_size,
			&mibib_addr, &ptable_addr, info->mibib_type);
	if (ret)
		goto done;

	ret = mibib_ptable_init((unsigned int *)ptable_addr);
	if (ret)
		goto done;

	sfi->flash_block_size = info->flash_block_size;
	ret = check_smem_flash_block_size(load_addr, mibib_addr, info->mibib_type);
	if (ret) {
		sfi->flash_block_size = 0;
		goto done;
	}

	sfi->flash_type = info->flash_type;
	sfi->flash_density = info->flash_size;

	get_kernel_fs_part_details();

done:
	printf("reload MIBIB from %s FLASH: ", info->flash_type_str);
	if (ret) {
		printf("failure (errno: %d)\n", ret);
		reset_smem_ptable_in_9008_mode();
	} else {
		const char *separator = "--------------------------------------------------------------------\n";
		puts("success\n");
		puts("please check if the smeminfo below is correct\n");
		puts("especially flash_type, flash_block_size, and start & size for each partition entry\n");
		puts(separator);
		run_command("smeminfo", 0);
		puts(separator);
		putc('\n');
	}

	unmap_sysmem(load_addr);
	return ret;
}

void reload_mibib_from_flash_and_set_default_flash_type_in_9008_mode(void)
{
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;

	if (!is_9008_mode())
		return;

	if (has_spi()) {
		struct spi_flash *spi;
		spi = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
		if (spi) {
			mibib_reload_info_t info = {
				.read_size = SZ_2M,
				.flash_block_size = SZ_64K,
				.flash_size = spi->size,
				.flash_type = SMEM_BOOT_SPI_FLASH,
				.flash_type_str = "SPI-NOR",
				.mibib_type = MIBIB_TYPE_NOR,
				.read_data_from_flash = read_data_from_spi
			};
			if (!reload_mibib_from_flash(&info))
				return;
		}
	}

	if (has_nand()) {
		nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
		if (nand->name && nand->size > 0 && nand->writesize > 0) {
			mibib_reload_info_t info = {
				.read_size = SZ_4M,
				.flash_block_size = nand->erasesize,
				.flash_size = nand->size,
#ifdef CONFIG_QPIC_SERIAL
				.flash_type = SMEM_BOOT_QSPI_NAND_FLASH,
#else
				.flash_type = SMEM_BOOT_NAND_FLASH,
#endif
				.flash_type_str = "NAND",
				.mibib_type = MIBIB_TYPE_NAND,
				.read_data_from_flash = read_data_from_nand
			};
			if (!reload_mibib_from_flash(&info))
				return;
		}
	}

	if (has_mmc() && !has_spi() && !has_nand())
		sfi->flash_type = SMEM_BOOT_MMC_FLASH;
}

// =============================================================================
// 地址合法性检测（检测文件上传地址及内存区域是否可用）
// =============================================================================

bool is_load_addr_valid(uintptr_t load_addr)
{
	/*
     * Do not load files to the reserved region or the
     * region where linux is executed.
     */
#ifdef CONFIG_IPQ806X
    if ((load_addr < IPQ_TFTP_MIN_ADDR) || (load_addr >= IPQ_TFTP_MAX_ADDR))
#else
    if ((load_addr < IPQ_TFTP_MIN_ADDR) || (load_addr >= CONFIG_SYS_SDRAM_END) ||
        ((load_addr >= CONFIG_IPQ_FDT_HIGH) && (load_addr < CONFIG_TZ_END_ADDR)))
#endif /* CONFIG_IPQ806X */
        return false;

	return true;
}

bool is_memory_region_available(uintptr_t load_addr, size_t size)
{
	uintptr_t end_addr;

	if (!is_load_addr_valid(load_addr))
		return false;

	end_addr = load_addr + size;

	/*
	 * The file to be loaded should not overwrite the
	 * code/stack area.
	 */
#ifdef CONFIG_IPQ806X
    if (end_addr >= IPQ_TFTP_MAX_ADDR)
#else
    if ((end_addr >= CONFIG_SYS_SDRAM_END) ||
        ((end_addr >= CONFIG_IPQ_FDT_HIGH) && (end_addr < CONFIG_TZ_END_ADDR)) ||
		((load_addr < CONFIG_IPQ_FDT_HIGH) && (end_addr >= CONFIG_TZ_END_ADDR)))
#endif /* CONFIG_IPQ806X */
        return false;

	return true;
}

// =============================================================================
// 打印设备信息
// =============================================================================

int print_board_info(void)
{
	const char *board_hostname, *board_model, *config_name;
	const char *unknown = "Unknown";
	int ret, configs_count;

	board_hostname = fdt_getprop(gd->fdt_blob, 0, "host_name", NULL);
	board_model = fdt_getprop(gd->fdt_blob, 0, "model", NULL);
	printf("Model: %s (%s)\n",
		board_hostname ? board_hostname : unknown,
		board_model ? board_model : unknown);

	puts("Config(s): ");
	configs_count = fdt_count_strings(gd->fdt_blob, 0, "config_name");
	for (int i = 0; i < configs_count; i++) {
		config_name = NULL;
		ret = fdt_get_string_index(gd->fdt_blob, 0, "config_name", i, &config_name);
		if (!ret && config_name)
			printf("%s%s", i ? ", " : "", config_name);
	}
	putc('\n');

	return 0;
}

// =============================================================================
// PPE Kick Start/Keep Alive
// =============================================================================

#if defined(CONFIG_ARCH_IPQ5332) || defined(CONFIG_ARCH_IPQ9574)
ulong ppe_last_pkt_rcvd_or_sent;
#endif /* CONFIG_ARCH_IPQ5332 || CONFIG_ARCH_IPQ9574 */

// =============================================================================
// 杂项
// =============================================================================

/**
 * json_escape - 对字符串进行JSON转义处理
 * @input: 要转义的输入字符串（可以为NULL）
 * @output: 存储转义后字符串的输出缓冲区
 * @output_buffer_size: 输出缓冲区的大小（字节）
 *
 * 该函数将输入字符串中的特殊字符转义为JSON兼容的格式，包括：
 *   - 双引号转义为 \"
 *   - 反斜杠转义为 \\
 *   - 换行符转义为 \n
 *   - 回车符转义为 \r
 *   - 制表符转义为 \t
 *   - 其他控制字符（< 0x20）替换为空格
 *   - 普通字符保持不变
 *
 * 返回值：写入输出缓冲区的字符数（不包括结尾的'\0'）
 */
size_t json_escape(const char *input, char *output, size_t output_buffer_size)
{
	int j = 0;

    if (!output || !output_buffer_size)
        return 0;

    if (!input)
        goto done;

    for (int i = 0; input[i] && j < output_buffer_size - 1; i++) {
        switch (input[i]) {
        case '"':
        case '\\':
            if (j + 2 >= output_buffer_size)
                goto done;
            output[j++] = '\\';
            output[j++] = input[i];
            break;
        case '\n':
        case '\r':
        case '\t':
            if (j + 2 >= output_buffer_size)
                goto done;
            output[j++] = '\\';
            if (input[i] == '\n')
                output[j++] = 'n';
            else if (input[i] == '\r')
                output[j++] = 'r';
            else
                output[j++] = 't';
            break;
        default:
            if ((unsigned char)input[i] < 0x20)
                output[j++] = ' ';
            else
                output[j++] = input[i];
        }
    }

done:
    output[j] = '\0';
    return j;
}

bool mmc_part_exists(const char *part_name)
{
	int ret;
	block_dev_desc_t *mmc_dev;
	disk_partition_t disk_info = {0};

	if (!has_mmc())
		return false;

	mmc_dev = mmc_get_dev(mmc_host.dev_num);
	if (!mmc_dev)
		return false;

	ret = get_partition_info_efi_by_name(mmc_dev, part_name, &disk_info);

	return ret ? false : true;
}

void set_file_info_env(ulong file_addr, ulong file_size_bytes)
{
	setenv_hex("fileaddr", file_addr);
    setenv_hex("filesize", file_size_bytes);
    if (has_mmc()) {
        block_dev_desc_t *mmc_dev = mmc_get_dev(mmc_host.dev_num);
        if (mmc_dev && mmc_dev->blksz) {
            setenv_hex("filesize_blks",
				file_size_bytes / mmc_dev->blksz
				+ (file_size_bytes % mmc_dev->blksz != 0));
        }
    }
}

void print_progress_bar(ulong progress, ulong interval, const char *end_str)
{
	int num_ch, num_hash, num_dot, idx = 0;
	char buf[256];

	if (interval == 0 || interval > 100 || 100 % interval != 0)
		return;

	memset(buf, '\0', sizeof(buf));

	num_ch = 100 / interval;
	num_hash = progress / interval;
	num_dot = num_ch - num_hash;

	buf[idx++] = '[';

	for (int i = 0; i < num_hash; i++)
		buf[idx++] = '#';

	for (int j = 0; j < num_dot; j++)
		buf[idx++] = '.';

	buf[idx++] = ']';

	idx += snprintf(buf + idx, sizeof(buf) - idx, " %lu%%", progress);

	if (end_str)
		strlcpy(buf + idx, end_str, sizeof(buf) - idx);

	puts(buf);
}

bool get_enable_state(const char *env_key, bool enable_by_default)
{
	if (!env_key || !env_key[0])
		return enable_by_default;

	const char *state_str = getenv(env_key);
	const char *disable_strs[] = {"0", "false", "no", "off"};
	const char *enable_strs[] = {"1", "true", "yes", "on"};

	if (!state_str)
		return enable_by_default;

	for (int i = 0; i < ARRAY_SIZE(disable_strs); i++)
		if (!strcasecmp(state_str, disable_strs[i]))
			return false;

	for (int i = 0; i < ARRAY_SIZE(enable_strs); i++)
		if (!strcasecmp(state_str, enable_strs[i]))
			return true;

	return enable_by_default;
}
