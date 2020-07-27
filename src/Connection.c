#include "pch.h"
#include "Connection.h"
#include "Hook.h"
#include "ScatterGather.h"

//..............................................................................

int
Connection_create(
	Connection** resultConnection,
	Hook* hook,
	struct file* filp,
	uint fileFlags
	)
{
	int result;
	const char* path;
	Connection* connection;

	connection = kmalloc(sizeof(Connection), GFP_KERNEL);
	if (!connection)
		return -ENOMEM;

	path = createPathString(&filp->f_path);
	if (IS_ERR(path))
	{
		kfree(connection);
		return PTR_ERR(path);
	}

	mutex_init(&connection->m_lock);
	INIT_LIST_HEAD(&connection->m_pendingReadList);
	INIT_LIST_HEAD(&connection->m_pendingNotifyList);
	init_waitqueue_head(&connection->m_notificationWaitQueue);
	HashTable_construct(&connection->m_ioctlDescMap, HashTableKeyType_Pointer, GFP_KERNEL);
	connection->m_hook = hook;
	connection->m_fileNameFilter = NULL;
	connection->m_ioctlDescTable = NULL;
	connection->m_originalFilp = filp;
	connection->m_inode = filp->f_inode;
	connection->m_path = path;
	connection->m_fileFlags = fileFlags;
	connection->m_readMode = dm_ReadMode_Stream;
	connection->m_pendingReadCount = 0;
	connection->m_pendingNotifyCount = 0;
	connection->m_pendingNotifySize = 0;
	connection->m_pendingNotifySizeLimit = dm_DefPendingNotifySizeLimit;

	connection->m_refCount = 1;
	connection->m_enableCount = 0; // initially disabled

	result = Hook_addConnection(hook, connection);
	if (result != 0)
	{
		kfree(path);
		kfree(connection);
		return result;
	}

	*resultConnection = connection;
	return 0;
}

long
Connection_release(Connection* self)
{
	long refCount;

	refCount = atomicDec(&self->m_refCount);
	if (refCount)
		return refCount;

	Connection_disconnect(self);

	ASSERT(list_empty(&self->m_pendingReadList));
	ASSERT(list_empty(&self->m_pendingNotifyList));

	if (self->m_fileNameFilter)
		FileNameFilter_delete(self->m_fileNameFilter);

	mutex_destroy(&self->m_lock);
	HashTable_destruct(&self->m_ioctlDescMap);
	kfree(self->m_ioctlDescTable);
	kfree(self->m_path);
	kfree(self);
	return 0;
}

void
Connection_disconnect(Connection* self)
{
	Connection_disable(self);
	Connection_detachFromHook(self);
}

void
Connection_detachFromHook(Connection* self)
{
	Hook* hook;

	mutex_lock(&self->m_lock);
	hook = self->m_hook;
	self->m_hook = NULL;
	mutex_unlock(&self->m_lock);

	if (hook)
		Hook_removeConnection(hook, self);
}

int
Connection_isEnabled(
	Connection* self,
	int __user* isEnabled_u
	)
{
	int isEnabled = self->m_enableCount > 0;
	int result = copy_to_user(isEnabled_u, &isEnabled, sizeof(int));
	return result == 0 ? 0 : -EFAULT;
}

void
Connection_enable(Connection* self)
{
	mutex_lock(&self->m_lock);
	self->m_enableCount++;
	mutex_unlock(&self->m_lock);
}

void
Connection_disable(Connection* self)
{
	struct list_head* link;
	PendingRead* read;
	PendingNotify* notify;

	mutex_lock(&self->m_lock);
	self->m_enableCount--;

	if (self->m_enableCount > 0)
	{
		mutex_unlock(&self->m_lock);
		return;
	}

	while (!list_empty(&self->m_pendingReadList))
	{
		link = self->m_pendingReadList.next;
		list_del(link);
		read = container_of(link, PendingRead, m_link);
		read->m_result = -ECANCELED;
		read->m_isCompleted = true;
		wake_up_interruptible(&read->m_waitQueue);
	}

	while (!list_empty(&self->m_pendingNotifyList))
	{
		link = self->m_pendingNotifyList.next;
		list_del(link);
		notify = container_of(link, PendingNotify, m_link);
		kfree(notify);
	}

	self->m_pendingReadCount = 0;
	self->m_pendingNotifyCount = 0;
	self->m_pendingNotifySize = 0;

	if (self->m_fileNameFilter)
		HashTable_clear(&self->m_fileNameFilter->m_fileSet);

	mutex_unlock(&self->m_lock);
}

