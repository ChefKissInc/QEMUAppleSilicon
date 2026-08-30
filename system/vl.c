/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qemu/help-texts.h"
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/target-info.h"
#include "exec/cpu-common.h"
#include "exec/page-vary.h"
#include "hw/qdev-properties.h"
#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qobject/qstring.h"
#include "qobject/qjson.h"
#include "qemu-version.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "qemu/hw-version.h"
#include "qemu/uuid.h"
#include "qemu/target-info.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/runstate-action.h"
#include "system/seccomp.h"
#include "system/tcg.h"

#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "qemu/accel.h"
#include "qemu/async-teardown.h"
#include "hw/usb.h"
#include "hw/sd/sd.h"
#include "monitor/qdev.h"
#include "net/net.h"
#include "net/slirp.h"
#include "monitor/monitor.h"
#include "ui/console.h"
#include "ui/input.h"
#include "system/system.h"
#include "system/hostmem.h"
#include "exec/gdbstub.h"
#include "gdbstub/enums.h"
#include "qemu/timer.h"
#include "chardev/char.h"
#include "qemu/bitmap.h"
#include "qemu/log.h"
#include "system/blockdev.h"
#include "hw/block/block.h"
#include "system/dma.h"
#include "audio/audio.h"
#include "system/cpus.h"
#include "system/cpu-timers.h"
#include "system/kvm.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/main-loop.h"

#include "trace.h"
#include "trace/control.h"
#include "qemu/queue.h"
#include "system/arch_init.h"

#include "qapi/string-input-visitor.h"
#include "qapi/opts-visitor.h"
#include "qapi/clone-visitor.h"
#include "qom/object_interfaces.h"
#include "crypto/init.h"
#include "qapi/qapi-events-run-state.h"
#include "qapi/qapi-types-audio.h"
#include "qapi/qapi-visit-audio.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qapi-visit-compat.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/qapi-visit-ui.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-visit-qom.h"
#include "qapi/qapi-commands-ui.h"
#include "block/qdict.h"
#include "qapi/qmp/qerror.h"
#include "system/iothread.h"
#include "qemu/guest-random.h"
#include "qemu/keyval.h"

typedef struct BlockdevOptionsQueueEntry
{
    BlockdevOptions* bdo;
    Location         loc;
    QSIMPLEQ_ENTRY(BlockdevOptionsQueueEntry) entry;
} BlockdevOptionsQueueEntry;

typedef QSIMPLEQ_HEAD(, BlockdevOptionsQueueEntry) BlockdevOptionsQueue;

typedef struct ObjectOption
{
    ObjectOptions* opts;
    QTAILQ_ENTRY(ObjectOption) next;
} ObjectOption;

typedef struct DeviceOption
{
    QDict*   opts;
    Location loc;
    QTAILQ_ENTRY(DeviceOption) next;
} DeviceOption;

static const char* cpu_option;
static const char* mem_path;
static const char* accelerators;
static bool        have_custom_ram_size;
static const char* ram_memdev_id;
static QDict*      machine_opts_dict;
static QTAILQ_HEAD(, ObjectOption) object_opts = QTAILQ_HEAD_INITIALIZER(object_opts);
static QTAILQ_HEAD(, DeviceOption) device_opts = QTAILQ_HEAD_INITIALIZER(device_opts);
static int                  display_remote;
static int                  snapshot;
static bool                 preconfig_requested;
static BlockdevOptionsQueue bdo_queue = QSIMPLEQ_HEAD_INITIALIZER(bdo_queue);
static bool                 nographic = false;
static int                  mem_prealloc; /* force preallocation of physical target memory */
static DisplayOptions       dpy;
static int                  num_serial_hds;
static Chardev**            serial_hds;
static const char*          log_mask;
static const char*          log_file;
static bool                 list_data_dirs;

static int has_defaults    = 1;
static int default_audio   = 1;
static int default_serial  = 1;
static int default_monitor = 1;
static int default_net     = 1;

static QemuOptsList qemu_rtc_opts = {
    .name        = "rtc",
    .head        = QTAILQ_HEAD_INITIALIZER(qemu_rtc_opts.head),
    .merge_lists = true,
    .desc        = {{
                        .name = "base",
                        .type = QEMU_OPT_STRING,
                    },
                    {
                        .name = "clock",
                        .type = QEMU_OPT_STRING,
                    },
                    {/* end of list */}},
};

static QemuOptsList qemu_accel_opts = {
    .name             = "accel",
    .implied_opt_name = "accel",
    .head             = QTAILQ_HEAD_INITIALIZER(qemu_accel_opts.head),
    .desc =
        {/*
          * no elements => accept any
          * sanity checking will happen later
          * when setting accelerator properties
          */
         {}},
};

static QemuOptsList qemu_add_fd_opts = {
    .name = "add-fd",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_add_fd_opts.head),
    .desc = {{
                 .name = "fd",
                 .type = QEMU_OPT_NUMBER,
                 .help = "file descriptor of which a duplicate is added to fd set",
             },
             {
                 .name = "set",
                 .type = QEMU_OPT_NUMBER,
                 .help = "ID of the fd set to add fd to",
             },
             {
                 .name = "opaque",
                 .type = QEMU_OPT_STRING,
                 .help = "free-form string used to describe fd",
             },
             {/* end of list */}},
};

static QemuOptsList qemu_object_opts = {
    .name             = "object",
    .implied_opt_name = "qom-type",
    .head             = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc             = {{}},
};

static QemuOptsList qemu_overcommit_opts = {
    .name = "overcommit",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_overcommit_opts.head),
    .desc = {{
                 .name = "mem-lock",
                 .type = QEMU_OPT_STRING,
             },
             {
                 .name = "cpu-pm",
                 .type = QEMU_OPT_BOOL,
             },
             {/* end of list */}},
};

static QemuOptsList qemu_msg_opts = {
    .name = "msg",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_msg_opts.head),
    .desc = {{
                 .name = "timestamp",
                 .type = QEMU_OPT_BOOL,
             },
             {
                 .name = "guest-name",
                 .type = QEMU_OPT_BOOL,
                 .help = "Prepends guest name for error messages but only if "
                         "-name guest is set otherwise option is ignored\n",
             },
             {/* end of list */}},
};

static QemuOptsList qemu_name_opts = {
    .name             = "name",
    .implied_opt_name = "guest",
    .merge_lists      = true,
    .head             = QTAILQ_HEAD_INITIALIZER(qemu_name_opts.head),
    .desc             = {{
                             .name = "guest",
                             .type = QEMU_OPT_STRING,
                             .help = "Sets the name of the guest.\n"
                                     "This name will be displayed in the SDL window caption.\n"
                                     "The name will also be used for the VNC server",
                         },
                         {
                             .name = "process",
                             .type = QEMU_OPT_STRING,
                             .help = "Sets the name of the QEMU process, as shown in top etc",
                         },
                         {
                             .name = "debug-threads",
                             .type = QEMU_OPT_BOOL,
                             .help = "When enabled, name the individual threads; defaults off.\n"
                                     "NOTE: The thread names are for debugging and not a\n"
                                     "stable API.",
                         },
                         {/* End of list */}},
};

static QemuOptsList qemu_mem_opts = {
    .name             = "memory",
    .implied_opt_name = "size",
    .head             = QTAILQ_HEAD_INITIALIZER(qemu_mem_opts.head),
    .merge_lists      = true,
    .desc             = {{
                             .name = "size",
                             .type = QEMU_OPT_SIZE,
                         },
                         {
                             .name = "slots",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "maxmem",
                             .type = QEMU_OPT_SIZE,
                         },
                         {/* end of list */}},
};

static QemuOptsList qemu_fw_cfg_opts = {
    .name             = "fw_cfg",
    .implied_opt_name = "name",
    .head             = QTAILQ_HEAD_INITIALIZER(qemu_fw_cfg_opts.head),
    .desc             = {{
                             .name = "name",
                             .type = QEMU_OPT_STRING,
                             .help = "Sets the fw_cfg name of the blob to be inserted",
                         },
                         {
                             .name = "file",
                             .type = QEMU_OPT_STRING,
                             .help = "Sets the name of the file from which "
                                     "the fw_cfg blob will be loaded",
                         },
                         {
                             .name = "string",
                             .type = QEMU_OPT_STRING,
                             .help = "Sets content of the blob to be inserted from a string",
                         },
                         {
                             .name = "gen_id",
                             .type = QEMU_OPT_STRING,
                             .help = "Sets id of the object generating the fw_cfg blob "
                                     "to be inserted",
                         },
                         {/* end of list */}},
};

static QemuOptsList qemu_action_opts = {
    .name        = "action",
    .merge_lists = true,
    .head        = QTAILQ_HEAD_INITIALIZER(qemu_action_opts.head),
    .desc        = {{
                        .name = "shutdown",
                        .type = QEMU_OPT_STRING,
                    },
                    {
                        .name = "reboot",
                        .type = QEMU_OPT_STRING,
                    },
                    {
                        .name = "panic",
                        .type = QEMU_OPT_STRING,
                    },
                    {
                        .name = "watchdog",
                        .type = QEMU_OPT_STRING,
                    },
                    {/* end of list */}},
};

const char* qemu_get_vm_name(void) { return qemu_name; }

static int parse_name(void* opaque, QemuOpts* opts, Error** errp)
{
    const char* proc_name;

    if (qemu_opt_get(opts, "debug-threads")) { qemu_thread_naming(qemu_opt_get_bool(opts, "debug-threads", false)); }
    qemu_name = qemu_opt_get(opts, "guest");

    proc_name = qemu_opt_get(opts, "process");
    if (proc_name) { os_set_proc_name(proc_name); }

    return 0;
}

