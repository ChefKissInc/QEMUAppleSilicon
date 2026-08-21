HXCOMM See docs/devel/docs.rst for the format of this file.
HXCOMM
HXCOMM This file defines the contents of an array of HMPCommand structs
HXCOMM which specify the name, behaviour and help text for HMP commands.
HXCOMM Text between SRST and ERST is rST format documentation.
HXCOMM HXCOMM can be used for comments, discarded from both rST and C.
HXCOMM
HXCOMM In this file, generally SRST fragments should have two extra
HXCOMM spaces of indent, so that the documentation list item for "info foo"
HXCOMM appears inside the documentation list item for the top level
HXCOMM "info" documentation entry. The exception is the first SRST
HXCOMM fragment that defines that top level entry.

SRST
``info`` *subcommand*
  Show various information about the system state.

ERST

    {
        .name       = "version",
        .args_type  = "",
        .params     = "",
        .help       = "show the version of QEMU",
        .cmd        = hmp_info_version,
        .flags      = "p",
    },

SRST
  ``info version``
    Show the version of QEMU.
ERST

    {
        .name       = "network",
        .args_type  = "",
        .params     = "",
        .help       = "show the network state",
        .cmd        = hmp_info_network,
    },

SRST
  ``info network``
    Show the network state.
ERST

    {
        .name       = "chardev",
        .args_type  = "",
        .params     = "",
        .help       = "show the character devices",
        .cmd        = hmp_info_chardev,
        .flags      = "p",
    },

SRST
  ``info chardev``
    Show the character devices.
ERST

    {
        .name       = "block",
        .args_type  = "nodes:-n,verbose:-v,device:B?",
        .params     = "[-n] [-v] [device]",
        .help       = "show info of one block device or all block devices "
                      "(-n: show named nodes; -v: show details)",
        .cmd        = hmp_info_block,
    },

SRST
  ``info block``
    Show info of one block device or all block devices.
ERST

    {
        .name       = "blockstats",
        .args_type  = "",
        .params     = "",
        .help       = "show block device statistics",
        .cmd        = hmp_info_blockstats,
    },

SRST
  ``info blockstats``
    Show block device statistics.
ERST

    {
        .name       = "block-jobs",
        .args_type  = "",
        .params     = "",
        .help       = "show progress of ongoing block device operations",
        .cmd        = hmp_info_block_jobs,
    },

SRST
  ``info block-jobs``
    Show progress of ongoing block device operations.
ERST

    {
        .name       = "registers",
        .args_type  = "cpustate_all:-a,vcpu:i?",
        .params     = "[-a|vcpu]",
        .help       = "show the cpu registers (-a: show register info for all cpus;"
                      " vcpu: specific vCPU to query; show the current CPU's registers if"
                      " no argument is specified)",
        .cmd        = hmp_info_registers,
    },

SRST
  ``info registers``
    Show the cpu registers.
ERST

    {
        .name       = "cpus",
        .args_type  = "",
        .params     = "",
        .help       = "show infos for each CPU",
        .cmd        = hmp_info_cpus,
    },

SRST
  ``info cpus``
    Show infos for each CPU.
ERST

    {
        .name       = "history",
        .args_type  = "",
        .params     = "",
        .help       = "show the command line history",
        .cmd        = hmp_info_history,
        .flags      = "p",
    },

SRST
  ``info history``
    Show the command line history.
ERST

    {
        .name       = "irq",
        .args_type  = "",
        .params     = "",
        .help       = "show the interrupts statistics (if available)",
        .cmd_info_hrt = qmp_x_query_irq,
    },

SRST
  ``info irq``
    Show the interrupts statistics (if available).
ERST

    {
        .name       = "pic",
        .args_type  = "",
        .params     = "",
        .help       = "show PIC state",
        .cmd_info_hrt = qmp_x_query_interrupt_controllers,
    },

SRST
  ``info pic``
    Show PIC state.
