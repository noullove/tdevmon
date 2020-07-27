#pragma once

//..............................................................................

char*
createDuplicateString(
	const char* string,
	gfp_t kmallocFlags // = GFP_KERNEL
	);

char*
createLowerCaseString(
	const char* string,
	gfp_t kmallocFlags // = GFP_KERNEL
	);

size_t
convertStringToLowerCase(char* string);

bool
wildcardCompareStringLowerCase(
	const char* string,
	const char* wildcard
	);

//..............................................................................
