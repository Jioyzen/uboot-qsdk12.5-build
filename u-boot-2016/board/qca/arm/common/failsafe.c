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
#include <spi.h>
#include <spi_flash.h>
#include <mmc.h>
#include <sdhci.h>
#include <nand.h>
#include <linux/sizes.h>
#include <failsafe/failsafe.h>
#include <failsafe/fw_dec.h>
#include <ipq_api.h>
#include <u-boot/md5.h>
#include <capture.h>
#include <bootconfig.h>
#include <libfdt.h>
#include <asm/unaligned.h>

#include "untar.h"

#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern struct sdhci_host mmc_host;
#endif

#define FAILSAFE_CAPTURE_OUTPUT_SIZE 0x400

#define MAX_CMD_COUNT       10
#define MAX_CMD_LEN         255
#define MAX_CMD_BUF_SIZE    (MAX_CMD_LEN + 1)

#ifdef CONFIG_TARGET_IPQ5018_LYW_30M
/* The LYW-30M uses one UBI covering the original rootfs, rootfs_1 and
 * plugins areas. The OEM MIBIB still describes the old split layout. */
#define LYW_UBI_OFFSET      0x00900000
#define LYW_UBI_SIZE        0x07700000
#endif

static struct cmd_list {
	char list[MAX_CMD_COUNT][MAX_CMD_BUF_SIZE];
	int count;
} *runcmd;

static struct jdc_fw_entry {
	const char *part_name;
	const char *node_prefix;
	char node_name[256];
} jdc_fw_entries[] = {
	{ .part_name = "0:HLOS", .node_prefix = "hlos" },
	{ .part_name = "rootfs", .node_prefix = "rootfs" },
	{
		.part_name = "0:WIFIFW",
#if defined(CONFIG_ARCH_IPQ5332)
		.node_prefix = "wifi_fw"
#elif defined(CONFIG_TARGET_IPQ5018_JDCLOUD_AX3000)
		.node_prefix = "wifi_fw_ipq5018_qcn6122cs",
#else
		.node_prefix = "wififw"
#endif /* CONFIG_ARCH_IPQ5332 */
	},
#ifdef CONFIG_TARGET_IPQ5018_JDCLOUD_AX3000
	{ .part_name = "0:BTFW", .node_prefix = "btfw" }
#endif
};

static char gl_fw_ubi_name[66];
static ulong factory_fw_kernel_size;

static char info[666];
static char resp[888];
static fw_type_t fw_type;

/* Implemented in: u-boot-2016/board/qca/arm/common/cmd_bootqca.c */
extern int config_select(unsigned int addr, char *rcmd, int rcmd_size);

// =============================================================================
// 错误处理函数
// =============================================================================

#define RETURN_IF_NOR_FLASH_NOT_FOUND \
    do {    \
        if (!has_spi()) {    \
            handle_flash_not_found("SPI-NOR");   \
            return RET_FLASH_NOT_FOUND; \
        }   \
    } while (0)

#define RETURN_IF_NAND_FLASH_NOT_FOUND \
    do {    \
        if (!has_nand()) {    \
            handle_flash_not_found("NAND");   \
            return RET_FLASH_NOT_FOUND; \
        }   \
    } while (0)

#define RETURN_IF_MMC_FLASH_NOT_FOUND \
    do {    \
        if (!has_mmc()) {    \
            handle_flash_not_found("EMMC");   \
            return RET_FLASH_NOT_FOUND; \
        }   \
    } while (0)

#define RETURN_IF_BOTH_NOR_FLASH_AND_MMC_FLASH_NOT_FOUND \
    do {    \
        if (!has_spi() && !has_mmc()) {    \
            handle_flash_not_found("SPI-NOR or EMMC");   \
            return RET_FLASH_NOT_FOUND; \
        }   \
    } while (0)

static void handle_wrong_fw_type(const char *expected_file_type_str)
{
	char *actual_file_type_str = fw_type_to_string(fw_type);

	snprintf(info, sizeof(info),
		"{\"type\":\"wrong_file_type\",\"expected\":\"%s\",\"actual\":\"%s\"}",
		expected_file_type_str, actual_file_type_str);

	printf("Error: wrong file type (expected: %s, actual: %s)\n",
		expected_file_type_str, actual_file_type_str);
}

static void handle_wrong_upgrade_type(void)
{
	strlcpy(info, "{\"type\":\"wrong_upgrade_type\"}", sizeof(info));
	puts("Error: not supported upgrade type\n");
}

static void handle_file_too_big(const char *file_name, ulong file_size,
		const char *part_name, unsigned long long part_size)
{
	snprintf(info, sizeof(info),
		"{\"type\":\"file_too_big\","
		"\"filename\":\"%s\",\"filesize\":\"%lu\","
		"\"partname\":\"%s\",\"partsize\":\"%llu\"}",
		file_name, file_size, part_name, part_size);

	printf("Error: %s size (%lu bytes) exceeds %s size (%llu bytes)\n",
		file_name, file_size, part_name, part_size);
}

static void handle_flash_not_found(const char *flash_type_str)
{
	const char *fw_type_str = fw_type_to_string(fw_type);

	snprintf(info, sizeof(info),
		"{\"type\":\"flash_not_found\","
		"\"filetype\":\"%s\",\"flashtype\":\"%s\"}", fw_type_str, flash_type_str);

	printf("Error: upload file is %s, but no %s FLASH found\n",
		fw_type_str, flash_type_str);
}

