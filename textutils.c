/* 
 * textutils.c -- byte-oriented character and string routines.
 *
 * Copyright (C) 2012 Jason Linehan 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, 
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>


/**
 * bwipe -- given a character buffer, set the contents to '\0'
 * @str : pointer to a character buffer
 * @len : size of the character buffer
 */
void bwipe(char *str)
{
        memset(str, '\0', strlen(str));
}


/**
 * bdup -- copy *str to a newly-alloc'd buffer, and return a pointer to it 
 *
 * @str: pointer to a '\0'-terminated char string
 *  RET: pointer to a copy of *str, else NULL.
 */
char *bdup(const char *str)
{
        char *copy;
        size_t len;

        len  = strlen(str) + 1;
        copy = malloc(len);

        return copy ? memcpy(copy, str, len) : NULL;
}


/**
 * match -- locate first occurance of string 'needle' in string 'haystack'
 * @haystack: the string being searched for a match
 * @needle  : the pattern being matched in 'haystack'
 */
char *match(const char *haystack, const char *needle)
{
        size_t len_haystack;
        size_t len_needle;

        if (!needle || !haystack)
                return NULL;

        len_haystack = strlen(haystack);
        len_needle   = strlen(needle);

        /* Needle can't be larger than haystack */
        if (len_needle > len_haystack)
                return NULL;

        return memmem(haystack, len_haystack, needle, len_needle);
}


/**
 * field -- return pointer to a delimited substring (not including delimiter)
 * @str  : the string being matched against
 * @delim: the delimiter to be searched for
 */
char *field(const char *string, const char *delimiter)
{
        size_t offset;
        char *frame;

        if (!string || !delimiter) 
                return NULL;

        if (frame = match(string, delimiter), !frame)
                return NULL;

        offset = strlen(delimiter);

        return &frame[offset];
}


/**
 * pumpf -- write a formatted character string into an auto-allocated buffer
 * @strp : pointer to a character buffer (will be allocated)
 * @fmt  : format string
 * @...  : format string arguments
 * @ret  : length of the formatted string at *strp
 */
void pumpf(char **strp, const char *fmt, ...) 
{
        va_list args;
        size_t len;
        FILE *stream;

        /* Open a new FILE stream. *strp will be dynamically allocated to
         * contain characters written to the stream, and len will reflect
         * these changes. See man(3) open_memstream. */
        stream = open_memstream(strp, &len);

        if (!stream)
        /* Unable to open FILE stream */
                return;

        /* Write formatted output to stream */
        va_start(args, fmt);
        vfprintf(stream, fmt, args);
        va_end(args);

        fflush(stream);
        fclose(stream);
}       

