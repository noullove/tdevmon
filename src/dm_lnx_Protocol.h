//..............................................................................
//
//  This file is part of Tibbo Device Monitor.
//  Copyright (c) 2014-2017, Tibbo Technology Inc
//  Author: Vladimir Gladkov
//
//  For legal details see accompanying license.txt file,
//  the public copy of which is also available at:
//  http://tibbo.com/downloads/archive/tdevmon/license.txt
//
//..............................................................................

#pragma once

#ifndef __cplusplus
typedef struct dm_String                dm_String;
typedef struct dm_List                  dm_List;
typedef struct dm_HookInfo              dm_HookInfo;
typedef struct dm_ConnectParams_v0302xx dm_ConnectParams_v0302xx;
typedef enum dm_ReadMode                dm_ReadMode;
typedef enum dm_IoctlFlag               dm_IoctlFlag;
typedef struct dm_IoctlDesc             dm_IoctlDesc;
typedef struct dm_IoctlDesc_v0302xx     dm_IoctlDesc_v0302xx;

typedef enum dm_NotifyCode              dm_NotifyCode;
typedef struct dm_NotifyHdr             dm_NotifyHdr;
typedef struct dm_OpenNotifyParams      dm_OpenNotifyParams;
typedef struct dm_CloseNotifyParams     dm_CloseNotifyParams;
typedef struct dm_ReadWriteNotifyParams dm_ReadWriteNotifyParams;
typedef struct dm_IoctlNotifyParams     dm_IoctlNotifyParams;
typedef union dm_NotifyParams           dm_NotifyParams;
typedef union dm_NotifyParamsPtr        dm_NotifyParamsPtr;
#endif

//..............................................................................

enum
{
	dm_ConnectionCountLimit      = 16,              // no more than 16 connections to a device
	dm_DefPendingNotifySizeLimit = 1 * 1024 * 1024, // drop notifications if application is not fast enough to pick'em up
	dm_NotifyHdrSignature        = 't' | 'm' << 8 | 'o' << 16 | 'n' << 24, // tmon
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

#define DM_DRIVER_FILE_NAME  "tdevmon.ko"
#define DM_DEVICE_CLASS_NAME "tdevmon"
#define DM_DEVICE_NAME       "tdevmon"

//..............................................................................

#define DM_CTL_CODE_EX(code, method) \
	CTL_CODE(dm_DeviceType, 0x800 + code, method, FILE_ANY_ACCESS)

#define DM_CTL_CODE(code) \
	DM_CTL_CODE_EX(code, METHOD_BUFFERED)

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

#define DM_IOCTL_MAGIC 'm'

#define DM_IOCTL_GET_VERSION          _IOR  (DM_IOCTL_MAGIC, 1, uint32_t)
#define DM_IOCTL_GET_DESCRIPTION      _IOWR(DM_IOCTL_MAGIC, 2, dm_String)
#define DM_IOCTL_GET_BUILD_TIME       _IOWR(DM_IOCTL_MAGIC, 3, dm_String)
#define DM_IOCTL_GET_HOOK_INFO_LIST   _IOWR(DM_IOCTL_MAGIC, 4, dm_List)
#define DM_IOCTL_GET_TARGET_HOOK_INFO _IOWR(DM_IOCTL_MAGIC, 5, dm_HookInfo)
#define DM_IOCTL_IS_CONNECTED         _IOR  (DM_IOCTL_MAGIC, 6, int)
#define DM_IOCTL_CONNECT_V0302XX      _IOW  (DM_IOCTL_MAGIC, 7, dm_ConnectParams_v0302xx)
#define DM_IOCTL_DISCONNECT           _IO   (DM_IOCTL_MAGIC, 8)
#define DM_IOCTL_UNHOOK               _IOW  (DM_IOCTL_MAGIC, 9, dm_String)
#define DM_IOCTL_STOP                 _IO   (DM_IOCTL_MAGIC, 10)
#define DM_IOCTL_CONNECT              _IOW  (DM_IOCTL_MAGIC, 11, dm_String)
#define DM_IOCTL_IS_ENABLED           _IOR  (DM_IOCTL_MAGIC, 12, int)
#define DM_IOCTL_ENABLE               _IO   (DM_IOCTL_MAGIC, 13)
#define DM_IOCTL_DISABLE              _IO   (DM_IOCTL_MAGIC, 14)
#define DM_IOCTL_GET_READ_MODE        _IOR  (DM_IOCTL_MAGIC, 15, int)
#define DM_IOCTL_SET_READ_MODE        _IO   (DM_IOCTL_MAGIC, 16)
#define DM_IOCTL_GET_FILE_NAME_FILTER _IOR  (DM_IOCTL_MAGIC, 17, dm_String)
#define DM_IOCTL_SET_FILE_NAME_FILTER _IOW  (DM_IOCTL_MAGIC, 18, dm_String)
#define DM_IOCTL_GET_IOCTL_DESC_TABLE _IOR  (DM_IOCTL_MAGIC, 19, dm_List)
#define DM_IOCTL_SET_IOCTL_DESC_TABLE _IOW  (DM_IOCTL_MAGIC, 20, dm_List)
#define DM_IOCTL_GET_PENDING_NOTIFY_SIZE_LIMIT _IOR(DM_IOCTL_MAGIC, 21, uint32_t)
#define DM_IOCTL_SET_PENDING_NOTIFY_SIZE_LIMIT _IO  (DM_IOCTL_MAGIC, 22)

//..............................................................................

struct dm_String
{
	uint32_t m_bufferSize; // when reading, this field is in/out (others are all out)
	uint32_t m_length;

