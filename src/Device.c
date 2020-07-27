#include "pch.h"
#include "Device.h"
#include "Hook.h"
#include "Connection.h"
#include "version.h"
#include "lkmUtils.h"

DeviceClass g_deviceClass = { 0 };
Device g_device = { 0 };
umode_t g_devicePermissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

//..............................................................................

int
DeviceClass_register(DeviceClass* self)
{
	self->m_fops.owner = THIS_MODULE;
	self->m_fops.open = Device_fop_open;
	self->m_fops.release = Device_fop_release;
	self->m_fops.read = Device_fop_read;
	self->m_fops.poll = Device_fop_poll;
	self->m_fops.unlocked_ioctl = Device_fop_ioctl;
	self->m_fops.compat_ioctl = Device_fop_ioctl;

	self->m_majorId = register_chrdev(0, DM_DEVICE_NAME, &self->m_fops);
	if (self->m_majorId < 0)
		return self->m_majorId;

	self->m_class = class_create(THIS_MODULE, DM_DEVICE_CLASS_NAME);
	if (IS_ERR(self->m_class))
	{
		unregister_chrdev(self->m_majorId, DM_DEVICE_NAME);
		return PTR_ERR(self->m_class);
	}

	self->m_class->devnode = DeviceClass_devnode;
	return 0;
}

void
DeviceClass_unregister(DeviceClass* self)
{
	class_destroy(self->m_class);
	unregister_chrdev(self->m_majorId, DM_DEVICE_NAME);
}

char*
DeviceClass_devnode(
	struct device* device,
	devnode_mode_t* mode
	)
{
	if (mode)
		*mode = g_devicePermissions;

	return NULL;
}
//..............................................................................

int
Device_construct(Device* self)
{
	self->m_devId = MKDEV(g_deviceClass.m_majorId, 0);

	self->m_device = device_create(
		g_deviceClass.m_class,
		NULL,
		self->m_devId,
		NULL,
		DM_DEVICE_NAME
		);

	if (IS_ERR(self->m_device))
		return PTR_ERR(self->m_device);

	mutex_init(&self->m_lock);
	INIT_LIST_HEAD(&self->m_hookList);
	self->m_hookCount = 0;
	HashTable_construct(&self->m_hookMap, HashTableKeyType_Pointer, GFP_KERNEL);

	return 0;
}

void
Device_destruct(Device* self)
{
	HashTable_destruct(&self->m_hookMap);
	device_destroy(g_deviceClass.m_class, self->m_devId);
	mutex_destroy(&self->m_lock);
}

int
Device_addHook(
	Device* self,
	Hook* hook,
	Hook** prevHook
	)
{
	HashTableEntry* entry;

	mutex_lock(&self->m_lock);
	if (self->m_state != DeviceState_Normal)
	{
		mutex_unlock(&self->m_lock);
		return -EBADFD;
	}

	entry = HashTable_visit(&self->m_hookMap, hook->m_fops);
	if (!entry)
	{
		mutex_unlock(&self->m_lock);
		return -ENOMEM;
	}
	else if (entry->m_value)
	{
		hook = entry->m_value;
		Hook_addRef(hook);
		mutex_unlock(&self->m_lock);

		*prevHook = hook;
		return EEXIST; // positive errno, not an error
	}

	hook->m_mapEntry = entry;
	entry->m_value = hook;
	Hook_addRef(hook);
	list_add_tail(&hook->m_link, &self->m_hookList);
	self->m_hookCount++;
	mutex_unlock(&self->m_lock);

	*prevHook = NULL;
	return 0;
}

void
Device_removeHook(
	Device* self,
	Hook* hook
	)
{
	mutex_lock(&self->m_lock);
	HashTable_remove(&self->m_hookMap, hook->m_mapEntry);
	list_del(&hook->m_link);
	self->m_hookCount--;
	mutex_unlock(&self->m_lock);

	Hook_release(hook);
}