ERST

    {
        .name       = "pci",
        .args_type  = "",
        .params     = "",
        .help       = "show PCI info",
        .cmd        = hmp_info_pci,
    },

SRST
  ``info pci``
    Show PCI information.
ERST

    {
        .name       = "mtree",
        .args_type  = "flatview:-f,dispatch_tree:-d,owner:-o,disabled:-D",
        .params     = "[-f][-d][-o][-D]",
        .help       = "show memory tree (-f: dump flat view for address spaces;"
                      "-d: dump dispatch tree, valid with -f only);"
                      "-o: dump region owners/parents;"
                      "-D: dump disabled regions",
        .cmd        = hmp_info_mtree,
    },

SRST
  ``info mtree``
    Show memory tree.
ERST

#if defined(CONFIG_TCG)
    {
        .name       = "jit",
        .args_type  = "",
        .params     = "",
        .help       = "show dynamic compiler info",
    },
#endif

SRST
  ``info jit``
    Show dynamic compiler info.
ERST

    {
        .name       = "sync-profile",
        .args_type  = "mean:-m,no_coalesce:-n,max:i?",
        .params     = "[-m] [-n] [max]",
        .help       = "show synchronization profiling info, up to max entries "
                      "(default: 10), sorted by total wait time. (-m: sort by "
                      "mean wait time; -n: do not coalesce objects with the "
                      "same call site)",
        .cmd        = hmp_info_sync_profile,
    },

    {
        .name       = "accel",
        .args_type  = "",
        .params     = "",
        .help       = "show accelerator info",
    },

SRST
  ``info accel``
    Show accelerator info.
ERST

SRST
  ``info sync-profile [-m|-n]`` [*max*]
    Show synchronization profiling info, up to *max* entries (default: 10),
    sorted by total wait time.

    ``-m``
      sort by mean wait time
    ``-n``
      do not coalesce objects with the same call site

    When different objects that share the same call site are coalesced,
    the "Object" field shows---enclosed in brackets---the number of objects
    being coalesced.
ERST

    {
        .name       = "kvm",
        .args_type  = "",
        .params     = "",
        .help       = "show KVM information",
        .cmd        = hmp_info_kvm,
    },

SRST
  ``info kvm``
    Show KVM information.
ERST

SRST
  ``info usb``
    Show guest USB devices.
ERST

    {
        .name       = "usbhost",
        .args_type  = "",
        .params     = "",
        .help       = "show host USB devices",
    },

SRST
  ``info usbhost``
    Show host USB devices.
ERST

    {
        .name       = "capture",
        .args_type  = "",
        .params     = "",
        .help       = "show capture information",
        .cmd        = hmp_info_capture,
    },

SRST
  ``info capture``
    Show capture information.
ERST

    {
        .name       = "snapshots",
        .args_type  = "",
        .params     = "",
        .help       = "show the currently saved VM snapshots",
        .cmd        = hmp_info_snapshots,
    },

SRST
  ``info snapshots``
    Show the currently saved VM snapshots.
ERST

    {
        .name       = "status",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM status (running|paused)",
        .cmd        = hmp_info_status,
        .flags      = "p",
    },

SRST
  ``info status``
    Show the current VM status (running|paused).
ERST

    {
        .name       = "mice",
        .args_type  = "",
        .params     = "",
        .help       = "show which guest mouse is receiving events",
        .cmd        = hmp_info_mice,
    },

SRST
  ``info mice``
    Show which guest mouse is receiving events.
ERST

#if defined(CONFIG_VNC)
    {
        .name       = "vnc",
        .args_type  = "",
        .params     = "",
        .help       = "show the vnc server status",
        .cmd        = hmp_info_vnc,
    },
#endif

SRST
  ``info vnc``
    Show the vnc server status.
ERST

    {
        .name       = "name",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM name",
        .cmd        = hmp_info_name,
        .flags      = "p",
    },

