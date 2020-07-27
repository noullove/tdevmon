#pragma once

#include "HashTable.h"
#include "Hook.h"
#include "dm_lnx_Protocol.h"

typedef enum DeviceState   DeviceState;
typedef struct DeviceClass DeviceClass;
typedef struct Device      Device;

//..............................................................................

struct DeviceClass
{
	struct class* m_class;
	int m_majorId;
	struct file_operations m_fops;
};

int
DeviceClass_register(DeviceClass* self);

void
DeviceClass_unregister(DeviceClass* self);

char*
DeviceClass_devnode(
	struct device* device,
	devnode_mode_t* mode
	);

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

extern DeviceClass g_deviceClass;

//..............................................................................

enum DeviceState
{
	DeviceState_Normal,
	DeviceState_Stopping,
	DeviceState_Stopped,
	DeviceState_Zombie,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct Device
{
	struct device* m_device;
	dev_t m_devId;

	struct mutex m_lock;
	DeviceState m_state;
	HashTable m_hookMap;
	struct list_head m_hookList;
	size_t m_hookCount;
	size_t m_connectionCount;
	size_t m_fileCount;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

int
Device_construct(Device* self);

void
Device_destruct(Device* self);

int
Device_addHook(
	Device* self,
	Hook* hook,
	Hook** prevHook
	);

void
Device_removeHook(
	Device* self,
	Hook* hook
	);

Hook*
Device_findHookAddRef(
	Device* self,
	const struct file_operations* fops
	);

int
Device_fop_open(
	struct inode* inodep,
	struct file* filp
	);

int
Device_fop_release(
	struct inode* inodep,
	struct file *filp
	);

ssize_t
Device_fop_read(
	struct file* filp,
	char __user* buffer_u,
	size_t size,
	loff_t* offset
	);

unsigned int
Device_fop_poll(
	struct file* filp,
	struct poll_table_struct* table
	);

long
Device_fop_ioctl(
	struct file* filp,
	unsigned int cmd,
	unsigned long arg
	);

int
Device_p_getVersion(
	Device* self,
	uint32_t __user* version_u
	);

int
Device_p_getDescription(
	Device* self,
	dm_String __user* description_u
	);

int
Device_p_getBuildTime(
	Device* self,
	dm_String __user* buildTime_u
	);

int
Device_p_isConnected(
	Device* self,
	struct file* filp,
	int __user* isConnected_u
	);

int
Device_p_getHookInfoList(
	Device* self,
	dm_List __user* list_u
	);

int
Device_p_getTargetHookInfo(
	Device* self,
	struct file* filp,
	dm_HookInfo __user* hookInfo_u
	);

int
Device_p_connect(
	Device* self,
	struct file* filp,
	const dm_String __user* fileName_u
	);

int
Device_p_connect_v0302xx(
	Device* self,
	struct file* filp,
	const dm_ConnectParams_v0302xx __user* params_u
	);

int
Device_p_connectImpl(
	Device* self,
	Connection** connection,
	struct file* filp,
	const char* fileName
	);

void
Device_p_disconnect(
	Device* self,
	struct file* filp
	);

int
Device_p_connectionIoctl(
	Device* self,
	struct file* filp,
	unsigned int code,
	unsigned long arg
	);

int
Device_p_unhook(
	Device* self,
	const dm_String __user* fileName_u
	);

int
Device_p_stop(
	Device* self,
	bool isUserCall
	);

static
inline
int
Device_stop(Device* self)
{
	return Device_p_stop(self, false);
}

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

extern Device g_device;
extern umode_t g_devicePermissions;

//..............................................................................