static void handle_part_not_found(const char *part_name)
{
	snprintf(info, sizeof(info),
		"{\"type\":\"part_not_found\",\"partname\":\"%s\"}", part_name);

	printf("Error: partition %s not found\n", part_name);
}

static void handle_invalid_factory_fw(void)
{
	strlcpy(info, "{\"type\":\"invalid_factory_firmware\"}", sizeof(info));
}

static void handle_invalid_sysupgrade_fw(void)
{
	strlcpy(info,
		"{\"type\":\"wrong_file_type\","
		"\"expected\":\"sysupgrade tar image\","
		"\"actual\":\"not a valid sysupgrade tar image\"}", sizeof(info));

	printf("Error: not a valid sysupgrade tar image\n");
}

static void handle_invalid_qsdk_fw(const char *node_prefix)
{
	snprintf(info, sizeof(info),
		"{\"type\":\"fit_node_not_found\",\"node_prefix\":\"%s\"}", node_prefix);
}

static void handle_command_too_long(const char *file, const char *func, int line, int len)
{
	snprintf(info, sizeof(info),
		"{\"type\":\"command_too_long\",\"len\":\"%d\",\"maxlen\":\"%d\","
		"\"file\":\"%s\",\"func\":\"%s\",\"line\":\"%d\"}",
		len, MAX_CMD_LEN, file, func, line);

	printf("\nError at %s:%d/%s()\nCommand too long (len: %d, maxlen: %d), "
		"please increase MAX_CMD_LEN\n", file, line, func, len, MAX_CMD_LEN);
}

static void handle_too_many_commands(const char *file, const char *func, int line)
{
	snprintf(info, sizeof(info),
		"{\"type\":\"too_many_commands\",\"max\":\"%d\","
		"\"file\":\"%s\",\"func\":\"%s\",\"line\":\"%d\"}",
		MAX_CMD_COUNT, file, func, line);

	printf("\nError at %s:%d/%s()\nToo many commands (max: %d), "
		"please increase MAX_CMD_COUNT\n", file, line, func, MAX_CMD_COUNT);
}

static void handle_run_command_failed(const char *cmd, const char *output)
{
	char esc_cmd[MAX_CMD_LEN * 2];
	char esc_output[FAILSAFE_CAPTURE_OUTPUT_SIZE * 2];

	printf("Failed to run: %s\n", cmd);

	json_escape(cmd, esc_cmd, sizeof(esc_cmd));
	json_escape(output, esc_output, sizeof(esc_output));

	snprintf(info, sizeof(info),
		"{\"type\":\"run_cmd_failed\",\"cmd\":\"%s\",\"output\":\"%s\"}",
		esc_cmd, esc_output);
}

// =============================================================================
// 帮助函数（验证相关）
// =============================================================================

static int part_exists(const char *part_name, int flag)
{
	if (smem_part_exists(part_name) || mmc_part_exists(part_name))
		return RET_SUCCESS;

	if (flag)
		handle_part_not_found(part_name);

	return RET_PART_NOT_FOUND;
}

static int validate_file_size(char *file_name, char *part_name, ulong file_size_bytes)
{
	int ret;
    block_dev_desc_t *mmc_dev;
    disk_partition_t disk_info = {0};
    ulong part_size_blocks = 0;
	ulong part_size_bytes = 0;
    ulong file_size_blocks = 0;
	uint32_t offset_bytes, size_bytes;

	if (has_spi() || has_nand()) {
		ret = getpart_offset_size(part_name, &offset_bytes, &size_bytes);
		if (!ret) {
			part_size_bytes = (ulong)size_bytes;
			if (file_size_bytes > part_size_bytes)
				goto file_too_big;
			return RET_SUCCESS;
		}
	}

	if (!has_mmc())
		goto part_not_found;

	mmc_dev = mmc_get_dev(mmc_host.dev_num);
	if (!mmc_dev)
		goto part_not_found;

	ret = get_partition_info_efi_by_name(mmc_dev, part_name, &disk_info);
	if (ret)
		goto part_not_found;

	part_size_blocks = (ulong)disk_info.size;
	part_size_bytes = part_size_blocks * disk_info.blksz;

	if (disk_info.blksz)
		file_size_blocks = file_size_bytes / disk_info.blksz
								+ (file_size_bytes % disk_info.blksz != 0);

	if (file_size_blocks > part_size_blocks)
		goto file_too_big;

	return RET_SUCCESS;

file_too_big:
	handle_file_too_big(file_name, file_size_bytes, part_name, part_size_bytes);
	return RET_FILE_TOO_BIG;

part_not_found:
	handle_part_not_found(part_name);
	return RET_PART_NOT_FOUND;
}

static int parse_glinet_firmware(const void *data_addr)
{
	int ret;

	ret = fit_image_get_node_by_prefix(data_addr, FIT_IMAGES_PATH, "ubi",
                            gl_fw_ubi_name, sizeof(gl_fw_ubi_name));
    if (ret)
		handle_invalid_qsdk_fw("ubi");

	return ret ? RET_FAILURE : RET_SUCCESS;
}

