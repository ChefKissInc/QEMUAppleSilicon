HXCOMM See docs/devel/docs.rst for the format of this file.
HXCOMM
HXCOMM This file defines the contents of an array of HMPCommand structs
HXCOMM which specify the name, behaviour and help text for HMP commands.
HXCOMM Text between SRST and ERST is rST format documentation.
HXCOMM HXCOMM can be used for comments, discarded from both rST and C.


    {
        .name       = "help|?",
        .args_type  = "name:S?",
        .params     = "[cmd]",
        .help       = "show the help",
        .cmd        = hmp_help,
        .flags      = "p",
    },

SRST
``help`` or ``?`` [*cmd*]
  Show the help for all commands or just for command *cmd*.
ERST

    {
        .name       = "commit",
        .args_type  = "device:B",
        .params     = "device|all",
        .help       = "commit changes to the disk images (if -snapshot is used) or backing files",
        .cmd        = hmp_commit,
    },

SRST
``commit``
  Commit changes to the disk images (if -snapshot is used) or backing files.
  If the backing file is smaller than the snapshot, then the backing file
  will be resized to be the same size as the snapshot.  If the snapshot is
  smaller than the backing file, the backing file will not be truncated.
  If you want the backing file to match the size of the smaller snapshot,
  you can safely truncate it yourself once the commit operation successfully
  completes.
ERST

    {
        .name       = "quit|q",
        .args_type  = "",
        .params     = "",
        .help       = "quit the emulator",
        .cmd        = hmp_quit,
        .flags      = "p",
    },

SRST
``quit`` or ``q``
  Quit the emulator.
ERST

    {
        .name       = "exit_preconfig",
        .args_type  = "",
        .params     = "",
        .help       = "exit the preconfig state",
        .cmd        = hmp_exit_preconfig,
        .flags      = "p",
    },

SRST
``exit_preconfig``
  This command makes QEMU exit the preconfig state and proceed with
  VM initialization using configuration data provided on the command line
  and via the QMP monitor during the preconfig state. The command is only
  available during the preconfig state (i.e. when the --preconfig command
  line option was in use).
ERST

    {
        .name       = "block_resize",
        .args_type  = "device:B,size:o",
        .params     = "device size",
        .help       = "resize a block image",
        .cmd        = hmp_block_resize,
        .coroutine  = true,
        .flags      = "p",
    },

SRST
``block_resize``
  Resize a block image while a guest is running.  Usually requires guest
  action to see the updated size.  Resize to a lower size is supported,
  but should be used with extreme caution.  Note that this command only
  resizes image files, it can not resize block devices like LVM volumes.
ERST

    {
        .name       = "block_stream",
        .args_type  = "device:B,base:s?",
        .params     = "device [base]",
        .help       = "copy data from a backing file into a block device",
        .cmd        = hmp_block_stream,
        .flags      = "p",
    },

SRST
``block_stream``
  Copy data from a backing file into a block device.
ERST

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

SRST
``block_job_cancel``
  Stop an active background block operation (streaming, mirroring).
ERST

    {
        .name       = "block_job_complete",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "stop an active background block operation",
        .cmd        = hmp_block_job_complete,
        .flags      = "p",
    },

SRST
``block_job_complete``
  Manually trigger completion of an active background block operation.
  For mirroring, this will switch the device to the destination path.
ERST

    {
        .name       = "block_job_pause",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "pause an active background block operation",
        .cmd        = hmp_block_job_pause,
        .flags      = "p",
    },

SRST
``block_job_pause``
  Pause an active block streaming operation.
ERST

    {
        .name       = "block_job_resume",
        .args_type  = "device:B",
        .params     = "device",
        .help       = "resume a paused background block operation",
        .cmd        = hmp_block_job_resume,
        .flags      = "p",
    },

SRST
``block_job_resume``
  Resume a paused block streaming operation.
ERST

    {
        .name       = "eject",
        .args_type  = "force:-f,device:B",
        .params     = "[-f] device",
        .help       = "eject a removable medium (use -f to force it)",
        .cmd        = hmp_eject,
    },

SRST
``eject [-f]`` *device*
  Eject a removable medium (use -f to force it).
