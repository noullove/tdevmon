#include "pch.h"
#include "stringUtils.h"

//..............................................................................

static
char*
createEmptyString(gfp_t kmallocFlags)
{
	char* string;
	string = kmalloc(sizeof(char), kmallocFlags);
	if (!string)
		return ERR_PTR(-ENOMEM);

	string[0] = 0;
	return string;
}

char*
createDuplicateString(
	const char* string,
	gfp_t kmallocFlags
	)
{
	size_t length;
	size_t size;
	char* stringCopy;

	if (!string)
		return createEmptyString(kmallocFlags);

	length = strlen(string);
	size = length + 1;

	stringCopy = kmalloc(size, kmallocFlags);
	if (!stringCopy)
		return ERR_PTR(-ENOMEM);

	memcpy(stringCopy, string, size);
	return stringCopy;
}

char*
createLowerCaseString(
	const char* string,
	gfp_t kmallocFlags
	)
{
	size_t i;
	size_t length;
	char* stringCopy;
	char c;

	if (!string)
		return createEmptyString(kmallocFlags);

	length = strlen(string);

	stringCopy = kmalloc(length + 1, kmallocFlags);
	if (!stringCopy)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < length; i++)
	{
		c = string[i];
		stringCopy[i] = (char)tolower(c);
	}

	stringCopy[length] = 0;
	return stringCopy;
}

size_t
convertStringToLowerCase(char* string)
{
	size_t i;
	char c;

	for (i = 0;; i++)
	{
		c = string[i];
		if (!c)
			break;

		string[i] = (char)tolower(c);
	}

	return i;
}

// based on:
// http://www.drdobbs.com/architecture-and-design/matching-wildcards-an-empirical-way-to-t/240169123

bool
wildcardCompareStringLowerCase(
	const char* string0,
	const char* wildcard
	)
{
	const char* string = string0;
	const char* stringBookmark = NULL;
	const char* wildcardBookmark = NULL;
	char c;

	for (;;)
	{
		if (*wildcard == '*')
		{
			while (*(++wildcard) == '*')
				;

			if (!*wildcard)
				return true;

			if (*wildcard == '\r') // rewind and check the next wildcard
			{
				string = string0;
				stringBookmark = NULL;
				wildcardBookmark = NULL;
				wildcard++;
				continue;
			}

			if (*wildcard != '?')
			{
				while ((char)tolower(*string) != *wildcard)
				{
					if (!(*(++string)))
						return false;
				}
			}

			wildcardBookmark = wildcard;
			stringBookmark = string;
		}
		else
		{
			c = (char)tolower(*string);

			if (c != *wildcard && *wildcard != '?')
			{
				if (wildcardBookmark)
				{
					if (wildcard != wildcardBookmark)
					{
						wildcard = wildcardBookmark;

						if (c != *wildcard)
						{
							string = ++stringBookmark;
							continue;
						}
						else
						{
							wildcard++;
						}
					}

					if (*string)
					{
						string++;
						continue;
					}
				}

				return false;
			}
		}

		string++;
		wildcard++;

		if (!*string)
		{
			while (*wildcard == '*')
				wildcard++;

			if (!*wildcard)
				return true;

			if (*wildcard == '\r') // rewind and check the next wildcard
			{
				string = string0;
				stringBookmark = NULL;
				wildcardBookmark = NULL;
				wildcard++;
				continue;
			}

			return false;
		}
	}
}

//..............................................................................