static int parse_jdcloud_firmware(const void *data_addr)
{
	int ret;
	struct jdc_fw_entry *entry;

	for (int i = 0; i < ARRAY_SIZE(jdc_fw_entries); i++) {
		entry = &jdc_fw_entries[i];
		ret = fit_image_get_node_by_prefix(data_addr, FIT_IMAGES_PATH,
				entry->node_prefix, entry->node_name, sizeof(entry->node_name));
		if (ret) {
			handle_invalid_qsdk_fw(entry->node_prefix);
			return RET_FAILURE;
		}
	}

	return RET_SUCCESS;
}

static int parse_factory_firmware(const void *data_addr, ulong data_size)
{
	const void *p = data_addr;
	const u32 magic = HEADER_MAGIC_SQUASHFS;
	const size_t magic_len = sizeof(u32);
	ulong size_remain = data_size;

	if (!p)
		goto fail;

	while (size_remain >= magic_len) {
		size_remain--;
		if (!memcmp(p, &magic, magic_len) &&
			((p - data_addr) % SZ_1M == 0)) {
			/* 内核大小需为 1 MiB 的整数倍 */
			factory_fw_kernel_size = p - data_addr;
			return RET_SUCCESS;
		}
		p++;
	}

fail:
	handle_invalid_factory_fw();
	return RET_FAILURE;
}

void get_image_md5_str(const void *data_addr, ulong data_size, char *buf, size_t buf_size)
{
	const char *hexchars = "0123456789abcdef";
	u8 md5_sum[16];

	memset(buf, 0, buf_size);
	if (buf_size < 33)
		return;

	md5((u8 *)data_addr, data_size, md5_sum);
	for (int i = 0; i < 16; i++) {
		u8 hex = (md5_sum[i] >> 4) & 0xf;
		buf[i * 2] = hexchars[hex];
		hex = md5_sum[i] & 0xf;
		buf[i * 2 + 1] = hexchars[hex];
	}
}

// =============================================================================
// 帮助函数（刷写相关）
// =============================================================================

#define ADD_CMD_TO_LIST(fmt, args...)   \
    do {    \
		int ret = _add_cmd_to_list(__FILE__, __func__, __LINE__, fmt, ##args);	\
		if (ret)	\
			return ret;	\
    } while (0)

static int _add_cmd_to_list(const char *file, const char *func, int line, const char *fmt, ...)
{
	va_list args;
	int len;

	if (runcmd->count < MAX_CMD_COUNT) {
		va_start(args, fmt);
		len = vsnprintf(runcmd->list[runcmd->count++], MAX_CMD_BUF_SIZE, fmt, args);
		va_end(args);
		if (len >= MAX_CMD_BUF_SIZE)	{
			handle_command_too_long(file, func, line, len);
			return RET_COMMAND_TOO_LONG;
		}
	} else {
		handle_too_many_commands(file, func, line);
		return RET_TOO_MANY_COMMANDS;
	}

	return RET_SUCCESS;
}

static void print_upgrade_hint(const char *upgrade_type_str)
{
	printf("\n"
		"********************************\n"
		" %s UPGRADING\n"
		" DO NOT POWER OFF DEVICE\n"
		"********************************\n", upgrade_type_str);
}

static int make_gpt_write_cmd(ulong data_addr, ulong data_size)
{
	block_dev_desc_t *mmc_dev;
    ulong data_size_blocks;

    mmc_dev = mmc_get_dev(mmc_host.dev_num);
    if (mmc_dev == NULL || mmc_dev->blksz == 0)
        data_size_blocks = 0;
    else
        data_size_blocks = data_size / mmc_dev->blksz
                             + (data_size % mmc_dev->blksz != 0);

	ADD_CMD_TO_LIST("mmc erase 0x0 0x%lx && mmc write 0x%lx 0x0 0x%lx",
		data_size_blocks, data_addr, data_size_blocks);

	return RET_SUCCESS;
}

static ulong get_nand_writable_data_size(uint32_t data_size)
{
	uint32_t adj_size, writable_size = data_size;
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];

	if (nand->writesize) {
		adj_size = data_size % nand->writesize;
		if (adj_size)
			writable_size += nand->writesize - adj_size;
	}

	return (ulong)writable_size;
}

static int failsafe_run_command(void *cmd)
{
	return run_command((const char *)cmd, 0);
}

static int failsafe_run_command_capture(const char *cmd)
{
	char output[FAILSAFE_CAPTURE_OUTPUT_SIZE];
	int ret;

	printf("\n### Executing: %s\n", cmd);

	ret = call_func_capture(failsafe_run_command, (void *)cmd,
			output, sizeof(output), NULL);

	if (ret)
		handle_run_command_failed(cmd, output);

	return ret;
}

static int failsafe_run_command_list(void)
{
    for (int i = 0; i < runcmd->count; i++)
        if (failsafe_run_command_capture(runcmd->list[i]))
            return RET_FAILURE;

    return RET_SUCCESS;
}

// =============================================================================
// 文件验证函数
// =============================================================================

#ifdef CONFIG_TARGET_IPQ5018_LYW_30M
#define LYW_PEB_SIZE 0x20000
#define LYW_LEB_SIZE (LYW_PEB_SIZE - 4096)
#define LYW_VTBL_ID 0x7fffefff

static int lyw_bad_factory(const char *reason)
{
	printf("LYW-30M: invalid factory UBI: %s\n", reason);
	snprintf(info, sizeof(info),
		"{\"type\":\"invalid_factory_firmware\",\"reason\":\"%s\"}", reason);
	return RET_WRONG_FW_TYPE;
}

