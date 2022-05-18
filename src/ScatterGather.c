#include "pch.h"
#include "ScatterGather.h"

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

size_t
getScatterGatherSize(
	const MemBlock* blockArray,
	size_t blockCount
	)
{
	const MemBlock* block = blockArray;
	const MemBlock* blockEnd = block + blockCount;

	size_t size = 0;

	for (; block < blockEnd; block++)
		size += block->m_size;

	return size;
}

static
inline
int
copyMemBlock(
	void* dst,
	const void* src,
	size_t size,
	uint flags
	)
{
	if (flags & MemBlockFlag_UserBuffer)
		return copy_from_user(dst, src, size) == 0 ? 0 : -EFAULT;

	if (flags & MemBlockFlag_IovIter)
		return copy_from_iter(dst, size, (struct iov_iter*)src) == size ? 0 : -EFAULT;

	memcpy(dst, src, size);
	return 0;
}

ssize_t
copyScatterGather(
	void* p, // must be big enough
	const MemBlock* blockArray,
	size_t blockCount
	)
{
	int result;
	const MemBlock* block = blockArray;
	const MemBlock* blockEnd = block + blockCount;
	char* dst = p;

	for (; block < blockEnd; block++)
	{
		result = copyMemBlock(dst, block->m_p, block->m_size, block->m_flags);
		if (result != 0)
			return result;

		dst += block->m_size;
	}

	return dst - (char*)p;
}

ssize_t
copyScatterGatherPartial(
	void* p,
	size_t size,
	MemBlock* blockArray,
	size_t blockCount,
	size_t* partialBlockIdx
	)
{
	int result;
	MemBlock* block = blockArray;
	MemBlock* blockEnd = block + blockCount;
	char* dst = p;
	char* dstEnd = dst + size;

	*partialBlockIdx = blockCount; // assume everything fits

	for (; block < blockEnd; block++)
	{
		size_t leftover = dstEnd - dst;
		if (leftover < block->m_size) // doesn't fit, copy and update the partial block
		{
			result = copyMemBlock(dst, block->m_p, leftover, block->m_flags);
			if (result != 0)
				return result;

			block->m_p = (char*)block->m_p + leftover;
			block->m_size -= leftover;
			*partialBlockIdx = block - blockArray;

			dst += leftover;
			break;
		}

		result = copyMemBlock(dst, block->m_p, block->m_size, block->m_flags);
		if (result != 0)
			return result;

		dst += block->m_size;
	}

	return dst - (char*)p;
}

//..............................................................................