ERST

    {
        .name       = "drive_del",
        .args_type  = "id:B",
        .params     = "device",
        .help       = "remove host block device",
        .cmd        = hmp_drive_del,
    },

SRST
``drive_del`` *device*
  Remove host block device.  The result is that guest generated IO is no longer
  submitted against the host device underlying the disk.  Once a drive has
  been deleted, the QEMU Block layer returns -EIO which results in IO
  errors in the guest for applications that are reading/writing to the device.
  These errors are always reported to the guest, regardless of the drive's error
  actions (drive options rerror, werror).
ERST

    {
        .name       = "change",
        .args_type  = "device:B,force:-f,target:F,arg:s?,read-only-mode:s?",
        .params     = "device [-f] filename [format [read-only-mode]]",
        .help       = "change a removable medium, optional format, use -f to force the operation",
        .cmd        = hmp_change,
    },

SRST
``change`` *device* *setting*
  Change the configuration of a device.

  ``change`` *diskdevice* [-f] *filename* [*format* [*read-only-mode*]]
    Change the medium for a removable disk device to point to *filename*. eg::

      (qemu) change ide1-cd0 /path/to/some.iso

    ``-f``
      forces the operation even if the guest has locked the tray.

    *format* is optional.

    *read-only-mode* may be used to change the read-only status of the device.
    It accepts the following values:

    retain
      Retains the current status; this is the default.

    read-only
      Makes the device read-only.

    read-write
      Makes the device writable.

  ``change vnc password`` [*password*]

    Change the password associated with the VNC server. If the new password
    is not supplied, the monitor will prompt for it to be entered. VNC
    passwords are only significant up to 8 letters. eg::

      (qemu) change vnc password
      Password: ********

ERST

#ifdef CONFIG_PIXMAN
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

SRST
``screendump`` *filename*
  Save screen into PPM image *filename*.
ERST
#endif

    {
        .name       = "logfile",
        .args_type  = "filename:F",
        .params     = "filename",
        .help       = "output logs to 'filename'",
        .cmd        = hmp_logfile,
    },

SRST
``logfile`` *filename*
  Output logs to *filename*.
ERST

    {
        .name       = "trace-event",
        .args_type  = "name:s,option:b,vcpu:i?",
        .params     = "name on|off [vcpu]",
        .help       = "changes status of a specific trace event "
                      "(vcpu: vCPU to set, default is all)",
        .cmd = hmp_trace_event,
        .command_completion = trace_event_completion,
    },

SRST
``trace-event``
  changes status of a trace event
ERST

#if defined(CONFIG_TRACE_SIMPLE)
    {
        .name       = "trace-file",
        .args_type  = "op:s?,arg:F?",
        .params     = "on|off|flush|set [arg]",
        .help       = "open, close, or flush trace file, or set a new file name",
        .cmd        = hmp_trace_file,
    },

SRST
``trace-file on|off|flush``
  Open, close, or flush the trace file.  If no argument is given, the
  status of the trace file is displayed.
ERST
#endif

    {
        .name       = "log",
        .args_type  = "items:s",
        .params     = "item1[,...]",
        .help       = "activate logging of the specified items",
        .cmd        = hmp_log,
    },

SRST
``log`` *item1*\ [,...]
  Activate logging of the specified items.
ERST

    {
        .name       = "one-insn-per-tb",
        .args_type  = "option:s?",
        .params     = "[on|off]",
        .help       = "run emulation with one guest instruction per translation block",
        .cmd        = hmp_one_insn_per_tb,
    },

SRST
``one-insn-per-tb [off]``
  Run the emulation with one guest instruction per translation block.
  This slows down emulation a lot, but can be useful in some situations,
  such as when trying to analyse the logs produced by the ``-d`` option.
  This only has an effect when using TCG, not with KVM or other accelerators.

  If called with option off, the emulation returns to normal mode.
ERST

    {
        .name       = "stop|s",
        .args_type  = "",
        .params     = "",
        .help       = "stop emulation",
        .cmd        = hmp_stop,
    },

SRST
``stop`` or ``s``
  Stop emulation.