#ifndef _WIN32
static int parse_add_fd(void* opaque, QemuOpts* opts, Error** errp)
{
    int         fd, dupfd, flags;
    int64_t     fdset_id;
    const char* fd_opaque = NULL;
    AddfdInfo*  fdinfo;

    fd        = qemu_opt_get_number(opts, "fd", -1);
    fdset_id  = qemu_opt_get_number(opts, "set", -1);
    fd_opaque = qemu_opt_get(opts, "opaque");

    if (fd < 0) {
        error_setg(errp, "fd option is required and must be non-negative");
        return -1;
    }

    if (fd <= STDERR_FILENO) {
        error_setg(errp, "fd cannot be a standard I/O stream");
        return -1;
    }

    /*
     * All fds inherited across exec() necessarily have FD_CLOEXEC
     * clear, while qemu sets FD_CLOEXEC on all other fds used internally.
     */
    flags = fcntl(fd, F_GETFD);
    if (flags == -1 || (flags & FD_CLOEXEC)) {
        error_setg(errp, "fd is not valid or already in use");
        return -1;
    }

    if (fdset_id < 0) {
        error_setg(errp, "set option is required and must be non-negative");
        return -1;
    }

    #ifdef F_DUPFD_CLOEXEC
    dupfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    #else
    dupfd = dup(fd);
    if (dupfd != -1) { qemu_set_cloexec(dupfd); }
    #endif
    if (dupfd == -1) {
        error_setg(errp, "error duplicating fd: %s", strerror(errno));
        return -1;
    }

    /* add the duplicate fd, and optionally the opaque string, to the fd set */
    fdinfo = monitor_fdset_add_fd(dupfd, true, fdset_id, fd_opaque, &error_abort);
    g_free(fdinfo);

    return 0;
}

static int cleanup_add_fd(void* opaque, QemuOpts* opts, Error** errp)
{
    int fd;

    fd = qemu_opt_get_number(opts, "fd", -1);
    close(fd);

    return 0;
}
#endif

/***********************************************************/
/* QEMU Block devices */

#define HD_OPTS     "media=disk"
#define CDROM_OPTS  "media=cdrom"
#define FD_OPTS     ""
#define PFLASH_OPTS ""
#define MTD_OPTS    ""
#define SD_OPTS     ""

static int drive_init_func(void* opaque, QemuOpts* opts, Error** errp)
{
    BlockInterfaceType* block_default_type = opaque;

    return drive_new(opts, *block_default_type, errp) == NULL;
}

static int drive_enable_snapshot(void* opaque, QemuOpts* opts, Error** errp)
{
    if (qemu_opt_get(opts, "snapshot") == NULL) { qemu_opt_set(opts, "snapshot", "on", &error_abort); }
    return 0;
}

static void configure_blockdev(BlockdevOptionsQueue* bdo_queue, MachineClass* machine_class, int snapshot)
{
    /*
     * If the currently selected machine wishes to override the
     * units-per-bus property of its default HBA interface type, do so
     * now.
     */
    if (machine_class->units_per_default_bus) {
        override_max_devs(machine_class->block_default_type, machine_class->units_per_default_bus);
    }

    /* open the virtual block devices */
    while (!QSIMPLEQ_EMPTY(bdo_queue)) {
        BlockdevOptionsQueueEntry* bdo = QSIMPLEQ_FIRST(bdo_queue);

        QSIMPLEQ_REMOVE_HEAD(bdo_queue, entry);
        loc_push_restore(&bdo->loc);
        qmp_blockdev_add(bdo->bdo, &error_fatal);
        loc_pop(&bdo->loc);
        qapi_free_BlockdevOptions(bdo->bdo);
        g_free(bdo);
    }
    if (snapshot) { qemu_opts_foreach(qemu_find_opts("drive"), drive_enable_snapshot, NULL, NULL); }
    if (qemu_opts_foreach(qemu_find_opts("drive"), drive_init_func, &machine_class->block_default_type, &error_fatal)) {
        /* We printed help */
        exit(0);
    }
}

static QemuOptsList qemu_smp_opts = {
    .name             = "smp-opts",
    .implied_opt_name = "cpus",
    .merge_lists      = true,
    .head             = QTAILQ_HEAD_INITIALIZER(qemu_smp_opts.head),
    .desc             = {{
                             .name = "cpus",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "drawers",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "books",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "sockets",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "dies",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "clusters",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "modules",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "cores",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "threads",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {
                             .name = "maxcpus",
                             .type = QEMU_OPT_NUMBER,
                         },
                         {/*End of list */}},
};

#if defined(CONFIG_POSIX) && !defined(EMSCRIPTEN)
static QemuOptsList qemu_run_with_opts = {
    .name = "run-with",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_run_with_opts.head),
    .desc =
        {
    #if defined(CONFIG_LINUX)
            {
                .name = "async-teardown",
                .type = QEMU_OPT_BOOL,
            },
    #endif
            {
                .name = "chroot",
                .type = QEMU_OPT_STRING,
            },
            {
                .name = "user",
                .type = QEMU_OPT_STRING,
            },
            {/* end of list */}},
};

    #define qemu_add_run_with_opts() qemu_add_opts(&qemu_run_with_opts)

#else

    #define qemu_add_run_with_opts()

#endif /* CONFIG_POSIX */

static void realtime_init(void)
{
    if (should_mlock(mlock_state)) {
        if (os_mlock(is_mlock_on_fault(mlock_state)) < 0) {
            error_report("locking memory failed");
            exit(1);
        }
    }
}

static void configure_msg(QemuOpts* opts)
{
    message_with_timestamp = qemu_opt_get_bool(opts, "timestamp", false);
    error_with_guestname   = qemu_opt_get_bool(opts, "guest-name", false);
}

/***********************************************************/
/* USB devices */

static bool usb_parse(const char* cmdline, Error** errp)
{
    assert(machine_usb(current_machine));

    if (!usbdevice_create(cmdline)) {
        error_setg(errp, "could not add USB device '%s'", cmdline);
        return false;
    }
    return true;
}

/***********************************************************/
/* machine registration */

static MachineClass* find_machine(const char* name, GSList* machines)
{
    GSList* el;

    for (el = machines; el; el = el->next) {
        MachineClass* mc = el->data;

        if (!strcmp(mc->name, name) || !g_strcmp0(mc->alias, name)) { return mc; }
    }

    return NULL;
}

static MachineClass* find_default_machine(GSList* machines)
{
    GSList*       el;
    MachineClass* default_machineclass = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass* mc = el->data;

        if (mc->is_default) {
            assert(default_machineclass == NULL && "Multiple default machines");
            default_machineclass = mc;
        }
    }

    return default_machineclass;
}

static void version(void)
{
    printf("QEMU emulator version " QEMU_FULL_VERSION "\n" QEMU_COPYRIGHT "\n\n"
           "ChefKiss Inferno\n"
           "Copyright (c) 2023-2026 Visual Ehrmanntraut and Inferno team\n\n");
}

static void help(int exitcode)
{
    version();
    printf("usage: %s [options] [disk_image]\n\n"
           "'disk_image' is a raw hard disk image for IDE hard disk 0\n\n",
           g_get_prgname());

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask)      \
    if (qemu_arch_available(arch_mask)) fputs(opt_help, stdout);

#define ARCHHEADING(text, arch_mask)                           \
    if (qemu_arch_available(arch_mask)) puts(stringify(text));

#define DEFHEADING(text) ARCHHEADING(text, QEMU_ARCH_ALL)

#include "qemu-options.def"

    printf("\nDuring emulation, the following keys are useful:\n"
           "ctrl-alt-f      toggle full screen\n"
           "ctrl-alt-n      switch to virtual console 'n'\n"
           "ctrl-alt-g      toggle mouse and keyboard grab\n"
           "\n"
           "When using -nographic, press 'ctrl-a h' to get some help.\n"
           "\n" QEMU_HELP_BOTTOM "\n");

    exit(exitcode);
}

enum
{

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask) opt_enum,
#define DEFHEADING(text)
#define ARCHHEADING(text, arch_mask)

#include "qemu-options.def"
};

#define HAS_ARG 0x0001

typedef struct QEMUOption
{
    const char* name;
    int         flags;
    int         index;
    uint32_t    arch_mask;
} QEMUOption;

static const QEMUOption qemu_options[] = {{"h", 0, QEMU_OPTION_h, QEMU_ARCH_ALL},

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask) {option, opt_arg, opt_enum, arch_mask},
#define DEFHEADING(text)
#define ARCHHEADING(text, arch_mask)

#include "qemu-options.def"
                                          {/* end of list */}};

static void parse_display_qapi(const char* str)
{
    DisplayOptions* opts;
    Visitor*        v;

    v = qobject_input_visitor_new_str(str, "type", &error_fatal);

    visit_type_DisplayOptions(v, NULL, &opts, &error_fatal);
    QAPI_CLONE_MEMBERS(DisplayOptions, &dpy, opts);

    qapi_free_DisplayOptions(opts);
    visit_free(v);
}

DisplayOptions* qmp_query_display_options(Error** errp) { return QAPI_CLONE(DisplayOptions, &dpy); }

static void parse_display(const char* p)
{
    if (is_help_option(p)) {
        qemu_display_help();
        exit(0);
    }

#ifdef CONFIG_VNC
    const char* opts;

    if (strstart(p, "vnc", &opts)) {
        /*
         * vnc isn't a (local) DisplayType but a protocol for remote
         * display access.
         */
        if (*opts == '=') {
            vnc_parse(opts + 1);
            display_remote++;
        }
        else {
            error_report("VNC requires a display argument vnc=<display>");
            exit(1);
        }
        return;
    }
#endif

    parse_display_qapi(p);
}

static int device_help_func(void* opaque, QemuOpts* opts, Error** errp) { return qdev_device_help(opts); }

