#pragma once

#include "dm_lnx_Protocol.h"
#include "FileNameFilter.h"
#include "lkmUtils.h"
#include "typedefs.h"

typedef struct PendingRead   PendingRead;
typedef struct PendingNotify PendingNotify;
typedef enum ConnectionFlag  ConnectionFlag;

//..............................................................................

struct PendingRead
{
	struct list_head m_link;
	wait_queue_head_t m_waitQueue;
	void* m_buffer; // kernel memory
	size_t m_size;
	ssize_t m_result;
	bool m_isCompleted;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct PendingNotify
{
	struct list_head m_link;
	size_t m_size;
	size_t m_streamPos;
	bool m_hasNotifyHdr;

	// followed by notification-specific data (m_size bytes)
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct Connection
{
	Hook* m_hook;
	struct list_head m_hookLink;
	struct inode* m_inode;
	const char* m_path;
	uint m_fileFlags;

	struct mutex m_lock;
	struct file* m_originalFilp;
	FileNameFilter* m_fileNameFilter;
	const dm_IoctlDesc* m_ioctlDescTable;
	HashTable m_ioctlDescMap;
	dm_ReadMode m_readMode;
	wait_queue_head_t m_notificationWaitQueue;
	struct list_head m_pendingReadList;
	struct list_head m_pendingNotifyList;
	size_t m_pendingReadCount;
	size_t m_pendingNotifyCount;
	size_t m_pendingNotifySize;
	size_t m_pendingNotifySizeLimit;

	volatile long m_refCount;
	volatile long m_enableCount;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

int
Connection_create(
	Connection** connection,
	Hook* Hook,
	struct file* filp,
	uint fileFlags // mostly curious about O_NONBLOCK
	);

static
inline
void
Connection_addRef(Connection* self)
{
	atomicInc(&self->m_refCount);
}

long
Connection_release(Connection* self);

void
Connection_disconnect(Connection* self);

void
Connection_detachFromHook(Connection* self);

int
Connection_isEnabled(
	Connection* self,
	int __user* isEnabled_u
	);

void
Connection_enable(Connection* self);

void
Connection_disable(Connection* self);

int
Connection_getReadMode(
	Connection* self,
	int __user* mode_u
	);

int
Connection_setReadMode(
	Connection* self,
	dm_ReadMode mode
	);

int
Connection_getFileNameFilter(
	Connection* self,
	dm_String __user* filter_u
	);

int
Connection_setFileNameFilter(
	Connection* self,
	const dm_String __user* filter_u
	);

int
Connection_getIoctlDescTable(
	Connection* self,
	dm_List __user* table_u
	);

int
Connection_setIoctlDescTable(
	Connection* self,
	const dm_List __user* table_u
	);

int
Connection_setIoctlDescTable_v0302xx(
	Connection* self,
	const dm_IoctlDesc_v0302xx __user* ioctlDesc_u,
	size_t count
	);

int
Connection_getPendingNotifySizeLimit(
	Connection* self,
	uint32_t __user* limit_u
	);

void
Connection_setPendingNotifySizeLimit(
	Connection* self,
	uint32_t limit
	);

bool
Connection_checkFile(
	Connection* self,
	FileNameFilterReq filterReq,
	struct file* filp,
	const char* fileName
	);

int
Connection_getIncomingDataSize(
	Connection* self,
	int __user* size_u
	);

ssize_t
Connection_read(
	Connection* self,
	void __user* buffer_u,
	size_t size
	);

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
	);

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

ssize_t
Connection_p_addPendingRead_l(
	Connection* self,
	void __user* buffer_u,
	size_t size
	);

ssize_t
Connection_p_readMessage_l(
	Connection* self,
	void __user* buffer_u,
	size_t size
	);

ssize_t
Connection_p_readStream_l(
	Connection* self,
	void __user* buffer_u,
	size_t size
	);

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
	);

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
	);

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
	);

void
Connection_p_markDataDropped_l(
	Connection* self,
	uint32_t pid,
	uint32_t tid,
	uint64_t timestamp
	);

void
Connection_p_resetPendingReadListOnDataDropped(
	struct list_head* list,
	uint32_t pid,
	uint32_t tid,
	uint64_t timestamp
	);

void
Connection_p_completePendingReadList(struct list_head* list);

bool
Connection_p_preIoctlNotify(
	Connection* self,
	MemBlock* paramBlockArray,
	size_t paramBlockCount
	);

void
Connection_p_cancelPendingReadList(Connection* self);

void
Connection_p_clearPendingNotifyList(Connection* self);

int
Connection_p_setIoctlDescTableImpl(
	Connection* self,
	const dm_IoctlDesc* ioctlDesc,
	size_t count,
	bool isV0302xx
	);

//..............................................................................