/* Validate the logical UBI geometry and this board's embedded FIT before
 * erasing anything. Use the checked HLOS area below U-Boot for scratch:
 * on 256 MiB boards HTTP uploads themselves start at 0x41000000. */
static int lyw_validate_factory(const void *image, ulong size)
{
	const u8 *data = image, *table = NULL, *first = NULL, *fdt, *prop;
	static const char * const names[] = { "kernel", "rootfs", "rootfs_data" };
	u8 *fit;
	ulong off, total, copied, length, fit_size;
	u32 volume, logical, reserved = 0, kernel_pebs = 0;
	int i, tables = 0, cfg, node, plen;

	if (size < 3 * LYW_PEB_SIZE || size % LYW_PEB_SIZE)
		return lyw_bad_factory("unaligned or truncated image");
	for (off = 0; off < size; off += LYW_PEB_SIZE) {
		const u8 *ec = data + off, *vid = ec + 2048;
		if (get_unaligned_be32(ec) != 0x55424923 || ec[4] != 1 ||
		    get_unaligned_be32(ec + 16) != 2048 ||
		    get_unaligned_be32(ec + 20) != 4096 ||
		    get_unaligned_be32(ec + 60) != (crc32(0, ec, 60) ^ 0xffffffff) ||
		    get_unaligned_be32(vid) != 0x55424921 || vid[4] != 1 ||
		    get_unaligned_be32(vid + 60) != (crc32(0, vid, 60) ^ 0xffffffff))
			return lyw_bad_factory("EC/VID geometry or CRC");
		volume = get_unaligned_be32(vid + 8);
		logical = get_unaligned_be32(vid + 12);
		if (volume == LYW_VTBL_ID) {
			if (logical > 1 || ++tables > 2)
				return lyw_bad_factory("layout volume copies");
			if (table && memcmp(table, ec + 4096, 128 * 172))
				return lyw_bad_factory("inconsistent volume tables");
			table = ec + 4096;
		} else if (volume > 2) {
			return lyw_bad_factory("unexpected user volume");
		} else if (volume == 0 && logical == 0) {
			if (first)
				return lyw_bad_factory("duplicate kernel first block");
			first = ec + 4096;
		}
	}
	if (tables != 2 || !table || !first)
		return lyw_bad_factory("missing volume table or kernel");
	for (i = 0; i < 128; i++) {
		const u8 *record = table + i * 172;
		u32 count = get_unaligned_be32(record);
		if (get_unaligned_be32(record + 168) != (crc32(0, record, 168) ^ 0xffffffff))
			return lyw_bad_factory("volume table CRC");
		if (i >= 3) {
			if (count)
				return lyw_bad_factory("extra volume reservation");
			continue;
		}
		if (!count || count > LYW_UBI_SIZE / LYW_PEB_SIZE ||
		    record[12] != 1 || get_unaligned_be32(record + 4) != 1 ||
		    get_unaligned_be16(record + 14) != strlen(names[i]) ||
		    memcmp(record + 16, names[i], strlen(names[i])))
			return lyw_bad_factory("expected kernel/rootfs/rootfs_data volumes");
		reserved += count;
		if (i == 0)
			kernel_pebs = count;
		if (i == 2 && !(record[144] & 1))
			return lyw_bad_factory("rootfs_data must autoresize");
	}
	if (reserved + 24 > LYW_UBI_SIZE / LYW_PEB_SIZE)
		return lyw_bad_factory("insufficient NAND reserve");
	if (fdt_check_header(first))
		return lyw_bad_factory("kernel is not a FIT");
	fit_size = fdt_totalsize(first);
	if (fit_size < 40 || fit_size > SZ_16M ||
	    fit_size > kernel_pebs * LYW_LEB_SIZE)
		return lyw_bad_factory("FIT size");
	/* Keep a guard below the FDT/code/stack boundary, then check both the
	 * platform memory map and the actual uploaded image. No heap expansion
	 * or upload-buffer relocation is needed for a multi-MiB FIT. */
	fit = (u8 *)(CONFIG_IPQ_FDT_HIGH - SZ_64K - ALIGN(fit_size, SZ_4K));
	if (!is_memory_region_available((ulong)fit, fit_size) ||
	    ((ulong)data < (ulong)fit + fit_size &&
	     (ulong)data + size > (ulong)fit))
		return lyw_bad_factory("no non-overlapping FIT workspace");
	for (copied = 0, logical = 0; copied < fit_size; logical++) {
		const u8 *found = NULL;
		for (off = 0; off < size; off += LYW_PEB_SIZE) {
			const u8 *vid = data + off + 2048;
			if (get_unaligned_be32(vid + 8) != 0 ||
			    get_unaligned_be32(vid + 12) != logical)
				continue;
			if (found)
				return lyw_bad_factory("duplicate kernel LEB");
			found = data + off + 4096;
		}
		if (!found)
			return lyw_bad_factory("missing kernel LEB");
		length = min((ulong)LYW_LEB_SIZE, fit_size - copied);
		memcpy(fit + copied, found, length);
		copied += length;
	}
	cfg = fdt_path_offset(fit, "/configurations/config@mp03.3");
	if (cfg < 0)
		return lyw_bad_factory("missing config@mp03.3");
	prop = fdt_getprop(fit, cfg, "fdt", &plen);
	if (!prop || plen < 2 || !memchr(prop, 0, plen))
		return lyw_bad_factory("missing FIT device tree reference");
	node = fdt_path_offset(fit, "/images");
	node = node < 0 ? node : fdt_subnode_offset(fit, node, (const char *)prop);
	fdt = node < 0 ? NULL : fdt_getprop(fit, node, "data", &plen);
	if (!fdt || plen < 40 || fdt_check_header(fdt) || fdt_totalsize(fdt) > plen ||
	    fdt_node_check_compatible(fdt, 0, "lingyan,lyw-30m"))
		return lyw_bad_factory("device tree is not LYW-30M");
	/* A RAM profile or a different NAND layout must not be installed. */
	if (!fdt_getprop(fdt, fdt_path_offset(fdt, "/chosen"), "lingyan,nand-writable", NULL))
		return lyw_bad_factory("not the single-system NAND profile");
	total = fit_size;
	printf("LYW-30M: factory UBI checked, FIT %lu bytes, %u reserved LEBs\n", total, reserved);
	return RET_SUCCESS;
}
#endif

