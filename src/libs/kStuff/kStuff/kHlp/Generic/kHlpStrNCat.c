/* $Id: kHlpStrNCat.c 29 2009-07-01 20:30:29Z bird $ */
/** @file
 * kHlpString - kHlpStrNCat.
 */

/*
 * Copyright (c) 2006-2007 Knut St. Osmundsen <bird-kStuff-spamix@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <k/kHlpString.h>


KHLP_DECL(char *) kHlpStrNCat(char *psz1, const char *psz2, KSIZE cb)
{
    char ch;
    char *pszDst = psz1;

    while (*pszDst != '\0')
        pszDst++;
    while (cb-- > 0)
    {
        ch = *psz2++;
        if (!ch)
            break;
        *pszDst++ = ch;
    }
    *pszDst = '\0';

    return psz1;
}

