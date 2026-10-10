#ifndef FAKE_DOS_H
#define FAKE_DOS_H
/* stand-ins for MS-DOS headers (HydraCom on Linux, a test reference) */
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
static char *strupr(char *s) { char *p = s; while (*p) { *p = toupper((unsigned char)*p); p++; } return s; }
static char *strlwr(char *s) { char *p = s; while (*p) { *p = tolower((unsigned char)*p); p++; } return s; }
#define strnicmp strncasecmp
#define stricmp  strcasecmp
#endif