Hook*
Device_findHookAddRef(
	Device* self,
	const struct file_operations* fops
	)
{
	Hook* hook;

	mutex_lock(&self->m_lock);
	hook = HashTable_findValue(&self->m_hookMap, fops);
	if (hook)
		Hook_addRef(hook);

	mutex_unlock(&self->m_lock);
	return hook;
}

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

int
Device_fop_open(
	struct inode* inodep,
	struct file* filp
	)
{
	Device* self = &g_device;

	ASSERT(filp->private_data == NULL);

	mutex_lock(&self->m_lock);
	if (self->m_state != DeviceState_Normal)
	{
		mutex_unlock(&self->m_lock);
		return -EBADFD;
	}

	self->m_fileCount++;
	mutex_unlock(&self->m_lock);
	return 0;
}

int
Device_fop_release(
	struct inode* inodep,
	struct file* filp
	)
{
	Device* self = &g_device;

	Device_p_disconnect(self, filp);

	mutex_lock(&self->m_lock);
	self->m_fileCount--;
	mutex_unlock(&self->m_lock);
	return 0;
}

ssize_t
Device_fop_read(
	struct file* filp,
	char __user* buffer_u,
	size_t size,
	loff_t* offset
	)
{
	Device* self = &g_device;
	ssize_t result;
	Connection* connection;

	if (size < sizeof(dm_NotifyHdr))
		return -EINVAL;

	mutex_lock(&self->m_lock);
	if (!filp->private_data) // not connected
	{
		mutex_unlock(&self->m_lock);
		return -ENOTCONN;
	}

	connection = filp->private_data;
	Connection_addRef(connection);
	mutex_unlock(&self->m_lock);

	result = Connection_read(connection, buffer_u, size);

	Connection_release(connection);
	return result;
}

unsigned int
Device_fop_poll(
	struct file* filp,
	struct poll_table_struct* table
	)
{
	Device* self = &g_device;
	unsigned int result = 0;
	Connection* connection;

	mutex_lock(&self->m_lock);
	if (!filp->private_data) // not connected
	{
		mutex_unlock(&self->m_lock);
		return POLLIN | POLLERR;
	}

	connection = filp->private_data;
	Connection_addRef(connection);
	mutex_unlock(&self->m_lock);

	poll_wait(filp, &connection->m_notificationWaitQueue, table);

	mutex_lock(&connection->m_lock);

	if (connection->m_pendingNotifyCount)
		result = POLLIN | POLLRDNORM;

	mutex_unlock(&connection->m_lock);

	Connection_release(connection);
	return result;
}

long
Device_fop_ioctl(
	struct file* filp,
	unsigned int code,
	unsigned long arg
	)
{
	Device* self = &g_device;
	int result = 0;

	switch (code)
	{
	case DM_IOCTL_GET_VERSION:
		result = Device_p_getVersion(self, (uint32_t __user*) arg);
		break;

	case DM_IOCTL_GET_DESCRIPTION:
		result = Device_p_getDescription(self, (dm_String __user*) arg);
		break;

	case DM_IOCTL_GET_BUILD_TIME:
		result = Device_p_getBuildTime(self, (dm_String __user*) arg);
		break;

	case DM_IOCTL_GET_HOOK_INFO_LIST:
		result = Device_p_getHookInfoList(self, (dm_List __user*) arg);
		break;

	case DM_IOCTL_GET_TARGET_HOOK_INFO:
		result = Device_p_getTargetHookInfo(self, filp, (dm_HookInfo __user*) arg);
		break;

	case DM_IOCTL_IS_CONNECTED:
		result = Device_p_isConnected(self, filp, (int __user*) arg);
		break;

	case DM_IOCTL_CONNECT_V0302XX:
		result = Device_p_connect_v0302xx(self, filp, (const dm_ConnectParams_v0302xx __user*) arg);
		break;

	case DM_IOCTL_CONNECT:
		result = Device_p_connect(self, filp, (const dm_String __user*) arg);
		break;

	case DM_IOCTL_DISCONNECT:
		Device_p_disconnect(self, filp);
		break;

	case FIONREAD:
	case DM_IOCTL_IS_ENABLED:
	case DM_IOCTL_ENABLE:
	case DM_IOCTL_DISABLE:
	case DM_IOCTL_GET_PENDING_NOTIFY_SIZE_LIMIT:
	case DM_IOCTL_SET_PENDING_NOTIFY_SIZE_LIMIT:
	case DM_IOCTL_GET_READ_MODE:
	case DM_IOCTL_SET_READ_MODE:
	case DM_IOCTL_GET_FILE_NAME_FILTER:
	case DM_IOCTL_SET_FILE_NAME_FILTER:
	case DM_IOCTL_GET_IOCTL_DESC_TABLE:
	case DM_IOCTL_SET_IOCTL_DESC_TABLE:
		result = Device_p_connectionIoctl(self, filp, code, arg);
		break;

	case DM_IOCTL_UNHOOK:
		result = Device_p_unhook(self, (dm_String __user*) arg);
		break;

	case DM_IOCTL_STOP:
		result = Device_p_stop(self, true);
		break;

	default:
		return -EINVAL;
	}

	return result;
}

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

