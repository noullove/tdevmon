#include "pch.h"
#include "Hook.h"
#include "Device.h"
#include "Connection.h"
#include "ScatterGather.h"

//#define _DM_TRACE_FOPS 1

//..............................................................................

int
Hook_create(
	Hook** resultHook,
	struct file_operations* fops,
	struct module* module,
	const struct path* path
	)
{
	int result;
	Hook* newHook;
	Hook* prevHook;

	ulong wpBackup[2]; // should be enough on all archs (you'll see a bugcheck otherwise)

	newHook = kmalloc(sizeof(Hook), GFP_KERNEL);
	if (!newHook)
		return -ENOMEM;

	mutex_init(&newHook->m_lock);
	INIT_LIST_HEAD(&newHook->m_connectionList);
	newHook->m_state = HookState_Normal;
	newHook->m_fops = fops;
	newHook->m_originalModule = module;
	newHook->m_connectionCount = 0;
	newHook->m_refCount = 1;

	result = Device_addHook(&g_device, newHook, &prevHook);
	if (result < 0 || prevHook) // may return +EEXIST
	{
		mutex_destroy(&newHook->m_lock);
		kfree(newHook);
		*resultHook = prevHook;
		return result;
	}

	newHook->m_originalPath = createPathString(path); // ignore errors (this is for info only)

	printk(KERN_INFO "tdevmon: hooking %s (fops: %p)\n", newHook->m_originalPath, fops);

	disablePreemptionAndWriteProtection(fops, fops + 1, wpBackup, sizeof(wpBackup));

	newHook->m_originalFops = *fops;
	newHook->m_fops->open = Hook_fop_open;
	newHook->m_fops->release = Hook_fop_release;
	newHook->m_fops->read = Hook_fop_read;
	newHook->m_fops->write = Hook_fop_write;
	newHook->m_fops->unlocked_ioctl = Hook_fop_unlocked_ioctl;
	newHook->m_fops->compat_ioctl = Hook_fop_compat_ioctl;

	restoreWriteProtectionAndPreemption(fops, fops + 1, wpBackup, sizeof(wpBackup));

	result = try_module_get(THIS_MODULE); // keep ourselves pinned as long as we're hooking
	ASSERT(result);

	if (newHook->m_originalModule)
	{
		result = try_module_get(newHook->m_originalModule);
		ASSERT(result); // otherwise, filp_open would have failed
	}

	*resultHook = newHook;
	return 0;
}

long
Hook_release(Hook* self)
{
	long refCount;
	struct list_head* link;
	Connection* connection;

	refCount = atomicDec(&self->m_refCount);
	if (refCount)
		return refCount;

	mutex_lock(&self->m_lock);
	self->m_state = HookState_Zombie; // enter zombie state

	while (!list_empty(&self->m_connectionList))
	{
		link = self->m_connectionList.next;
		connection = container_of(link, Connection, m_hookLink);
		mutex_unlock(&self->m_lock);

		Connection_detachFromHook(connection);

		mutex_lock(&self->m_lock);
	}

	ASSERT(self->m_connectionCount == 0);

	mutex_unlock(&self->m_lock);
	mutex_destroy(&self->m_lock);
	kfree(self->m_originalPath);
	kfree(self);
	return 0;
}

// Note: Hook_stop is called with Device::m_lock being held

bool
Hook_stop(Hook* self)
{
	ulong wpBackup[2]; // should be enough on all archs (you'll see a bugcheck otherwise)

	mutex_lock(&self->m_lock);

	if (self->m_state >= HookState_Stopped)
	{
		mutex_unlock(&self->m_lock);
		return true;
	}

	if (self->m_refCount > 1)
	{
		printk(KERN_WARNING "tdevmon: can't unhook %s (fops: %p) due to refs: %ld\n", self->m_originalPath, self->m_fops, self->m_refCount);
		mutex_unlock(&self->m_lock);
		return false;
	}

	disablePreemptionAndWriteProtection(self->m_fops, self->m_fops + 1, wpBackup, sizeof(wpBackup));

	// check if somebody has already re-hooked the file_operations

	if (self->m_fops->open != Hook_fop_open ||
		self->m_fops->release != Hook_fop_release ||
		self->m_fops->read != Hook_fop_read ||
		self->m_fops->write != Hook_fop_write ||
		self->m_fops->unlocked_ioctl != Hook_fop_unlocked_ioctl ||
		self->m_fops->compat_ioctl != Hook_fop_compat_ioctl)
	{
		printk(KERN_WARNING "tdevmon: somebody has re-hooked %s (fops %p); try again later\n", self->m_originalPath, self->m_fops);
		restoreWriteProtectionAndPreemption(self->m_fops, self->m_fops + 1, wpBackup, sizeof(wpBackup));
		mutex_unlock(&self->m_lock);
		return false;
	}

	printk(KERN_INFO "tdevmon: unhooking %s (fops: %p)\n", self->m_originalPath, self->m_fops);

	self->m_fops->open = self->m_originalFops.open;
	self->m_fops->release = self->m_originalFops.release;
	self->m_fops->read = self->m_originalFops.read;
	self->m_fops->write = self->m_originalFops.write;
	self->m_fops->unlocked_ioctl = self->m_originalFops.unlocked_ioctl;
	self->m_fops->compat_ioctl = self->m_originalFops.compat_ioctl;

	restoreWriteProtectionAndPreemption(self->m_fops, self->m_fops + 1, wpBackup, sizeof(wpBackup));

	self->m_state = HookState_Stopped;
	mutex_unlock(&self->m_lock);

	if (self->m_originalModule)
		module_put(self->m_originalModule);

	module_put(THIS_MODULE);
	return true;
}

