/*
 * QEMU Module Infrastructure
 *
 * Copyright IBM, Corp. 2009
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
#include "qemu/queue.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#include "qapi/error.h"
#include "trace.h"

typedef struct ModuleEntry
{
    void (*init)(void);
    QTAILQ_ENTRY(ModuleEntry) node;
    module_init_type type;
} ModuleEntry;

typedef QTAILQ_HEAD(, ModuleEntry) ModuleTypeList;

static ModuleTypeList init_type_list[MODULE_INIT_MAX];
static bool           modules_init_done[MODULE_INIT_MAX];

static ModuleTypeList dso_init_list;

static void init_lists(void)
{
    static int inited;
    int        i;

    if (inited) { return; }

    for (i = 0; i < MODULE_INIT_MAX; i++) { QTAILQ_INIT(&init_type_list[i]); }

    QTAILQ_INIT(&dso_init_list);

    inited = 1;
}

static ModuleTypeList* find_type(module_init_type type)
{
    init_lists();

    return &init_type_list[type];
}

void register_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry*    e;
    ModuleTypeList* l;

    e       = g_malloc0(sizeof(*e));
    e->init = fn;
    e->type = type;

    l = find_type(type);

    QTAILQ_INSERT_TAIL(l, e, node);
}

void register_dso_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry* e;

    init_lists();

    e       = g_malloc0(sizeof(*e));
    e->init = fn;
    e->type = type;

    QTAILQ_INSERT_TAIL(&dso_init_list, e, node);
}

void module_call_init(module_init_type type)
{
    ModuleTypeList* l;
    ModuleEntry*    e;

    if (modules_init_done[type]) { return; }

    l = find_type(type);

    QTAILQ_FOREACH (e, l, node) { e->init(); }

    modules_init_done[type] = true;
}