static int device_init_func(void* opaque, QemuOpts* opts, Error** errp)
{
    DeviceState* dev;

    dev = qdev_device_add(opts, errp);
    if (!dev && *errp) {
        error_report_err(*errp);
        return -1;
    }
    else if (dev) {
        object_unref(OBJECT(dev));
    }
    return 0;
}

static int chardev_init_func(void* opaque, QemuOpts* opts, Error** errp)
{
    Error* local_err = NULL;

    if (!qemu_chr_new_from_opts(opts, NULL, &local_err)) {
        if (local_err) {
            error_propagate(errp, local_err);
            return -1;
        }
        exit(0);
    }
    return 0;
}

static int mon_init_func(void* opaque, QemuOpts* opts, Error** errp) { return monitor_init_opts(opts, errp); }

static void monitor_parse(const char* str, const char* mode, bool pretty)
{
    static int  monitor_device_index = 0;
    QemuOpts*   opts;
    const char* p;
    char        label[32];

    if (strstart(str, "chardev:", &p)) { snprintf(label, sizeof(label), "%s", p); }
    else {
        snprintf(label, sizeof(label), "compat_monitor%d", monitor_device_index);
        opts = qemu_chr_parse_compat(label, str, true);
        if (!opts) {
            error_report("parse error: %s", str);
            exit(1);
        }
    }

    opts = qemu_opts_create(qemu_find_opts("mon"), label, 1, &error_fatal);
    qemu_opt_set(opts, "mode", mode, &error_abort);
    qemu_opt_set(opts, "chardev", label, &error_abort);
    if (!strcmp(mode, "control")) { qemu_opt_set_bool(opts, "pretty", pretty, &error_abort); }
    else {
        assert(pretty == false);
    }
    monitor_device_index++;
}

struct device_config
{
    enum
    {
        DEV_USB,      /* -usbdevice     */
        DEV_SERIAL,   /* -serial        */
        DEV_DEBUGCON, /* -debugcon */
        DEV_GDB,      /* -gdb, -s */
    } type;
    const char* cmdline;
    Location    loc;
    QTAILQ_ENTRY(device_config) next;
};

static QTAILQ_HEAD(, device_config) device_configs = QTAILQ_HEAD_INITIALIZER(device_configs);

static void add_device_config(int type, const char* cmdline)
{
    struct device_config* conf;

    conf          = g_malloc0(sizeof(*conf));
    conf->type    = type;
    conf->cmdline = cmdline;
    loc_save(&conf->loc);
    QTAILQ_INSERT_TAIL(&device_configs, conf, next);
}

/**
 * foreach_device_config_or_exit(): process per-device configs
 * @type: device_config type
 * @func: device specific config function, returning pass/fail
 *
 * @func is called with the &error_fatal handler so device specific
 * error messages can be reported on failure.
 */
static void foreach_device_config_or_exit(int type, bool (*func)(const char* cmdline, Error** errp))
{
    struct device_config* conf;

    QTAILQ_FOREACH (conf, &device_configs, next) {
        if (conf->type != type) { continue; }
        loc_push_restore(&conf->loc);
        func(conf->cmdline, &error_fatal);
        loc_pop(&conf->loc);
    }
}

static void qemu_disable_default_devices(void)
{
    MachineClass* machine_class = MACHINE_GET_CLASS(current_machine);

    if (!has_defaults) {
        default_serial  = 0;
        default_audio   = 0;
        default_monitor = 0;
        default_net     = 0;
    }
    else {
        if (default_net && machine_class->default_nic && !module_object_class_by_name(machine_class->default_nic)) {
            warn_report("Default NIC '%s' is not available in this binary", machine_class->default_nic);
            default_net = 0;
        }
    }
}

static void qemu_setup_display(void)
{
    if (dpy.type == DISPLAY_TYPE_DEFAULT && !display_remote) {
        if (!qemu_display_find_default(&dpy)) {
            dpy.type = DISPLAY_TYPE_NONE;
#if defined(CONFIG_VNC)
            vnc_parse("localhost:0,to=99,id=default");
            display_remote++;
#endif
        }
    }
    if (dpy.type == DISPLAY_TYPE_DEFAULT) { dpy.type = DISPLAY_TYPE_NONE; }

    qemu_display_early_init(&dpy);
}

static void qemu_create_default_devices(void)
{
    const char* vc = qemu_display_get_vc(&dpy);

    if (is_daemonized()) {
        /* According to documentation and historically, -nographic redirects
         * serial port, parallel port and monitor to stdio, which does not work
         * with -daemonize.  We can redirect these to null instead, but since
         * -nographic is legacy, let's just error out.
         * We disallow -nographic only if all other ports are not redirected
         * explicitly, to not break existing legacy setups which uses
         * -nographic _and_ redirects all ports explicitly - this is valid
         * usage, -nographic is just a no-op in this case.
         */
        if (nographic && (default_serial || default_monitor)) {
            error_report("-nographic cannot be used with -daemonize");
            exit(1);
        }
    }

    if (nographic) {
        if (default_serial && default_monitor) { add_device_config(DEV_SERIAL, "mon:stdio"); }
        else {
            if (default_serial) { add_device_config(DEV_SERIAL, "stdio"); }
            if (default_monitor) { monitor_parse("stdio", "readline", false); }
        }
    }
    else {
        if (default_serial) { add_device_config(DEV_SERIAL, vc ?: "null"); }
        if (default_monitor && vc) { monitor_parse(vc, "readline", false); }
    }

    if (default_net) {
        QemuOptsList* net = qemu_find_opts("net");
        qemu_opts_parse(net, "nic", true, &error_abort);
#ifdef CONFIG_SLIRP
        qemu_opts_parse(net, "user", true, &error_abort);
#endif
    }
}

static bool serial_parse(const char* devname, Error** errp)
{
    int index = num_serial_hds;

    serial_hds = g_renew(Chardev*, serial_hds, index + 1);

    if (strcmp(devname, "none") == 0) {
        /* Don't allocate a serial device for this index */
        serial_hds[index] = NULL;
    }
    else {
        char label[32];
        snprintf(label, sizeof(label), "serial%d", index);

        serial_hds[index] = qemu_chr_new_mux_mon(label, devname, NULL);
        if (!serial_hds[index]) {
            error_setg(errp,
                       "could not connect serial device"
                       " to character backend '%s'",
                       devname);
            return false;
        }
    }
    num_serial_hds++;
    return true;
}

Chardev* serial_hd(int i)
{
    assert(i >= 0);
    if (i < num_serial_hds) { return serial_hds[i]; }
    return NULL;
}

static bool debugcon_parse(const char* devname, Error** errp)
{
    QemuOpts* opts;

    if (!qemu_chr_new_mux_mon("debugcon", devname, NULL)) {
        error_setg(errp, "invalid character backend '%s'", devname);
        return false;
    }
    opts = qemu_opts_create(qemu_find_opts("device"), "debugcon", 1, NULL);
    if (!opts) {
        error_setg(errp, "already have a debugcon device");
        return false;
    }
    qemu_opt_set(opts, "driver", "isa-debugcon", &error_abort);
    qemu_opt_set(opts, "chardev", "debugcon", &error_abort);
    return true;
}

static gint machine_class_cmp(gconstpointer a, gconstpointer b, gpointer d)
{
    const MachineClass *mc1 = a, *mc2 = b;
    int                 res;

    if (mc1->family == NULL) {
        if (mc2->family == NULL) {
            /* Compare standalone machine types against each other; they sort
             * in increasing order.
             */
            return strcmp(object_class_get_name(OBJECT_CLASS(mc1)), object_class_get_name(OBJECT_CLASS(mc2)));
        }

        /* Standalone machine types sort after families. */
        return 1;
    }

    if (mc2->family == NULL) {
        /* Families sort before standalone machine types. */
        return -1;
    }

    /* Families sort between each other alphabetically increasingly. */
    res = strcmp(mc1->family, mc2->family);
    if (res != 0) { return res; }

    /* Within the same family, machine types sort in decreasing order. */
    return strcmp(object_class_get_name(OBJECT_CLASS(mc2)), object_class_get_name(OBJECT_CLASS(mc1)));
}

static void machine_help_func(const QDict* qdict)
{
    g_autoptr(GSList) machines = NULL;
    GSList*     el;
    const char* type = qdict_get_try_str(qdict, "type");

    machines = object_class_get_list(target_machine_typename(), false);
    if (type) {
        ObjectClass* machine_class = OBJECT_CLASS(find_machine(type, machines));
        if (machine_class) {
            type_print_class_properties(object_class_get_name(machine_class));
            return;
        }
    }

    printf("Supported machines are:\n");
    machines = g_slist_sort_with_data(machines, machine_class_cmp, NULL);
    for (el = machines; el; el = el->next) {
        MachineClass* mc = el->data;
        if (mc->alias) { printf("%-20s %s (alias of %s)\n", mc->alias, mc->desc, mc->name); }
        printf("%-20s %s%s%s\n", mc->name, mc->desc, mc->is_default ? " (default)" : "",
               mc->deprecation_reason ? " (deprecated)" : "");
    }
}

static void machine_merge_property(const char* propname, QDict* prop, Error** errp)
{
    QDict* opts;

    opts = qdict_new();
    /* Preserve the caller's reference to prop.  */
    qobject_ref(prop);
    qdict_put(opts, propname, prop);
    keyval_merge(machine_opts_dict, opts, errp);
    qobject_unref(opts);
}

static void machine_parse_property_opt(QemuOptsList* opts_list, const char* propname, const char* arg)
{
    QDict* prop = NULL;
    bool   help = false;

    prop = keyval_parse(arg, opts_list->implied_opt_name, &help, &error_fatal);
    if (help) {
        qemu_opts_print_help(opts_list, true);
        exit(0);
    }
    machine_merge_property(propname, prop, &error_fatal);
    qobject_unref(prop);
}