int
Connection_getReadMode(
	Connection* self,
	int __user* mode_u
	)
{
	int result;
	int mode;

	mutex_lock(&self->m_lock);
	mode = self->m_readMode;
	mutex_unlock(&self->m_lock);

	result = copy_to_user(mode_u, &mode, sizeof(int));
	return result == 0 ? 0 : -EFAULT;
}

int
Connection_setReadMode(
	Connection* self,
	dm_ReadMode mode
	)
{
	mutex_lock(&self->m_lock);
	if (self->m_enableCount)
	{
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	self->m_readMode = mode;
	mutex_unlock(&self->m_lock);
	return 0;
}

int
Connection_getFileNameFilter(
	Connection* self,
	dm_String __user* filter_u
	)
{
	int result;

	mutex_lock(&self->m_lock);
	if (self->m_enableCount)
	{
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	result = self->m_fileNameFilter ? copyStringToUser(filter_u, self->m_fileNameFilter->m_fileNameWildcard) : 0;

	mutex_unlock(&self->m_lock);
	return result;
}

int
Connection_setFileNameFilter(
	Connection* self,
	const dm_String __user* filter_u
	)
{
	int result;
	const char* wildcard;

	wildcard = copyStringFromUser(filter_u);
	if (IS_ERR(wildcard))
		return PTR_ERR(wildcard);

	mutex_lock(&self->m_lock);
	if (self->m_enableCount)
	{
		mutex_unlock(&self->m_lock);
		kfree(wildcard);
		return -EBUSY;
	}

	if (self->m_fileNameFilter)
	{
		FileNameFilter_delete(self->m_fileNameFilter);
		self->m_fileNameFilter = NULL;
	}

	result = *wildcard ? FileNameFilter_create(&self->m_fileNameFilter, wildcard, GFP_KERNEL) : 0;

	kfree(wildcard);
	mutex_unlock(&self->m_lock);
	return result;
}

int
Connection_getIoctlDescTable(
	Connection* self,
	dm_List __user* table_u
	)
{
	int result;
	dm_List table;
	size_t bufferSize;

	result = copy_from_user(&table, table_u, sizeof(dm_List));
	if (result != 0)
		return -EFAULT;

	if (table.m_bufferSize < sizeof(dm_List))
		return -EINVAL;

	mutex_lock(&self->m_lock);
	table.m_elementCount = (uint32_t)self->m_ioctlDescMap.m_entryCount;
	table.m_dataSize = (uint32_t)self->m_ioctlDescMap.m_entryCount * sizeof(dm_IoctlDesc);

	bufferSize = sizeof(dm_List) + table.m_dataSize;

	if (table.m_bufferSize < bufferSize)
	{
		mutex_unlock(&self->m_lock);
		table.m_bufferSize = bufferSize;
		result = copy_to_user(table_u, &table, sizeof(dm_List));
		return result == 0 ? -ENOBUFS : -EFAULT;
	}

	result = copy_to_user(table_u, &table, sizeof(dm_List));

	if (result == 0)
		result = copy_to_user(table_u + 1, self->m_ioctlDescTable, table.m_dataSize);

	mutex_unlock(&self->m_lock);
	return result == 0 ? 0 : -EFAULT;
}

int
Connection_setIoctlDescTable(
	Connection* self,
	const dm_List __user* table_u
	)
{
	int result;
	dm_List table;
	const dm_IoctlDesc* ioctlDesc_u;

	result = copy_from_user(&table, table_u, sizeof(dm_List));
	if (result != 0)
		return -EFAULT;

	mutex_lock(&self->m_lock);
	if (self->m_enableCount)
	{
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	ioctlDesc_u = (const dm_IoctlDesc*) (table_u + 1);
	result = Connection_p_setIoctlDescTableImpl(self, ioctlDesc_u, table.m_elementCount, false);

	mutex_unlock(&self->m_lock);
	return result;
}

int
Connection_setIoctlDescTable_v0302xx(
	Connection* self,
	const dm_IoctlDesc_v0302xx __user* ioctlDesc_u,
	size_t count
	)
{
	int result;

	mutex_lock(&self->m_lock);
	if (self->m_enableCount)
	{
		mutex_unlock(&self->m_lock);
		return -EBUSY;
	}

	result = Connection_p_setIoctlDescTableImpl(self, (const dm_IoctlDesc*) ioctlDesc_u, count, true);

	mutex_unlock(&self->m_lock);
	return result;
}

int
Connection_getPendingNotifySizeLimit(
	Connection* self,
	uint32_t __user* limit_u
	)
{
	int result;
	size_t limit;

	mutex_lock(&self->m_lock);
	limit = self->m_pendingNotifySizeLimit;
	mutex_unlock(&self->m_lock);

	result = copy_to_user(limit_u, &limit, sizeof(uint32_t));
	return result == 0 ? 0 : -EFAULT;
}

void
Connection_setPendingNotifySizeLimit(
	Connection* self,
	uint32_t limit
	)
{
	mutex_lock(&self->m_lock);
	self->m_pendingNotifySizeLimit = limit;
	mutex_unlock(&self->m_lock);
}

bool
Connection_checkFile(
	Connection* self,
	FileNameFilterReq filterReq,
	struct file* filp,
	const char* fileName
	)
{
	bool result;

	mutex_lock(&self->m_lock);
	if (self->m_enableCount <= 0)
	{
		mutex_unlock(&self->m_lock);
		return false;
	}

	if (!self->m_fileNameFilter)
	{
		mutex_unlock(&self->m_lock);
		return self->m_inode == filp->f_inode;
	}

	result = FileNameFilter_checkFile(self->m_fileNameFilter, filterReq, filp, fileName);
	mutex_unlock(&self->m_lock);
	return result;
}

int
Connection_getIncomingDataSize(
	Connection* self,
	int __user* size_u
	)
{
	int result;
	size_t size;

	mutex_lock(&self->m_lock);
	size = self->m_pendingNotifySize;
	mutex_unlock(&self->m_lock);

	result = copy_to_user(size_u, &size, sizeof(int));
	return result == 0 ? 0 : -EFAULT;
}

ssize_t
Connection_read(
	Connection* self,
	void __user* buffer_u,
	size_t size
	)
{
	mutex_lock(&self->m_lock);

	if (list_empty(&self->m_pendingNotifyList))
		return Connection_p_addPendingRead_l(self, buffer_u, size);

	ASSERT(list_empty(&self->m_pendingReadList));

	return self->m_readMode == dm_ReadMode_Message ?
		Connection_p_readMessage_l(self, buffer_u, size) :
		Connection_p_readStream_l(self, buffer_u, size);
}

void
Connection_notify(
	Connection* self,
	struct file* filp,
	uint16_t code,
	int result,
	uint32_t pid,
	uint32_t tid,
	uint64_t timestamp,
	MemBlock* paramBlockArray,
	size_t paramBlockCount
	)
{
	size_t paramSize;
	bool hasArgData;

	if (code == dm_NotifyCode_UnlockedIoctl || code == dm_NotifyCode_CompatIoctl)
	{
		hasArgData = Connection_p_preIoctlNotify(self, paramBlockArray, paramBlockCount);
		if (hasArgData)
			paramBlockCount++;
	}

	paramSize = getScatterGatherSize(paramBlockArray, paramBlockCount);

	mutex_lock(&self->m_lock);
	if (filp == self->m_originalFilp) // don't dispatch close notification for the filp used to create this connection
	{
		if (code == dm_NotifyCode_Close)
			self->m_originalFilp = NULL;
		else
			printk(KERN_WARNING "tdevmon: unexpected notification on the original filp; code: %d\n", code); // flush? anyway, not a big deal

		mutex_unlock(&self->m_lock);
		return;
	}

	if (list_empty(&self->m_pendingReadList))
	{
		Connection_p_addPendingNotification_l(
			self,
			true,
			code,
			result,
			pid,
			tid,
			timestamp,
			paramBlockArray,
			paramBlockCount,
			paramSize
			);

		return;
	}

	ASSERT(list_empty(&self->m_pendingNotifyList));

	if (self->m_readMode == dm_ReadMode_Message)
		Connection_p_notifyMessage_l(
			self,
			code,
			result,
			pid,
			tid,
			timestamp,
			paramBlockArray,
			paramBlockCount,
			paramSize
			);
	else
		Connection_p_notifyStream_l(
			self,
			code,
			result,
			pid,
			tid,
			timestamp,
			paramBlockArray,
			paramBlockCount,
			paramSize
			);
}

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

ssize_t
Connection_p_addPendingRead_l(
	Connection* self,
	void __user* buffer_u,
	size_t size
	)
{
	int result;
	PendingRead read;

	if (self->m_fileFlags & O_NONBLOCK)
	{
		mutex_unlock(&self->m_lock);
		return -EWOULDBLOCK;
	}

	// TODO: use memory locking instead of double-buffering (user buffers may be HUGE!)

	read.m_buffer = kmalloc(size, GFP_KERNEL);
	if (!read.m_buffer)
	{
		mutex_unlock(&self->m_lock);
		return -ENOMEM;
	}

	init_waitqueue_head(&read.m_waitQueue);
	read.m_size = size;
	read.m_isCompleted = false;

	list_add_tail(&read.m_link, &self->m_pendingReadList);
	self->m_pendingReadCount++;
	mutex_unlock(&self->m_lock);

	result = wait_event_interruptible(read.m_waitQueue, read.m_isCompleted);
	if (result != 0)
	{
		kfree(read.m_buffer);
		return result;
	}

	if (read.m_result > 0)
	{
		result = copy_to_user(buffer_u, read.m_buffer, read.m_result);
		if (result != 0)
		{
			kfree(read.m_buffer);
			return -EFAULT;
		}
	}

	kfree(read.m_buffer);
	return read.m_result;
}

ssize_t
Connection_p_readMessage_l(
	Connection* self,
	void __user* buffer_u,
	size_t size
	)
{
	int result;
	PendingNotify* notify;
	dm_NotifyHdr notifyHdr;
	size_t notifySize;

	ASSERT(size >= sizeof(dm_NotifyHdr)); // should have been checked
	ASSERT(!list_empty(&self->m_pendingNotifyList));

	notify = container_of(self->m_pendingNotifyList.next, PendingNotify, m_link);
	ASSERT(notify->m_hasNotifyHdr);

	if (size < notify->m_size)
	{
		notifyHdr = *(const dm_NotifyHdr*) (notify + 1);
		mutex_unlock(&self->m_lock);

		notifyHdr.m_flags |= dm_NotifyFlag_InsufficientBuffer;

		result = copy_to_user(buffer_u, &notifyHdr, sizeof(dm_NotifyHdr));
		return result == 0 ? sizeof(dm_NotifyHdr) : -EFAULT;
	}

	notifySize = notify->m_size;
	result = copy_to_user(buffer_u, notify + 1, notifySize);
	if (result != 0)
	{
		mutex_unlock(&self->m_lock);
		return -EFAULT;
	}

	list_del(&notify->m_link);
	self->m_pendingNotifySize -= notifySize;
	self->m_pendingNotifyCount--;
	mutex_unlock(&self->m_lock);

	kfree(notify);
	return notifySize;
}

ssize_t
Connection_p_readStream_l(
	Connection* self,
	void __user* buffer_u,
	size_t size
	)
{
	int result;
	PendingNotify* notify;
	char* p;
	size_t copySize;
	size_t totalSize;

	ASSERT(!list_empty(&self->m_pendingNotifyList));

	totalSize = 0;

	do
	{
		notify = container_of(self->m_pendingNotifyList.next, PendingNotify, m_link);
		ASSERT(notify->m_streamPos < notify->m_size);

		p = (char*)(notify + 1) + notify->m_streamPos;
		copySize = notify->m_size - notify->m_streamPos;

		if (size < copySize)
		{
			result = copy_to_user(buffer_u, p, size);
			if (result != 0)
			{
				mutex_unlock(&self->m_lock);
				return -EFAULT;
			}

			notify->m_streamPos += size;
			totalSize += size;
			break;
		}

		result = copy_to_user(buffer_u, p, copySize);
		if (result != 0)
		{
			mutex_unlock(&self->m_lock);
			return -EFAULT;
		}

		list_del(&notify->m_link);
		self->m_pendingNotifySize -= notify->m_size;
		self->m_pendingNotifyCount--;
		kfree(notify);

		buffer_u = (char*)buffer_u + copySize;
		size -= copySize;
		totalSize += copySize;
	}
	while (!list_empty(&self->m_pendingNotifyList));

	mutex_unlock(&self->m_lock);
	return totalSize;
}

bool
Connection_p_addPendingNotification_l(
	Connection* self,
	bool hasNotifyHdr,
	uint16_t code,
	int result,
	uint32_t pid,
	uint32_t tid,
	uint64_t timestamp,
	MemBlock* paramBlockArray,
	size_t paramBlockCount,
	size_t paramSize
	)
{
	PendingNotify* notify;
	dm_NotifyHdr* notifyHdr;
	size_t notifySize;

	if (self->m_pendingNotifySize >= self->m_pendingNotifySizeLimit)
	{
		printk(
			KERN_WARNING "tdevmon: notification dropped: pending notify size limit exceeded (size: %zu; limit: %zu)\n",
			self->m_pendingNotifySize,
			self->m_pendingNotifySizeLimit
			);

		Connection_p_markDataDropped_l(self, pid, tid, timestamp);
		return false;
	}

	notifySize = hasNotifyHdr ?	sizeof(dm_NotifyHdr) + paramSize : paramSize;

	notify = kmalloc(sizeof(PendingNotify) + notifySize, GFP_KERNEL);
	if (!notify)
	{
		printk(
			KERN_WARNING "tdevmon: notification dropped: could not allocate notification buffer (size: %zu)\n",
			notifySize
			);

		Connection_p_markDataDropped_l(self, pid, tid, timestamp);
		return false;
	}

	notify->m_size = notifySize;
	notify->m_streamPos = 0;
	notify->m_hasNotifyHdr = hasNotifyHdr;

	if (!hasNotifyHdr)
	{
		copyScatterGather(notify + 1, paramBlockArray, paramBlockCount);
	}
	else
	{
		notifyHdr = (dm_NotifyHdr*)(notify + 1);
		notifyHdr->m_signature = dm_NotifyHdrSignature;
		notifyHdr->m_code = code;
		notifyHdr->m_flags = 0;
		notifyHdr->m_result = result;
		notifyHdr->m_pid = pid;
		notifyHdr->m_tid = tid;
		notifyHdr->m_timestamp = timestamp;
		notifyHdr->m_paramSize = (uint32_t)paramSize;

		copyScatterGather(notifyHdr + 1, paramBlockArray, paramBlockCount);
	}

	list_add_tail(&notify->m_link, &self->m_pendingNotifyList);
	self->m_pendingNotifyCount++;
	self->m_pendingNotifySize += notify->m_size;
	wake_up_interruptible(&self->m_notificationWaitQueue);
	mutex_unlock(&self->m_lock);

	return true;
}

void
Connection_p_notifyMessage_l(
	Connection* self,
	uint16_t code,
	int result,
	uint32_t pid,
	uint32_t tid,
	uint64_t timestamp,
	MemBlock* paramBlockArray,
	size_t paramBlockCount,
	size_t paramSize
	)
{
	struct list_head* link;
	struct list_head readCompletionList;
	PendingRead* read;
	dm_NotifyHdr* notifyHdr;
	size_t notifySize;

	ASSERT(!list_empty(&self->m_pendingReadList));

	INIT_LIST_HEAD(&readCompletionList);
	notifySize = sizeof(dm_NotifyHdr) + paramSize;

	do
	{
		link = self->m_pendingReadList.next;
		list_del(link);
		read = container_of(link, PendingRead, m_link);
		self->m_pendingReadCount--;

		ASSERT(read->m_size >= sizeof(dm_NotifyHdr)); // should have been checked

		notifyHdr = read->m_buffer;
		notifyHdr->m_signature = dm_NotifyHdrSignature;
		notifyHdr->m_code = code;
		notifyHdr->m_flags = 0;
		notifyHdr->m_result = result;
		notifyHdr->m_pid = pid;
		notifyHdr->m_tid = tid;
		notifyHdr->m_timestamp = timestamp;
		notifyHdr->m_paramSize = (uint32_t)paramSize;

		if (read->m_size >= notifySize)
		{
			copyScatterGather(notifyHdr + 1, paramBlockArray, paramBlockCount);
			read->m_result = notifySize;
		}
		else
		{
			notifyHdr->m_flags |= dm_NotifyFlag_InsufficientBuffer;
			read->m_result = sizeof(dm_NotifyHdr);
		}

		list_add_tail(&read->m_link, &readCompletionList);

		if (read->m_result == notifySize)
		{
			mutex_unlock(&self->m_lock);
			Connection_p_completePendingReadList(&readCompletionList);
			return;
		}
	}
	while (!list_empty(&self->m_pendingReadList));

	Connection_p_addPendingNotification_l(
		self,
		true,
		code,
		result,
		pid,
		tid,
		timestamp,
		paramBlockArray,
		paramBlockCount,
		paramSize
		);

	Connection_p_completePendingReadList(&readCompletionList);
}

void
Connection_p_notifyStream_l(
	Connection* self,
	uint16_t code,
	int result,
	uint32_t pid,
	uint32_t tid,
	uint64_t timestamp,
	MemBlock* paramBlockArray,
	size_t paramBlockCount,
	size_t paramSize
	)
{
	struct list_head* link;
	struct list_head readCompletionList;
	PendingRead* read;
	dm_NotifyHdr* notifyHdr;
	size_t notifySize;
	size_t notifyPos;
	size_t copySize;
	size_t partialBlockIdx;
	bool isPendingNotificationAdded;

	ASSERT(!list_empty(&self->m_pendingReadList));

	INIT_LIST_HEAD(&readCompletionList);
	notifySize = sizeof(dm_NotifyHdr) + paramSize;
	notifyPos = 0;

	do
	{
		link = self->m_pendingReadList.next;
		list_del(link);
		read = container_of(link, PendingRead, m_link);
		self->m_pendingReadCount--;

		if (notifyPos > 0)
		{
			copySize = copyScatterGatherPartial(
				read->m_buffer,
				read->m_size,
				paramBlockArray,
				paramBlockCount,
				&partialBlockIdx
				);

			read->m_result = copySize;
		}
		else
		{
			ASSERT(read->m_size >= sizeof(dm_NotifyHdr)); // should have been checked

			notifyHdr = read->m_buffer;
			notifyHdr->m_signature = dm_NotifyHdrSignature;
			notifyHdr->m_code = code;
			notifyHdr->m_flags = 0;
			notifyHdr->m_result = result;
			notifyHdr->m_pid = pid;
			notifyHdr->m_tid = tid;
			notifyHdr->m_timestamp = timestamp;
			notifyHdr->m_paramSize = (uint32_t)paramSize;

			copySize = copyScatterGatherPartial(
				notifyHdr + 1,
				read->m_size - sizeof(dm_NotifyHdr),
				paramBlockArray,
				paramBlockCount,
				&partialBlockIdx
				);

			read->m_result = sizeof(dm_NotifyHdr) + copySize;
		}

		if (partialBlockIdx != paramBlockCount)
			printk(KERN_WARNING  "tdevmon: partial scatter-gather: copied %zu of %zu\n", partialBlockIdx, paramBlockCount);

		list_add_tail(&read->m_link, &readCompletionList);
		notifyPos += read->m_result;

		if (notifyPos == notifySize)
		{
			mutex_unlock(&self->m_lock);
			Connection_p_completePendingReadList(&readCompletionList);
			return;
		}

		ASSERT(partialBlockIdx <= paramBlockCount && copySize <= paramSize);

		paramBlockArray += partialBlockIdx;
		paramBlockCount -= partialBlockIdx;
		paramSize -= copySize;
	} while (!list_empty(&self->m_pendingReadList));

	ASSERT(notifyPos >= sizeof(dm_NotifyHdr));

	printk(KERN_WARNING "adding partial pending notification: size: %zu\n", paramSize);

	isPendingNotificationAdded = Connection_p_addPendingNotification_l(
		self,
		false,
		code,
		result,
		pid,
		tid,
		timestamp,
		paramBlockArray,
		paramBlockCount,
		paramSize
		);

	if (!isPendingNotificationAdded)
		Connection_p_resetPendingReadListOnDataDropped(
			&readCompletionList,
			pid,
			tid,
			timestamp
			);

	Connection_p_completePendingReadList(&readCompletionList);
}

void
Connection_p_markDataDropped_l(
	Connection* self,
	uint32_t pid,
	uint32_t tid,
	uint64_t timestamp
	)
{
	PendingNotify* notify;
	dm_NotifyHdr* notifyHdr;

	if (!list_empty(&self->m_pendingNotifyList))
	{
		notify = container_of(self->m_pendingNotifyList.prev, PendingNotify, m_link);
		if (notify->m_hasNotifyHdr)
		{
			notifyHdr = (dm_NotifyHdr*)(notify + 1);
			notifyHdr->m_flags |= dm_NotifyFlag_DataDropped;
			mutex_unlock(&self->m_lock);
			return;
		}
	}

	notify = kmalloc(sizeof(PendingNotify) + sizeof(dm_NotifyHdr), GFP_KERNEL);
	if (!notify) // there's nothing else we can do
	{
		mutex_unlock(&self->m_lock);
		return;
	}

	notify->m_size = sizeof(dm_NotifyHdr);
	notify->m_streamPos = 0;
	notify->m_hasNotifyHdr = true;

	notifyHdr = (dm_NotifyHdr*)(notify + 1);
	notifyHdr->m_signature = dm_NotifyHdrSignature;
	notifyHdr->m_code = dm_NotifyCode_DataDropped;
	notifyHdr->m_flags = dm_NotifyFlag_DataDropped;
	notifyHdr->m_result = 0;
	notifyHdr->m_pid = pid;
	notifyHdr->m_tid = tid;
	notifyHdr->m_timestamp = timestamp;
	notifyHdr->m_paramSize = 0;

	list_add_tail(&notify->m_link, &self->m_pendingNotifyList);
	self->m_pendingNotifyCount++;
	self->m_pendingNotifySize += notify->m_size;
	wake_up_interruptible(&self->m_notificationWaitQueue);
	mutex_unlock(&self->m_lock);
}

void
Connection_p_resetPendingReadListOnDataDropped(
	struct list_head* list,
	uint32_t pid,
	uint32_t tid,
	uint64_t timestamp
	)
{
	struct list_head* link;
	PendingRead* read;
	dm_NotifyHdr* notifyHdr;
	uint16_t code = dm_NotifyCode_DataDropped;

	printk(KERN_WARNING "tdevmon: resetting read results on data dropped\n");

	for (link = list->next; link != list; link = link->next)
	{
		read = container_of(link, PendingRead, m_link);
		ASSERT(read->m_size >= sizeof(dm_NotifyHdr));
		read->m_result = sizeof(dm_NotifyHdr);

		notifyHdr = read->m_buffer;
		notifyHdr->m_signature = dm_NotifyHdrSignature;
		notifyHdr->m_code = code;
		notifyHdr->m_flags = dm_NotifyFlag_DataDropped;
		notifyHdr->m_result = 0;
		notifyHdr->m_pid = pid;
		notifyHdr->m_tid = tid;
		notifyHdr->m_timestamp = timestamp;
		notifyHdr->m_paramSize = 0;

		code = 0; // only use dm_NotifyCode_DataDropped once
	}
}

void
Connection_p_completePendingReadList(struct list_head* list)
{
	struct list_head* link;
	PendingRead* read;

	while (!list_empty(list))
	{
		link = list->next;
		list_del(link);
		read = container_of(link, PendingRead, m_link);
		read->m_isCompleted = true;
		wake_up_interruptible(&read->m_waitQueue);
		kfree(read);
	}
}

bool
Connection_p_preIoctlNotify(
	Connection* self,
	MemBlock* paramBlockArray,
	size_t paramBlockCount
	)
{
	dm_IoctlNotifyParams* notifyParams;
	const dm_IoctlDesc* ioctlDesc;
	size_t argSize;

	ASSERT(paramBlockCount == 1);

	notifyParams = (dm_IoctlNotifyParams*)paramBlockArray[0].m_p; // const cast

	ioctlDesc = HashTable_findValue(
		&self->m_ioctlDescMap,
		(const void*) (uintptr_t)notifyParams->m_code
		);

	if (!ioctlDesc)
	{
		notifyParams->m_argSize = 0;
		return false;
	}

	argSize = ioctlDesc->m_argFixedSize;

	if (ioctlDesc->m_flags & dm_IoctlFlag_HasArgSizeField)
	{
		// TODO: copy_from_user and add dynamic size from field
	}

	notifyParams->m_argSize = argSize;

	paramBlockArray[1].m_flags = MemBlockFlag_UserBuffer;
	paramBlockArray[1].m_p = (void*)(uintptr_t)notifyParams->m_arg;
	paramBlockArray[1].m_size = argSize;

	return true;
}

inline
void
Connection_p_convertIoctlDesc_v0302xx(dm_IoctlDesc* ioctlDesc)
{
	dm_IoctlDesc_v0302xx ioctlDesc_v0302xx;

	ioctlDesc_v0302xx = *(dm_IoctlDesc_v0302xx*)ioctlDesc;
	ioctlDesc->m_argFixedSize= ioctlDesc_v0302xx.m_argFixedSize;
	ioctlDesc->m_argSizeFieldOffset= ioctlDesc_v0302xx.m_argSizeFieldOffset;
	ioctlDesc->m_flags = ioctlDesc_v0302xx.m_flags;
}

int
Connection_p_setIoctlDescTableImpl(
	Connection* self,
	const dm_IoctlDesc __user* ioctlDesc_u,
	size_t count,
	bool isV0302xx
	)
{
	int result;
	size_t size;
	dm_IoctlDesc* table;
	dm_IoctlDesc* p;
	HashTableEntry* entry;
	size_t i;

	ASSERT(sizeof(dm_IoctlDesc) == sizeof(dm_IoctlDesc_v0302xx));

	size = sizeof(dm_IoctlDesc)* count;
	table = kmalloc(size, GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	result = copy_from_user(table, ioctlDesc_u, size);
	if (result != 0)
	{
		kfree(table);
		return -EFAULT;
	}

	HashTable_clear(&self->m_ioctlDescMap);

	printk(KERN_INFO "tdevmon: setting %zu IOCTL descriptors on connection %p to %s (inode: %p):\n", count, self, self->m_hook->m_originalPath, self->m_inode);

	for (i = 0, p = table; i < count; i++, p++)
	{
		if (isV0302xx)
			Connection_p_convertIoctlDesc_v0302xx(p);

		printk(KERN_INFO "tdevmon: ... [%zu] 0x%x -> %d B\n", i, p->m_code, p->m_argFixedSize);

		entry = HashTable_visit(
			&self->m_ioctlDescMap,
			(const void*) (uintptr_t)p->m_code
			);

		if (!entry)
		{
			HashTable_clear(&self->m_ioctlDescMap);
			kfree(table);
			return -ENOMEM;
		}

		entry->m_value = &table[i];
	}

	kfree(self->m_ioctlDescTable);
	self->m_ioctlDescTable = table;
	return 0;
}

//..............................................................................
