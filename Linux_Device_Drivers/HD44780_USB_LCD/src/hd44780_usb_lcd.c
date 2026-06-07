// SPDX-License-Identifier: GPL-2.0
/*
 * USB bridge driver for HD44780-compatible character LCD modules.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include "hd44780_usb_lcd.h"

#define DRIVER_NAME "hd44780_usb_lcd"
#define USB_REQ_LCD_NIBBLE 0x44
#define MAX_NAME 32

struct hd44780_usb_lcd {
	struct usb_device *udev;
	struct usb_interface *interface;
	struct miscdevice miscdev;
	struct mutex lock;
	char name[MAX_NAME];
	u8 row;
	u8 column;
};

static ushort vid;
module_param(vid, ushort, 0444);
MODULE_PARM_DESC(vid, "USB bridge vendor ID");

static ushort pid;
module_param(pid, ushort, 0444);
MODULE_PARM_DESC(pid, "USB bridge product ID");

static int timeout_ms = 1000;
module_param(timeout_ms, int, 0644);
MODULE_PARM_DESC(timeout_ms, "USB control-transfer timeout in milliseconds");

static int lcd_index;

static void hd44780_delay_command(u8 command)
{
	if (command == HD44780_CMD_CLEAR || command == HD44780_CMD_HOME)
		msleep(2);
	else
		usleep_range(40, 80);
}

static int hd44780_usb_write4(struct hd44780_usb_lcd *lcd, u8 nibble, bool rs)
{
	u16 value = (nibble & 0x0f) | (rs ? BIT(8) : 0);
	int ret;

	ret = usb_control_msg(lcd->udev,
			      usb_sndctrlpipe(lcd->udev, 0),
			      USB_REQ_LCD_NIBBLE,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      value, 0, NULL, 0, timeout_ms);
	return ret < 0 ? ret : 0;
}

static int hd44780_usb_write8(struct hd44780_usb_lcd *lcd, u8 byte, bool rs)
{
	int ret;

	ret = hd44780_usb_write4(lcd, byte >> 4, rs);
	if (ret)
		return ret;

	ret = hd44780_usb_write4(lcd, byte, rs);
	if (ret)
		return ret;

	if (rs)
		usleep_range(40, 80);
	else
		hd44780_delay_command(byte);

	return 0;
}

static int hd44780_usb_command(struct hd44780_usb_lcd *lcd, u8 command)
{
	return hd44780_usb_write8(lcd, command, false);
}

static int hd44780_usb_data(struct hd44780_usb_lcd *lcd, u8 data)
{
	return hd44780_usb_write8(lcd, data, true);
}

static int hd44780_usb_line(struct hd44780_usb_lcd *lcd, u8 row)
{
	lcd->row = row ? 1 : 0;
	lcd->column = 0;
	return hd44780_usb_command(lcd, row ? HD44780_CMD_LINE2 : HD44780_CMD_LINE1);
}

static int hd44780_usb_init_lcd(struct hd44780_usb_lcd *lcd)
{
	int ret;

	msleep(20);
	ret = hd44780_usb_write4(lcd, 0x03, false);
	if (ret)
		return ret;
	msleep(5);

	ret = hd44780_usb_write4(lcd, 0x03, false);
	if (ret)
		return ret;
	usleep_range(150, 250);

	ret = hd44780_usb_write4(lcd, 0x03, false);
	if (ret)
		return ret;
	usleep_range(150, 250);

	ret = hd44780_usb_write4(lcd, 0x02, false);
	if (ret)
		return ret;

	ret = hd44780_usb_command(lcd, HD44780_CMD_FUNCTION_4BIT_2LINE);
	if (ret)
		return ret;
	ret = hd44780_usb_command(lcd, HD44780_CMD_DISPLAY_ON);
	if (ret)
		return ret;
	ret = hd44780_usb_command(lcd, HD44780_CMD_ENTRY_MODE);
	if (ret)
		return ret;
	ret = hd44780_usb_command(lcd, HD44780_CMD_CLEAR);
	if (ret)
		return ret;

	lcd->row = 0;
	lcd->column = 0;
	return 0;
}

static struct hd44780_usb_lcd *file_to_lcd(struct file *file)
{
	struct miscdevice *misc = file->private_data;

	return container_of(misc, struct hd44780_usb_lcd, miscdev);
}

static ssize_t hd44780_usb_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct hd44780_usb_lcd *lcd = file_to_lcd(file);
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
				ret = hd44780_usb_command(lcd, HD44780_CMD_CLEAR);
				lcd->row = 0;
				lcd->column = 0;
			} else if (ch == '\n') {
				ret = hd44780_usb_line(lcd, 1);
			} else {
				ret = hd44780_usb_data(lcd, ch);
				lcd->column++;
				if (lcd->column >= 16)
					ret = hd44780_usb_line(lcd, lcd->row ^ 1);
			}

			if (ret)
				break;
		}

		if (ret)
			break;
		done += chunk;
	}
	mutex_unlock(&lcd->lock);

	return ret ? ret : count;
}

static long hd44780_usb_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	struct hd44780_usb_lcd *lcd = file_to_lcd(file);
	int ret;

	if (_IOC_TYPE(cmd) != HD44780_USB_IOC_MAGIC)
		return -ENOTTY;

	mutex_lock(&lcd->lock);
	switch (cmd) {
	case HD44780_USB_IOC_CLEAR:
		ret = hd44780_usb_command(lcd, HD44780_CMD_CLEAR);
		lcd->row = 0;
		lcd->column = 0;
		break;
	case HD44780_USB_IOC_HOME:
		ret = hd44780_usb_command(lcd, HD44780_CMD_HOME);
		lcd->row = 0;
		lcd->column = 0;
		break;
	case HD44780_USB_IOC_DISPLAY_ON:
		ret = hd44780_usb_command(lcd, HD44780_CMD_DISPLAY_ON);
		break;
	case HD44780_USB_IOC_DISPLAY_OFF:
		ret = hd44780_usb_command(lcd, HD44780_CMD_DISPLAY_OFF);
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&lcd->lock);

	return ret;
}

static const struct file_operations hd44780_usb_fops = {
	.owner = THIS_MODULE,
	.write = hd44780_usb_write,
	.unlocked_ioctl = hd44780_usb_ioctl,
	.llseek = no_llseek,
};

static int hd44780_usb_probe(struct usb_interface *interface,
			     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct hd44780_usb_lcd *lcd;
	int ret;

	if (!vid || !pid)
		return -ENODEV;

	lcd = kzalloc(sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	mutex_init(&lcd->lock);
	lcd->udev = usb_get_dev(udev);
	lcd->interface = interface;
	snprintf(lcd->name, sizeof(lcd->name), "hd44780_usb%d", lcd_index++);

	lcd->miscdev.minor = MISC_DYNAMIC_MINOR;
	lcd->miscdev.name = lcd->name;
	lcd->miscdev.fops = &hd44780_usb_fops;

	ret = misc_register(&lcd->miscdev);
	if (ret)
		goto err_put_usb;

	ret = hd44780_usb_init_lcd(lcd);
	if (ret)
		goto err_misc;

	usb_set_intfdata(interface, lcd);
	dev_info(&interface->dev, "registered /dev/%s\n", lcd->name);
	return 0;

err_misc:
	misc_deregister(&lcd->miscdev);
err_put_usb:
	usb_put_dev(lcd->udev);
	kfree(lcd);
	return ret;
}

static void hd44780_usb_disconnect(struct usb_interface *interface)
{
	struct hd44780_usb_lcd *lcd = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);
	if (!lcd)
		return;

	misc_deregister(&lcd->miscdev);
	usb_put_dev(lcd->udev);
	kfree(lcd);
	dev_info(&interface->dev, "USB LCD disconnected\n");
}

static struct usb_device_id hd44780_usb_ids[] = {
	{ .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
			 USB_DEVICE_ID_MATCH_PRODUCT },
	{ }
};
MODULE_DEVICE_TABLE(usb, hd44780_usb_ids);

static struct usb_driver hd44780_usb_driver = {
	.name = DRIVER_NAME,
	.probe = hd44780_usb_probe,
	.disconnect = hd44780_usb_disconnect,
	.id_table = hd44780_usb_ids,
};

static int __init hd44780_usb_init(void)
{
	if (!vid || !pid) {
		pr_info(DRIVER_NAME ": load with vid=0xNNNN pid=0xNNNN\n");
		return -EINVAL;
	}

	hd44780_usb_ids[0].idVendor = vid;
	hd44780_usb_ids[0].idProduct = pid;
	return usb_register(&hd44780_usb_driver);
}

static void __exit hd44780_usb_exit(void)
{
	usb_deregister(&hd44780_usb_driver);
}

module_init(hd44780_usb_init);
module_exit(hd44780_usb_exit);

MODULE_AUTHOR("Kernel Knight");
MODULE_DESCRIPTION("USB bridge character driver for HD44780 LCD modules");
MODULE_LICENSE("GPL");
