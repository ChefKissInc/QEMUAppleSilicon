HXCOMM This file defines the contents of an array of HMPCommand structs
HXCOMM which specify the name, behaviour and help text for HMP commands.
HXCOMM HXCOMM can be used for comments.


    {
        .name       = "help|?",
        .args_type  = "name:S?",
        .params     = "[cmd]",
        .help       = "show the help",
        .cmd        = hmp_help,
        .flags      = "p",
    },


    {
        .name       = "commit",
        .args_type  = "device:B",
        .params     = "device|all",
        .help       = "commit changes to the disk images (if -snapshot is used) or backing files",
        .cmd        = hmp_commit,
    },


    {
        .name       = "quit|q",
        .args_type  = "",
        .params     = "",
        .help       = "quit the emulator",
        .cmd        = hmp_quit,
        .flags      = "p",
    },


    {
        .name       = "exit_preconfig",
        .args_type  = "",
        .params     = "",
        .help       = "exit the preconfig state",
        .cmd        = hmp_exit_preconfig,
        .flags      = "p",
    },


    {
        .name       = "block_resize",
        .args_type  = "device:B,size:o",
        .params     = "device size",
        .help       = "resize a block image",
        .cmd        = hmp_block_resize,
        .coroutine  = true,
        .flags      = "p",
    },


    {
        .name       = "block_stream",
        .args_type  = "device:B,base:s?",
        .params     = "device [base]",
        .help       = "copy data from a backing file into a block device",
        .cmd        = hmp_block_stream,
        .flags      = "p",
    },


    {
        .name       = "block_job_cancel",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "stop an active background block operation (use -f"
                      "\n\t\t\t if you want to abort the operation immediately"
                      "\n\t\t\t instead of keep running until data is in sync)",
        .cmd        = hmp_block_job_cancel,
        .flags      = "p",
    },


    {
        .name       = "block_job_complete",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "stop an active background block operation",
        .cmd        = hmp_block_job_complete,
        .flags      = "p",
    },


    {
        .name       = "block_job_pause",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "pause an active background block operation",
        .cmd        = hmp_block_job_pause,
        .flags      = "p",
    },


    {
        .name       = "block_job_resume",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "resume a paused background block operation",
        .cmd        = hmp_block_job_resume,
        .flags      = "p",
    },


    {
        .name       = "eject",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "eject a removable medium (use -f to force it)",
        .cmd        = hmp_eject,
    },


    {
        .name       = "drive_del",
        .args_type  = "id:B",
        .params     = "device",
        .help       = "remove host block device",
        .cmd        = hmp_drive_del,
    },


    {
        .name       = "change",
        .args_type  = "device:B,force:-f,target:F,arg:s?,read-only-mode:s?",
        .params     = "device [-f] filename [format [read-only-mode]]",
        .help       = "change a removable medium, optional format, use -f to force the operation",
        .cmd        = hmp_change,
    },

    {
        .name       = "screendump",
        .args_type  = "filename:F,format:-fs,device:s?,head:i?",
        .params     = "filename [-f format] [device [head]]",
        .help       = "save screen from head 'head' of display device 'device'"
                      "in specified format 'format' as image 'filename'."
                      "Currently only 'png' and 'ppm' formats are supported.",
         .cmd        = hmp_screendump,
        .coroutine  = true,
    },

    {
        .name       = "logfile",
        .args_type  = "filename:F",
        .params     = "filename",
        .help       = "output logs to 'filename'",
        .cmd        = hmp_logfile,
    },


    {
        .name       = "trace-event",
        .args_type  = "name:s,option:b,vcpu:i?",
        .params     = "name on|off [vcpu]",
        .help       = "changes status of a specific trace event "
                      "(vcpu: vCPU to set, default is all)",
        .cmd = hmp_trace_event,
        .command_completion = trace_event_completion,
    },


#if defined(CONFIG_TRACE_SIMPLE)
    {
        .name       = "trace-file",
        .args_type  = "op:s?,arg:F?",
        .params     = "on|off|flush|set [arg]",
        .help       = "open, close, or flush trace file, or set a new file name",
        .cmd        = hmp_trace_file,
    },

