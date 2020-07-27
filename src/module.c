#include "pch.h"
#include "Device.h"
#include "version.h"
#include "dm_lnx_Protocol.h"

//..............................................................................

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vladimir Gladkov");
MODULE_DESCRIPTION("Tibbo Device Monitor for Linux");
MODULE_VERSION(VERSION_STRING);

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

static uint permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

module_param(permissions, uint, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(permissions, "Default permissions for /dev/" DM_DEVICE_NAME);

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

static
int
__init
tdevmon_init(void)
{
	int result;

	printk(KERN_INFO "tdevmon: kernel module v" VERSION_STRING " initializing\n");

	if (permissions != g_devicePermissions)
	{
		printk(KERN_WARNING "tdevmon: overriding default permissions for /dev/" DM_DEVICE_NAME ": 0%o\n", permissions);
		g_devicePermissions = permissions;
	}

	result = DeviceClass_register(&g_deviceClass);
	if (result != 0)
	{
		printk(KERN_ERR "tdevmon: failed to register device class " DM_DEVICE_CLASS_NAME ": %d\n", result);
		return result;
	}

	result = Device_construct(&g_device);
	if (result != 0)
	{
		printk(KERN_ERR "tdevmon: failed to create device /dev/" DM_DEVICE_NAME ": %d\n", result);
		DeviceClass_unregister(&g_deviceClass);
		return result;
	}

	return 0;
}

static
void
__exit
tdevmon_exit(void)
{
	int result;

	result = Device_stop(&g_device);
	if (result != 0)
	{
		printk(KERN_ERR "tdevmon: cannot safely unload now (driver remains in memory)\n");
		try_module_get(THIS_MODULE);
		return;
	}

	Device_destruct(&g_device);
	DeviceClass_unregister(&g_deviceClass);
	printk(KERN_INFO "tdevmon: uninititialized\n");
}

//..............................................................................

module_init(tdevmon_init);
module_exit(tdevmon_exit);

//..............................................................................