ERST

    {
        .name       = "cont|c",
        .args_type  = "",
        .params     = "",
        .help       = "resume emulation",
        .cmd        = hmp_cont,
    },

SRST
``cont`` or ``c``
  Resume emulation.
ERST

    {
        .name       = "system_wakeup",
        .args_type  = "",
        .params     = "",
        .help       = "wakeup guest from suspend",
        .cmd        = hmp_system_wakeup,
    },

SRST
``system_wakeup``
  Wakeup guest from suspend.
ERST

    {
        .name       = "gdbserver",
        .args_type  = "device:s?",
        .params     = "[device]",
        .help       = "start gdbserver on given device (default 'tcp::1234'), stop with 'none'",
        .cmd        = hmp_gdbserver,
    },

SRST
``gdbserver`` [*port*]
  Start gdbserver session (default *port*\=1234)
ERST

    {
        .name       = "x",
        .args_type  = "fmt:/,addr:l",
        .params     = "/fmt addr",
        .help       = "virtual memory dump starting at 'addr'",
        .cmd        = hmp_memory_dump,
    },

SRST
``x/``\ *fmt* *addr*
  Virtual memory dump starting at *addr*.
ERST

    {
        .name       = "xp",
        .args_type  = "fmt:/,addr:l",
        .params     = "/fmt addr",
        .help       = "physical memory dump starting at 'addr'",
        .cmd        = hmp_physical_memory_dump,
    },

SRST
``xp /``\ *fmt* *addr*
  Physical memory dump starting at *addr*.

  *fmt* is a format which tells the command how to format the
  data. Its syntax is: ``/{count}{format}{size}``

  *count*
    is the number of items to be dumped.
  *format*
    can be x (hex), d (signed decimal), u (unsigned decimal), o (octal),
    c (char) or i (asm instruction).
  *size*
    can be b (8 bits), h (16 bits), w (32 bits) or g (64 bits). On x86,
    ``h`` or ``w`` can be specified with the ``i`` format to
    respectively select 16 or 32 bit code instruction size.

  Examples:

  Dump 10 instructions at the current instruction pointer::

    (qemu) x/10i $eip
    0x90107063:  ret
    0x90107064:  sti
    0x90107065:  lea    0x0(%esi,1),%esi
    0x90107069:  lea    0x0(%edi,1),%edi
    0x90107070:  ret
    0x90107071:  jmp    0x90107080
    0x90107073:  nop
    0x90107074:  nop
    0x90107075:  nop
    0x90107076:  nop

  Dump 80 16 bit values at the start of the video memory::

    (qemu) xp/80hx 0xb8000
    0x000b8000: 0x0b50 0x0b6c 0x0b65 0x0b78 0x0b38 0x0b36 0x0b2f 0x0b42
    0x000b8010: 0x0b6f 0x0b63 0x0b68 0x0b73 0x0b20 0x0b56 0x0b47 0x0b41
    0x000b8020: 0x0b42 0x0b69 0x0b6f 0x0b73 0x0b20 0x0b63 0x0b75 0x0b72
    0x000b8030: 0x0b72 0x0b65 0x0b6e 0x0b74 0x0b2d 0x0b63 0x0b76 0x0b73
    0x000b8040: 0x0b20 0x0b30 0x0b35 0x0b20 0x0b4e 0x0b6f 0x0b76 0x0b20
    0x000b8050: 0x0b32 0x0b30 0x0b30 0x0b33 0x0720 0x0720 0x0720 0x0720
    0x000b8060: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
    0x000b8070: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
    0x000b8080: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
    0x000b8090: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720

ERST

    {
        .name       = "gpa2hva",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the host virtual address corresponding to a guest physical address",
        .cmd        = hmp_gpa2hva,
    },

SRST
``gpa2hva`` *addr*
  Print the host virtual address at which the guest's physical address *addr*
  is mapped.
ERST

#ifdef CONFIG_LINUX
    {
        .name       = "gpa2hpa",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the host physical address corresponding to a guest physical address",
        .cmd        = hmp_gpa2hpa,
    },
#endif

SRST
``gpa2hpa`` *addr*
  Print the host physical address at which the guest's physical address *addr*
  is mapped.