int
Device_p_getVersion(
	Device* self,
	uint32_t __user* version_u
	)
{
	int result;
	static uint32_t version = VERSION_FULL;

	result = copy_to_user(version_u, &version, sizeof(uint32_t));
	return result == 0 ? 0 : -EFAULT;
}

int
Device_p_getDescription(
	Device* self,
	dm_String __user* description_u
	)
{
	static char description[] = "Tibbo Device Monitor kernel module (" VERSION_CPU_STRING VERSION_DEBUG_SUFFIX ")";
	return copyStringToUser(description_u, description);
}

int
Device_p_getBuildTime(
	Device* self,
	dm_String __user* buildTime_u
	)
{
	static char buildTime[] = __DATE__ " " __TIME__;
	return copyStringToUser(buildTime_u, buildTime);
}

int
Device_p_getHookInfoList(
	Device* self,
	dm_List __user* list_u
	)
{
	int result;
	dm_List list;
	dm_HookInfo hookInfo;
	dm_HookInfo __user* hookInfo_u;
	char __user* moduleName_u;
	char __user* fileName_u;
	const char* moduleName;
	const char* fileName;
	struct list_head* link;
	Hook* hook;
	size_t moduleNameLength;
	size_t fileNameLength;
	size_t dataSize;
	size_t bufferSize;

	result = copy_from_user(&list, list_u, sizeof(dm_List));
	if (result != 0)
		return -EFAULT;

	if (list.m_bufferSize < sizeof(dm_List))
		return -EINVAL;

	mutex_lock(&self->m_lock);
	if (self->m_state != DeviceState_Normal)
	{
		mutex_unlock(&self->m_lock);
		return -EBADFD;
	}

	dataSize = 0;
	link = self->m_hookList.next;
	for (; link != &self->m_hookList; link = link->next)
	{
		hook = container_of(link, Hook, m_link);
		moduleName = hook->m_originalModule ? hook->m_originalModule->name : "";
		fileName = hook->m_originalPath ? hook->m_originalPath : "";
		moduleNameLength = strlen(moduleName);
		fileNameLength = strlen(fileName);
		dataSize += sizeof(dm_HookInfo) + moduleNameLength + fileNameLength + 2; // 2 null-terminators
	}

	list.m_elementCount = (uint32_t)self->m_hookCount;
	list.m_dataSize = (uint32_t)dataSize;

	bufferSize = sizeof(dm_List) + dataSize;

	if (list.m_bufferSize < bufferSize)
	{
		mutex_unlock(&self->m_lock);

		list.m_bufferSize = bufferSize;
		result = copy_to_user(list_u, &list, sizeof(dm_List));
		return result == 0 ? -ENOBUFS : -EFAULT;
	}

	result = copy_to_user(list_u, &list, sizeof(dm_List));
	if (result != 0)
	{
		mutex_unlock(&self->m_lock);
		return -EFAULT;
	}

	hookInfo_u = (dm_HookInfo*)(list_u + 1);

	link = self->m_hookList.next;
	for (; link != &self->m_hookList; link = link->next)
	{
		hook = container_of(link, Hook, m_link);

		moduleName = hook->m_originalModule ? hook->m_originalModule->name : "";
		fileName = hook->m_originalPath ? hook->m_originalPath : "";
		moduleNameLength = strlen(moduleName);
		fileNameLength = strlen(fileName);

		hookInfo.m_bufferSize = sizeof(dm_HookInfo) + moduleNameLength + fileNameLength + 2; // 2 null-terminators
		hookInfo.m_connectionCount = (uint32_t)hook->m_connectionCount;
		hookInfo.m_moduleNameLength = (uint32_t)moduleNameLength;
		hookInfo.m_fileNameLength = (uint32_t)fileNameLength;

		moduleName_u = (char*)(hookInfo_u + 1);
		fileName_u = moduleName_u + moduleNameLength + 1;

		result = copy_to_user(hookInfo_u, &hookInfo, sizeof(dm_HookInfo));

		if (result == 0)
			result = copy_to_user(moduleName_u, moduleName, moduleNameLength + 1);

		if (result == 0)
			result = copy_to_user(fileName_u, fileName, fileNameLength + 1);

		if (result != 0)
		{
			mutex_unlock(&self->m_lock);
			return -EFAULT;
		}

		hookInfo_u = (dm_HookInfo*)(fileName_u + fileNameLength + 1);
	}

	mutex_unlock(&self->m_lock);
	return 0;
}

