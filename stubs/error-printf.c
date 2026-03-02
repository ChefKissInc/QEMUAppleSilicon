#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "monitor/monitor.h"

int error_vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stderr, fmt, ap);
}

int error_vprintf_unless_qmp(const char *fmt, va_list ap)
{
    return error_vprintf(fmt, ap);
}
