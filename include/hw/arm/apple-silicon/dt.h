/*
 * Apple Device Tree.
 *
 * Copyright (c) 2024-2025 Visual Ehrmanntraut (VisualEhrmanntraut).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_ARM_APPLE_SILICON_DT_H
#define HW_ARM_APPLE_SILICON_DT_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"

typedef struct {
    uint32_t len;
    bool placeholder;
    void *data;
} AppleDTProp;

typedef struct {
    GHashTable *props;
    GList *children;
    bool finalised;
} AppleDTNode;

AppleDTNode *apple_dt_node_new(AppleDTNode *parent, const char *name);
AppleDTNode *apple_dt_deserialise(void *blob);

bool apple_dt_del_node_named(AppleDTNode *parent, const char *name);
void apple_dt_del_node(AppleDTNode *parent, AppleDTNode *node);
bool apple_dt_del_prop_named(AppleDTNode *node, const char *name);

AppleDTProp *apple_dt_set_prop(AppleDTNode *n, const char *name, uint32_t len,
                               const void *val);
AppleDTProp *apple_dt_set_prop_null(AppleDTNode *node, const char *name);
AppleDTProp *apple_dt_set_prop_u32(AppleDTNode *node, const char *name,
                                   uint32_t val);
AppleDTProp *apple_dt_set_prop_u64(AppleDTNode *node, const char *name,
                                   uint64_t val);
AppleDTProp *apple_dt_set_prop_str(AppleDTNode *node, const char *name,
                                   const char *val);
AppleDTProp *apple_dt_set_prop_strn(AppleDTNode *node, const char *name,
                                    uint32_t max_len, const char *val);

AppleDTNode *apple_dt_get_node(AppleDTNode *node, const char *path);
AppleDTProp *apple_dt_get_prop(AppleDTNode *node, const char *name);
const char *apple_dt_get_prop_str(AppleDTNode *node, const char *name,
                                  Error **errp);
char *apple_dt_get_prop_strdup(AppleDTNode *node, const char *name,
                               Error **errp);
const char *apple_dt_get_prop_str_or(AppleDTNode *node, const char *name,
                                     const char *default_s, Error **errp);
char *apple_dt_get_prop_strdup_or(AppleDTNode *node, const char *name,
                                  const char *default_s, Error **errp);
uint8_t apple_dt_get_prop_u8_or(AppleDTNode *node, const char *name,
                                uint8_t default_val, Error **invalid_errp);
uint16_t apple_dt_get_prop_u16_or(AppleDTNode *node, const char *name,
                                  uint16_t default_val, Error **invalid_errp);
uint32_t apple_dt_get_prop_u32_or(AppleDTNode *node, const char *name,
                                  uint32_t default_val, Error **invalid_errp);
uint64_t apple_dt_get_prop_u64_or(AppleDTNode *node, const char *name,
                                  uint64_t default_val, Error **invalid_errp);
uint8_t apple_dt_get_prop_u8(AppleDTNode *node, const char *name, Error **errp);
uint16_t apple_dt_get_prop_u16(AppleDTNode *node, const char *name,
                               Error **errp);
uint32_t apple_dt_get_prop_u32(AppleDTNode *node, const char *name,
                               Error **errp);
uint64_t apple_dt_get_prop_u64(AppleDTNode *node, const char *name,
                               Error **errp);

void apple_dt_connect_function_prop_out_in(DeviceState *target_device,
                                           DeviceState *src_device,
                                           AppleDTProp *function_prop,
                                           const char *name);
void apple_dt_connect_function_prop_out_in_gpio(DeviceState *src_device,
                                                AppleDTProp *function_prop,
                                                const char *gpio_name);
void apple_dt_connect_function_prop_in_out(DeviceState *target_device,
                                           DeviceState *src_device,
                                           AppleDTProp *function_prop,
                                           const char *name);
void apple_dt_connect_function_prop_in_out_gpio(DeviceState *src_device,
                                                AppleDTProp *function_prop,
                                                const char *gpio_name);

uint64_t apple_dt_finalise(AppleDTNode *root);
void apple_dt_serialise(AppleDTNode *root, void *buf);
void apple_dt_unfinalise(AppleDTNode *root);

#endif /* HW_ARM_APPLE_SILICON_DT_H */