int
Device_p_getTargetHookInfo(
	Device* self,
	struct file* fileObject,
	dm_HookInfo __user* hookInfo_u
	)
{
	int result;
	dm_HookInfo hookInfo;
	Hook* hook;
	char __user* moduleName_u;
	char __user* fileName_u;
	const char* moduleName;
	const char* fileName;
	size_t moduleNameLength;
	size_t fileNameLength;
	size_t bufferSize;

	result = copy_from_user(&hookInfo, hookInfo_u, sizeof(dm_HookInfo));
	if (result != 0)
		return -EFAULT;

	if (hookInfo.m_bufferSize < sizeof(dm_HookInfo))
		return -EINVAL;

	mutex_lock(&self->m_lock);
	if (!fileObject->private_data) // not connected
	{
		mutex_unlock(&self->m_lock);
		return -ENOTCONN;
	}

	if (self->m_state != DeviceState_Normal)
	{
		mutex_unlock(&self->m_lock);
		return -EBADFD;
	}

	hook = ((Connection*)fileObject->private_data)->m_hook;
	moduleName = hook->m_originalModule ? hook->m_originalModule->name : "";
	fileName = hook->m_originalPath ? hook->m_originalPath : "";
	moduleNameLength = strlen(moduleName);
	fileNameLength = strlen(fileName);
	bufferSize = sizeof(dm_HookInfo) + moduleNameLength + fileNameLength + 2; // 2 null-terminators

	hookInfo.m_connectionCount = (uint32_t)hook->m_connectionCount;
	hookInfo.m_moduleNameLength = (uint32_t)moduleNameLength;
	hookInfo.m_fileNameLength = (uint32_t)fileNameLength;

	if (hookInfo.m_bufferSize < bufferSize)
	{
		mutex_unlock(&self->m_lock);
		hookInfo.m_bufferSize = bufferSize;

		result = copy_to_user(hookInfo_u, &hookInfo, sizeof(dm_HookInfo));
		return result == 0 ? -ENOBUFS : -EFAULT;
	}

	moduleName_u = (char*)(hookInfo_u + 1);
	fileName_u = moduleName_u + moduleNameLength + 1;

	result = copy_to_user(hookInfo_u, &hookInfo, sizeof(dm_HookInfo));

	if (result == 0)
		result = copy_to_user(moduleName_u, moduleName, moduleNameLength + 1);

	if (result == 0)
		result = copy_to_user(fileName_u, fileName, fileNameLength + 1);

	mutex_unlock(&self->m_lock);
	return 0;
}

