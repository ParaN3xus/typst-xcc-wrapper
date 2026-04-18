#pragma once

#include <stddef.h>

void typst_wasi_reset_diagnostics(void);
const char *typst_wasi_diagnostics(size_t *len);
