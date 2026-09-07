project('mlib', 'c')

py = import('python').find_installation()
mlib_include_dir = meson.current_build_dir() / 'include'
run_command(py, '-c',
  'import os,sys; d=sys.argv[1]; os.path.lexists(d) or os.symlink(sys.argv[2], d)',
  mlib_include_dir / 'mlib',
  meson.current_source_dir(),
  check: true
)