int
Device_p_isConnected(
	Device* self,
	struct file* filp,
	int __user* isConnected_u
	)
{
	int result;
	int isConnected = filp->private_data != NULL;

	result = copy_to_user(isConnected_u, &isConnected, sizeof(int));
	return result == 0 ? 0 : -EFAULT;
}

int
Device_p_connect(
	Device* self,
	struct file* filp,
	const dm_String __user* string_u
	)
{
	int result;
	char* fileName;

	fileName = copyStringFromUser(string_u);
	if (IS_ERR(fileName))
		return PTR_ERR(fileName);

	result = Device_p_connectImpl(self, NULL, filp, fileName);
	kfree(fileName);
	return result;
}

int
Device_p_connect_v0302xx(
	Device* self,
	struct file* filp,
	const dm_ConnectParams_v0302xx __user* params_u
	)
{
	int result;
	dm_ConnectParams_v0302xx params;
	const dm_IoctlDesc_v0302xx* ioctlDescArray_u;
	const char* fileName_u;
	char* fileName;
	Connection* connection;

	result = copy_from_user(&params, params_u, sizeof(dm_ConnectParams_v0302xx));
	if (result != 0)
		return -EFAULT;

	fileName = kmalloc(params.m_fileNameLength + 1, GFP_KERNEL);
	if (!fileName)
		return -ENOMEM;

	ioctlDescArray_u = (const dm_IoctlDesc_v0302xx*) (params_u + 1);
	fileName_u = (const char*) (ioctlDescArray_u + params.m_ioctlDescCount);

	result = copy_from_user(fileName, fileName_u, params.m_fileNameLength);
	if (result != 0)
	{
		kfree(fileName);
		return -EFAULT;
	}

	fileName[params.m_fileNameLength] = 0; // ensure zero-terminated

	result = Device_p_connectImpl(self, &connection, filp, fileName);

	kfree(fileName);

	if (result != 0)
		return result;

	result = Connection_setIoctlDescTable_v0302xx(connection, ioctlDescArray_u, params.m_ioctlDescCount);
	if (result != 0)
	{
		Connection_release(connection);
		Device_p_disconnect(self, filp);
		return result;
	}

	Connection_setPendingNotifySizeLimit(connection, params.m_pendingNotifySizeLimit);
	Connection_enable(connection);
	return 0;
}

int
Device_p_connectImpl(
	Device* self,
	Connection** resultConnection,
	struct file* filp,
	const char* fileName
	)
{
	int result;
	Hook* hook;
	Connection* connection;
	struct file* targetFilp;
	struct file_operations* fops;

	targetFilp = filp_open(fileName, O_RDWR | O_NONBLOCK | O_NOCTTY, 0);

	if (IS_ERR(targetFilp))
		return PTR_ERR(targetFilp);

	if (targetFilp->f_inode == filp->f_inode)
	{
		printk(KERN_WARNING "tdevmon: self-hooking attempt detected\n");
		filp_close(targetFilp, 0);
		return -EINVAL;
	}

	fops = (struct file_operations*) targetFilp->f_op; // const cast
	if (!fops)
	{
		filp_close(targetFilp, 0);
		return -EBADTYPE;
	}

	mutex_lock(&self->m_lock);
	if (self->m_state != DeviceState_Normal)
	{
		mutex_unlock(&self->m_lock);
		filp_close(targetFilp, 0);
		return -EBADFD;
	}

	if (filp->private_data) // already connected
	{
		mutex_unlock(&self->m_lock);
		filp_close(targetFilp, 0);
		return -EALREADY;
	}

	hook = HashTable_findValue(&self->m_hookMap, fops);
	if (hook)
	{
		Hook_addRef(hook);
		mutex_unlock(&self->m_lock);
	}
	else
	{
		mutex_unlock(&self->m_lock);

		result = Hook_create(
			&hook,
			fops,
			getOwnerModule(targetFilp),
			&targetFilp->f_path
			);

		if (result < 0) // may return +EEXIST
		{
			filp_close(targetFilp, 0);
			return result;
		}
	}

	result = Connection_create(
		&connection,
		hook,
		targetFilp,
		filp->f_flags
		);

	filp_close(targetFilp, 0); // close now regardless

	if (result != 0)
	{
		Hook_release(hook);
		return result;
	}

	mutex_lock(&self->m_lock);
	if (filp->private_data) // check again before assiging private_data
	{
		mutex_unlock(&self->m_lock);
		Hook_removeConnection(hook, connection);
		Hook_release(hook);
		Connection_release(connection);
		return -EBUSY;
	}

	filp->private_data = connection;
	self->m_connectionCount++;

	if (resultConnection)
	{
		Connection_addRef(connection);
		*resultConnection = connection;
	}

	mutex_unlock(&self->m_lock);
	Hook_release(hook);
	return 0;
}