#endif

    {
        .name       = "log",
        .args_type  = "items:s",
        .params     = "item1[,...]",
        .help       = "activate logging of the specified items",
        .cmd        = hmp_log,
    },


    {
        .name       = "one-insn-per-tb",
        .args_type  = "option:s?",
        .params     = "[on|off]",
        .help       = "run emulation with one guest instruction per translation block",
        .cmd        = hmp_one_insn_per_tb,
    },


    {
        .name       = "stop|s",
        .args_type  = "",
        .params     = "",
        .help       = "stop emulation",
        .cmd        = hmp_stop,
    },


    {
        .name       = "cont|c",
        .args_type  = "",
        .params     = "",
        .help       = "resume emulation",
        .cmd        = hmp_cont,
    },


    {
        .name       = "system_wakeup",
        .args_type  = "",
        .params     = "",
        .help       = "wakeup guest from suspend",
        .cmd        = hmp_system_wakeup,
    },


    {
        .name       = "gdbserver",
        .args_type  = "device:s?",
        .params     = "[device]",
        .help       = "start gdbserver on given device (default 'tcp::1234'), stop with 'none'",
        .cmd        = hmp_gdbserver,
    },


    {
        .name       = "x",
        .args_type  = "fmt:/,addr:l",
        .params     = "/fmt addr",
        .help       = "virtual memory dump starting at 'addr'",
        .cmd        = hmp_memory_dump,
    },


    {
        .name       = "xp",
        .args_type  = "fmt:/,addr:l",
        .params     = "/fmt addr",
        .help       = "physical memory dump starting at 'addr'",
        .cmd        = hmp_physical_memory_dump,
    },


    {
        .name       = "gpa2hva",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the host virtual address corresponding to a guest physical address",
        .cmd        = hmp_gpa2hva,
    },


#ifdef CONFIG_LINUX
    {
        .name       = "gpa2hpa",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the host physical address corresponding to a guest physical address",
        .cmd        = hmp_gpa2hpa,
    },
