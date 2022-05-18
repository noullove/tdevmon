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
typedef enum dm_MiMsgCode       dm_MiMsgCode;
typedef struct dm_MiMsgHdr      dm_MiMsgHdr;
typedef enum dm_MiStartFlag     dm_MiStartFlag
typedef struct dm_MiStartParams dm_MiStartParams;
#endif

//..............................................................................

enum dm_MiMsgCode
{
	dm_MiMsgCode_Error,
	dm_MiMsgCode_Start,
	dm_MiMsgCode_Notification,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct dm_MiMsgHdr
{
	uint32_t m_code;
	uint32_t m_paramSize;
};

//..............................................................................

enum dm_MiStartFlag
{
	dm_MiStartFlag_Windows = 0x0001,
	dm_MiStartFlag_Posix   = 0x0002,
	dm_MiStartFlag_Linux   = 0x0004,
	dm_MiStartFlag_Bsd     = 0x0008,
	dm_MiStartFlag_Darwin  = 0x0010,

	dm_MiStartFlag_X86     = 0x0100,
	dm_MiStartFlag_Amd64   = 0x0200,
	dm_MiStartFlag_Arm32   = 0x0400,
	dm_MiStartFlag_Arm64   = 0x0800,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct dm_MiStartParams
{
	uint32_t m_version;
	uint32_t m_flags;

	// followed by device name
	// on linux, it may be followed by module name (if detected successfully)
};

//..............................................................................
