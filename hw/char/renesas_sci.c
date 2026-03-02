/*
 * Renesas Serial Communication Interface
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 *            (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/char/renesas_sci.h"
#include "migration/vmstate.h"

/* SCI register map */
REG8(SMR, 0)
  REG_FIELD(SMR, CKS,  0, 2)
  REG_FIELD(SMR, MP,   2, 1)
  REG_FIELD(SMR, STOP, 3, 1)
  REG_FIELD(SMR, PM,   4, 1)
  REG_FIELD(SMR, PE,   5, 1)
  REG_FIELD(SMR, CHR,  6, 1)
  REG_FIELD(SMR, CM,   7, 1)
REG8(BRR, 1)
REG8(SCR, 2)
  REG_FIELD(SCR, CKE,  0, 2)
  REG_FIELD(SCR, TEIE, 2, 1)
  REG_FIELD(SCR, MPIE, 3, 1)
  REG_FIELD(SCR, RE,   4, 1)
  REG_FIELD(SCR, TE,   5, 1)
  REG_FIELD(SCR, RIE,  6, 1)
  REG_FIELD(SCR, TIE,  7, 1)
REG8(TDR, 3)
REG8(SSR, 4)
  REG_FIELD(SSR, MPBT, 0, 1)
  REG_FIELD(SSR, MPB,  1, 1)
  REG_FIELD(SSR, TEND, 2, 1)
  REG_FIELD(SSR, ERR,  3, 3)
    REG_FIELD(SSR, PER,  3, 1)
    REG_FIELD(SSR, FER,  4, 1)
    REG_FIELD(SSR, ORER, 5, 1)
  REG_FIELD(SSR, RDRF, 6, 1)
  REG_FIELD(SSR, TDRE, 7, 1)
REG8(RDR, 5)
REG8(SCMR, 6)
  REG_FIELD(SCMR, SMIF, 0, 1)
  REG_FIELD(SCMR, SINV, 2, 1)
  REG_FIELD(SCMR, SDIR, 3, 1)
  REG_FIELD(SCMR, BCP2, 7, 1)
REG8(SEMR, 7)
  REG_FIELD(SEMR, ACS0, 0, 1)
  REG_FIELD(SEMR, ABCS, 4, 1)

static int can_receive(void *opaque)
{
    RSCIState *sci = RSCI(opaque);
    if (sci->rx_next > qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
        return 0;
    } else {
        return REG_FIELD_EX8(sci->scr, SCR, RE);
    }
}

static void receive(void *opaque, const uint8_t *buf, int size)
{
    RSCIState *sci = RSCI(opaque);
    sci->rx_next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + sci->trtime;
    if (REG_FIELD_EX8(sci->ssr, SSR, RDRF) || size > 1) {
        sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, ORER, 1);
        if (REG_FIELD_EX8(sci->scr, SCR, RIE)) {
            qemu_set_irq(sci->irq[ERI], 1);
        }
    } else {
        sci->rdr = buf[0];
        sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, RDRF, 1);
        if (REG_FIELD_EX8(sci->scr, SCR, RIE)) {
            qemu_irq_pulse(sci->irq[RXI]);
        }
    }
}

static void send_byte(RSCIState *sci)
{
    if (qemu_chr_fe_backend_connected(&sci->chr)) {
        qemu_chr_fe_write_all(&sci->chr, &sci->tdr, 1);
    }
    timer_mod(&sci->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + sci->trtime);
    sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, TEND, 0);
    sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, TDRE, 1);
    qemu_set_irq(sci->irq[TEI], 0);
    if (REG_FIELD_EX8(sci->scr, SCR, TIE)) {
        qemu_irq_pulse(sci->irq[TXI]);
    }
}

static void txend(void *opaque)
{
    RSCIState *sci = RSCI(opaque);
    if (!REG_FIELD_EX8(sci->ssr, SSR, TDRE)) {
        send_byte(sci);
    } else {
        sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, TEND, 1);
        if (REG_FIELD_EX8(sci->scr, SCR, TEIE)) {
            qemu_set_irq(sci->irq[TEI], 1);
        }
    }
}

static void update_trtime(RSCIState *sci)
{
    /* char per bits */
    sci->trtime = 8 - REG_FIELD_EX8(sci->smr, SMR, CHR);
    sci->trtime += REG_FIELD_EX8(sci->smr, SMR, PE);
    sci->trtime += REG_FIELD_EX8(sci->smr, SMR, STOP) + 1;
    /* x bit transmit time (32 * divrate * brr) / base freq */
    sci->trtime *= 32 * sci->brr;
    sci->trtime *= 1 << (2 * REG_FIELD_EX8(sci->smr, SMR, CKS));
    sci->trtime *= NANOSECONDS_PER_SECOND;
    sci->trtime /= sci->input_freq;
}

static bool sci_is_tr_enabled(RSCIState *sci)
{
    return REG_FIELD_EX8(sci->scr, SCR, TE) || REG_FIELD_EX8(sci->scr, SCR, RE);
}

