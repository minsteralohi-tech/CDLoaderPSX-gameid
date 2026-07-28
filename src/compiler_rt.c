#include <stddef.h>

/*
 * Freestanding builds can still cause GCC to emit implicit calls to memset
 * (e.g. when zero-initialising local arrays or structs). memcpy/strlen/etc. are
 * provided by the BIOS thunks in bioscalls.S, but memset is not, so we supply
 * it (and memmove) here.
 */

void * memset(void * dst, int c, size_t n)
{
	unsigned char * d = (unsigned char *) dst;
	while (n--) {
		*d++ = (unsigned char) c;
	}
	return dst;
}

void * memmove(void * dst, const void * src, size_t n)
{
	unsigned char * d = (unsigned char *) dst;
	const unsigned char * s = (const unsigned char *) src;
	if (d < s) {
		while (n--) {
			*d++ = *s++;
		}
	} else {
		d += n;
		s += n;
		while (n--) {
			*--d = *--s;
		}
	}
	return dst;
}