static int failsafe_validate_firmware(const void *data_addr, ulong data_size)
{
    int ret = RET_SUCCESS;
	size_t kernel_size, rootfs_size;

    switch (fw_type) {
    case FW_TYPE_FIT:
		RETURN_IF_BOTH_NOR_FLASH_AND_MMC_FLASH_NOT_FOUND;
        ret = parse_factory_firmware(data_addr, data_size);
		if (ret)
			break;
        ret = validate_file_size("firmware kernel", "0:HLOS", factory_fw_kernel_size);
        if (ret)
            break;
        ret = validate_file_size("firmware rootfs", "rootfs", data_size - factory_fw_kernel_size);
        break;
    case FW_TYPE_SYSUPGRADE:
		RETURN_IF_BOTH_NOR_FLASH_AND_MMC_FLASH_NOT_FOUND;
		ret = parse_tar_image(data_addr, (size_t)data_size,
				NULL, &kernel_size, NULL, &rootfs_size);
        if (ret) {
            handle_invalid_sysupgrade_fw();
            return RET_WRONG_FW_TYPE;
        }
        ret = validate_file_size("firmware kernel", "0:HLOS", (ulong)kernel_size);
        if (ret)
            break;
        ret = validate_file_size("firmware rootfs", "rootfs", (ulong)rootfs_size);
        break;
    case FW_TYPE_ASUSWRT_EMMC:
		RETURN_IF_MMC_FLASH_NOT_FOUND;
        ret = part_exists("0:HLOS", 1);
        if (ret)
            break;
        ret = part_exists("rootfs", 1);
        break;
	case FW_TYPE_GLINET_V3:
	case FW_TYPE_GLINET_V4:
		RETURN_IF_NAND_FLASH_NOT_FOUND;
		ret = part_exists("rootfs", 1);
        if (ret)
            break;
		ret = parse_glinet_firmware(data_addr);
		break;
    case FW_TYPE_JDCLOUD:
		RETURN_IF_MMC_FLASH_NOT_FOUND;
		for (int i = 0; i < ARRAY_SIZE(jdc_fw_entries); i++) {
			ret = part_exists(jdc_fw_entries[i].part_name, 1);
			if (ret)
				break;
		}
        if (ret)
            break;
#ifndef CONFIG_ARCH_IPQ5332
        ret = part_exists("rootfs_data", 1);
        if (ret)
            break;
#endif /* CONFIG_ARCH_IPQ5332 */
        ret = parse_jdcloud_firmware(data_addr);
        break;
    case FW_TYPE_UBI:
        RETURN_IF_NAND_FLASH_NOT_FOUND;
#ifdef CONFIG_TARGET_IPQ5018_LYW_30M
        if ((uint32_t)data_size > LYW_UBI_SIZE) {
            handle_file_too_big("LYW-30M single UBI", data_size,
                    "rootfs", LYW_UBI_SIZE);
            return RET_FILE_TOO_BIG;
        }
        return lyw_validate_factory(data_addr, data_size);
#else
        ret = validate_file_size("firmware", "rootfs", data_size);
#endif
        break;
    default:
        handle_wrong_fw_type("FIRMWARE");
        ret = RET_WRONG_FW_TYPE;
    }

    return ret;
}

static int failsafe_validate_uboot(const void *data_addr, ulong data_size)
{
    if (fw_type != FW_TYPE_ELF) {
        handle_wrong_fw_type("U-BOOT ELF");
        return RET_WRONG_FW_TYPE;
    }

    return validate_file_size("U-BOOT", "0:APPSBL", data_size);
}

static int failsafe_validate_art(const void *data_addr, ulong data_size)
{
    /*
     * ART 没有固定的魔数，所以无法识别一个文件是否为 ART。
     * 这里使用排除法，排除一些已知的、非 ART 的文件类型。
     */
    switch (fw_type) {
    case FW_TYPE_ASUSWRT_EMMC:
    case FW_TYPE_CDT:
    case FW_TYPE_ELF:
    case FW_TYPE_FIT:
	case FW_TYPE_GLINET_V3:
	case FW_TYPE_GLINET_V4:
    case FW_TYPE_GPT:
    case FW_TYPE_JDCLOUD:
    case FW_TYPE_LEGACY_IMAGE:
    case FW_TYPE_MIBIB_NAND:
    case FW_TYPE_MIBIB_NOR:
    case FW_TYPE_SIMG_NAND:
    case FW_TYPE_SIMG_NOR:
    case FW_TYPE_SYSUPGRADE:
    case FW_TYPE_UBI:
        handle_wrong_fw_type("ART");
        return RET_WRONG_FW_TYPE;
    default:
        return validate_file_size("ART", "0:ART", data_size);
    }
}

