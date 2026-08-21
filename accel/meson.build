common_ss.add(files('accel-common.c'))
specific_ss.add(files('accel-target.c'))
system_ss.add(files('accel-system.c', 'accel-blocker.c', 'accel-qmp.c'))

subdir('tcg')
if have_system
  subdir('hvf')
  subdir('kvm')
  subdir('stubs')
endif