static const char* pid_file;
struct UnlinkPidfileNotifier
{
    Notifier notifier;
    char*    pid_file_realpath;
};
static struct UnlinkPidfileNotifier qemu_unlink_pidfile_notifier;

static void qemu_unlink_pidfile(Notifier* n, void* data)
{
    struct UnlinkPidfileNotifier* upn;

    upn = DO_UPCAST(struct UnlinkPidfileNotifier, notifier, n);
    unlink(upn->pid_file_realpath);
}

static const QEMUOption* lookup_opt(int argc, char** argv, const char** poptarg, int* poptind)
{
    const QEMUOption* popt;
    int               optind = *poptind;
    char*             r      = argv[optind];
    const char*       optarg;

    loc_set_cmdline(argv, optind, 1);
    optind++;
    /* Treat --foo the same as -foo.  */
    if (r[1] == '-') { r++; }
    popt = qemu_options;
    for (;;) {
        if (!popt->name) {
            error_report("invalid option");
            exit(1);
        }
        if (!strcmp(popt->name, r + 1)) { break; }
        popt++;
    }
    if (popt->flags & HAS_ARG) {
        if (optind >= argc) {
            error_report("requires an argument");
            exit(1);
        }
        optarg = argv[optind++];
        loc_set_cmdline(argv, optind - 2, 2);
    }
    else {
        optarg = NULL;
    }

    *poptarg = optarg;
    *poptind = optind;

    return popt;
}

static MachineClass* select_machine(QDict* qdict, Error** errp)
{
    ERRP_GUARD();
    const char* machine_type    = qdict_get_try_str(qdict, "type");
    g_autoptr(GSList) machines  = object_class_get_list(TYPE_MACHINE, false);
    MachineClass* machine_class = NULL;

    if (machine_type) {
        machine_class = find_machine(machine_type, machines);
        if (!machine_class) { error_setg(errp, "unsupported machine type: \"%s\"", machine_type); }
        qdict_del(qdict, "type");
    }
    else {
        machine_class = find_default_machine(machines);
        if (!machine_class) { error_setg(errp, "No machine specified, and there is no default"); }
    }

    if (!machine_class) { error_append_hint(errp, "Use -machine help to list supported machines\n"); }
    return machine_class;
}

static int object_parse_property_opt(Object* obj, const char* name, const char* value, const char* skip, Error** errp)
{
    if (g_str_equal(name, skip)) { return 0; }

    if (!object_property_parse(obj, name, value, errp)) { return -1; }

    return 0;
}

/* *Non*recursively replace underscores with dashes in QDict keys.  */
static void keyval_dashify(QDict* qdict, Error** errp)
{
    const QDictEntry *ent, *next;
    char*             p;

    for (ent = qdict_first(qdict); ent; ent = next) {
        g_autofree char* new_key = NULL;

        next = qdict_next(qdict, ent);
        if (!strchr(ent->key, '_')) { continue; }
        new_key = g_strdup(ent->key);
        for (p = new_key; *p; p++) {
            if (*p == '_') { *p = '-'; }
        }
        if (qdict_haskey(qdict, new_key)) {
            error_setg(errp, "Conflict between '%s' and '%s'", ent->key, new_key);
            return;
        }
        qobject_ref(ent->value);
        qdict_put_obj(qdict, new_key, ent->value);
        qdict_del(qdict, ent->key);
    }
}

static void qemu_apply_legacy_machine_options(QDict* qdict)
{
    const char* value;
    QObject*    prop;

    keyval_dashify(qdict, &error_fatal);

    /* Legacy options do not correspond to MachineState properties.  */
    value = qdict_get_try_str(qdict, "accel");
    if (value) {
        accelerators = g_strdup(value);
        qdict_del(qdict, "accel");
    }

    value = qdict_get_try_str(qdict, "kvm-shadow-mem");
    if (value) {
        object_register_sugar_prop(ACCEL_CLASS_NAME("kvm"), "kvm-shadow-mem", value, false);
        qdict_del(qdict, "kvm-shadow-mem");
    }

    value = qdict_get_try_str(qdict, "kernel-irqchip");
    if (value) {
        object_register_sugar_prop(ACCEL_CLASS_NAME("kvm"), "kernel-irqchip", value, false);
        object_register_sugar_prop(ACCEL_CLASS_NAME("whpx"), "kernel-irqchip", value, false);
        qdict_del(qdict, "kernel-irqchip");
    }

    value = qdict_get_try_str(qdict, "memory-backend");
    if (value) {
        if (mem_path) {
            error_report("'-mem-path' can't be used together with"
                         "'-machine memory-backend'");
            exit(EXIT_FAILURE);
        }

        /* Resolved later.  */
        ram_memdev_id = g_strdup(value);
        qdict_del(qdict, "memory-backend");
    }

    prop = qdict_get(qdict, "memory");
    if (prop) {
        have_custom_ram_size = qobject_type(prop) == QTYPE_QDICT && qdict_haskey(qobject_to(QDict, prop), "size");
    }
}

static void object_option_foreach_add(bool (*type_opt_predicate)(const char*))
{
    ObjectOption *opt, *next;

    QTAILQ_FOREACH_SAFE (opt, &object_opts, next, next) {
        const char* type = ObjectType_str(opt->opts->qom_type);
        if (type_opt_predicate(type)) {
            user_creatable_add_qapi(opt->opts, &error_fatal);
            qapi_free_ObjectOptions(opt->opts);
            QTAILQ_REMOVE(&object_opts, opt, next);
            g_free(opt);
        }
    }
}

static void object_option_add_visitor(Visitor* v)
{
    ObjectOption* opt = g_new0(ObjectOption, 1);
    visit_type_ObjectOptions(v, NULL, &opt->opts, &error_fatal);
    QTAILQ_INSERT_TAIL(&object_opts, opt, next);
}

static void object_option_parse(const char* str)
{
    QemuOpts*   opts;
    const char* type;
    Visitor*    v;

    if (str[0] == '{') {
        QObject* obj = qobject_from_json(str, &error_fatal);

        v = qobject_input_visitor_new(obj);
        qobject_unref(obj);
    }
    else {
        opts = qemu_opts_parse_noisily(qemu_find_opts("object"), str, true);
        if (!opts) { exit(1); }

        type = qemu_opt_get(opts, "qom-type");
        if (!type) {
            error_report(QERR_MISSING_PARAMETER, "qom-type");
            exit(1);
        }
        if (user_creatable_print_help(type, opts)) { exit(0); }

        v = opts_visitor_new(opts);
    }

    object_option_add_visitor(v);
    visit_free(v);
}

static void overcommit_parse(const char* str)
{
    QemuOpts*   opts;
    const char* mem_lock_opt;

    opts = qemu_opts_parse_noisily(qemu_find_opts("overcommit"), str, false);
    if (!opts) { exit(1); }

    enable_cpu_pm = qemu_opt_get_bool(opts, "cpu-pm", enable_cpu_pm);

    mem_lock_opt = qemu_opt_get(opts, "mem-lock");
    if (!mem_lock_opt) { return; }

    if (strcmp(mem_lock_opt, "on") == 0) {
        mlock_state = MLOCK_ON;
        return;
    }

    if (strcmp(mem_lock_opt, "off") == 0) {
        mlock_state = MLOCK_OFF;
        return;
    }

    if (strcmp(mem_lock_opt, "on-fault") == 0) {
        mlock_state = MLOCK_ON_FAULT;
        return;
    }

    error_report("parameter 'mem-lock' expects one of "
                 "'on', 'off', 'on-fault'");
    exit(1);
}

/*
 * Very early object creation, before the sandbox options have been activated.
 */
static bool object_create_pre_sandbox(const char* type)
{
    /*
     * Objects should in general not get initialized "too early" without
     * a reason. If you add one, state the reason in a comment!
     */

    /*
     * Reason: -sandbox on,resourcecontrol=deny disallows setting CPU
     * affinity of threads.
     */
    if (g_str_equal(type, "thread-context")) { return true; }

    return false;
}

/*
 * Initial object creation happens before all other
 * QEMU data types are created. The majority of objects
 * can be created at this point. The rng-egd object
 * cannot be created here, as it depends on the chardev
 * already existing.
 */
static bool object_create_early(const char* type)
{
    /*
     * Objects should not be made "delayed" without a reason.  If you
     * add one, state the reason in a comment!
     */

    /* Reason: already created. */
    if (object_create_pre_sandbox(type)) { return false; }

    /* Reason: property "chardev" */
    if (g_str_equal(type, "rng-egd")) { return false; }

    /*
     * Allocation of large amounts of memory may delay
     * chardev initialization for too long, and trigger timeouts
     * on software that waits for a monitor socket to be created
     */
    if (g_str_has_prefix(type, "memory-backend-")) { return false; }

    return true;
}

static void qemu_apply_machine_options(QDict* qdict)
{ object_set_properties_from_keyval(OBJECT(current_machine), qdict, false, &error_fatal); }

static void qemu_create_early_backends(void)
{
    MachineClass* machine_class = MACHINE_GET_CLASS(current_machine);
#if defined(CONFIG_SDL)
    const bool use_sdl = (dpy.type == DISPLAY_TYPE_SDL);
#else
    const bool use_sdl = false;
#endif
#if defined(CONFIG_GTK)
    const bool use_gtk = (dpy.type == DISPLAY_TYPE_GTK);
#else
    const bool use_gtk = false;
#endif

    if (dpy.has_window_close && !use_gtk && !use_sdl) {
        error_report("window-close is only valid for GTK and SDL, "
                     "ignoring option");
    }

    qemu_console_early_init();

    object_option_foreach_add(object_create_early);

    qemu_opts_foreach(qemu_find_opts("chardev"), chardev_init_func, NULL, &error_fatal);

    /*
     * Note: we need to create audio and block backends before
     * setting machine properties, so they can be referred to.
     */
    configure_blockdev(&bdo_queue, machine_class, snapshot);
    audio_init_audiodevs();
    if (default_audio) { audio_create_default_audiodevs(); }
}

