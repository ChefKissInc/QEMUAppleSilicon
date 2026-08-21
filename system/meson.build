specific_ss.add(when: 'CONFIG_SYSTEM_ONLY', if_true: [files(
  'arch_init.c',
  'globals-target.c',
)])

system_ss.add(files(
  'vl.c',
), sdl, libpmem, libdaxctl)

system_ss.add(files(
  'bootdevice.c',
  'cpus.c',
  'cpu-timers.c',
  'datadir.c',
  'dma-helpers.c',
  'globals.c',
  'ioport.c',
  'ram-block-attributes.c',
  'memory_mapping.c',
  'memory.c',
  'physmem.c',
  'qdev-monitor.c',
  'rtc.c',
  'runstate-action.c',
  'runstate-hmp-cmds.c',
  'runstate.c',
  'watchpoint.c',
))

system_ss.add(when: seccomp, if_true: files('qemu-seccomp.c'))
if host_os == 'linux'
  system_ss.add(files('async-teardown.c'))
endif
