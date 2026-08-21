system_ss.add([files(
  'hostmem-ram.c',
  'hostmem.c',
  'rng-builtin.c',
  'rng-egd.c',
  'rng.c',
)])

if host_os != 'windows'
  system_ss.add(files('rng-random.c'))
  if host_os != 'emscripten'
    system_ss.add(files('hostmem-file.c'))
    system_ss.add([files('hostmem-shm.c'), rt])
  endif
endif
if host_os == 'linux'
  system_ss.add(files('hostmem-memfd.c'))
  system_ss.add(files('host_iommu_device.c'))
endif
