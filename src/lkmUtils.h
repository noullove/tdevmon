#pragma once

#include "dm_lnx_Protocol.h"

//..............................................................................

#ifdef CONFIG_X86
static
inline
size_t
getWriteProtectionBackupSize(
	const void* begin,
	const void* end
	)
{
	return sizeof(ulong);
}
#else
size_t
getWriteProtectionBackupSize(
	const void* begin,
	const void* end
	);
#endif

void
disablePreemptionAndWriteProtection(
	const void* begin,
	const void* end,
	void* backup,
	size_t backupSize
	);

void
restoreWriteProtectionAndPreemption(
	const void* begin,
	const void* end,
	const void* backup,
	size_t backupSize
	);

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

char*
createPathString(const struct path* path);

char*
copyStringFromUser(const dm_String __user* string_u);

int
copyStringToUser(
	dm_String __user* string_u,
	const char* p
	);

static
inline
long
atomicInc(volatile long* p)
{
	return __sync_add_and_fetch(p, 1);
}

static
inline
long
atomicDec(volatile long* p)
{
	return __sync_sub_and_fetch(p, 1);
}

uint64_t
getTimestamp(void);

struct module*
getOwnerModule(struct file* filp);

//..............................................................................
