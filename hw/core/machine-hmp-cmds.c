/*
 * HMP commands related to machines and CPUs
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qapi/qapi-commands-accelerator.h"
#include "qapi/qapi-commands-machine.h"
#include "qobject/qdict.h"
#include "qapi/string-output-visitor.h"
#include "qemu/error-report.h"
#include "hw/boards.h"

void hmp_info_cpus(Monitor *mon, const QDict *qdict)
{
    CpuInfoFastList *cpu_list, *cpu;

    cpu_list = qmp_query_cpus_fast(NULL);

    for (cpu = cpu_list; cpu; cpu = cpu->next) {
        g_autofree char *cpu_model = cpu_model_from_type(cpu->value->qom_type);
        int active = ' ';

        if (cpu->value->cpu_index == monitor_get_cpu_index(mon)) {
            active = '*';
        }

        monitor_printf(mon, "%c CPU #%" PRId64 ":", active,
                       cpu->value->cpu_index);
        monitor_printf(mon, " thread_id=%" PRId64 " model=%s\n",
                       cpu->value->thread_id, cpu_model);
    }

    qapi_free_CpuInfoFastList(cpu_list);
}

void hmp_hotpluggable_cpus(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    HotpluggableCPUList *l = qmp_query_hotpluggable_cpus(&err);
    HotpluggableCPUList *saved = l;
    CpuInstanceProperties *c;

    if (hmp_handle_error(mon, err)) {
        return;
    }

    monitor_printf(mon, "Hotpluggable CPUs:\n");
    while (l) {
        monitor_printf(mon, "  type: \"%s\"\n", l->value->type);
        monitor_printf(mon, "  vcpus_count: \"%" PRIu64 "\"\n",
                       l->value->vcpus_count);
        if (l->value->qom_path) {
            monitor_printf(mon, "  qom_path: \"%s\"\n", l->value->qom_path);
        }

        c = l->value->props;
        monitor_printf(mon, "  CPUInstance Properties:\n");
        if (c->has_drawer_id) {
            monitor_printf(mon, "    drawer-id: \"%" PRIu64 "\"\n", c->drawer_id);
        }
        if (c->has_book_id) {
            monitor_printf(mon, "    book-id: \"%" PRIu64 "\"\n", c->book_id);
        }
        if (c->has_socket_id) {
            monitor_printf(mon, "    socket-id: \"%" PRIu64 "\"\n", c->socket_id);
        }
        if (c->has_die_id) {
            monitor_printf(mon, "    die-id: \"%" PRIu64 "\"\n", c->die_id);
        }
        if (c->has_cluster_id) {
            monitor_printf(mon, "    cluster-id: \"%" PRIu64 "\"\n",
                           c->cluster_id);
        }
        if (c->has_module_id) {
            monitor_printf(mon, "    module-id: \"%" PRIu64 "\"\n",
                           c->module_id);
        }
        if (c->has_core_id) {
            monitor_printf(mon, "    core-id: \"%" PRIu64 "\"\n", c->core_id);
        }
        if (c->has_thread_id) {
            monitor_printf(mon, "    thread-id: \"%" PRIu64 "\"\n", c->thread_id);
        }

        l = l->next;
    }

    qapi_free_HotpluggableCPUList(saved);
}

void hmp_info_memdev(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    MemdevList *memdev_list = qmp_query_memdev(&err);
    MemdevList *m = memdev_list;

    while (m) {
        monitor_printf(mon, "memory backend: %s\n", m->value->id);
        monitor_printf(mon, "  size:  %" PRId64 "\n", m->value->size);
        monitor_printf(mon, "  merge: %s\n",
                       m->value->merge ? "true" : "false");
        monitor_printf(mon, "  dump: %s\n",
                       m->value->dump ? "true" : "false");
        monitor_printf(mon, "  prealloc: %s\n",
                       m->value->prealloc ? "true" : "false");
        monitor_printf(mon, "  share: %s\n",
                       m->value->share ? "true" : "false");
        if (m->value->has_reserve) {
            monitor_printf(mon, "  reserve: %s\n",
                           m->value->reserve ? "true" : "false");
        }

        m = m->next;
    }

    monitor_printf(mon, "\n");

    qapi_free_MemdevList(memdev_list);
    hmp_handle_error(mon, err);
}

void hmp_info_kvm(Monitor *mon, const QDict *qdict)
{
    KvmInfo *info;

    info = qmp_query_kvm(NULL);
    monitor_printf(mon, "kvm support: ");
    if (info->present) {
        monitor_printf(mon, "%s\n", info->enabled ? "enabled" : "disabled");
    } else {
        monitor_printf(mon, "not compiled\n");
    }

    qapi_free_KvmInfo(info);
}

void hmp_info_uuid(Monitor *mon, const QDict *qdict)
{
    UuidInfo *info;

    info = qmp_query_uuid(NULL);
    monitor_printf(mon, "%s\n", info->UUID);
    qapi_free_UuidInfo(info);
}

void hmp_system_reset(Monitor *mon, const QDict *qdict)
{
    qmp_system_reset(NULL);
}

void hmp_system_powerdown(Monitor *mon, const QDict *qdict)
{
    qmp_system_powerdown(NULL);
}

void hmp_memsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");
    Error *err = NULL;
    int cpu_index = monitor_get_cpu_index(mon);

    if (cpu_index < 0) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    qmp_memsave(addr, size, filename, true, cpu_index, &err);
    hmp_handle_error(mon, err);
}

void hmp_pmemsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");
    Error *err = NULL;

    qmp_pmemsave(addr, size, filename, &err);
    hmp_handle_error(mon, err);
}

void hmp_system_wakeup(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_system_wakeup(&err);
    hmp_handle_error(mon, err);
}

void hmp_nmi(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_inject_nmi(&err);
    hmp_handle_error(mon, err);
}

void hmp_info_memory_size_summary(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    MemoryInfo *info = qmp_query_memory_size_summary(&err);
    if (info) {
        monitor_printf(mon, "base memory: %" PRIu64 "\n",
                       info->base_memory);

        qapi_free_MemoryInfo(info);
    }
    hmp_handle_error(mon, err);
}