	// followed by actual string chars (m_length + null)
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct dm_List
{
	uint32_t m_bufferSize; // when readng, this field is in/out (others are all out)
	uint32_t m_elementCount;
	uint32_t m_dataSize;

	// followed by T [m_elementCount] (m_dataSize total bytes)
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

enum dm_DeviceFlag
{
	dm_DeviceFlag_Char  = 0x01,
	dm_DeviceFlag_Block = 0x02,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct dm_HookInfo
{
	uint32_t m_bufferSize; // in, out (other fields are all out)
	uint32_t m_connectionCount;
	uint32_t m_moduleNameLength;
	uint32_t m_fileNameLength;

	// followed by a module name (m_moduleNameLength + null)
	// followed by a file name used during hooking (m_fileNameLength + null)
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct dm_ConnectParams_v0302xx
{
	uint32_t m_pendingNotifySizeLimit; // 0 -- use dm_DefPendingNotifySizeLimit
	uint32_t m_ioctlDescCount;
	uint32_t m_fileNameLength;

	// followed by a ioctl descriptor table (dm_IoctlDesc x m_ioctlDescCount)
	// followed by a file name (m_fileNameLength + null)
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

enum dm_ReadMode
{
	dm_ReadMode_Undefined = 0,
	dm_ReadMode_Stream,
	dm_ReadMode_Message,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

enum dm_IoctlFlag
{
	dm_IoctlFlag_HasArgSizeField       = 0x01,
	dm_IoctlFlag_ArgSizeField8         = 0x02,
	dm_IoctlFlag_ArgSizeField16        = 0x04, // else 32-bit size
	dm_IoctlFlag_ArgSizeFieldBigEndian = 0x08,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct dm_IoctlDesc_v0302xx
{
	uint32_t m_code;
	uint32_t m_flags;
	uint32_t m_argFixedSize;
	uint32_t m_argSizeFieldOffset;
};

struct dm_IoctlDesc // more convenient to define tables with curly initializers
{
	uint32_t m_code;
	uint32_t m_argFixedSize;
	uint32_t m_argSizeFieldOffset;
	uint32_t m_flags;
};

//..............................................................................

enum dm_NotifyCode
{
	dm_NotifyCode_Undefined = 0,
	dm_NotifyCode_Open,
	dm_NotifyCode_Close,
	dm_NotifyCode_Read,
	dm_NotifyCode_Write,
	dm_NotifyCode_UnlockedIoctl,
	dm_NotifyCode_CompatIoctl,
	dm_NotifyCode_DataDropped,
	dm_NotifyCode__Count,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

enum dm_NotifyFlag
{
	dm_NotifyFlag_InsufficientBuffer = 0x01, // buffer is not big enough, resize and try again (dm_ReadMode_Message)
	dm_NotifyFlag_DataDropped        = 0x02, // one or more notifications after this one were dropped
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct dm_NotifyHdr
{
	uint32_t m_signature;
	uint16_t m_code;
	uint16_t m_flags;
	uint32_t m_result;
	uint32_t m_paramSize;
	uint32_t m_pid;
	uint32_t m_tid;
	uint64_t m_timestamp;

	// followed by params
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct dm_OpenNotifyParams
{
	uint64_t m_fileId;
	uint32_t m_flags;
	uint32_t m_mode;
	uint32_t m_fileNameLength;
	uint32_t _m_padding;

	// followed by file name
};

struct dm_CloseNotifyParams
{
	uint64_t m_fileId;
};

struct dm_ReadWriteNotifyParams
{
	uint64_t m_fileId;
	uint64_t m_offset;
	uint32_t m_bufferSize;
	uint32_t m_dataSize;

	// followed by read/write data
};

struct dm_IoctlNotifyParams
{
	uint64_t m_fileId;
	uint32_t m_code;
	uint32_t m_argSize;
	uint64_t m_arg;

	// followed by argument data
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

union dm_NotifyParams
{
	dm_OpenNotifyParams m_openParams;
	dm_CloseNotifyParams m_closeParams;
	dm_ReadWriteNotifyParams m_readWriteParams;
	dm_IoctlNotifyParams m_ioctlParams;
};

union dm_NotifyParamsPtr
{
	void* m_params;
	dm_OpenNotifyParams* m_openParams;
	dm_CloseNotifyParams* m_closeParams;
	dm_ReadWriteNotifyParams* m_readWriteParams;
	dm_IoctlNotifyParams* m_ioctlParams;
};

//..............................................................................
