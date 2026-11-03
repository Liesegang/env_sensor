#include "sd_test.h"

#include <errno.h>
#include <ff.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define TEST_BYTES 1024

static FATFS fatfs;
static struct fs_mount_t mount = {
	.type = FS_FATFS,
	.fs_data = &fatfs,
	.mnt_point = "/SD:",
};
static uint8_t expected[TEST_BYTES];
static uint8_t received[TEST_BYTES];
static char filename[32];
static struct k_thread sd_thread;
K_THREAD_STACK_DEFINE(sd_stack, 6144);
static const struct gpio_dt_spec display_cs =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), display_cs_gpios);

static int select_new_filename(void)
{
	struct fs_dirent entry;

	for (unsigned int index = 0; index < 1000000; index++) {
		snprintf(filename, sizeof(filename), "/SD:/CT%06u.BIN", index);
		int err = fs_stat(filename, &entry);

		if (err == -ENOENT) {
			return 0;
		}
		if (err != 0) {
			return err;
		}
	}
	return -ENOSPC;
}

static int write_test_file(void)
{
	struct fs_file_t file;
	fs_file_t_init(&file);
	int err = fs_open(&file, filename, FS_O_CREATE | FS_O_WRITE);

	if (err != 0) {
		return err;
	}
	for (size_t offset = 0; offset < sizeof(expected);) {
		ssize_t count = fs_write(&file, expected + offset, sizeof(expected) - offset);

		if (count <= 0) {
			err = count < 0 ? (int)count : -ENOSPC;
			break;
		}
		offset += count;
	}
	if (err == 0) {
		err = fs_sync(&file);
	}
	int close_err = fs_close(&file);

	return err != 0 ? err : close_err;
}

static int verify_test_file(void)
{
	struct fs_file_t file;
	fs_file_t_init(&file);
	int err = fs_open(&file, filename, FS_O_READ);

	if (err != 0) {
		return err;
	}
	for (size_t offset = 0; offset < sizeof(received);) {
		ssize_t count = fs_read(&file, received + offset, sizeof(received) - offset);

		if (count <= 0) {
			err = count < 0 ? (int)count : -EIO;
			break;
		}
		offset += count;
	}
	if (err == 0 && memcmp(expected, received, sizeof(expected)) != 0) {
		err = -EILSEQ;
	}
	if (err == 0) {
		uint8_t extra;
		ssize_t count = fs_read(&file, &extra, sizeof(extra));

		err = count < 0 ? (int)count : count == 0 ? 0 : -EIO;
	}
	int close_err = fs_close(&file);

	return err != 0 ? err : close_err;
}

static void sd_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	printk("SD TEST: hardware SPI20, max 4 MHz; existing FAT required\n");
	int err;

	while ((err = fs_mount(&mount)) != 0) {
		printk("SD TEST: mount failed: %d; retry in 10 s\n", err);
		k_sleep(K_SECONDS(10));
	}
	printk("SD TEST: FAT mounted\n");
	bool mounted = true;

	/* Exercise multiple sectors with a non-repeating byte pattern. */
	for (size_t i = 0; i < sizeof(expected); i++) {
		expected[i] = (uint8_t)((i * 73U + 19U) ^ (i >> 8));
	}
	err = select_new_filename();
	if (err == 0) {
		err = write_test_file();
	}
	if (err == 0) {
		printk("SD TEST: wrote and synced %u bytes to %s\n", TEST_BYTES, filename);
		/* Re-mount before reading so verification reaches the card again. */
		err = fs_unmount(&mount);
		if (err == 0) {
			mounted = false;
			err = fs_mount(&mount);
			mounted = err == 0;
		}
	}
	if (err == 0) {
		err = verify_test_file();
	}
	if (err == 0) {
		printk("SD TEST: PASS, %u bytes read back after remount and matched\n", TEST_BYTES);
		int remove_err = fs_unlink(filename);

		if (remove_err == 0) {
			printk("SD TEST: temporary file removed\n");
		} else {
			printk("SD TEST: cleanup failed: %d; file=%s\n", remove_err, filename);
		}
	} else {
		printk("SD TEST: FAIL: %d; file=%s\n", err,
		       filename[0] != '\0' ? filename : "none");
	}
	if (mounted) {
		int unmount_err = fs_unmount(&mount);

		if (unmount_err != 0) {
			printk("SD TEST: unmount failed: %d\n", unmount_err);
		} else {
			printk("SD TEST: unmounted\n");
		}
	}
}

int sd_test_start(void)
{
	if (!gpio_is_ready_dt(&display_cs)) {
		return -ENODEV;
	}
	int err = gpio_pin_configure_dt(&display_cs, GPIO_OUTPUT_INACTIVE);

	if (err != 0) {
		return err;
	}
	k_thread_create(&sd_thread, sd_stack, K_THREAD_STACK_SIZEOF(sd_stack),
			sd_worker, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	return 0;
}