static int failsafe_validate_cdt(const void *data_addr, ulong data_size)
{
    if (fw_type != FW_TYPE_CDT) {
        handle_wrong_fw_type("CDT");
        return RET_WRONG_FW_TYPE;
    }

    return validate_file_size("CDT", "0:CDT", data_size);
}

static int failsafe_validate_ptable(const void *data_addr, ulong data_size)
{
	switch(fw_type) {
	case FW_TYPE_GPT:
		RETURN_IF_MMC_FLASH_NOT_FOUND;
		return RET_SUCCESS;
    case FW_TYPE_MIBIB_NAND:
        RETURN_IF_NAND_FLASH_NOT_FOUND;
        return validate_file_size("MIBIB", "0:MIBIB", data_size);
    case FW_TYPE_MIBIB_NOR:
        RETURN_IF_NOR_FLASH_NOT_FOUND;
        return validate_file_size("MIBIB", "0:MIBIB", data_size);
    default:
        handle_wrong_fw_type("Partition Table (GPT or MIBIB)");
        return RET_WRONG_FW_TYPE;
    }
}

static int failsafe_validate_simg(const void *data_addr, ulong data_size)
{
	struct mmc *mmc;
	struct spi_flash *spi;
	nand_info_t *nand;
	unsigned long long flash_device_size;

    switch(fw_type) {
    case FW_TYPE_SIMG_EMMC:
        RETURN_IF_MMC_FLASH_NOT_FOUND;
		mmc = find_mmc_device(mmc_host.dev_num);
		flash_device_size = mmc->capacity_user;
        break;
    case FW_TYPE_SIMG_NAND:
        RETURN_IF_NAND_FLASH_NOT_FOUND;
		nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
		flash_device_size = nand->size;
        break;
    case FW_TYPE_SIMG_NOR:
		RETURN_IF_NOR_FLASH_NOT_FOUND;
		spi = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
					CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
		flash_device_size = spi->size;
		break;
    default:
        handle_wrong_fw_type("Single Image");
        return RET_WRONG_FW_TYPE;
    }

	if ((unsigned long long)data_size > flash_device_size) {
		handle_file_too_big("Single Image", data_size, "Whole Chip", flash_device_size);
		return RET_FILE_TOO_BIG;
	}

	return RET_SUCCESS;
}

static int failsafe_validate_initramfs(const void *data_addr, ulong data_size)
{
    if (fw_type != FW_TYPE_FIT) {
        handle_wrong_fw_type("FIT INITRAMFS UIMAGE");
        return RET_WRONG_FW_TYPE;
    }

    return RET_SUCCESS;
}

// =============================================================================
// 文件刷写函数
// =============================================================================

static int failsafe_write_firmware(ulong data_addr, ulong data_size)
{
#ifdef CONFIG_TARGET_IPQ5018_LYW_30M
	/* Recheck at the destructive boundary as well as during upload. */
	int ret = failsafe_validate_firmware((const void *)data_addr, data_size);
	if (ret)
		return ret;
#endif
	print_upgrade_hint("FIRMWARE");

	switch (fw_type) {
	case FW_TYPE_FIT:
		RETURN_IF_BOTH_NOR_FLASH_AND_MMC_FLASH_NOT_FOUND;
		ADD_CMD_TO_LIST("flash 0:HLOS 0x%lx 0x%lx",
			data_addr, factory_fw_kernel_size);
		ADD_CMD_TO_LIST("flash rootfs 0x%lx 0x%lx",
			data_addr + factory_fw_kernel_size,
			data_size - factory_fw_kernel_size);
		break;
	case FW_TYPE_GLINET_V3:
	case FW_TYPE_GLINET_V4:
		RETURN_IF_NAND_FLASH_NOT_FOUND;
		ADD_CMD_TO_LIST("xtract_n_flash 0x%lx %s rootfs",
			data_addr, gl_fw_ubi_name);
		break;
	case FW_TYPE_JDCLOUD:
		RETURN_IF_MMC_FLASH_NOT_FOUND;
		for (int i = 0; i < ARRAY_SIZE(jdc_fw_entries); i++) {
			struct jdc_fw_entry *entry = &jdc_fw_entries[i];
			ADD_CMD_TO_LIST("xtract_n_flash 0x%lx %s %s",
				data_addr, entry->node_name, entry->part_name);
		}
#if !defined(CONFIG_ARCH_IPQ5332) && !defined(CONFIG_TARGET_IPQ5018_JDCLOUD_AX3000)
		ADD_CMD_TO_LIST("flasherase rootfs_data");
#endif /* !CONFIG_ARCH_IPQ5332 && !CONFIG_TARGET_IPQ5018_JDCLOUD_AX3000 */
		break;
	case FW_TYPE_SYSUPGRADE:
	case FW_TYPE_ASUSWRT_EMMC:
		RETURN_IF_BOTH_NOR_FLASH_AND_MMC_FLASH_NOT_FOUND;
		ADD_CMD_TO_LIST("untar 0x%lx 0x%lx", data_addr, data_size);
		ADD_CMD_TO_LIST("flash 0:HLOS $kernel_addr $kernel_size");
		ADD_CMD_TO_LIST("flash rootfs $rootfs_addr $rootfs_size");
		break;
	case FW_TYPE_UBI:
		RETURN_IF_NAND_FLASH_NOT_FOUND;
#ifdef CONFIG_TARGET_IPQ5018_LYW_30M
		ADD_CMD_TO_LIST("nand device %d && nand erase 0x%x 0x%x && "
			"nand write 0x%lx 0x%x 0x%lx",
			CONFIG_NAND_FLASH_INFO_IDX, LYW_UBI_OFFSET, LYW_UBI_SIZE,
			data_addr, LYW_UBI_OFFSET,
			get_nand_writable_data_size(data_size));
		/* A stock APPSBLENV does not contain the single-UBI variables.
		 * Install the boot recipe only after the firmware write succeeds;
		 * keep factory identity and calibration-related environment fields. */
		ADD_CMD_TO_LIST("setenv lyw_mtd '%s'", LYW_MTD_COMMAND);
		ADD_CMD_TO_LIST("setenv lyw_args '%s'", LYW_ARGS_COMMAND);
		ADD_CMD_TO_LIST("setenv lyw_boot '%s'", LYW_BOOT_COMMAND);
		ADD_CMD_TO_LIST("setenv bootcmd 'run lyw_boot'");
		ADD_CMD_TO_LIST("saveenv");
#else
		ADD_CMD_TO_LIST("flash rootfs 0x%lx 0x%lx", data_addr, data_size);
#endif
		break;
	default:
		handle_wrong_fw_type("FIRMWARE");
		return RET_WRONG_FW_TYPE;
	}

#ifdef CONFIG_TARGET_IPQ5018_JDCLOUD_AX3000
	if (!part_exists("rootfs_data", 0))
		ADD_CMD_TO_LIST("flasherase rootfs_data");
#endif

#ifndef CONFIG_TARGET_IPQ5018_LYW_30M
	/* LYW-30M boots a single UBI using lyw_boot. Its preserved OEM
	 * BOOTCONFIG is not part of firmware installation or slot selection. */
	if (validate_bootconfig())
		ADD_CMD_TO_LIST("bootconfig set firmware 0");
#endif

	return failsafe_run_command_list();
}