ERST

    {
        .name       = "gva2gpa",
        .args_type  = "addr:l",
        .params     = "addr",
        .help       = "print the guest physical address corresponding to a guest virtual address",
        .cmd        = hmp_gva2gpa,
    },

SRST
``gva2gpa`` *addr*
  Print the guest physical address at which the guest's virtual address *addr*
  is mapped based on the mapping for the current CPU.
ERST

    {
        .name       = "print|p",
        .args_type  = "fmt:/,val:l",
        .params     = "/fmt expr",
        .help       = "print expression value (use $reg for CPU register access)",
        .cmd        = hmp_print,
    },

SRST
``print`` or ``p/``\ *fmt* *expr*
  Print expression value. Only the *format* part of *fmt* is
  used.
ERST

    {
        .name       = "i",
        .args_type  = "fmt:/,addr:i,index:i.",
        .params     = "/fmt addr",
        .help       = "I/O port read",
        .cmd        = hmp_ioport_read,
    },

SRST
``i/``\ *fmt* *addr* [.\ *index*\ ]
  Read I/O port.
ERST

    {
        .name       = "o",
        .args_type  = "fmt:/,addr:i,val:i",
        .params     = "/fmt addr value",
        .help       = "I/O port write",
        .cmd        = hmp_ioport_write,
    },

SRST
``o/``\ *fmt* *addr* *val*
  Write to I/O port.
ERST

    {
        .name       = "sendkey",
        .args_type  = "keys:s,hold-time:i?",
        .params     = "keys [hold_ms]",
        .help       = "send keys to the VM (e.g. 'sendkey ctrl-alt-f1', default hold time=100 ms)",
        .cmd        = hmp_sendkey,
        .command_completion = sendkey_completion,
    },

SRST
``sendkey`` *keys*
  Send *keys* to the guest. *keys* could be the name of the
  key or the raw value in hexadecimal format. Use ``-`` to press
  several keys simultaneously. Example::

    sendkey ctrl-alt-f1

  This command is useful to send keys that your graphical user interface
  intercepts at low level, such as ``ctrl-alt-f1`` in X Window.
ERST
    {
        .name       = "sync-profile",
        .args_type  = "op:s?",
        .params     = "[on|off|reset]",
        .help       = "enable, disable or reset synchronization profiling. "
                      "With no arguments, prints whether profiling is on or off.",
        .cmd        = hmp_sync_profile,
    },

SRST
``sync-profile [on|off|reset]``
  Enable, disable or reset synchronization profiling. With no arguments, prints
  whether profiling is on or off.
ERST

    {
        .name       = "system_reset",
        .args_type  = "",
        .params     = "",
        .help       = "reset the system",
        .cmd        = hmp_system_reset,
    },

SRST
``system_reset``
  Reset the system.
ERST

    {
        .name       = "system_powerdown",
        .args_type  = "",
        .params     = "",
        .help       = "send system power down event",
        .cmd        = hmp_system_powerdown,
    },

SRST
``system_powerdown``
  Power down the system (if supported).
ERST

    {
        .name       = "sum",
        .args_type  = "start:i,size:i",
        .params     = "addr size",
        .help       = "compute the checksum of a memory region",
        .cmd        = hmp_sum,
    },

SRST
``sum`` *addr* *size*
  Compute the checksum of a memory region.
ERST

    {
        .name       = "device_add",
        .args_type  = "device:O",
        .params     = "driver[,prop=value][,...]",
        .help       = "add device, like -device on the command line",
        .cmd        = hmp_device_add,
        .command_completion = device_add_completion,
    },

SRST
``device_add`` *config*
  Add device.
ERST

    {
        .name       = "device_del",
        .args_type  = "id:s",
        .params     = "device",
        .help       = "remove device",
        .cmd        = hmp_device_del,
        .command_completion = device_del_completion,
    },

SRST
``device_del`` *id*
  Remove device *id*. *id* may be a short ID
  or a QOM object path.
ERST

    {
        .name       = "cpu",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set the default CPU",
        .cmd        = hmp_cpu,
    },

SRST
``cpu`` *index*
  Set the default CPU.