#endif


    {
        .name       = "gva2gpa",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the guest physical address corresponding to a guest virtual address",
        .cmd        = hmp_gva2gpa,
    },


    {
        .name       = "print|p",
        .args_type  = "fmt:/,val:l",
        .params     = "/fmt expr",
        .help       = "print expression value (use $reg for CPU register access)",
        .cmd        = hmp_print,
    },


    {
        .name       = "i",
        .args_type  = "fmt:/,addr:i,index:i.",
        .params     = "/fmt addr",
        .help       = "I/O port read",
        .cmd        = hmp_ioport_read,
    },


    {
        .name       = "o",
        .args_type  = "fmt:/,addr:i,val:i",
        .params     = "/fmt addr value",
        .help       = "I/O port write",
        .cmd        = hmp_ioport_write,
    },


    {
        .name       = "sendkey",
        .args_type  = "keys:s,hold-time:i?",
        .params     = "keys [hold_ms]",
        .help       = "send keys to the VM (e.g. 'sendkey ctrl-alt-f1', default hold time=100 ms)",
        .cmd        = hmp_sendkey,
        .command_completion = sendkey_completion,
    },

    {
        .name       = "sync-profile",
        .args_type  = "op:s?",
        .params     = "[on|off|reset]",
        .help       = "enable, disable or reset synchronization profiling. "
                      "With no arguments, prints whether profiling is on or off.",
        .cmd        = hmp_sync_profile,
    },


    {
        .name       = "system_reset",
        .args_type  = "",
        .params     = "",
        .help       = "reset the system",
        .cmd        = hmp_system_reset,
    },


    {
        .name       = "system_powerdown",
        .args_type  = "",
        .params     = "",
        .help       = "send system power down event",
        .cmd        = hmp_system_powerdown,
    },


    {
        .name       = "sum",
        .args_type  = "start:i,size:i",
        .params     = "addr size",
        .help       = "compute the checksum of a memory region",
        .cmd        = hmp_sum,
    },


    {
        .name       = "device_add",
        .args_type  = "device:O",
        .params     = "driver[,prop=value][,...]",
        .help       = "add device, like -device on the command line",
        .cmd        = hmp_device_add,
        .command_completion = device_add_completion,
    },


    {
        .name       = "device_del",
        .args_type  = "id:s",
        .params     = "device",
        .help       = "remove device",
        .cmd        = hmp_device_del,
        .command_completion = device_del_completion,
    },


    {
        .name       = "cpu",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set the default CPU",
        .cmd        = hmp_cpu,
    },


    {
        .name       = "mouse_move",
        .args_type  = "dx_str:s,dy_str:s,dz_str:s?",
        .params     = "dx dy [dz]",
        .help       = "send mouse move events",
        .cmd        = hmp_mouse_move,
    },


    {
        .name       = "mouse_button",
        .args_type  = "button_state:i",
        .params     = "state",
        .help       = "change mouse button state (1=L, 2=M, 4=R)",
        .cmd        = hmp_mouse_button,
    },


    {
        .name       = "mouse_set",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set which mouse device receives events",
        .cmd        = hmp_mouse_set,
    },


    {
        .name       = "wavcapture",
        .args_type  = "path:F,audiodev:s,freq:i?,bits:i?,nchannels:i?",
        .params     = "path audiodev [frequency [bits [channels]]]",
        .help       = "capture audio to a wave file (default frequency=48000 bits=16 channels=2)",
        .cmd        = hmp_wavcapture,
    },

    {
        .name       = "stopcapture",
        .args_type  = "n:i",
        .params     = "capture index",
        .help       = "stop capture",
        .cmd        = hmp_stopcapture,
    },

    {
        .name       = "memsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk virtual memory dump starting at 'addr' of size 'size'",
        .cmd        = hmp_memsave,
    },


    {
        .name       = "pmemsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk physical memory dump starting at 'addr' of size 'size'",
        .cmd        = hmp_pmemsave,
    },


    {
        .name       = "nmi",
        .args_type  = "",
        .params     = "",
        .help       = "inject an NMI",
        .cmd        = hmp_nmi,
    },

    {
        .name       = "ringbuf_write",
        .args_type  = "device:s,data:s",
        .params     = "device data",
        .help       = "Write to a ring buffer character device",
        .cmd        = hmp_ringbuf_write,
        .command_completion = ringbuf_write_completion,
    },


    {
        .name       = "ringbuf_read",
        .args_type  = "device:s,size:i",
        .params     = "device size",
        .help       = "Read from a ring buffer character device",
        .cmd        = hmp_ringbuf_read,
        .command_completion = ringbuf_write_completion,
    },


    {
        .name       = "snapshot_blkdev",
        .args_type  = "reuse:-n,device:B,snapshot-file:s?,format:s?",
        .params     = "[-n] device [new-image-file] [format]",
        .help       = "initiates a live snapshot\n\t\t\t"
                      "of device. If a new image file is specified, the\n\t\t\t"
                      "new image file will become the new root image.\n\t\t\t"
                      "If format is specified, the snapshot file will\n\t\t\t"
                      "be created in that format.\n\t\t\t"
                      "The default format is qcow2.  The -n flag requests QEMU\n\t\t\t"
                      "to reuse the image found in new-image-file, instead of\n\t\t\t"
                      "recreating it from scratch.",
        .cmd        = hmp_snapshot_blkdev,
    },


    {
        .name       = "snapshot_blkdev_internal",
        .args_type  = "device:B,name:s",
        .params     = "device name",
        .help       = "take an internal snapshot of device.\n\t\t\t"
                      "The format of the image used by device must\n\t\t\t"
                      "support it, such as qcow2.\n\t\t\t",
        .cmd        = hmp_snapshot_blkdev_internal,
    },


    {
        .name       = "snapshot_delete_blkdev_internal",
        .args_type  = "device:B,name:s,id:s?",
        .params     = "device name [id]",
        .help       = "delete an internal snapshot of device.\n\t\t\t"
                      "If id is specified, qemu will try delete\n\t\t\t"
                      "the snapshot matching both id and name.\n\t\t\t"
                      "The format of the image used by device must\n\t\t\t"
                      "support it, such as qcow2.\n\t\t\t",
        .cmd        = hmp_snapshot_delete_blkdev_internal,
    },


    {
        .name       = "drive_mirror",
        .args_type  = "reuse:-n,full:-f,device:B,target:s,format:s?",
        .params     = "[-n] [-f] device target [format]",
        .help       = "initiates live storage\n\t\t\t"
                      "migration for a device. The device's contents are\n\t\t\t"
                      "copied to the new image file, including data that\n\t\t\t"
                      "is written after the command is started.\n\t\t\t"
                      "The -n flag requests QEMU to reuse the image found\n\t\t\t"
                      "in new-image-file, instead of recreating it from scratch.\n\t\t\t"
                      "The -f flag requests QEMU to copy the whole disk,\n\t\t\t"
                      "so that the result does not need a backing file.\n\t\t\t",
        .cmd        = hmp_drive_mirror,
    },

    {
        .name       = "drive_backup",
        .args_type  = "reuse:-n,full:-f,compress:-c,device:B,target:s,format:s?",
        .params     = "[-n] [-f] [-c] device target [format]",
        .help       = "initiates a point-in-time\n\t\t\t"
                      "copy for a device. The device's contents are\n\t\t\t"
                      "copied to the new image file, excluding data that\n\t\t\t"
                      "is written after the command is started.\n\t\t\t"
                      "The -n flag requests QEMU to reuse the image found\n\t\t\t"
                      "in new-image-file, instead of recreating it from scratch.\n\t\t\t"
                      "The -f flag requests QEMU to copy the whole disk,\n\t\t\t"
                      "so that the result does not need a backing file.\n\t\t\t"
                      "The -c flag requests QEMU to compress backup data\n\t\t\t"
                      "(if the target format supports it).\n\t\t\t",
        .cmd        = hmp_drive_backup,
    },

    {
        .name       = "drive_add",
        .args_type  = "node:-n,pci_addr:s,opts:s",
        .params     = "[-n] [[<domain>:]<bus>:]<slot>\n"
                      "[file=file][,if=type][,bus=n]\n"
                      "[,unit=m][,media=d][,index=i]\n"
                      "[,snapshot=on|off][,cache=on|off]\n"
                      "[,readonly=on|off][,copy-on-read=on|off]",
        .help       = "add drive to PCI storage controller",
        .cmd        = hmp_drive_add,
    },


    {
        .name       = "pcie_aer_inject_error",
        .args_type  = "advisory_non_fatal:-a,correctable:-c,"
	              "id:s,error_status:s,"
	              "header0:i?,header1:i?,header2:i?,header3:i?,"
	              "prefix0:i?,prefix1:i?,prefix2:i?,prefix3:i?",
        .params     = "[-a] [-c] id "
                      "<error_status> [<tlp header> [<tlp header prefix>]]",
        .help       = "inject pcie aer error\n\t\t\t"
	              " -a for advisory non fatal error\n\t\t\t"
	              " -c for correctable error\n\t\t\t"
                      "<id> = qdev device id\n\t\t\t"
                      "<error_status> = error string or 32bit\n\t\t\t"
                      "<tlp header> = 32bit x 4\n\t\t\t"
                      "<tlp header prefix> = 32bit x 4",
        .cmd        = hmp_pcie_aer_inject_error,
    },


    {
        .name       = "netdev_add",
        .args_type  = "netdev:O",
        .params     = "[user|tap|socket|stream|dgram|vde|bridge|hubport|netmap"
#ifdef CONFIG_VMNET
                      "|vmnet-host|vmnet-shared|vmnet-bridged"
#endif
                      "],id=str[,prop=value][,...]",
        .help       = "add host network device",
        .cmd        = hmp_netdev_add,
        .command_completion = netdev_add_completion,
        .flags      = "p",
    },


    {
        .name       = "netdev_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove host network device",
        .cmd        = hmp_netdev_del,
        .command_completion = netdev_del_completion,
        .flags      = "p",
    },


    {
        .name       = "object_add",
        .args_type  = "object:S",
        .params     = "[qom-type=]type,id=str[,prop=value][,...]",
        .help       = "create QOM object",
        .cmd        = hmp_object_add,
        .command_completion = object_add_completion,
        .flags      = "p",
    },


    {
        .name       = "object_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "destroy QOM object",
        .cmd        = hmp_object_del,
        .command_completion = object_del_completion,
        .flags      = "p",
    },