/*
 * The remainder of object creation happens after the
 * creation of chardev, net clients and device data types.
 */
static bool object_create_late(const char* type)
{ return !object_create_early(type) && !object_create_pre_sandbox(type); }

static void qemu_create_late_backends(void)
{
    net_init_clients();

    object_option_foreach_add(object_create_late);

    /*
     * Wait for any outstanding memory prealloc from created memory
     * backends to complete.
     */
    if (!qemu_finish_async_prealloc_mem(&error_fatal)) { exit(1); }

    qemu_opts_foreach(qemu_find_opts("mon"), mon_init_func, NULL, &error_fatal);

    foreach_device_config_or_exit(DEV_SERIAL, serial_parse);
    foreach_device_config_or_exit(DEV_DEBUGCON, debugcon_parse);
}

static void qemu_resolve_machine_memdev(void)
{
    if (ram_memdev_id) {
        Object*    backend;
        ram_addr_t backend_size;

        backend = object_resolve_path_type(ram_memdev_id, TYPE_MEMORY_BACKEND, NULL);
        if (!backend) {
            error_report("Memory backend '%s' not found", ram_memdev_id);
            exit(EXIT_FAILURE);
        }
        if (!have_custom_ram_size) {
            backend_size              = object_property_get_uint(backend, "size", &error_abort);
            current_machine->ram_size = backend_size;
        }
        object_property_set_link(OBJECT(current_machine), "memory-backend", backend, &error_fatal);
    }
}

static void parse_memory_options(void)
{
    QemuOpts*   opts = qemu_find_opts_singleton("memory");
    QDict *     dict, *prop;
    const char* mem_str;
    Location    loc;

    loc_push_none(&loc);
    qemu_opts_loc_restore(opts);

    prop = qdict_new();

    if (qemu_opt_get_size(opts, "size", 0) != 0) {
        /* Fix up legacy suffix-less format */
        mem_str = qemu_opt_get(opts, "size");
        if (g_ascii_isdigit(mem_str[strlen(mem_str) - 1])) {
            g_autofree char* mib_str = g_strdup_printf("%sM", mem_str);
            qdict_put_str(prop, "size", mib_str);
        }
        else {
            qdict_put_str(prop, "size", mem_str);
        }
    }

    if (qemu_opt_get(opts, "maxmem")) { qdict_put_str(prop, "max-size", qemu_opt_get(opts, "maxmem")); }
    if (qemu_opt_get(opts, "slots")) { qdict_put_str(prop, "slots", qemu_opt_get(opts, "slots")); }

    dict = qdict_new();
    qdict_put(dict, "memory", prop);
    keyval_merge(machine_opts_dict, dict, &error_fatal);
    qobject_unref(dict);
    loc_pop(&loc);
}

static void qemu_create_machine_containers(Object* machine)
{
    static const char* const containers[] = {
        "unattached",
        "peripheral",
        "peripheral-anon",
    };

    for (unsigned i = 0; i < ARRAY_SIZE(containers); i++) { object_property_add_new_container(machine, containers[i]); }
}

static void qemu_create_machine(QDict* qdict)
{
    MachineClass* machine_class = select_machine(qdict, &error_fatal);
    object_set_machine_compat_props(machine_class->compat_props);

    current_machine = MACHINE(object_new_with_class(OBJECT_CLASS(machine_class)));
    object_property_add_child(object_get_root(), "machine", OBJECT(current_machine));
    qemu_create_machine_containers(OBJECT(current_machine));
    object_property_add_child(machine_get_container("unattached"), "sysbus", OBJECT(sysbus_get_default()));

    if (machine_class->minimum_page_bits) {
        if (!set_preferred_target_page_bits(machine_class->minimum_page_bits)) {
            /* This would be a board error: specifying a minimum smaller than
             * a target's compile-time fixed setting.
             */
            assert_not_reached();
        }
    }

    cpu_exec_init_all();

    if (machine_class->hw_version) { qemu_set_hw_version(machine_class->hw_version); }

    /*
     * Get the default machine options from the machine if it is not already
     * specified either by the configuration file or by the command line.
     */
    if (machine_class->default_machine_opts) {
        QDict* default_opts = keyval_parse(machine_class->default_machine_opts, NULL, NULL, &error_abort);
        qemu_apply_legacy_machine_options(default_opts);
        object_set_properties_from_keyval(OBJECT(current_machine), default_opts, false, &error_abort);
        qobject_unref(default_opts);
    }
}

static int global_init_func(void* opaque, QemuOpts* opts, Error** errp)
{
    GlobalProperty* g;

    g           = g_malloc0(sizeof(*g));
    g->driver   = qemu_opt_get(opts, "driver");
    g->property = qemu_opt_get(opts, "property");
    g->value    = qemu_opt_get(opts, "value");
    qdev_prop_register_global(g);
    return 0;
}

/*
 * Return whether configuration group @group is stored in QemuOpts, or
 * recorded as one or more QDicts by qemu_record_config_group.
 */
static bool is_qemuopts_group(const char* group)
{
    if (g_str_equal(group, "object") || g_str_equal(group, "audiodev") || g_str_equal(group, "machine")
        || g_str_equal(group, "smp-opts"))
    {
        return false;
    }
    return true;
}

static void qemu_record_config_group(const char* group, QDict* dict, bool from_json, Error** errp)
{
    if (g_str_equal(group, "object")) {
        Visitor* v = qobject_input_visitor_new_keyval(QOBJECT(dict));
        object_option_add_visitor(v);
        visit_free(v);
    }
    else if (g_str_equal(group, "audiodev")) {
        Audiodev* dev = NULL;
        Visitor*  v   = qobject_input_visitor_new_keyval(QOBJECT(dict));
        if (visit_type_Audiodev(v, NULL, &dev, errp)) { audio_define(dev); }
        visit_free(v);
    }
    else if (g_str_equal(group, "machine")) {
        /*
         * Cannot merge string-valued and type-safe dictionaries, so JSON
         * is not accepted yet for -M.
         */
        assert(!from_json);
        keyval_merge(machine_opts_dict, dict, errp);
    }
    else if (g_str_equal(group, "smp-opts")) {
        machine_merge_property("smp", dict, &error_fatal);
    }
    else {
        abort();
    }
}

/*
 * Parse non-QemuOpts config file groups, pass the rest to
 * qemu_config_do_parse.
 */
static void qemu_parse_config_group(const char* group, QDict* qdict, void* opaque, Error** errp)
{
    QObject* crumpled;
    if (is_qemuopts_group(group)) {
        qemu_config_do_parse(group, qdict, opaque, errp);
        return;
    }

    crumpled = qdict_crumple(qdict, errp);
    if (!crumpled) { return; }
    switch (qobject_type(crumpled)) {
        case QTYPE_QDICT: qemu_record_config_group(group, qobject_to(QDict, crumpled), false, errp); break;
        case QTYPE_QLIST: error_setg(errp, "Lists cannot be at top level of a configuration section"); break;
        default         : assert_not_reached();
    }
    qobject_unref(crumpled);
}

static void qemu_read_default_config_file(Error** errp)
{
    ERRP_GUARD();
    int              ret;
    g_autofree char* file = get_relocated_path(CONFIG_QEMU_CONFDIR "/qemu.conf");

    ret = qemu_read_config_file(file, qemu_parse_config_group, errp);
    if (ret < 0) {
        if (ret == -ENOENT) {
            error_free(*errp);
            *errp = NULL;
        }
    }
}

static void qemu_set_option(const char* str, Error** errp)
{
    char          group[64], id[64], arg[64];
    QemuOptsList* list;
    QemuOpts*     opts;
    int           rc, offset;

    rc = sscanf(str, "%63[^.].%63[^.].%63[^=]%n", group, id, arg, &offset);
    if (rc < 3 || str[offset] != '=') {
        error_setg(errp, "can't parse: \"%s\"", str);
        return;
    }

    if (!is_qemuopts_group(group)) { error_setg(errp, "-set is not supported with %s", group); }
    else {
        list = qemu_find_opts_err(group, errp);
        if (list) {
            opts = qemu_opts_find(list, id);
            if (!opts) {
                error_setg(errp, "there is no %s \"%s\" defined", group, id);
                return;
            }
            qemu_opt_set(opts, arg, str + offset + 1, errp);
        }
    }
}

static void user_register_global_props(void)
{ qemu_opts_foreach(qemu_find_opts("global"), global_init_func, NULL, NULL); }

static int accelerator_set_property(void* opaque, const char* name, const char* value, Error** errp)
{ return object_parse_property_opt(opaque, name, value, "accel", errp); }

static int do_configure_accelerator(void* opaque, QemuOpts* opts, Error** errp)
{
    bool*       p_init_failed = opaque;
    const char* acc           = qemu_opt_get(opts, "accel");
    AccelClass* ac            = accel_find(acc);
    AccelState* accel;
    int         ret;

    if (!acc) {
        error_setg(errp, QERR_MISSING_PARAMETER, "accel");
        goto bad;
    }

    if (!ac) {
        error_report("invalid accelerator %s", acc);
        goto bad;
    }
    accel = ACCEL(object_new_with_class(OBJECT_CLASS(ac)));
    object_apply_compat_props(OBJECT(accel));
    qemu_opt_foreach(opts, accelerator_set_property, accel, &error_fatal);

    ret = accel_init_machine(accel, current_machine);
    if (ret < 0) {
        error_report("failed to initialize %s: %s", acc, strerror(-ret));
        goto bad;
    }

    return 1;

bad:
    *p_init_failed = true;
    return 0;
}

