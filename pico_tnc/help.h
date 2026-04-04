#ifndef HELP_H
#define HELP_H

#include <stdbool.h>
#include <stdint.h>

#include "tty.h"

bool cmd_help(tty_t *ttyp, uint8_t *buf, int len);

#endif
