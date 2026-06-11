// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file string.c
 * @brief Minimal freestanding string/stdlib functions.
 * @copyright 2026 Jakob Kastelic
 *
 * This bootloader links with -nostdlib, so the handful of <string.h> /
 * <stdlib.h> functions used by printf.c and cmd.c are provided here. The
 * compiler may also emit memset/memcpy for aggregate copies and zeroing.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

void *memset(void *dst, int c, size_t n)
{
   uint8_t *d = dst;
   while (n-- != 0U) {
      *d++ = (uint8_t)c;
   }
   return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
   uint8_t *d = dst;
   const uint8_t *s = src;
   while (n-- != 0U) {
      *d++ = *s++;
   }
   return dst;
}

size_t strlen(const char *s)
{
   const char *p = s;
   while (*p != '\0') {
      p++;
   }
   return (size_t)(p - s);
}

int strncmp(const char *a, const char *b, size_t n)
{
   while (n-- != 0U) {
      unsigned char ca = (unsigned char)*a++;
      unsigned char cb = (unsigned char)*b++;
      if (ca != cb) {
         return (int)ca - (int)cb;
      }
      if (ca == '\0') {
         break;
      }
   }
   return 0;
}

char *strchr(const char *s, int c)
{
   for (; *s != '\0'; s++) {
      if (*s == (char)c) {
         return (char *)s;
      }
   }
   return ((char)c == '\0') ? (char *)s : NULL;
}

char *strncpy(char *dst, const char *src, size_t n)
{
   size_t i = 0;
   for (; i < n && src[i] != '\0'; i++) {
      dst[i] = src[i];
   }
   for (; i < n; i++) {
      dst[i] = '\0';
   }
   return dst;
}

/* Minimal strtoul: leading space, optional sign, base 0/8/10/16. */
unsigned long strtoul(const char *nptr, char **endptr, int base)
{
   const char *p = nptr;
   unsigned long val = 0;

   while (*p == ' ' || *p == '\t') {
      p++;
   }
   if (*p == '+') {
      p++;
   }
   if ((base == 0 || base == 16) && p[0] == '0' &&
       (p[1] == 'x' || p[1] == 'X')) {
      p += 2;
      base = 16;
   } else if (base == 0 && p[0] == '0') {
      base = 8;
   } else if (base == 0) {
      base = 10;
   }

   for (;;) {
      unsigned int digit;
      char c = *p;
      if (c >= '0' && c <= '9') {
         digit = (unsigned int)(c - '0');
      } else if (c >= 'a' && c <= 'z') {
         digit = (unsigned int)(c - 'a') + 10U;
      } else if (c >= 'A' && c <= 'Z') {
         digit = (unsigned int)(c - 'A') + 10U;
      } else {
         break;
      }
      if (digit >= (unsigned int)base) {
         break;
      }
      val = val * (unsigned long)base + digit;
      p++;
   }

   if (endptr != NULL) {
      *endptr = (char *)p;
   }
   return val;
}
