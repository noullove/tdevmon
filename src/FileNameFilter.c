#include "pch.h"
#include "FileNameFilter.h"

//..............................................................................

int
FileNameFilter_create(
	FileNameFilter** resultFilter,
	const char* fileNameWildcard,
	gfp_t kmallocFlags
	)
{
	FileNameFilter* filter;
	char* cachedWildcard;

	ASSERT(fileNameWildcard && *fileNameWildcard);

	cachedWildcard = createLowerCaseString(fileNameWildcard, kmallocFlags);
	if (IS_ERR(cachedWildcard))
		return PTR_ERR(cachedWildcard);

	filter = kmalloc(sizeof(FileNameFilter), kmallocFlags);
	if (!filter)
	{
		kfree(cachedWildcard);
		return -ENOMEM;
	}

	filter->m_fileNameWildcard = cachedWildcard;
	HashTable_construct(&filter->m_fileSet, HashTableKeyType_Pointer, kmallocFlags);
	*resultFilter = filter;
	return 0;
}

void
FileNameFilter_delete(FileNameFilter* self)
{
	ASSERT(self->m_fileNameWildcard);

	HashTable_destruct(&self->m_fileSet);
	kfree(self->m_fileNameWildcard);
	kfree(self);
}

bool
FileNameFilter_checkFile(
	FileNameFilter* self,
	FileNameFilterReq req,
	struct file* filp,
	const char* fileName
	)
{
	bool isMatch;

	switch (req)
	{
	case FileNameFilterReq_Open:
		ASSERT(fileName);
		isMatch = wildcardCompareStringLowerCase(fileName, self->m_fileNameWildcard);
		if (isMatch)
			HashTable_visit(&self->m_fileSet, filp);
		break;

	case FileNameFilterReq_OpenError:
		ASSERT(fileName);
		isMatch = wildcardCompareStringLowerCase(fileName, self->m_fileNameWildcard);
		break;

	case FileNameFilterReq_Close:
		isMatch = HashTable_removeKey(&self->m_fileSet, filp);
		break;

	case FileNameFilterReq_Other:
		isMatch = HashTable_find(&self->m_fileSet, filp) != NULL;
		break;

	default:
		ASSERT(false);
		isMatch = false;
	}

	return isMatch;
}

//..............................................................................

