#pragma once

#include "HashTable.h"
#include "ScatterGather.h"
#include "lkmUtils.h"
#include "typedefs.h"

typedef enum HookState    HookState;

//..............................................................................

enum HookState
{
	HookState_Normal,
	HookState_Stopped,
	HookState_Removed,
	HookState_Zombie,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct Hook
{
	struct list_head m_link;
	struct file_operations* m_fops;
	struct file_operations m_originalFops;
	struct module* m_originalModule;
	const char* m_originalPath;
	HashTableEntry* m_mapEntry;

	struct mutex m_lock;
	HookState m_state;
	struct list_head m_connectionList;
	size_t m_connectionCount;
	volatile long m_refCount;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

int
Hook_create(
	Hook** hook,
	struct file_operations* fops,
	struct module* module,
	const struct path* path
	);

static
inline
long
Hook_addRef(Hook* self)
{
	return atomicInc(&self->m_refCount);
}

long
Hook_release(Hook* self);

bool
Hook_stop(Hook* self);

bool
Hook_remove(Hook* self);

int
Hook_addConnection(
	Hook* self,
	Connection* connection
	);

void
Hook_removeConnection(
	Hook* self,
	Connection* connection
	);

int
Hook_fop_open(
	struct inode* inodep,
	struct file* filp
	);

int
Hook_fop_release(
	struct inode* inodep,
	struct file* filp
	);

ssize_t
Hook_fop_read(
	struct file* filp,
	char __user* buffer_u,
	size_t size,
	loff_t *offset
	);

ssize_t
Hook_fop_write(
	struct file* filp,
	const char __user* buffer_u,
	size_t size,
	loff_t* offset
	);

long
Hook_fop_unlocked_ioctl(
	struct file* filp,
	unsigned int cmd,
	unsigned long arg
	);

long
Hook_fop_compat_ioctl(
	struct file* filp,
	unsigned int cmd,
	unsigned long arg
	);

bool
Hook_p_hasConnections(
	Hook* self,
	struct inode* inodep
	);

long
Hook_p_postProcessIoctl(
	Hook* self,
	struct file* filp,
	unsigned int ioctlCode,
	unsigned long arg,
	long result,
	uint16_t notifyCode
	);

void
Hook_p_notify(
	Hook* self,
	struct file* filp,
	uint16_t code,
	int result,
	MemBlock* paramBlockArray,
	size_t paramBlockCount
	);

//..............................................................................
