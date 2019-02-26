#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "shared.h"


char *jf_concat(size_t n, ...)
{
	char *buf;
	size_t *argv_len;
	size_t len = 0;
	size_t i;
	va_list ap;

	if ((argv_len = (size_t *)malloc(sizeof(size_t)* n)) == NULL) {
		return (char *)NULL;
	}
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		argv_len[i] = strlen(va_arg(ap, char*));
		len += argv_len[i];
	}
	va_end(ap);

	if ((buf = (char *)malloc(len + 1)) == NULL) {
		free(argv_len);
		return (char *)NULL;
	}
	len = 0;
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		memcpy(buf + len, va_arg(ap, char*), argv_len[i]);
		len += argv_len[i];
	}
	buf[len] = '\0';
	va_end(ap);
	
	free(argv_len);
	return buf;
}