int
Device_p_connectionIoctl(
	Device* self,
	struct file* filp,
	unsigned int code,
	unsigned long arg
	)
{
	int result = 0;
	Connection* connection;

	mutex_lock(&self->m_lock);
	if (!filp->private_data) // not connected
	{
		mutex_unlock(&self->m_lock);
		return -ENOTCONN;
	}

	connection = filp->private_data;
	Connection_addRef(connection);
	mutex_unlock(&self->m_lock);

	switch (code)
	{
	case FIONREAD:
		result = Connection_getIncomingDataSize(connection, (int __user*) arg);
		break;

	case DM_IOCTL_IS_ENABLED:
		result = Connection_isEnabled(connection, (int __user*) arg);
		break;

	case DM_IOCTL_ENABLE:
		Connection_enable(connection);
		break;

	case DM_IOCTL_DISABLE:
		Connection_disable(connection);
		break;

	case DM_IOCTL_GET_READ_MODE:
		result = Connection_getReadMode(connection, (int __user*) arg);
		break;

	case DM_IOCTL_SET_READ_MODE:
		result = Connection_setReadMode(connection, (dm_ReadMode)arg);
		break;

	case DM_IOCTL_GET_FILE_NAME_FILTER:
		result = Connection_getFileNameFilter(connection, (dm_String __user*) arg);
		break;

	case DM_IOCTL_SET_FILE_NAME_FILTER:
		result = Connection_setFileNameFilter(connection, (const dm_String __user*) arg);
		break;

	case DM_IOCTL_GET_IOCTL_DESC_TABLE:
		result = Connection_getIoctlDescTable(connection, (dm_List __user*) arg);
		break;

	case DM_IOCTL_SET_IOCTL_DESC_TABLE:
		result = Connection_setIoctlDescTable(connection, (const dm_List __user*) arg);
		break;

	case DM_IOCTL_GET_PENDING_NOTIFY_SIZE_LIMIT:
		result = Connection_getPendingNotifySizeLimit(connection, (uint32_t __user*) arg);
		break;

	case DM_IOCTL_SET_PENDING_NOTIFY_SIZE_LIMIT:
		Connection_setPendingNotifySizeLimit(connection, (uint32_t)arg);
		break;
	}

	Connection_release(connection);
	return result;
}

void
Device_p_disconnect(
	Device* self,
	struct file* filp
	)
{
	Connection* connection;

	mutex_lock(&self->m_lock);
	if (!filp->private_data) // not connected
	{
		mutex_unlock(&self->m_lock);
		return;
	}

	connection = filp->private_data;
	filp->private_data = NULL;
	self->m_connectionCount--;
	mutex_unlock(&self->m_lock);

	Connection_disconnect(connection);
	Connection_release(connection);
}

