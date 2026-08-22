#pragma once

#define MAX_TRACE_STRLEN 512
#define _STR(x)          #x
#define STR(x)           _STR(x)

extern int trace_marker_fd;

bool ftrace_init(void);