ERST

    {
        .name       = "mouse_move",
        .args_type  = "dx_str:s,dy_str:s,dz_str:s?",
        .params     = "dx dy [dz]",
        .help       = "send mouse move events",
        .cmd        = hmp_mouse_move,
    },

SRST
``mouse_move`` *dx* *dy* [*dz*]
  Move the active mouse to the specified coordinates *dx* *dy*
  with optional scroll axis *dz*.
ERST

    {
        .name       = "mouse_button",
        .args_type  = "button_state:i",
        .params     = "state",
        .help       = "change mouse button state (1=L, 2=M, 4=R)",
        .cmd        = hmp_mouse_button,
    },

SRST
``mouse_button`` *val*
  Change the active mouse button state *val* (1=L, 2=M, 4=R).
ERST

    {
        .name       = "mouse_set",
        .args_type  = "index:i",
        .params     = "index",
        .help       = "set which mouse device receives events",
        .cmd        = hmp_mouse_set,
    },

SRST
``mouse_set`` *index*
  Set which mouse device receives events at given *index*, index
  can be obtained with::

    info mice

ERST

    {
        .name       = "wavcapture",
        .args_type  = "path:F,audiodev:s,freq:i?,bits:i?,nchannels:i?",
        .params     = "path audiodev [frequency [bits [channels]]]",
        .help       = "capture audio to a wave file (default frequency=48000 bits=16 channels=2)",
        .cmd        = hmp_wavcapture,
    },
SRST
``wavcapture`` *filename* *audiodev* [*frequency* [*bits* [*channels*]]]
  Capture audio into *filename* from *audiodev*, using sample rate
  *frequency* bits per sample *bits* and number of channels
  *channels*.

  Defaults:

  - Sample rate = 48000 Hz - DVD quality
  - Bits = 16
  - Number of channels = 2 - Stereo
ERST

    {
        .name       = "stopcapture",
        .args_type  = "n:i",
        .params     = "capture index",
        .help       = "stop capture",
        .cmd        = hmp_stopcapture,
    },
SRST
``stopcapture`` *index*
  Stop capture with a given *index*, index can be obtained with::

    info capture

ERST

    {
        .name       = "memsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk virtual memory dump starting at 'addr' of size 'size'",
        .cmd        = hmp_memsave,
    },

SRST
``memsave`` *addr* *size* *file*
  save to disk virtual memory dump starting at *addr* of size *size*.
ERST

    {
        .name       = "pmemsave",
        .args_type  = "val:l,size:i,filename:s",
        .params     = "addr size file",
        .help       = "save to disk physical memory dump starting at 'addr' of size 'size'",
        .cmd        = hmp_pmemsave,
    },

SRST
``pmemsave`` *addr* *size* *file*
  save to disk physical memory dump starting at *addr* of size *size*.
ERST

    {
        .name       = "nmi",
        .args_type  = "",
        .params     = "",
        .help       = "inject an NMI",
        .cmd        = hmp_nmi,
    },
SRST
``nmi`` *cpu*
  Inject an NMI on the default CPU (x86).
ERST

    {
        .name       = "ringbuf_write",
        .args_type  = "device:s,data:s",
        .params     = "device data",
        .help       = "Write to a ring buffer character device",
        .cmd        = hmp_ringbuf_write,
        .command_completion = ringbuf_write_completion,
    },

SRST
``ringbuf_write`` *device* *data*
  Write *data* to ring buffer character device *device*.
  *data* must be a UTF-8 string.
ERST

    {
        .name       = "ringbuf_read",
        .args_type  = "device:s,size:i",
        .params     = "device size",
        .help       = "Read from a ring buffer character device",
        .cmd        = hmp_ringbuf_read,
        .command_completion = ringbuf_write_completion,
    },

SRST
``ringbuf_read`` *device*
  Read and print up to *size* bytes from ring buffer character
  device *device*.
  Certain non-printable characters are printed ``\uXXXX``, where ``XXXX`` is the
  character code in hexadecimal.  Character ``\`` is printed ``\\``.
  Bug: can screw up when the buffer contains invalid UTF-8 sequences,
  NUL characters, after the ring buffer lost data, and when reading
  stops because the size limit is reached.
