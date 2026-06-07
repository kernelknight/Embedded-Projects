// SPDX-License-Identifier: GPL-2.0
/*
 * HD44780 character LCD driver with demo, DB-25 parallel-port, and USB bridge
 * backends.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include "hd44780_lcd.h"

#define DRIVER_NAME "hd44780_lcd"
#define MAX_LCDS 8
#define USB_REQ_WRITE_NIBBLE 0x01

struct hd44780_bus_ops {
	int (*write4)(void *ctx, u8 nibble, bool rs);
	void (*destroy)(void *ctx);
};

struct hd44780_lcd {
	struct miscdevice miscdev;
	struct mutex lock;
	const struct hd44780_bus_ops *ops;
	void *ctx;
	char name[32];
	u8 column;
	u8 row;
};

struct parport_ctx {
	unsigned long base;
	u8 rs_bit;
	u8 e_bit;
};

struct usb_lcd_ctx {
	struct usb_device *udev;
};

static DEFINE_MUTEX(registry_lock);
static struct hd44780_lcd *registry[MAX_LCDS];

static bool enable_demo = true;
module_param(enable_demo, bool, 0444);
MODULE_PARM_DESC(enable_demo, "Create a no-hardware demo LCD device");

static bool enable_parport;
module_param(enable_parport, bool, 0444);
MODULE_PARM_DESC(enable_parport, "Create a DB-25/LPT parallel-port LCD device");

static ulong parport_base = 0x378;
module_param(parport_base, ulong, 0444);
MODULE_PARM_DESC(parport_base, "Legacy parallel-port base I/O address");

static uint parport_rs_bit = 4;
module_param(parport_rs_bit, uint, 0444);
MODULE_PARM_DESC(parport_rs_bit, "Parallel-port data bit used for LCD RS");

static uint parport_e_bit = 5;
module_param(parport_e_bit, uint, 0444);
MODULE_PARM_DESC(parport_e_bit, "Parallel-port data bit used for LCD E");

static ushort usb_vid;
module_param(usb_vid, ushort, 0444);
MODULE_PARM_DESC(usb_vid, "USB bridge vendor ID");

static ushort usb_pid;
module_param(usb_pid, ushort, 0444);
MODULE_PARM_DESC(usb_pid, "USB bridge product ID");

static void hd44780_delay_command(u8 byte)
{
	if (byte == HD44780_CMD_CLEAR || byte == HD44780_CMD_HOME)
		msleep(2);
	else
		usleep_range(40, 80);
}

static int hd44780_write4(struct hd44780_lcd *lcd, u8 nibble, bool rs)
{
	return lcd->ops->write4(lcd->ctx, nibble & 0x0f, rs);
}

static int hd44780_write8(struct hd44780_lcd *lcd, u8 byte, bool rs)
{
	int ret;

	ret = hd44780_write4(lcd, byte >> 4, rs);
	if (ret)
		return ret;

	ret = hd44780_write4(lcd, byte, rs);
	if (ret)
		return ret;

	if (!rs)
		hd44780_delay_command(byte);
	else
		usleep_range(40, 80);

	return 0;
}

static int hd44780_command(struct hd44780_lcd *lcd, u8 command)
{
	return hd44780_write8(lcd, command, false);
}

static int hd44780_data(struct hd44780_lcd *lcd, u8 data)
{
	return hd44780_write8(lcd, data, true);
}

static int hd44780_goto_line(struct hd44780_lcd *lcd, u8 row)
{
	lcd->row = row ? 1 : 0;
	lcd->column = 0;
	return hd44780_command(lcd, row ? HD44780_CMD_LINE2 : HD44780_CMD_LINE1);
}

static int hd44780_hw_init(struct hd44780_lcd *lcd)
{
	int ret;

	msleep(20);
	ret = hd44780_write4(lcd, 0x03, false);
	if (ret)
		return ret;
	msleep(5);

	ret = hd44780_write4(lcd, 0x03, false);
	if (ret)
		return ret;
	usleep_range(150, 250);

	ret = hd44780_write4(lcd, 0x03, false);
	if (ret)
		return ret;
	usleep_range(150, 250);

	ret = hd44780_write4(lcd, 0x02, false);
	if (ret)
		return ret;
	usleep_range(150, 250);

	ret = hd44780_command(lcd, HD44780_CMD_FUNCTION_4BIT_2LINE);
	if (ret)
		return ret;
	ret = hd44780_command(lcd, HD44780_CMD_DISPLAY_ON);
	if (ret)
		return ret;
	ret = hd44780_command(lcd, HD44780_CMD_ENTRY_MODE);
	if (ret)
		return ret;
	ret = hd44780_command(lcd, HD44780_CMD_CLEAR);
	if (ret)
		return ret;

	lcd->row = 0;
	lcd->column = 0;
	return 0;
}

static struct hd44780_lcd *file_to_lcd(struct file *file)
{
	struct miscdevice *misc = file->private_data;

	return container_of(misc, struct hd44780_lcd, miscdev);
}

static ssize_t hd44780_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct hd44780_lcd *lcd = file_to_lcd(file);
	char local[64];
	size_t done = 0;
	int ret = 0;

	mutex_lock(&lcd->lock);
	while (done < count) {
		size_t chunk = min(sizeof(local), count - done);
		size_t i;

		if (copy_from_user(local, buf + done, chunk)) {
			ret = -EFAULT;
			break;
		}

		for (i = 0; i < chunk; i++) {
			char ch = local[i];

			if (ch == '\f') {
				ret = hd44780_command(lcd, HD44780_CMD_CLEAR);
				lcd->row = 0;
				lcd->column = 0;
			} else if (ch == '\n') {
				ret = hd44780_goto_line(lcd, 1);
			} else {
				ret = hd44780_data(lcd, ch);
				lcd->column++;
				if (lcd->column >= 16)
					ret = hd44780_goto_line(lcd, lcd->row ^ 1);
			}

			if (ret)
				break;
		}

		if (ret)
			break;
		done += chunk;
	}
	mutex_unlock(&lcd->lock);

	if (ret)
		return ret;
	return count;
}

static long hd44780_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hd44780_lcd *lcd = file_to_lcd(file);
	int ret = 0;

	if (_IOC_TYPE(cmd) != HD44780_IOC_MAGIC)
		return -ENOTTY;

	mutex_lock(&lcd->lock);
	switch (cmd) {
	case HD44780_IOC_CLEAR:
		ret = hd44780_command(lcd, HD44780_CMD_CLEAR);
		lcd->row = 0;
		lcd->column = 0;
		break;
	case HD44780_IOC_HOME:
		ret = hd44780_command(lcd, HD44780_CMD_HOME);
		lcd->row = 0;
		lcd->column = 0;
		break;
	case HD44780_IOC_DISPLAY_ON:
		ret = hd44780_command(lcd, HD44780_CMD_DISPLAY_ON);
		break;
	case HD44780_IOC_DISPLAY_OFF:
		ret = hd44780_command(lcd, HD44780_CMD_DISPLAY_OFF);
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&lcd->lock);

	return ret;
}

static const struct file_operations hd44780_fops = {
	.owner = THIS_MODULE,
	.write = hd44780_write,
	.unlocked_ioctl = hd44780_ioctl,
	.llseek = no_llseek,
};

static int hd44780_register_lcd(const char *name,
				const struct hd44780_bus_ops *ops,
				void *ctx, struct hd44780_lcd **out)
{
	struct hd44780_lcd *lcd;
	int slot;
	int ret;

	lcd = kzalloc(sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	mutex_init(&lcd->lock);
	lcd->ops = ops;
	lcd->ctx = ctx;
	strscpy(lcd->name, name, sizeof(lcd->name));
	lcd->miscdev.minor = MISC_DYNAMIC_MINOR;
	lcd->miscdev.name = lcd->name;
	lcd->miscdev.fops = &hd44780_fops;

	mutex_lock(&registry_lock);
	for (slot = 0; slot < MAX_LCDS; slot++) {
		if (!registry[slot])
			break;
	}
	if (slot == MAX_LCDS) {
		mutex_unlock(&registry_lock);
		kfree(lcd);
		return -ENOSPC;
	}
	registry[slot] = lcd;
	mutex_unlock(&registry_lock);

	ret = misc_register(&lcd->miscdev);
	if (ret)
		goto err_registry;

	ret = hd44780_hw_init(lcd);
	if (ret)
		goto err_misc;

	pr_info(DRIVER_NAME ": registered /dev/%s\n", lcd->name);
	if (out)
		*out = lcd;
	return 0;

err_misc:
	misc_deregister(&lcd->miscdev);
err_registry:
	mutex_lock(&registry_lock);
	registry[slot] = NULL;
	mutex_unlock(&registry_lock);
	kfree(lcd);
	return ret;
}

static void hd44780_unregister_lcd(struct hd44780_lcd *lcd)
{
	int slot;

	if (!lcd)
		return;

	misc_deregister(&lcd->miscdev);

	mutex_lock(&registry_lock);
	for (slot = 0; slot < MAX_LCDS; slot++) {
		if (registry[slot] == lcd) {
			registry[slot] = NULL;
			break;
		}
	}
	mutex_unlock(&registry_lock);

	if (lcd->ops->destroy)
		lcd->ops->destroy(lcd->ctx);
	kfree(lcd);
}

static int demo_write4(void *ctx, u8 nibble, bool rs)
{
	const char *kind = rs ? "data" : "cmd";

	pr_debug(DRIVER_NAME ": demo %s nibble=0x%x\n", kind, nibble);
	return 0;
}

static const struct hd44780_bus_ops demo_ops = {
	.write4 = demo_write4,
};

static struct hd44780_lcd *demo_lcd;

static int parport_write4(void *ctx, u8 nibble, bool rs)
{
	struct parport_ctx *par = ctx;
	u8 value = nibble & 0x0f;

	if (rs)
		value |= BIT(par->rs_bit);

	outb(value, par->base);
	udelay(1);
	outb(value | BIT(par->e_bit), par->base);
	udelay(1);
	outb(value, par->base);
	udelay(40);

	return 0;
}

static void parport_destroy(void *ctx)
{
	struct parport_ctx *par = ctx;

	release_region(par->base, 1);
	kfree(par);
}

static const struct hd44780_bus_ops parport_ops = {
	.write4 = parport_write4,
	.destroy = parport_destroy,
};

static struct hd44780_lcd *parport_lcd;

static int parport_create(void)
{
	struct parport_ctx *par;
	int ret;

	if (parport_rs_bit > 7 || parport_e_bit > 7 ||
	    parport_rs_bit == parport_e_bit)
		return -EINVAL;

	if (!request_region(parport_base, 1, DRIVER_NAME))
		return -EBUSY;

	par = kzalloc(sizeof(*par), GFP_KERNEL);
	if (!par) {
		release_region(parport_base, 1);
		return -ENOMEM;
	}

	par->base = parport_base;
	par->rs_bit = parport_rs_bit;
	par->e_bit = parport_e_bit;

	ret = hd44780_register_lcd("hd44780_lpt0", &parport_ops, par,
				   &parport_lcd);
	if (ret) {
		release_region(parport_base, 1);
		kfree(par);
	}

	return ret;
}

static int usb_bridge_write4(void *ctx, u8 nibble, bool rs)
{
	struct usb_lcd_ctx *usbctx = ctx;
	u16 value = nibble | (rs ? BIT(8) : 0);
	int ret;

	ret = usb_control_msg(usbctx->udev,
			      usb_sndctrlpipe(usbctx->udev, 0),
			      USB_REQ_WRITE_NIBBLE,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			      value, 0, NULL, 0, 1000);
	return ret < 0 ? ret : 0;
}

static void usb_bridge_destroy(void *ctx)
{
	struct usb_lcd_ctx *usbctx = ctx;

	usb_put_dev(usbctx->udev);
	kfree(usbctx);
}

static const struct hd44780_bus_ops usb_bridge_ops = {
	.write4 = usb_bridge_write4,
	.destroy = usb_bridge_destroy,
};

static int usb_probe(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_lcd_ctx *usbctx;
	struct hd44780_lcd *lcd;
	char name[32];
	int ret;

	if (!usb_vid || !usb_pid)
		return -ENODEV;

	usbctx = kzalloc(sizeof(*usbctx), GFP_KERNEL);
	if (!usbctx)
		return -ENOMEM;

	usbctx->udev = usb_get_dev(udev);
	snprintf(name, sizeof(name), "hd44780_usb%d",
		 interface->minor >= 0 ? interface->minor : 0);

	ret = hd44780_register_lcd(name, &usb_bridge_ops, usbctx, &lcd);
	if (ret) {
		usb_put_dev(udev);
		kfree(usbctx);
		return ret;
	}

	usb_set_intfdata(interface, lcd);
	return 0;
}

static void usb_disconnect(struct usb_interface *interface)
{
	struct hd44780_lcd *lcd = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);
	hd44780_unregister_lcd(lcd);
}

static struct usb_device_id usb_ids[] = {
	{ .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
			 USB_DEVICE_ID_MATCH_PRODUCT },
	{ }
};
MODULE_DEVICE_TABLE(usb, usb_ids);

static struct usb_driver hd44780_usb_driver = {
	.name = DRIVER_NAME "_usb",
	.probe = usb_probe,
	.disconnect = usb_disconnect,
	.id_table = usb_ids,
};

static int __init hd44780_init(void)
{
	int ret;

	if (usb_vid && usb_pid) {
		usb_ids[0].idVendor = usb_vid;
		usb_ids[0].idProduct = usb_pid;
	}

	if (enable_demo) {
		ret = hd44780_register_lcd("hd44780_demo0", &demo_ops, NULL,
					   &demo_lcd);
		if (ret)
			return ret;
	}

	if (enable_parport) {
		ret = parport_create();
		if (ret)
			goto err_demo;
	}

	ret = usb_register(&hd44780_usb_driver);
	if (ret)
		goto err_parport;

	return 0;

err_parport:
	hd44780_unregister_lcd(parport_lcd);
err_demo:
	hd44780_unregister_lcd(demo_lcd);
	return ret;
}

static void __exit hd44780_exit(void)
{
	usb_deregister(&hd44780_usb_driver);
	hd44780_unregister_lcd(parport_lcd);
	hd44780_unregister_lcd(demo_lcd);
}

module_init(hd44780_init);
module_exit(hd44780_exit);

MODULE_AUTHOR("Kernel Knight");
MODULE_DESCRIPTION("HD44780 character LCD driver for DB-25 and USB bridge backends");
MODULE_LICENSE("GPL");