static void configure_accelerators(const char* progname)
{
    bool init_failed = false;

    if (QTAILQ_EMPTY(&qemu_accel_opts.head)) {
        char **accel_list, **tmp;

        if (accelerators == NULL) {
            /* Select the default accelerator */
            bool have_tcg = accel_find("tcg");
            bool have_kvm = accel_find("kvm");
            bool have_hvf = accel_find("hvf");

            if (have_tcg && have_kvm) {
                if (g_str_has_suffix(progname, "kvm")) {
                    /* If the program name ends with "kvm", we prefer KVM */
                    accelerators = "kvm:tcg";
                }
                else {
                    accelerators = "tcg:kvm";
                }
            }
            else if (have_kvm) {
                accelerators = "kvm";
            }
            else if (have_tcg) {
                accelerators = "tcg";
            }
            else if (have_hvf) {
                accelerators = "hvf";
            }
            else {
                error_report("No accelerator selected and"
                             " no default accelerator available");
                exit(1);
            }
        }
        accel_list = g_strsplit(accelerators, ":", 0);

        for (tmp = accel_list; *tmp; tmp++) {
            /*
             * Filter invalid accelerators here, to prevent obscenities
             * such as "-machine accel=tcg,,thread=single".
             */
            if (accel_find(*tmp)) { qemu_opts_parse_noisily(qemu_find_opts("accel"), *tmp, true); }
            else {
                init_failed = true;
                error_report("invalid accelerator %s", *tmp);
            }
        }
        g_strfreev(accel_list);
    }
    else {
        if (accelerators != NULL) {
            error_report("The -accel and \"-machine accel=\" options are incompatible");
            exit(1);
        }
    }

    if (!qemu_opts_foreach(qemu_find_opts("accel"), do_configure_accelerator, &init_failed, &error_fatal)) {
        if (!init_failed) { error_report("no accelerator found"); }
        exit(1);
    }

    if (init_failed) { error_report("falling back to %s", current_accel_name()); }
}

static void qemu_validate_options(const QDict* machine_opts)
{
    const char* kernel_filename = qdict_get_try_str(machine_opts, "kernel");
    const char* shim_filename   = qdict_get_try_str(machine_opts, "shim");
    const char* initrd_filename = qdict_get_try_str(machine_opts, "initrd");
    const char* kernel_cmdline  = qdict_get_try_str(machine_opts, "append");

    if (kernel_filename == NULL) {
        if (kernel_cmdline != NULL) {
            error_report("-append only allowed with -kernel option");
            exit(1);
        }

        if (shim_filename != NULL) {
            error_report("-shim only allowed with -kernel option");
            exit(1);
        }

        if (initrd_filename != NULL) {
            error_report("-initrd only allowed with -kernel option");
            exit(1);
        }
    }

#ifdef CONFIG_CURSES
    if (is_daemonized() && dpy.type == DISPLAY_TYPE_CURSES) {
        error_report("curses display cannot be used with -daemonize");
        exit(1);
    }
#endif
}

static void qemu_process_sugar_options(void)
{
    if (mem_prealloc) {
        QObject* smp = qdict_get(machine_opts_dict, "smp");
        if (smp && qobject_type(smp) == QTYPE_QDICT) {
            QObject* cpus = qdict_get(qobject_to(QDict, smp), "cpus");
            if (cpus && qobject_type(cpus) == QTYPE_QSTRING) {
                const char* val = qstring_get_str(qobject_to(QString, cpus));
                object_register_sugar_prop("memory-backend", "prealloc-threads", val, false);
            }
        }
        object_register_sugar_prop("memory-backend", "prealloc", "on", false);
    }
}

/* -action processing */

/*
 * Process all the -action parameters parsed from cmdline.
 */
static int process_runstate_actions(void* opaque, QemuOpts* opts, Error** errp)
{
    Error*   local_err = NULL;
    QDict*   qdict     = qemu_opts_to_qdict(opts, NULL);
    QObject* ret       = NULL;
    qmp_marshal_set_action(qdict, &ret, &local_err);
    qobject_unref(ret);
    qobject_unref(qdict);
    if (local_err) {
        error_propagate(errp, local_err);
        return 1;
    }
    return 0;
}

static void qemu_process_early_options(void)
{
    qemu_opts_foreach(qemu_find_opts("name"), parse_name, NULL, &error_fatal);

    object_option_foreach_add(object_create_pre_sandbox);

#ifdef CONFIG_SECCOMP
    QemuOptsList* olist = qemu_find_opts_err("sandbox", NULL);
    if (olist) { qemu_opts_foreach(olist, parse_sandbox, NULL, &error_fatal); }
#endif

    if (qemu_opts_foreach(qemu_find_opts("action"), process_runstate_actions, NULL, &error_fatal)) { exit(1); }

#ifndef _WIN32
    qemu_opts_foreach(qemu_find_opts("add-fd"), parse_add_fd, NULL, &error_fatal);

    qemu_opts_foreach(qemu_find_opts("add-fd"), cleanup_add_fd, NULL, &error_fatal);
#endif

    /* Open the logfile at this point and set the log mask if necessary.  */
    {
        int mask = 0;
        if (log_mask) {
            mask = qemu_str_to_log_mask(log_mask);
            if (!mask) {
                qemu_print_log_usage(stdout);
                exit(1);
            }
        }
        qemu_set_log_filename_flags(log_file, mask, &error_fatal);
    }

    qemu_add_default_firmwarepath();
}

static void qemu_process_help_options(void)
{
    /*
     * Check for -cpu help and -device help before we call select_machine(),
     * which will return an error if the architecture has no default machine
     * type and the user did not specify one, so that the user doesn't need
     * to say '-cpu help -machine something'.
     */
    if (cpu_option && is_help_option(cpu_option)) {
        list_cpus();
        exit(0);
    }

    if (qemu_opts_foreach(qemu_find_opts("device"), device_help_func, NULL, NULL)) { exit(0); }

    /* -L help lists the data directories and exits. */
    if (list_data_dirs) {
        qemu_list_data_dirs();
        exit(0);
    }
}

static void qemu_maybe_daemonize(const char* pid_file)
{
    Error* err = NULL;

    os_daemonize();
    rcu_disable_atfork();

    if (pid_file) {
        char* pid_file_realpath = NULL;

        if (!qemu_write_pidfile(pid_file, &err)) {
            error_reportf_err(err, "cannot create PID file: ");
            exit(1);
        }

        pid_file_realpath = g_malloc0(PATH_MAX);
        if (!realpath(pid_file, pid_file_realpath)) {
            if (errno != ENOENT) {
                warn_report("not removing PID file on exit: cannot resolve PID "
                            "file path: %s: %s",
                            pid_file, strerror(errno));
            }
            return;
        }

        qemu_unlink_pidfile_notifier = (struct UnlinkPidfileNotifier){
            .notifier =
                {
                    .notify = qemu_unlink_pidfile,
                },
            .pid_file_realpath = pid_file_realpath,
        };
        qemu_add_exit_notifier(&qemu_unlink_pidfile_notifier.notifier);
    }
}

static void qemu_init_displays(void)
{
    DisplayState* ds;

    /* init local displays */
    ds = init_displaystate();
    qemu_display_init(ds, &dpy);

    /* must be after terminal init, SDL library changes signal handlers */
    os_setup_signal_handling();

    /* init remote displays */
#ifdef CONFIG_VNC
    qemu_opts_foreach(qemu_find_opts("vnc"), vnc_init_func, NULL, &error_fatal);
#endif
}

static void qemu_init_board(void)
{
    /* From here on we enter MACHINE_PHASE_INITIALIZED.  */
    machine_run_board_init(current_machine, mem_path, &error_fatal);

    drive_check_orphaned();

    realtime_init();
}

static void qemu_create_cli_devices(void)
{
    DeviceOption* opt;

    /* init USB devices */
    if (machine_usb(current_machine)) { foreach_device_config_or_exit(DEV_USB, usb_parse); }

    /* init generic devices */
    qemu_opts_foreach(qemu_find_opts("device"), device_init_func, NULL, &error_fatal);
    QTAILQ_FOREACH (opt, &device_opts, next) {
        QObject* ret_data = NULL;

        loc_push_restore(&opt->loc);
        qmp_device_add(opt->opts, &ret_data, &error_fatal);
        assert(ret_data == NULL); /* error_fatal aborts */
        loc_pop(&opt->loc);
    }
}

static bool qemu_machine_creation_done(Error** errp)
{
    /* Did we create any drives that we failed to create a device for? */
    drive_check_orphaned();

    /* Don't warn about the default network setup that you get if
     * no command line -net or -netdev options are specified. There
     * are two cases that we would otherwise complain about:
     * (1) board doesn't support a NIC but the implicit "-net nic"
     * requested one
     * (2) CONFIG_SLIRP not set, in which case the implicit "-net nic"
     * sets up a nic that isn't connected to anything.
     */
    if (!default_net) { net_check_clients(); }

    qdev_prop_check_globals();

    qdev_machine_creation_done();

    foreach_device_config_or_exit(DEV_GDB, gdbserver_start);

    return true;
}

void qmp_x_exit_preconfig(Error** errp)
{
    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp, "The command is permitted only before machine initialization");
        return;
    }

    qemu_init_board();
    qemu_create_cli_devices();
    if (!qemu_machine_creation_done(errp)) { return; }

    if (autostart) { qmp_cont(NULL); }
}