#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_add",
        .args_type  = "arg1:s,arg2:s?",
        .params     = "[netdev_id] [tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport",
        .help       = "redirect TCP or UDP connections from host to guest (requires -net user)",
        .cmd        = hmp_hostfwd_add,
    },
#endif

#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_remove",
        .args_type  = "arg1:s,arg2:s?",
        .params     = "[netdev_id] [tcp|udp]:[hostaddr]:hostport",
        .help       = "remove host-to-guest TCP or UDP redirection",
        .cmd        = hmp_hostfwd_remove,
    },

#endif


    {
        .name       = "watchdog_action",
        .args_type  = "action:s",
        .params     = "[reset|shutdown|poweroff|pause|debug|none|inject-nmi]",
        .help       = "change watchdog action",
        .cmd        = hmp_watchdog_action,
        .command_completion = watchdog_action_completion,
    },


#ifdef CONFIG_POSIX
    {
        .name       = "getfd",
        .args_type  = "fdname:s",
        .params     = "getfd name",
        .help       = "receive a file descriptor via SCM rights and assign it a name",
        .cmd        = hmp_getfd,
        .flags      = "p",
    },

#endif

    {
        .name       = "closefd",
        .args_type  = "fdname:s",
        .params     = "closefd name",
        .help       = "close a file descriptor previously passed via SCM rights",
        .cmd        = hmp_closefd,
        .flags      = "p",
    },


    {
        .name       = "set_password",
        .args_type  = "protocol:s,password:s,display:-ds,connected:s?",
        .params     = "protocol password [-d display] [action-if-connected]",
        .help       = "set vnc password",
        .cmd        = hmp_set_password,
    },


    {
        .name       = "expire_password",
        .args_type  = "protocol:s,time:s,display:-ds",
        .params     = "protocol time [-d display]",
        .help       = "set vnc password expire-time",
        .cmd        = hmp_expire_password,
    },


    {
        .name       = "chardev-add",
        .args_type  = "args:s",
        .params     = "args",
        .help       = "add chardev",
        .cmd        = hmp_chardev_add,
        .command_completion = chardev_add_completion,
    },


    {
        .name       = "chardev-change",
        .args_type  = "id:s,args:s",
        .params     = "id args",
        .help       = "change chardev",
        .cmd        = hmp_chardev_change,
    },


    {
        .name       = "chardev-remove",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove chardev",
        .cmd        = hmp_chardev_remove,
        .command_completion = chardev_remove_completion,
    },


    {
        .name       = "chardev-send-break",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "send a break on chardev",
        .cmd        = hmp_chardev_send_break,
        .command_completion = chardev_remove_completion,
    },


    {
        .name       = "qom-list",
        .args_type  = "path:s?",
        .params     = "path",
        .help       = "list QOM properties",
        .cmd        = hmp_qom_list,
        .flags      = "p",
    },


    {
        .name       = "qom-get",
        .args_type  = "path:s,property:s",
        .params     = "path property",
        .help       = "print QOM property",
        .cmd        = hmp_qom_get,
        .flags      = "p",
    },


    {
        .name       = "qom-set",
        .args_type  = "json:-j,path:s,property:s,value:S",
        .params     = "[-j] path property value",
        .help       = "set QOM property.\n\t\t\t"
                      "-j: the value is specified in json format.",
        .cmd        = hmp_qom_set,
        .flags      = "p",
    },


    {
        .name       = "info",
        .args_type  = "item:s?",
        .params     = "[subcommand]",
        .help       = "show various information about the system state",
        .cmd        = hmp_info_help,
        .sub_table  = hmp_info_cmds,
        .flags      = "p",
    },
