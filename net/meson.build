system_ss.add(files(
  'announce.c',
  'checksum.c',
  'dgram.c',
  'eth.c',
  'hub.c',
  'net-hmp-cmds.c',
  'net.c',
  'queue.c',
  'socket.c',
  'stream.c',
  'stream_data.c',
  'util.c',
))

if have_l2tpv3
  system_ss.add(files('l2tpv3.c'))
endif
system_ss.add(when: slirp, if_true: files('slirp.c'))
system_ss.add(when: vde, if_true: files('vde.c'))
if have_netmap
  system_ss.add(files('netmap.c'))
endif

if host_os == 'windows'
  system_ss.add(files('tap-win32.c'))
elif host_os == 'linux'
  system_ss.add(files('tap.c', 'tap-linux.c'))
elif host_os in bsd_oses
  system_ss.add(files('tap.c', 'tap-bsd.c'))
elif host_os == 'sunos'
  system_ss.add(files('tap.c', 'tap-solaris.c'))
else
  system_ss.add(files('tap.c', 'tap-stub.c'))
endif

vmnet_files = files(
  'vmnet-common.m',
  'vmnet-bridged.m',
  'vmnet-host.c',
  'vmnet-shared.c'
)
system_ss.add(when: vmnet, if_true: vmnet_files)
subdir('can')
