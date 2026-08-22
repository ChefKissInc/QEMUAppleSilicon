/* SPDX-License-Identifier: LGPL-2.1-or-later */

#pragma once

void kvm_report_irq_delivered(int delivered);
void kvm_reset_irq_delivered(void);
int  kvm_get_irq_delivered(void);