int
Device_p_unhook(
	Device* self,
	const dm_String __user* string_u
	)
{
	int result;
	char* fileName;
	Hook* hook;
	struct file* targetFilp;

	fileName = copyStringFromUser(string_u);
	if (IS_ERR(fileName))
		return PTR_ERR(fileName);

	targetFilp = filp_open(fileName, O_RDWR | O_NONBLOCK | O_NOCTTY, 0);
	kfree(fileName);

	if (IS_ERR(targetFilp))
		return PTR_ERR(targetFilp);

	mutex_lock(&self->m_lock);
	if (self->m_state != DeviceState_Normal)
	{
		mutex_unlock(&self->m_lock);
		filp_close(targetFilp, 0);
		return -EBADFD;
	}

	hook = HashTable_findValue(&self->m_hookMap, targetFilp->f_op);
	if (!hook)
	{
		mutex_unlock(&self->m_lock);
		filp_close(targetFilp, 0);
		return -EINVAL;
	}

	result = Hook_stop(hook);
	if (!result)
	{
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	Hook_addRef(hook);
	mutex_unlock(&self->m_lock);

	Hook_remove(hook);
	Hook_release(hook);
	return 0;
}

int
Device_p_stop(
	Device* self,
	bool isUserCall
	)
{
	bool result;
	struct list_head* link;
	Hook* hook;
	Hook* stoppedHookStackArray[16];
	Hook** stoppedHookArray = stoppedHookStackArray;
	size_t stoppedHookCount;
	size_t size;
	size_t i;

	mutex_lock(&self->m_lock);
	if (self->m_state == DeviceState_Stopped)
	{
		mutex_unlock(&self->m_lock);
		return 0;
	}

	if (self->m_state != DeviceState_Normal)
	{
		mutex_unlock(&self->m_lock);
		return -EBADFD;
	}

	self->m_state = DeviceState_Stopping;

	if (self->m_connectionCount)
	{
		printk(KERN_WARNING "tdevmon: stop failed: connections: %zu\n", self->m_connectionCount);
		self->m_state = isUserCall ? DeviceState_Normal : DeviceState_Zombie;
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	size = self->m_hookCount * sizeof(Hook*);
	if (size > sizeof(stoppedHookStackArray))
	{
		stoppedHookArray = kmalloc(size, GFP_KERNEL);
		if (!stoppedHookArray)
		{
			mutex_unlock(&self->m_lock);
			return -ENOMEM;
		}
	}

	// walk the list backward and try to stop everybody (last-in-first-out)

	link = self->m_hookList.prev;
	stoppedHookCount = 0;

	for (; link != &self->m_hookList; link = link->prev)
	{
		hook = container_of(link, Hook, m_link);

		result = Hook_stop(hook);
		if (!result)
		{
			printk(KERN_WARNING "tdevmon: can't stop hook: fops: %p\n", hook->m_fops);
			continue;
		}

		Hook_addRef(hook);
		stoppedHookArray[stoppedHookCount] = hook;
		stoppedHookCount++;
	}

	mutex_unlock(&self->m_lock);

	for (i = 0; i < stoppedHookCount; i++)
	{
		hook = stoppedHookArray[i];
		Hook_remove(hook);
		Hook_release(hook);
	}

	if (stoppedHookArray != stoppedHookStackArray)
		kfree(stoppedHookArray);

	mutex_lock(&self->m_lock);
	if (self->m_hookCount)
	{
		printk(KERN_WARNING "tdevmon: stop failed: hooks: %zu\n", self->m_hookCount);
		self->m_state = isUserCall ? DeviceState_Normal : DeviceState_Zombie;
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	if (self->m_fileCount > (size_t)(isUserCall ? 1 : 0))
	{
		printk(KERN_WARNING "tdevmon: stop failed: files: %zu\n", self->m_fileCount);
		self->m_state = isUserCall ? DeviceState_Normal : DeviceState_Zombie;
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	self->m_state = DeviceState_Stopped;
	mutex_unlock(&self->m_lock);
	return 0;
}

//..............................................................................