bool
Hook_remove(Hook* self)
{
	mutex_lock(&self->m_lock);

	if (self->m_state >= HookState_Removed)
	{
		mutex_unlock(&self->m_lock);
		return false;
	}

	self->m_state = HookState_Removed;
	mutex_unlock(&self->m_lock);

	Device_removeHook(&g_device, self); // <-- this hook will be deleted here
	return true;
}

int
Hook_addConnection(
	Hook* self,
	Connection* connection
	)
{
	mutex_lock(&self->m_lock);
	if (self->m_state != HookState_Normal)
	{
		mutex_unlock(&self->m_lock);
		return -EBADFD;
	}

	if (self->m_connectionCount >= dm_ConnectionCountLimit)
	{
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	printk(KERN_INFO "tdevmon: adding connection %p to %s (inodep: %p)\n", connection, self->m_originalPath, connection->m_inode);

	Connection_addRef(connection);
	list_add_tail(&connection->m_hookLink, &self->m_connectionList);
	self->m_connectionCount++;
	mutex_unlock(&self->m_lock);

	return 0;
}

void
Hook_removeConnection(
	Hook* self,
	Connection* connection
	)
{
	mutex_lock(&self->m_lock);
	list_del(&connection->m_hookLink);
	self->m_connectionCount--;
	mutex_unlock(&self->m_lock);

	printk(KERN_INFO "tdevmon: removing connection %p from %s (inodep: %p)\n", connection, self->m_originalPath, connection->m_inode);

	Connection_release(connection);
}

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

int
Hook_fop_open(
	struct inode* inodep,
	struct file* filp
	)
{
	int result;
	Hook* self;
	dm_OpenNotifyParams notifyParams;
	MemBlock paramBlockArray[2];
	char* path;

	self = Device_findHookAddRef(&g_device, filp->f_op);
	if (!self)
	{
		printk(KERN_ERR "tdevmon: could not find hook: op: open: fops: %p\n", filp->f_op);
		return -ENOENT;
	}

	result = self->m_originalFops.open(inodep, filp);

#ifdef _DM_TRACE_FOPS
	printk(KERN_INFO "tdevmon: open (inodep: %p, filp: %p) => %d\n", inodep, filp, result);
#endif

	if (!Hook_p_hasConnections(self, filp->f_inode)) // check before allocating path string
	{
		Hook_release(self);
		return result;
	}

	path = createPathString(&filp->f_path);

	notifyParams.m_fileId = (uintptr_t)filp;
	notifyParams.m_flags = filp->f_flags;
	notifyParams.m_mode = filp->f_mode;
	notifyParams.m_fileNameLength = path ? strlen(path) : 0;

	paramBlockArray[0].m_p = &notifyParams;
	paramBlockArray[0].m_size = sizeof(notifyParams);
	paramBlockArray[0].m_flags = 0;
	paramBlockArray[1].m_p = path ? path : "";
	paramBlockArray[1].m_size = notifyParams.m_fileNameLength + 1;
	paramBlockArray[1].m_flags = 0;

	Hook_p_notify(
		self,
		filp,
		dm_NotifyCode_Open,
		result,
		paramBlockArray,
		2
		);

	Hook_release(self);
	kfree(path);
	return result;
}

int
Hook_fop_release(
	struct inode* inodep,
	struct file* filp
	)
{
	int result;
	Hook* self;
	dm_CloseNotifyParams notifyParams;
	MemBlock paramBlock;

	self = Device_findHookAddRef(&g_device, filp->f_op);
	if (!self)
	{
		printk(KERN_ERR "tdevmon: could not find hook: op: release: fops: %p\n", filp->f_op);
		return -ENOENT;
	}

	result = self->m_originalFops.release(inodep, filp);

#ifdef _DM_TRACE_FOPS
	printk(KERN_INFO "tdevmon: release (inodep: %p, filp: %p) => %d\n", inodep, filp, result);
#endif

	notifyParams.m_fileId = (uintptr_t)filp;

	paramBlock.m_p = &notifyParams;
	paramBlock.m_size = sizeof(notifyParams);
	paramBlock.m_flags = 0;

	Hook_p_notify(
		self,
		filp,
		dm_NotifyCode_Close,
		result,
		&paramBlock,
		1
		);

	Hook_release(self);
	return result;
}

ssize_t
Hook_fop_read(
	struct file* filp,
	char __user* buffer_u,
	size_t size,
	loff_t* offset
	)
{
	ssize_t result;
	Hook* self;
	dm_ReadWriteNotifyParams notifyParams;
	MemBlock paramBlockArray[2];

	self = Device_findHookAddRef(&g_device, filp->f_op);
	if (!self)
	{
		printk(KERN_ERR "tdevmon: could not find hook: op: read: fops: %p\n", filp->f_op);
		return -ENOENT;
	}

	result = self->m_originalFops.read(filp, buffer_u, size, offset);

#ifdef _DM_TRACE_FOPS
	printk(KERN_INFO "tdevmon: read (filp: %p, buffer: %p, size: %zu, offset: %p) => %zu\n", filp, buffer_u, size, offset, result);
#endif

	notifyParams.m_fileId = (uintptr_t)filp;
	notifyParams.m_offset = offset ? *offset : 0;
	notifyParams.m_bufferSize = size;
	notifyParams.m_dataSize = result >= 0 ? result : 0;

	paramBlockArray[0].m_p = &notifyParams;
	paramBlockArray[0].m_size = sizeof(notifyParams);
	paramBlockArray[0].m_flags = 0;
	paramBlockArray[1].m_p = buffer_u;
	paramBlockArray[1].m_size = notifyParams.m_dataSize;
	paramBlockArray[1].m_flags = MemBlockFlag_UserBuffer;

	Hook_p_notify(
		self,
		filp,
		dm_NotifyCode_Read,
		result,
		paramBlockArray,
		2
		);

	Hook_release(self);
	return result;
}

ssize_t
Hook_fop_write(
	struct file* filp,
	const char __user* buffer_u,
	size_t size,
	loff_t* offset
	)
{
	ssize_t result;
	Hook* self;
	dm_ReadWriteNotifyParams notifyParams;
	MemBlock paramBlockArray[2];

	self = Device_findHookAddRef(&g_device, filp->f_op);
	if (!self)
	{
		printk(KERN_ERR "tdevmon: could not find hook: op: write: fops: %p\n", filp->f_op);
		return -ENOENT;
	}

	result = self->m_originalFops.write(filp, buffer_u, size, offset);

#ifdef _DM_TRACE_FOPS
	printk(KERN_INFO "tdevmon: write (filp: %p, buffer: %p, size: %zu, offset: %p) => %zu\n", filp, buffer_u, size, offset, result);
#endif

	notifyParams.m_fileId = (uintptr_t)filp;
	notifyParams.m_offset = offset ? *offset : 0;
	notifyParams.m_bufferSize = size;
	notifyParams.m_dataSize = result >= 0 ? result : 0;

	paramBlockArray[0].m_p = &notifyParams;
	paramBlockArray[0].m_size = sizeof(notifyParams);
	paramBlockArray[0].m_flags = 0;
	paramBlockArray[1].m_p = buffer_u;
	paramBlockArray[1].m_size = notifyParams.m_dataSize;
	paramBlockArray[1].m_flags = MemBlockFlag_UserBuffer;

	Hook_p_notify(
		self,
		filp,
		dm_NotifyCode_Write,
		result,
		paramBlockArray,
		2
		);

	Hook_release(self);
	return result;
}

long
Hook_fop_unlocked_ioctl(
	struct file* filp,
	unsigned int code,
	unsigned long arg
	)
{
	long result;
	Hook* self;

	self = Device_findHookAddRef(&g_device, filp->f_op);
	if (!self)
	{
		printk(KERN_ERR "tdevmon: could not find hook: op: unlocked_ioctl: fops: %p\n", filp->f_op);
		return -ENOENT;
	}

	result = self->m_originalFops.unlocked_ioctl(filp, code, arg);

#ifdef _DM_TRACE_FOPS
	printk(KERN_INFO "tdevmon: unlocked_ioctl (filp: %p, code: %d, arg: %ld) => %ld\n", filp, code, arg, result);
#endif

	return Hook_p_postProcessIoctl(self, filp, code, arg, result, dm_NotifyCode_UnlockedIoctl);
}

long
Hook_fop_compat_ioctl(
	struct file* filp,
	unsigned int code,
	unsigned long arg
	)
{
	long result;
	Hook* self;

	self = Device_findHookAddRef(&g_device, filp->f_op);
	if (!self)
	{
		printk(KERN_ERR "tdevmon: could not find hook: op: compat_ioctl: fops: %p\n", filp->f_op);
		return -ENOENT;
	}

	result = self->m_originalFops.compat_ioctl(filp, code, arg);

#ifdef _DM_TRACE_FOPS
	printk(KERN_INFO "tdevmon: compat_ioctl (filp: %p, code: %d, arg: %ld) => %ld\n", filp, code, arg, result);
#endif

	return Hook_p_postProcessIoctl(self, filp, code, arg, result, dm_NotifyCode_CompatIoctl);
}

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

long
Hook_p_postProcessIoctl(
	Hook* self,
	struct file* filp,
	unsigned int ioctlCode,
	unsigned long arg,
	long result,
	uint16_t notifyCode
	)
{
	dm_IoctlNotifyParams notifyParams;
	MemBlock paramBlockArray[2]; // reserve one block for arg data

	notifyParams.m_fileId = (uintptr_t)filp;
	notifyParams.m_code = ioctlCode;
	notifyParams.m_arg = arg;
	notifyParams.m_argSize = 0; // may be changed per-connection

	paramBlockArray[0].m_p = &notifyParams;
	paramBlockArray[0].m_size = sizeof(notifyParams);
	paramBlockArray[0].m_flags = 0;

	Hook_p_notify(
		self,
		filp,
		notifyCode,
		result,
		paramBlockArray,
		1 // maybe, 2 -- depends on ioctl desc map in particular connection
		);

	Hook_release(self);
	return result;
}

bool
Hook_p_hasConnections(
	Hook* self,
	struct inode* inodep
	)
{
	struct list_head* link;
	Connection* connection;

	mutex_lock(&self->m_lock);

	for (
		link = self->m_connectionList.next;
		link != &self->m_connectionList;
		link = link->next
		)
	{
		connection = container_of(link, Connection, m_hookLink);

		if (connection->m_inode == inodep)
		{
			mutex_unlock(&self->m_lock);
			return true;
		}
	}

	mutex_unlock(&self->m_lock);
	return false;
}

void
Hook_p_notify(
	Hook* self,
	struct file* filp,
	uint16_t code,
	int result,
	MemBlock* paramBlockArray,
	size_t paramBlockCount
	)
{
	struct list_head* link;
	size_t count;
	size_t i;
	uint64_t timestamp;
	uint32_t pid;
	uint32_t tid;
	Connection* connectionArray[dm_ConnectionCountLimit];
	Connection* connection;
	FileNameFilterReq filterReq;
	const char* fileName;
	bool isMatch;

	timestamp = getTimestamp();
	pid = current->tgid;
	tid = current->pid;

	mutex_lock(&self->m_lock);

	ASSERT(self->m_connectionCount <= dm_ConnectionCountLimit);

	for (
		link = self->m_connectionList.next, count = 0;
		link != &self->m_connectionList;
		link = link->next
		)
	{
		connection = container_of(link, Connection, m_hookLink);
		Connection_addRef(connection);
		connectionArray[count++] = connection;
	}

	mutex_unlock(&self->m_lock);

	switch (code)
	{
	case dm_NotifyCode_Open:
		filterReq = result == 0 ? FileNameFilterReq_Open : FileNameFilterReq_OpenError;
		fileName = paramBlockArray[1].m_p;
		break;

	case dm_NotifyCode_Close:
		filterReq = FileNameFilterReq_Close;
		fileName = "";
		break;

	default:
		filterReq = FileNameFilterReq_Other;
		fileName = "";
	}

	for (i = 0; i < count; i++)
	{
		connection = connectionArray[i];

		isMatch = Connection_checkFile(connection, filterReq, filp, fileName);
		if (isMatch)
			Connection_notify(connection, filp, code, result, pid, tid, timestamp, paramBlockArray, paramBlockCount);

		Connection_release(connection);
	}
}

//..............................................................................