ERST

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

SRST
``snapshot_blkdev``
  Snapshot device, using snapshot file as target if provided
ERST

    {
        .name       = "snapshot_blkdev_internal",
        .args_type  = "device:B,name:s",
        .params     = "device name",
        .help       = "take an internal snapshot of device.\n\t\t\t"
                      "The format of the image used by device must\n\t\t\t"
                      "support it, such as qcow2.\n\t\t\t",
        .cmd        = hmp_snapshot_blkdev_internal,
    },

SRST
``snapshot_blkdev_internal``
  Take an internal snapshot on device if it support
ERST

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

SRST
``snapshot_delete_blkdev_internal``
  Delete an internal snapshot on device if it support
ERST

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
SRST
``drive_mirror``
  Start mirroring a block device's writes to a new destination,
  using the specified target.
ERST

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
SRST
``drive_backup``
  Start a point-in-time copy of a block device to a specified target.
ERST

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

SRST
``drive_add``
  Add drive to PCI storage controller.
ERST

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

SRST
``pcie_aer_inject_error``
  Inject PCIe AER error
ERST

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

SRST
``netdev_add``
  Add host network device.
ERST

    {
        .name       = "netdev_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove host network device",
        .cmd        = hmp_netdev_del,
        .command_completion = netdev_del_completion,
        .flags      = "p",
    },

SRST
``netdev_del``
  Remove host network device.
ERST

    {
        .name       = "object_add",
        .args_type  = "object:S",
        .params     = "[qom-type=]type,id=str[,prop=value][,...]",
        .help       = "create QOM object",
        .cmd        = hmp_object_add,
        .command_completion = object_add_completion,
        .flags      = "p",
    },

SRST
``object_add``
  Create QOM object.
ERST

    {
        .name       = "object_del",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "destroy QOM object",
        .cmd        = hmp_object_del,
        .command_completion = object_del_completion,
        .flags      = "p",
    },

SRST
``object_del``
  Destroy QOM object.
ERST

#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_add",
        .args_type  = "arg1:s,arg2:s?",
        .params     = "[netdev_id] [tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport",
        .help       = "redirect TCP or UDP connections from host to guest (requires -net user)",
        .cmd        = hmp_hostfwd_add,
    },
#endif
SRST
``hostfwd_add``
  Redirect TCP or UDP connections from host to guest (requires -net user).
ERST

#ifdef CONFIG_SLIRP
    {
        .name       = "hostfwd_remove",
        .args_type  = "arg1:s,arg2:s?",
        .params     = "[netdev_id] [tcp|udp]:[hostaddr]:hostport",
        .help       = "remove host-to-guest TCP or UDP redirection",
        .cmd        = hmp_hostfwd_remove,
    },

#endif
SRST
``hostfwd_remove``
  Remove host-to-guest TCP or UDP redirection.
ERST

SRST
``set_link`` *name* ``[on|off]``
  Switch link *name* on (i.e. up) or off (i.e. down).
ERST

    {
        .name       = "watchdog_action",
        .args_type  = "action:s",
        .params     = "[reset|shutdown|poweroff|pause|debug|none|inject-nmi]",
        .help       = "change watchdog action",
        .cmd        = hmp_watchdog_action,
        .command_completion = watchdog_action_completion,
    },

SRST
``watchdog_action``
  Change watchdog action.
ERST

#ifdef CONFIG_POSIX
    {
        .name       = "getfd",
        .args_type  = "fdname:s",
        .params     = "getfd name",
        .help       = "receive a file descriptor via SCM rights and assign it a name",
        .cmd        = hmp_getfd,
        .flags      = "p",
    },

SRST
``getfd`` *fdname*
  If a file descriptor is passed alongside this command using the SCM_RIGHTS
  mechanism on unix sockets, it is stored using the name *fdname* for
  later use by other monitor commands.
ERST
#endif

    {
        .name       = "closefd",
        .args_type  = "fdname:s",
        .params     = "closefd name",
        .help       = "close a file descriptor previously passed via SCM rights",
        .cmd        = hmp_closefd,
        .flags      = "p",
    },