void qemu_init(int argc, char** argv)
{
    QemuOpts*     opts;
    QemuOpts*     accel_opts = NULL;
    QemuOptsList* olist;
    int           optind;
    const char*   optarg;
    MachineClass* machine_class;
    bool          userconfig = true;

    qemu_add_opts(&qemu_drive_opts);
    qemu_add_drive_opts(&qemu_legacy_drive_opts);
    qemu_add_drive_opts(&qemu_common_drive_opts);
    qemu_add_drive_opts(&qemu_drive_opts);
    qemu_add_drive_opts(&bdrv_runtime_opts);
    qemu_add_opts(&qemu_chardev_opts);
    qemu_add_opts(&qemu_device_opts);
    qemu_add_opts(&qemu_netdev_opts);
    qemu_add_opts(&qemu_nic_opts);
    qemu_add_opts(&qemu_net_opts);
    qemu_add_opts(&qemu_rtc_opts);
    qemu_add_opts(&qemu_global_opts);
    qemu_add_opts(&qemu_mon_opts);
    qemu_add_opts(&qemu_trace_opts);
    qemu_add_opts(&qemu_accel_opts);
    qemu_add_opts(&qemu_mem_opts);
    qemu_add_opts(&qemu_smp_opts);
    qemu_add_opts(&qemu_add_fd_opts);
    qemu_add_opts(&qemu_object_opts);
    qemu_add_opts(&qemu_overcommit_opts);
    qemu_add_opts(&qemu_msg_opts);
    qemu_add_opts(&qemu_name_opts);
    qemu_add_opts(&qemu_fw_cfg_opts);
    qemu_add_opts(&qemu_action_opts);
    qemu_add_run_with_opts();
    module_call_init(MODULE_INIT_OPTS);

    error_init(argv[0]);
    qemu_init_exec_dir(argv[0]);

    os_setup_limits();

    qemu_init_subsystems();

    /* first pass of option parsing */
    optind = 1;
    while (optind < argc) {
        if (argv[optind][0] != '-') {
            /* disk image */
            optind++;
        }
        else {
            const QEMUOption* popt;

            popt = lookup_opt(argc, argv, &optarg, &optind);
            switch (popt->index) {
                case QEMU_OPTION_nouserconfig: userconfig = false; break;
            }
        }
    }

    machine_opts_dict = qdict_new();
    if (userconfig) { qemu_read_default_config_file(&error_fatal); }

    /* second pass of option parsing */
    optind = 1;
    for (;;) {
        if (optind >= argc) { break; }
        if (argv[optind][0] != '-') {
            loc_set_cmdline(argv, optind, 1);
            drive_add(IF_DEFAULT, 0, argv[optind++], HD_OPTS);
        }
        else {
            const QEMUOption* popt;

            popt = lookup_opt(argc, argv, &optarg, &optind);
            if (!qemu_arch_available(popt->arch_mask)) {
                error_report("Option not supported for this target");
                exit(1);
            }
            switch (popt->index) {
                case QEMU_OPTION_cpu:
                    /* hw initialization will check this */
                    cpu_option = optarg;
                    break;
                case QEMU_OPTION_hda:
                case QEMU_OPTION_hdb:
                case QEMU_OPTION_hdc:
                case QEMU_OPTION_hdd     : drive_add(IF_DEFAULT, popt->index - QEMU_OPTION_hda, optarg, HD_OPTS); break;
                case QEMU_OPTION_blockdev: {
                    Visitor*                   v;
                    BlockdevOptionsQueueEntry* bdo;

                    v = qobject_input_visitor_new_str(optarg, "driver", &error_fatal);

                    bdo = g_new(BlockdevOptionsQueueEntry, 1);
                    visit_type_BlockdevOptions(v, NULL, &bdo->bdo, &error_fatal);
                    visit_free(v);
                    loc_save(&bdo->loc);
                    QSIMPLEQ_INSERT_TAIL(&bdo_queue, bdo, entry);
                    break;
                }
                case QEMU_OPTION_drive:
                    opts = qemu_opts_parse_noisily(qemu_find_opts("drive"), optarg, false);
                    if (opts == NULL) { exit(1); }
                    break;
                case QEMU_OPTION_set: qemu_set_option(optarg, &error_fatal); break;
                case QEMU_OPTION_global:
                    if (qemu_global_option(optarg) != 0) { exit(1); }
                    break;
                case QEMU_OPTION_mtdblock: drive_add(IF_MTD, -1, optarg, MTD_OPTS); break;
                case QEMU_OPTION_sd      : drive_add(IF_SD, -1, optarg, SD_OPTS); break;
                case QEMU_OPTION_pflash  : drive_add(IF_PFLASH, -1, optarg, PFLASH_OPTS); break;
                case QEMU_OPTION_snapshot: snapshot = 1; break;
                case QEMU_OPTION_display : parse_display(optarg); break;
                case QEMU_OPTION_nographic:
                    qdict_put_str(machine_opts_dict, "graphics", "off");
                    nographic = true;
                    dpy.type  = DISPLAY_TYPE_NONE;
                    break;
                case QEMU_OPTION_kernel: qdict_put_str(machine_opts_dict, "kernel", optarg); break;
                case QEMU_OPTION_shim  : qdict_put_str(machine_opts_dict, "shim", optarg); break;
                case QEMU_OPTION_initrd: qdict_put_str(machine_opts_dict, "initrd", optarg); break;
                case QEMU_OPTION_append: qdict_put_str(machine_opts_dict, "append", optarg); break;
                case QEMU_OPTION_dtb   : qdict_put_str(machine_opts_dict, "dtb", optarg); break;
                case QEMU_OPTION_cdrom : drive_add(IF_DEFAULT, 2, optarg, CDROM_OPTS); break;
                case QEMU_OPTION_netdev:
                    default_net = 0;
                    if (netdev_is_modern(optarg)) { netdev_parse_modern(optarg); }
                    else {
                        net_client_parse(qemu_find_opts("netdev"), optarg);
                    }
                    break;
                case QEMU_OPTION_nic:
                    default_net = 0;
                    net_client_parse(qemu_find_opts("nic"), optarg);
                    break;
                case QEMU_OPTION_net:
                    default_net = 0;
                    net_client_parse(qemu_find_opts("net"), optarg);
                    break;
                case QEMU_OPTION_audiodev:
                    default_audio = 0;
                    audio_parse_option(optarg);
                    break;
                case QEMU_OPTION_h: help(0); break;
                case QEMU_OPTION_version:
                    version();
                    exit(0);
                    break;
                case QEMU_OPTION_m:
                    opts = qemu_opts_parse_noisily(qemu_find_opts("memory"), optarg, true);
                    if (opts == NULL) { exit(1); }
                    break;
                case QEMU_OPTION_mempath     : mem_path = optarg; break;
                case QEMU_OPTION_mem_prealloc: mem_prealloc = 1; break;
                case QEMU_OPTION_d           : log_mask = optarg; break;
                case QEMU_OPTION_D           : log_file = optarg; break;
                case QEMU_OPTION_DFILTER     : qemu_set_dfilter_ranges(optarg, &error_fatal); break;
                case QEMU_OPTION_seed        : qemu_guest_random_seed_main(optarg, &error_fatal); break;
                case QEMU_OPTION_s           : add_device_config(DEV_GDB, "tcp::" DEFAULT_GDBSTUB_PORT); break;
                case QEMU_OPTION_gdb         : add_device_config(DEV_GDB, optarg); break;
                case QEMU_OPTION_L:
                    if (is_help_option(optarg)) { list_data_dirs = true; }
                    else {
                        qemu_add_data_dir(g_strdup(optarg));
                    }
                    break;
                case QEMU_OPTION_bios: qdict_put_str(machine_opts_dict, "firmware", optarg); break;
                case QEMU_OPTION_S   : autostart = 0; break;
                case QEMU_OPTION_k   : keyboard_layout = optarg; break;
                case QEMU_OPTION_echr: {
                    char* r;
                    term_escape_char = strtol(optarg, &r, 0);
                    if (r == optarg) { printf("Bad argument to echr\n"); }
                    break;
                }
                case QEMU_OPTION_monitor:
                    default_monitor = 0;
                    if (strncmp(optarg, "none", 4)) { monitor_parse(optarg, "readline", false); }
                    break;
                case QEMU_OPTION_qmp:
                    monitor_parse(optarg, "control", false);
                    default_monitor = 0;
                    break;
                case QEMU_OPTION_qmp_pretty:
                    monitor_parse(optarg, "control", true);
                    default_monitor = 0;
                    break;
                case QEMU_OPTION_mon:
                    opts = qemu_opts_parse_noisily(qemu_find_opts("mon"), optarg, true);
                    if (!opts) { exit(1); }
                    default_monitor = 0;
                    break;
                case QEMU_OPTION_chardev:
                    opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"), optarg, true);
                    if (!opts) { exit(1); }
                    break;
                case QEMU_OPTION_serial:
                    add_device_config(DEV_SERIAL, optarg);
                    default_serial = 0;
                    if (strncmp(optarg, "mon:", 4) == 0) { default_monitor = 0; }
                    break;
                case QEMU_OPTION_action:
                    olist = qemu_find_opts("action");
                    if (!qemu_opts_parse_noisily(olist, optarg, false)) { exit(1); }
                    break;
                case QEMU_OPTION_watchdog_action: {
                    opts = qemu_opts_create(qemu_find_opts("action"), NULL, 0, &error_abort);
                    qemu_opt_set(opts, "watchdog", optarg, &error_abort);
                    break;
                }
                case QEMU_OPTION_debugcon: add_device_config(DEV_DEBUGCON, optarg); break;
                case QEMU_OPTION_full_screen:
                    dpy.has_full_screen = true;
                    dpy.full_screen     = true;
                    break;
                case QEMU_OPTION_pidfile: pid_file = optarg; break;
                case QEMU_OPTION_fwcfg:
                    opts = qemu_opts_parse_noisily(qemu_find_opts("fw_cfg"), optarg, true);
                    if (opts == NULL) { exit(1); }
                    break;
                case QEMU_OPTION_preconfig : preconfig_requested = true; break;
                case QEMU_OPTION_enable_kvm: qdict_put_str(machine_opts_dict, "accel", "kvm"); break;
                case QEMU_OPTION_M         :
                case QEMU_OPTION_machine   : {
                    bool help;

                    keyval_parse_into(machine_opts_dict, optarg, "type", &help, &error_fatal);
                    if (help) {
                        machine_help_func(machine_opts_dict);
                        exit(EXIT_SUCCESS);
                    }
                    break;
                }
                case QEMU_OPTION_accel:
                    accel_opts = qemu_opts_parse_noisily(qemu_find_opts("accel"), optarg, true);
                    optarg     = qemu_opt_get(accel_opts, "accel");
                    if (!optarg || is_help_option(optarg)) {
                        printf("Accelerators supported in QEMU binary:\n");
                        GSList *el, *accel_list = object_class_get_list(TYPE_ACCEL, false);
                        for (el = accel_list; el; el = el->next) {
                            gchar* typename = g_strdup(object_class_get_name(OBJECT_CLASS(el->data)));
                            if (g_str_has_suffix(typename, ACCEL_CLASS_SUFFIX)) {
                                gchar** optname = g_strsplit(typename, ACCEL_CLASS_SUFFIX, 0);
                                printf("%s\n", optname[0]);
                                g_strfreev(optname);
                            }
                            g_free(typename);
                        }
                        g_slist_free(accel_list);
                        exit(0);
                    }
                    break;
                case QEMU_OPTION_usb: qdict_put_str(machine_opts_dict, "usb", "on"); break;
                case QEMU_OPTION_usbdevice:
                    qdict_put_str(machine_opts_dict, "usb", "on");
                    add_device_config(DEV_USB, optarg);
                    break;
                case QEMU_OPTION_device:
                    if (optarg[0] == '{') {
                        QObject*      obj = qobject_from_json(optarg, &error_fatal);
                        DeviceOption* opt = g_new0(DeviceOption, 1);
                        opt->opts         = qobject_to(QDict, obj);
                        loc_save(&opt->loc);
                        assert(opt->opts != NULL);
                        QTAILQ_INSERT_TAIL(&device_opts, opt, next);
                    }
                    else {
                        if (!qemu_opts_parse_noisily(qemu_find_opts("device"), optarg, true)) { exit(1); }
                    }
                    break;
                case QEMU_OPTION_smp: machine_parse_property_opt(qemu_find_opts("smp-opts"), "smp", optarg); break;
#ifdef CONFIG_VNC
                case QEMU_OPTION_vnc:
                    vnc_parse(optarg);
                    display_remote++;
                    break;
#endif
                case QEMU_OPTION_no_reboot:
                    olist = qemu_find_opts("action");
                    qemu_opts_parse_noisily(olist, "reboot=shutdown", false);
                    break;
                case QEMU_OPTION_no_shutdown:
                    olist = qemu_find_opts("action");
                    qemu_opts_parse_noisily(olist, "shutdown=pause", false);
                    break;
                case QEMU_OPTION_uuid:
                    if (qemu_uuid_parse(optarg, &qemu_uuid) < 0) {
                        error_report("failed to parse UUID string: wrong format");
                        exit(1);
                    }
                    qemu_uuid_set = true;
                    break;
                case QEMU_OPTION_name:
                    opts = qemu_opts_parse_noisily(qemu_find_opts("name"), optarg, true);
                    if (!opts) { exit(1); }
                    /* Capture guest name if -msg guest-name is used later */
                    error_guest_name = qemu_opt_get(opts, "guest");
                    break;
                case QEMU_OPTION_rtc:
                    opts = qemu_opts_parse_noisily(qemu_find_opts("rtc"), optarg, false);
                    if (!opts) { exit(1); }
                    break;
                case QEMU_OPTION_trace: trace_opt_parse(optarg); break;
                case QEMU_OPTION_readconfig:
                    qemu_read_config_file(optarg, qemu_parse_config_group, &error_fatal);
                    break;
                case QEMU_OPTION_sandbox:
                    olist = qemu_find_opts("sandbox");
                    if (!olist) {
#ifndef CONFIG_SECCOMP
                        error_report("-sandbox support is not enabled "
                                     "in this QEMU binary");
#endif
                        exit(1);
                    }

                    opts = qemu_opts_parse_noisily(olist, optarg, true);
                    if (!opts) { exit(1); }
                    break;
                case QEMU_OPTION_add_fd:
#ifndef _WIN32
                    opts = qemu_opts_parse_noisily(qemu_find_opts("add-fd"), optarg, false);
                    if (!opts) { exit(1); }
#else
                    error_report("File descriptor passing is disabled on this "
                                 "platform");
                    exit(1);
#endif
                    break;
                case QEMU_OPTION_object    : object_option_parse(optarg); break;
                case QEMU_OPTION_overcommit: overcommit_parse(optarg); break;
                case QEMU_OPTION_compat    : {
                    CompatPolicy* opts_policy;
                    Visitor*      v;

                    v = qobject_input_visitor_new_str(optarg, NULL, &error_fatal);

                    visit_type_CompatPolicy(v, NULL, &opts_policy, &error_fatal);
                    QAPI_CLONE_MEMBERS(CompatPolicy, &compat_policy, opts_policy);

                    qapi_free_CompatPolicy(opts_policy);
                    visit_free(v);
                    break;
                }
                case QEMU_OPTION_msg:
                    opts = qemu_opts_parse_noisily(qemu_find_opts("msg"), optarg, false);
                    if (!opts) { exit(1); }
                    configure_msg(opts);
                    break;
                case QEMU_OPTION_enable_sync_profile: qsp_enable(); break;
                case QEMU_OPTION_nouserconfig:
                    /* Nothing to be parsed here. Especially, do not error out below. */
                    break;
#if defined(CONFIG_POSIX) && !defined(EMSCRIPTEN)
                case QEMU_OPTION_daemonize: os_set_daemonize(true); break;
                case QEMU_OPTION_run_with : {
                    const char* str;
                    opts = qemu_opts_parse_noisily(qemu_find_opts("run-with"), optarg, false);
                    if (!opts) { exit(1); }
    #if defined(CONFIG_LINUX)
                    if (qemu_opt_get_bool(opts, "async-teardown", false)) { init_async_teardown(); }
    #endif
                    str = qemu_opt_get(opts, "chroot");
                    if (str) { os_set_chroot(str); }
                    str = qemu_opt_get(opts, "user");
                    if (str) {
                        if (!os_set_runas(str)) {
                            error_report("User \"%s\" doesn't exist"
                                         " (and is not <uid>:<gid>)",
                                         optarg);
                            exit(1);
                        }
                    }

                    break;
                }
#endif /* CONFIG_POSIX */

                default: error_report("Option not supported in this build"); exit(1);
            }
        }
    }
    /*
     * Clear error location left behind by the loop.
     * Best done right after the loop.  Do not insert code here!
     */
    loc_set_none();

    qemu_validate_options(machine_opts_dict);
    qemu_process_sugar_options();

    /*
     * These options affect everything else and should be processed
     * before daemonizing.
     */
    qemu_process_early_options();

    qemu_process_help_options();
    qemu_maybe_daemonize(pid_file);

    /*
     * The trace backend must be initialized after daemonizing.
     * trace_init_backends() will call st_init(), which will create the
     * trace thread in the parent, and also register st_flush_trace_buffer()
     * in atexit(). This function will force the parent to wait for the
     * writeout thread to finish, which will not occur, and the parent
     * process will be left in the host.
     */
    if (!trace_init_backends()) { exit(1); }
    trace_init_file();

    qemu_init_main_loop(&error_fatal);
    cpu_timers_init();

    user_register_global_props();

    configure_rtc(qemu_find_opts_singleton("rtc"));

    /* Transfer QemuOpts options into machine options */
    parse_memory_options();

    qemu_create_machine(machine_opts_dict);

    suspend_mux_open();

    qemu_disable_default_devices();
    qemu_setup_display();
    qemu_create_default_devices();
    qemu_create_early_backends();

    qemu_apply_legacy_machine_options(machine_opts_dict);
    qemu_apply_machine_options(machine_opts_dict);
    qobject_unref(machine_opts_dict);
    phase_advance(PHASE_MACHINE_CREATED);

    /*
     * Note: uses machine properties such as kernel-irqchip, must run
     * after qemu_apply_machine_options.
     */
    configure_accelerators(argv[0]);
    phase_advance(PHASE_ACCEL_CREATED);

    /*
     * Beware, QOM objects created before this point miss global and
     * compat properties.
     *
     * Global properties get set up by qdev_prop_register_global(),
     * called from user_register_global_props(), and certain option
     * desugaring.  Also in CPU feature desugaring (buried in
     * parse_cpu_option()), which happens below this point, but may
     * only target the CPU type, which can only be created after
     * parse_cpu_option() returned the type.
     *
     * Machine compat properties: object_set_machine_compat_props().
     * Accelerator compat props: object_set_accelerator_compat_props(),
     * called from do_configure_accelerator().
     */

    machine_class = MACHINE_GET_CLASS(current_machine);
    if (machine_class->deprecation_reason) {
        warn_report("Machine type '%s' is deprecated: %s", machine_class->name, machine_class->deprecation_reason);
    }

    qemu_create_late_backends();
    phase_advance(PHASE_LATE_BACKENDS_CREATED);

    /* parse features once if machine provides default cpu_type */
    current_machine->cpu_type = machine_class_default_cpu_type(machine_class);
    if (cpu_option) { current_machine->cpu_type = parse_cpu_option(cpu_option); }
    /* NB: for machine none cpu_type could STILL be NULL here! */

    qemu_resolve_machine_memdev();

    if (!preconfig_requested) { qmp_x_exit_preconfig(&error_fatal); }
    qemu_init_displays();
    accel_setup_post(current_machine);
    os_setup_post();
    resume_mux_open();
}
