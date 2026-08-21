if not have_tcg
   subdir_done()
endif

tcg_ss = ss.source_set()

tcg_ss.add(files(
  'cpu-exec.c',
  'cpu-exec-common.c',
  'tcg-runtime.c',
  'tcg-runtime-gvec.c',
  'tb-maint.c',
  'tcg-all.c',
  'tcg-stats.c',
  'translate-all.c',
  'translator.c',
))

system_ss.add_all(tcg_ss)

system_ss.add(files(
  'cputlb.c',
  'monitor.c',
  'tcg-accel-ops.c',
  'tcg-accel-ops-mttcg.c',
  'tcg-accel-ops-rr.c',
  'watchpoint.c',
))