SRST
``closefd`` *fdname*
  Close the file descriptor previously assigned to *fdname* using the
  ``getfd`` command. This is only needed if the file descriptor was never
  used by another monitor command.
ERST

    {
        .name       = "set_password",
        .args_type  = "protocol:s,password:s,display:-ds,connected:s?",
        .params     = "protocol password [-d display] [action-if-connected]",
        .help       = "set vnc password",
        .cmd        = hmp_set_password,
    },

SRST
``set_password [ vnc ] password [ -d display ] [ action-if-connected ]``
  Change vnc password.  *display* can be used with 'vnc' to specify
  which display to set the password on.  *action-if-connected* specifies
  what should happen in case a connection is established: *fail* makes
  the password change fail.  *disconnect* changes the password and
  disconnects the client.  *keep* changes the password and keeps the
  connection up.  *keep* is the default.
ERST

    {
        .name       = "expire_password",
        .args_type  = "protocol:s,time:s,display:-ds",
        .params     = "protocol time [-d display]",
        .help       = "set vnc password expire-time",
        .cmd        = hmp_expire_password,
    },

SRST
``expire_password [ vnc ] expire-time [ -d display ]``
  Specify when a password for vnc becomes invalid.
  *display* behaves the same as in ``set_password``.
  *expire-time* accepts:

  ``now``
    Invalidate password instantly.
  ``never``
    Password stays valid forever.
  ``+``\ *nsec*
    Password stays valid for *nsec* seconds starting now.
  *nsec*
    Password is invalidated at the given time.  *nsec* are the seconds
    passed since 1970, i.e. unix epoch.

ERST

    {
        .name       = "chardev-add",
        .args_type  = "args:s",
        .params     = "args",
        .help       = "add chardev",
        .cmd        = hmp_chardev_add,
        .command_completion = chardev_add_completion,
    },

SRST
``chardev-add`` *args*
  chardev-add accepts the same parameters as the -chardev command line switch.
ERST

    {
        .name       = "chardev-change",
        .args_type  = "id:s,args:s",
        .params     = "id args",
        .help       = "change chardev",
        .cmd        = hmp_chardev_change,
    },

SRST
``chardev-change`` *args*
  chardev-change accepts existing chardev *id* and then the same arguments
  as the -chardev command line switch (except for "id").
ERST

    {
        .name       = "chardev-remove",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "remove chardev",
        .cmd        = hmp_chardev_remove,
        .command_completion = chardev_remove_completion,
    },

SRST
``chardev-remove`` *id*
  Removes the chardev *id*.
ERST

    {
        .name       = "chardev-send-break",
        .args_type  = "id:s",
        .params     = "id",
        .help       = "send a break on chardev",
        .cmd        = hmp_chardev_send_break,
        .command_completion = chardev_remove_completion,
    },

SRST
``chardev-send-break`` *id*
  Send a break on the chardev *id*.
ERST

    {
        .name       = "qom-list",
        .args_type  = "path:s?",
        .params     = "path",
        .help       = "list QOM properties",
        .cmd        = hmp_qom_list,
        .flags      = "p",
    },

SRST
``qom-list`` [*path*]
  Print QOM properties of object at location *path*
ERST

    {
        .name       = "qom-get",
        .args_type  = "path:s,property:s",
        .params     = "path property",
        .help       = "print QOM property",
        .cmd        = hmp_qom_get,
        .flags      = "p",
    },

SRST
``qom-get`` *path* *property*
  Print QOM property *property* of object at location *path*
ERST

    {
        .name       = "qom-set",
        .args_type  = "json:-j,path:s,property:s,value:S",
        .params     = "[-j] path property value",
        .help       = "set QOM property.\n\t\t\t"
                      "-j: the value is specified in json format.",
        .cmd        = hmp_qom_set,
        .flags      = "p",
    },

SRST
``qom-set`` *path* *property* *value*
  Set QOM property *property* of object at location *path* to value *value*
ERST

    {
        .name       = "info",
        .args_type  = "item:s?",
        .params     = "[subcommand]",
        .help       = "show various information about the system state",
        .cmd        = hmp_info_help,
        .sub_table  = hmp_info_cmds,
        .flags      = "p",
    },