static void sci_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    RSCIState *sci = RSCI(opaque);

    switch (offset) {
    case A_SMR:
        if (!sci_is_tr_enabled(sci)) {
            sci->smr = val;
            update_trtime(sci);
        }
        break;
    case A_BRR:
        if (!sci_is_tr_enabled(sci)) {
            sci->brr = val;
            update_trtime(sci);
        }
        break;
    case A_SCR:
        sci->scr = val;
        if (REG_FIELD_EX8(sci->scr, SCR, TE)) {
            sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, TDRE, 1);
            sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, TEND, 1);
            if (REG_FIELD_EX8(sci->scr, SCR, TIE)) {
                qemu_irq_pulse(sci->irq[TXI]);
            }
        }
        if (!REG_FIELD_EX8(sci->scr, SCR, TEIE)) {
            qemu_set_irq(sci->irq[TEI], 0);
        }
        if (!REG_FIELD_EX8(sci->scr, SCR, RIE)) {
            qemu_set_irq(sci->irq[ERI], 0);
        }
        break;
    case A_TDR:
        sci->tdr = val;
        if (REG_FIELD_EX8(sci->ssr, SSR, TEND)) {
            send_byte(sci);
        } else {
            sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, TDRE, 0);
        }
        break;
    case A_SSR:
        sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, MPBT,
                             REG_FIELD_EX8(val, SSR, MPBT));
        sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, ERR,
                             REG_FIELD_EX8(val, SSR, ERR) & 0x07);
        if (REG_FIELD_EX8(sci->read_ssr, SSR, ERR) &&
            REG_FIELD_EX8(sci->ssr, SSR, ERR) == 0) {
            qemu_set_irq(sci->irq[ERI], 0);
        }
        break;
    case A_RDR:
        qemu_log_mask(LOG_GUEST_ERROR, "reneas_sci: RDR is read only.\n");
        break;
    case A_SCMR:
        sci->scmr = val; break;
    case A_SEMR: /* SEMR */
        sci->semr = val; break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_sci: Register 0x%" HWADDR_PRIX " "
                                 "not implemented\n",
                      offset);
    }
}

static uint64_t sci_read(void *opaque, hwaddr offset, unsigned size)
{
    RSCIState *sci = RSCI(opaque);

    switch (offset) {
    case A_SMR:
        return sci->smr;
    case A_BRR:
        return sci->brr;
    case A_SCR:
        return sci->scr;
    case A_TDR:
        return sci->tdr;
    case A_SSR:
        sci->read_ssr = sci->ssr;
        return sci->ssr;
    case A_RDR:
        sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, RDRF, 0);
        return sci->rdr;
    case A_SCMR:
        return sci->scmr;
    case A_SEMR:
        return sci->semr;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_sci: Register 0x%" HWADDR_PRIX
                      " not implemented.\n", offset);
    }
    return UINT64_MAX;
}

static const MemoryRegionOps sci_ops = {
    .write = sci_write,
    .read  = sci_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.max_access_size = 1,
    .valid.max_access_size = 1,
};

static void rsci_reset(DeviceState *dev)
{
    RSCIState *sci = RSCI(dev);
    sci->smr = sci->scr = 0x00;
    sci->brr = 0xff;
    sci->tdr = 0xff;
    sci->rdr = 0x00;
    sci->ssr = 0x84;
    sci->scmr = 0x00;
    sci->semr = 0x00;
    sci->rx_next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void sci_event(void *opaque, QEMUChrEvent event)
{
    RSCIState *sci = RSCI(opaque);
    if (event == CHR_EVENT_BREAK) {
        sci->ssr = REG_FIELD_DP8(sci->ssr, SSR, FER, 1);
        if (REG_FIELD_EX8(sci->scr, SCR, RIE)) {
            qemu_set_irq(sci->irq[ERI], 1);
        }
    }
}

static void rsci_realize(DeviceState *dev, Error **errp)
{
    RSCIState *sci = RSCI(dev);

    if (sci->input_freq == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_sci: input-freq property must be set.");
        return;
    }
    qemu_chr_fe_set_handlers(&sci->chr, can_receive, receive,
                             sci_event, NULL, sci, NULL, true);
}

static void rsci_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RSCIState *sci = RSCI(obj);
    int i;

    memory_region_init_io(&sci->memory, OBJECT(sci), &sci_ops,
                          sci, "renesas-sci", 0x8);
    sysbus_init_mmio(d, &sci->memory);

    for (i = 0; i < SCI_NR_IRQ; i++) {
        sysbus_init_irq(d, &sci->irq[i]);
    }
    timer_init_ns(&sci->timer, QEMU_CLOCK_VIRTUAL, txend, sci);
}

static const VMStateDescription vmstate_rsci = {
    .name = "renesas-sci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(trtime, RSCIState),
        VMSTATE_INT64(rx_next, RSCIState),
        VMSTATE_UINT8(smr, RSCIState),
        VMSTATE_UINT8(brr, RSCIState),
        VMSTATE_UINT8(scr, RSCIState),
        VMSTATE_UINT8(tdr, RSCIState),
        VMSTATE_UINT8(ssr, RSCIState),
        VMSTATE_UINT8(rdr, RSCIState),
        VMSTATE_UINT8(scmr, RSCIState),
        VMSTATE_UINT8(semr, RSCIState),
        VMSTATE_UINT8(read_ssr, RSCIState),
        VMSTATE_TIMER(timer, RSCIState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property rsci_properties[] = {
    DEFINE_PROP_UINT64("input-freq", RSCIState, input_freq, 0),
    DEFINE_PROP_CHR("chardev", RSCIState, chr),
};

static void rsci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rsci_realize;
    dc->vmsd = &vmstate_rsci;
    device_class_set_legacy_reset(dc, rsci_reset);
    device_class_set_props(dc, rsci_properties);
}

static const TypeInfo rsci_info = {
    .name = TYPE_RENESAS_SCI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RSCIState),
    .instance_init = rsci_init,
    .class_init = rsci_class_init,
};

static void rsci_register_types(void)
{
    type_register_static(&rsci_info);
}

type_init(rsci_register_types)
