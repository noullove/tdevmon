#pragma once

#include "typedefs.h"

//..............................................................................

enum MemBlockFlag
{
	MemBlockFlag_UserBuffer = 0x01, // need to use copy_from_user ()
	MemBlockFlag_IovIter    = 0x02, // need to iterate over iov_iter
};

struct MemBlock
{
	const void* m_p;
	size_t m_size;
	uint m_flags;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

size_t
getScatterGatherSize(
	const MemBlock* blockArray,
	size_t blockCount
	);

ssize_t
copyScatterGather(
	void* p, // must be big enough
	const MemBlock* blockArray,
	size_t blockCount
	);

ssize_t
copyScatterGatherPartial(
	void* p,
	size_t size,
	MemBlock* blockArray,
	size_t blockCount,
	size_t* partialBlockIdx
	);

//..............................................................................