static int failsafe_write_uboot(ulong data_addr, ulong data_size)
{
    print_upgrade_hint("U-BOOT");

    if (fw_type != FW_TYPE_ELF) {
		handle_wrong_fw_type("U-BOOT ELF");
        return RET_WRONG_FW_TYPE;
	}

	ADD_CMD_TO_LIST("flash 0:APPSBL 0x%lx 0x%lx", data_addr, data_size);

#ifndef CONFIG_TARGET_IPQ5018_LYW_30M
	if (!part_exists("0:APPSBL_1", 0))
		ADD_CMD_TO_LIST("flash 0:APPSBL_1 0x%lx 0x%lx", data_addr, data_size);
#endif

	return failsafe_run_command_list();
}

static int failsafe_write_art(ulong data_addr, ulong data_size)
{
    print_upgrade_hint("ART");

	ADD_CMD_TO_LIST("flash 0:ART 0x%lx 0x%lx", data_addr, data_size);

	return failsafe_run_command_list();
}

static int failsafe_write_cdt(ulong data_addr, ulong data_size)
{
    print_upgrade_hint("CDT");

    if (fw_type != FW_TYPE_CDT) {
		handle_wrong_fw_type("CDT");
        return RET_WRONG_FW_TYPE;
	}

	ADD_CMD_TO_LIST("flash 0:CDT 0x%lx 0x%lx", data_addr, data_size);

	if (!part_exists("0:CDT_1", 0))
		ADD_CMD_TO_LIST("flash 0:CDT_1 0x%lx 0x%lx", data_addr, data_size);

	return failsafe_run_command_list();
}

static int failsafe_write_ptable(ulong data_addr, ulong data_size)
{
	int ret;

	print_upgrade_hint("PTABLE");

	switch(fw_type) {
	case FW_TYPE_GPT:
		RETURN_IF_MMC_FLASH_NOT_FOUND;
		ret = make_gpt_write_cmd(data_addr, data_size);
		if (ret)
			return ret;
		break;
    case FW_TYPE_MIBIB_NAND:
        RETURN_IF_NAND_FLASH_NOT_FOUND;
		ADD_CMD_TO_LIST("flash 0:MIBIB 0x%lx 0x%lx", data_addr, data_size);
        break;
    case FW_TYPE_MIBIB_NOR:
        RETURN_IF_NOR_FLASH_NOT_FOUND;
		ADD_CMD_TO_LIST("flash 0:MIBIB 0x%lx 0x%lx", data_addr, data_size);
        break;
    default:
        handle_wrong_fw_type("Partition Table (GPT or MIBIB)");
        return RET_WRONG_FW_TYPE;
    }

	return failsafe_run_command_list();
}

