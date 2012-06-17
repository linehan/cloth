#ifndef __TEXTUTILS_H
#define __TEXTUTILS_H

void bwipe(char *str);
char *bdup(const char *str);
char *match(const char *haystack, const char *needle);
char *field(const char *string, const char *delimiter);
void pumpf(char **strp, const char *fmt, ...);

#endif
