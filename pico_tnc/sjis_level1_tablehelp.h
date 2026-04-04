#ifndef SJIS_LEVEL1_TABLE_HELP_H
#define SJIS_LEVEL1_TABLE_HELP_H

#include <stdint.h>

extern const char *const sjis_help_lines_ja_utf8[];

int sjis_level1_utf8_to_sjis_line(const char *utf8, uint8_t *sjis_buf, int sjis_buf_len);

#endif
