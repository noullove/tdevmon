#pragma once

#define _DM_NO_FSRTL

#include "HashTable.h"
#include "stringUtils.h"

typedef enum FileNameFilterReq FileNameFilterReq;
typedef struct FileNameFilter  FileNameFilter;

//..............................................................................

enum FileNameFilterReq
{
	FileNameFilterReq_Open,
	FileNameFilterReq_OpenError,
	FileNameFilterReq_Close,
	FileNameFilterReq_Other,
};

//..............................................................................

struct FileNameFilter
{
	char* m_fileNameWildcard;
	HashTable m_fileSet;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

int
FileNameFilter_create(
	FileNameFilter** filter,
	const char* fileNameWildcard,
	gfp_t kmallocFlags // = GFP_KERNEL
	);

void
FileNameFilter_delete(FileNameFilter* self);

bool
FileNameFilter_checkFile(
	FileNameFilter* self,
	FileNameFilterReq req,
	struct file* filp,
	const char* fileName
	);

//..............................................................................
