#ifndef WCHAR_H
#define WCHAR_H

#include "dqn.h"

FILE_SCOPE inline i32 wchar_strlen(const wchar_t *const a)
{
	i32 result = 0;
	while (a && a[result]) result++;
	return result;
}

FILE_SCOPE inline i32 wchar_strlen_delimit_with(const wchar_t *a,
                                                const wchar_t delimiter)
{
	i32 result = 0;
	while (a && a[result] && a[result] != delimiter) result++;
	return result;
}

FILE_SCOPE inline bool wchar_is_digit(const wchar_t c)
{
	if (c >= L'0' && c <= L'9') return true;
	return false;
}

FILE_SCOPE inline i32 wchar_strcmp(const wchar_t *a, const wchar_t *b)
{
	if (!a && !b) return -1;
	if (!a) return -1;
	if (!b) return -1;

	while ((*a) == (*b))
	{
		if (!(*a)) return 0;
		a++;
		b++;
	}

	return (((*a) < (*b)) ? -1 : 1);
}

FILE_SCOPE inline wchar_t wchar_to_lower(const wchar_t a)
{
	if (a >= L'A' && a <= L'Z')
	{
		i32 shiftOffset = L'a' - L'A';
		return (a + (wchar_t)shiftOffset);
	}

	return a;
}

FILE_SCOPE bool wchar_has_substring(const wchar_t *const a, const i32 lenA,
                                    const wchar_t *const b, const i32 lenB)
{
	if (!a || !b) return false;
	if (lenA == 0 || lenB == 0) return false;

	const wchar_t *longStr, *shortStr;
	i32 longLen, shortLen;
	if (lenA > lenB)
	{
		longStr  = a;
		longLen  = lenA;

		shortStr = b;
		shortLen = lenB;
	}
	else
	{
		longStr  = b;
		longLen  = lenB;

		shortStr = a;
		shortLen = lenA;
	}

	bool matchedSubstr = false;
	for (i32 indexIntoLong = 0; indexIntoLong < longLen && !matchedSubstr;
	     indexIntoLong++)
	{
		// NOTE: As we scan through, if the longer string we index into becomes
		// shorter than the substring we're checking then the substring is not
		// contained in the long string.
		i32 remainingLenInLongStr = longLen - indexIntoLong;
		if (remainingLenInLongStr < shortLen) break;

		const wchar_t *longSubstr = &longStr[indexIntoLong];
		i32 index = 0;
		for (;;)
		{
			if (wchar_to_lower(longSubstr[index]) ==
			    wchar_to_lower(shortStr[index]))
			{
				index++;
				if (index >= shortLen || !shortStr[index])
				{
					matchedSubstr = true;
					break;
				}
			}
			else
			{
				break;
			}
		}
	}

	return matchedSubstr;
}

FILE_SCOPE inline void wchar_str_to_lower(wchar_t *const a, const i32 len)
{
	for (i32 i = 0; i < len; i++)
		a[i]   = wchar_to_lower(a[i]);
}

DQN_FILE_SCOPE bool wchar_str_reverse(wchar_t *buf, const i32 bufSize)
{
	if (!buf) return false;
	i32 mid = bufSize / 2;

	for (i32 i = 0; i < mid; i++)
	{
		wchar_t tmp            = buf[i];
		buf[i]                 = buf[(bufSize - 1) - i];
		buf[(bufSize - 1) - i] = tmp;
	}

	return true;
}

DQN_FILE_SCOPE i32 wchar_i32_to_str(i32 value, wchar_t *buf, i32 bufSize)
{
	if (!buf || bufSize == 0) return 0;

	if (value == 0)
	{
		buf[0] = L'0';
		return 0;
	}
	
	// NOTE(doyle): Max 32bit integer (+-)2147483647
	i32 charIndex = 0;
	bool negative           = false;
	if (value < 0) negative = true;

	if (negative) buf[charIndex++] = L'-';

	i32 val = DQN_ABS(value);
	while (val != 0 && charIndex < bufSize)
	{
		i32 rem          = val % 10;
		buf[charIndex++] = (u8)rem + '0';
		val /= 10;
	}

	// NOTE(doyle): If string is negative, we only want to reverse starting
	// from the second character, so we don't put the negative sign at the end
	if (negative)
	{
		wchar_str_reverse(buf + 1, charIndex - 1);
	}
	else
	{
		wchar_str_reverse(buf, charIndex);
	}

	return charIndex;
}

FILE_SCOPE i32 wchar_str_to_i32(const wchar_t *const buf, const i32 bufSize)
{
	if (!buf || bufSize == 0) return 0;

	i32 index       = 0;
	bool isNegative = false;
	if (buf[index] == L'-' || buf[index] == L'+')
	{
		if (buf[index] == L'-') isNegative = true;
		index++;
	}
	else if (!wchar_is_digit(buf[index]))
	{
		return 0;
	}

	i32 result = 0;
	for (i32 i = index; i < bufSize; i++)
	{
		if (wchar_is_digit(buf[i]))
		{
			result *= 10;
			result += (buf[i] - L'0');
		}
		else
		{
			break;
		}
	}

	if (isNegative) result *= -1;

	return result;
}


#endif