SRST
  ``info name``
    Show the current VM name.
ERST

    {
        .name       = "uuid",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM UUID",
        .cmd        = hmp_info_uuid,
        .flags      = "p",
    },

SRST
  ``info uuid``
    Show the current VM UUID.
ERST

#if defined(CONFIG_SLIRP)
    {
        .name       = "usernet",
        .args_type  = "",
        .params     = "",
        .help       = "show user network stack connection states",
        .cmd        = hmp_info_usernet,
    },
#endif

SRST
  ``info usernet``
    Show user network stack connection states.
ERST

    {
        .name       = "qtree",
        .args_type  = "brief:-b",
        .params     = "[-b]",
        .help       = "show device tree (-b: brief, omit properties)",
        .cmd        = hmp_info_qtree,
    },

SRST
  ``info qtree``
    Show device tree.
ERST

    {
        .name       = "qdm",
        .args_type  = "",
        .params     = "",
        .help       = "show qdev device model list",
        .cmd        = hmp_info_qdm,
    },

SRST
  ``info qdm``
    Show qdev device model list.
ERST

    {
        .name       = "qom-tree",
        .args_type  = "path:s?",
        .params     = "[path]",
        .help       = "show QOM composition tree",
        .cmd        = hmp_info_qom_tree,
        .flags      = "p",
    },

SRST
  ``info qom-tree``
    Show QOM composition tree.
ERST

    {
        .name       = "trace-events",
        .args_type  = "name:s?,vcpu:i?",
        .params     = "[name] [vcpu]",
        .help       = "show available trace-events & their state "
                      "(name: event name pattern; vcpu: vCPU to query, default is any)",
        .cmd = hmp_info_trace_events,
        .command_completion = info_trace_events_completion,
    },

SRST
  ``info trace-events``
    Show available trace-events & their state.
ERST

    {
        .name       = "memdev",
        .args_type  = "",
        .params     = "",
        .help       = "show memory backends",
        .cmd        = hmp_info_memdev,
        .flags      = "p",
    },

SRST
  ``info memdev``
    Show memory backends
ERST

SRST
  ``info iothreads``
    Show iothread's identifiers.
ERST

    {
        .name       = "ramblock",
        .args_type  = "",
        .params     = "",
        .help       = "Display system ramblock information",
        .cmd_info_hrt = qmp_x_query_ramblock,
    },

SRST
  ``info ramblock``
    Dump all the ramblocks of the system.
ERST

    {
        .name       = "hotpluggable-cpus",
        .args_type  = "",
        .params     = "",
        .help       = "Show information about hotpluggable CPUs",
        .cmd        = hmp_hotpluggable_cpus,
        .flags      = "p",
    },

SRST
  ``info hotpluggable-cpus``
    Show information about hotpluggable CPUs
ERST

    {
        .name       = "memory_size_summary",
        .args_type  = "",
        .params     = "",
        .help       = "show the amount of initially allocated and "
                      "present hotpluggable (if enabled) memory in bytes.",
        .cmd        = hmp_info_memory_size_summary,
    },

SRST
  ``info memory_size_summary``
    Display the amount of initially allocated and present hotpluggable (if
    enabled) memory in bytes.
ERST

#if defined(TARGET_AARCH64)
    {
        .name         = "dart",
        .args_type    = "name:s?",
        .params       = "[name]",
        .help         = "show guest Apple DART IOMMUs",
        .cmd          = hmp_info_dart,
    },
#endif

SRST
  ``info dart``
    Show guest Apple DART IOMMUs.
ERST

    {
        .name       = "stats",
        .args_type  = "target:s,names:s?,provider:s?",
        .params     = "target [names] [provider]",
        .help       = "show statistics for the given target (vm or vcpu); optionally filter by"
                      "name (comma-separated list, or * for all) and provider",
        .cmd        = hmp_info_stats,
    },

SRST
  ``info stats``
    Show runtime-collected statistics
ERST
