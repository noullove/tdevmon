#pragma once

#ifdef DEBUG
#	define VERSION_DEBUG_SUFFIX " Debug"
#else
#	define VERSION_DEBUG_SUFFIX ""
#endif

#define VERSION_MAJOR      3
#define VERSION_MINOR      3
#define VERSION_REVISION   6
#define VERSION_FULL       (VERSION_REVISION | (VERSION_MINOR << 8) | (VERSION_MAJOR << 16))
#define VERSION_TAG        ""
#define VERSION_TAG_SUFFIX ""
#define VERSION_CPU_STRING "amd64"
#define VERSION_STRING     "3.3.6 (amd64" VERSION_DEBUG_SUFFIX ")"
#define VERSION_COMPANY    "Tibbo Technology Inc"
#define VERSION_YEARS      "2007-2020"
