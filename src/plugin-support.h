// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <util/base.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char *PLUGIN_NAME;
extern const char *PLUGIN_VERSION;

// Logs a message prefixed with the plugin name, at one of the LOG_* levels from util/base.h.
void obs_log(int log_level, const char *format, ...);

#ifdef __cplusplus
}
#endif