static int failsafe_write_simg(ulong data_addr, ulong data_size)
{
	int ret;
	ulong writable_size;

	print_upgrade_hint("SIMG");

	switch (fw_type) {
	case FW_TYPE_SIMG_EMMC:
		RETURN_IF_MMC_FLASH_NOT_FOUND;
		ret = make_gpt_write_cmd(data_addr, data_size);
		if (ret)
			return ret;
		break;
	case FW_TYPE_SIMG_NAND:
		RETURN_IF_NAND_FLASH_NOT_FOUND;
		writable_size = get_nand_writable_data_size(data_size);
		ADD_CMD_TO_LIST("nand device %d && nand erase 0x0 0x%lx && nand write 0x%lx 0x0 0x%lx",
			CONFIG_NAND_FLASH_INFO_IDX, writable_size, data_addr, writable_size);
		break;
	case FW_TYPE_SIMG_NOR:
		RETURN_IF_NOR_FLASH_NOT_FOUND;
		ADD_CMD_TO_LIST("sf probe && sf update 0x%lx 0x0 0x%lx", data_addr, data_size);
		break;
	default:
		handle_wrong_fw_type("Single Image");
		return RET_WRONG_FW_TYPE;
	}

	return failsafe_run_command_list();
}

// =============================================================================
// 暴露给外部的 API
// =============================================================================

int boot_from_mem(ulong data_addr)
{
    int ret;
	char rcmd[99], bootm_arg[66];

	puts("\n"
        "********************************\n"
        " INITRAMFS BOOTING\n"
        " DO NOT POWER OFF DEVICE\n"
        "********************************\n");

    ret = config_select((unsigned int)data_addr, bootm_arg, sizeof(bootm_arg));

    if (!ret)
		snprintf(rcmd, sizeof(rcmd), "bootm %s", bootm_arg);
	else
		snprintf(rcmd, sizeof(rcmd), "bootm 0x%lx", data_addr);

	printf("\n### Executing: %s\n", rcmd);

	return run_command(rcmd, 0);
}

int failsafe_validate_image(upgrade_type_t upgrade_type, const char *filename,
		const void *data_addr, ulong data_size, struct httpd_response *response)
{
	int ret;

	fw_type = check_fw_type((uintptr_t)data_addr, data_size);

	memset(info, 0, sizeof(info));
	memset(resp, 0, sizeof(resp));

	httpd_debug("fw_type = %d (%s), data_addr = 0x%lx, data_size = %lu (0x%lx)\n",
		fw_type, fw_type_to_string(fw_type), (ulong)data_addr, data_size, data_size);

	switch (upgrade_type) {
	case WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE:
		ret = failsafe_validate_firmware(data_addr, data_size);
		break;
	case WEBFAILSAFE_UPGRADE_TYPE_UBOOT:
		ret = failsafe_validate_uboot(data_addr, data_size);
		break;
	case WEBFAILSAFE_UPGRADE_TYPE_ART:
		ret = failsafe_validate_art(data_addr, data_size);
		break;
	case WEBFAILSAFE_UPGRADE_TYPE_CDT:
		ret = failsafe_validate_cdt(data_addr, data_size);
		break;
	case WEBFAILSAFE_UPGRADE_TYPE_PTABLE:
		ret = failsafe_validate_ptable(data_addr, data_size);
		break;
	case WEBFAILSAFE_UPGRADE_TYPE_SIMG:
		ret = failsafe_validate_simg(data_addr, data_size);
		break;
	case WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS:
		ret = failsafe_validate_initramfs(data_addr, data_size);
		break;
	default:
		handle_wrong_upgrade_type();
		ret = RET_WRONG_UPGRADE_TYPE;
	}

	if (!ret) {
		char md5_str[33], esc_filename[512];

		get_image_md5_str(data_addr, data_size, md5_str, sizeof(md5_str));
		json_escape(filename, esc_filename, sizeof(esc_filename));

		snprintf(info, sizeof(info),
			"{\"type\":\"%s\",\"size\":\"%lu\",\"md5\":\"%s\",\"name\":\"%s\"}",
			fw_type_to_string(fw_type), data_size, md5_str, esc_filename[0] ? esc_filename : "NONE");
	}

	snprintf(resp, sizeof(resp),
		"{\"status\":\"%s\",\"info\":%s}",
		ret ? "fail" : "success", info);
	response->data = resp;
	response->size = strlen(response->data);

	return ret;
}

int failsafe_write_image(upgrade_type_t upgrade_type, ulong data_addr,
		ulong data_size, struct httpd_response *response)
{
    int ret;
	struct cmd_list cmdlist;

	runcmd = &cmdlist;
	runcmd->count = 0;
	fw_type = check_fw_type((uintptr_t)data_addr, data_size);

	memset(info, 0, sizeof(info));
	memset(resp, 0, sizeof(resp));

	switch (upgrade_type) {
    case WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE:
        ret = failsafe_write_firmware(data_addr, data_size);
        break;
    case WEBFAILSAFE_UPGRADE_TYPE_UBOOT:
        ret = failsafe_write_uboot(data_addr, data_size);
        break;
    case WEBFAILSAFE_UPGRADE_TYPE_ART:
        ret = failsafe_write_art(data_addr, data_size);
        break;
    case WEBFAILSAFE_UPGRADE_TYPE_CDT:
        ret = failsafe_write_cdt(data_addr, data_size);
        break;
    case WEBFAILSAFE_UPGRADE_TYPE_PTABLE:
        ret = failsafe_write_ptable(data_addr, data_size);
        break;
    case WEBFAILSAFE_UPGRADE_TYPE_SIMG:
        ret = failsafe_write_simg(data_addr, data_size);
        break;
    default:
		handle_wrong_upgrade_type();
        ret = RET_WRONG_UPGRADE_TYPE;
	}

	if (ret) {
		snprintf(resp, sizeof(resp),
			"{\"status\":\"fail\",\"info\":%s}", info);
		response->data = resp;
	}

    return ret;
}
