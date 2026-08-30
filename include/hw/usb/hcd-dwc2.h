/*
 * dwc-hsotg (dwc2) USB host controller state definitions
 *
 * Based on hw/usb/hcd-ehci.h
 *
 * Copyright (c) 2020 Paul Zimmerman <pauldzim@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#pragma once

#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/usb.h"
#include "system/dma.h"
#include "qom/object.h"

#define DWC2_MMIO_SIZE 0x11000

#define DWC2_NB_CHAN       16     /* Number of host channels */
#define DWC2_MAX_XFER_SIZE 0x1000 /* Max transfer size expected in HCTSIZ */
#define DWC2_NB_EP         16     /* Number of device endpoints */

typedef struct DWC2Packet      DWC2Packet;
typedef struct DWC2DeviceState DWC2DeviceState;
typedef struct DWC2State       DWC2State;
typedef struct DWC2Class       DWC2Class;

enum async_state
{
    DWC2_ASYNC_NONE = 0,
    DWC2_ASYNC_INITIALIZED,
    DWC2_ASYNC_INFLIGHT,
    DWC2_ASYNC_FINISHED,
};

struct DWC2Packet
{
    USBPacket packet;
    uint32_t  devadr;
    uint32_t  epnum;
    uint32_t  epdir;
    uint32_t  mps;
    uint32_t  pid;
    uint32_t  index;
    uint32_t  pcnt;
    uint32_t  len;
    int32_t   async;
    bool      small;
    bool      needs_service;
};

struct DWC2DeviceState
{
    USBDevice  parent_obj;
    DWC2State* dwc2;
};

struct DWC2State
{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    USBBus bus;
    /* This device in device mode */
    DWC2DeviceState* device;
    uint8_t          pending_addr;
    bool             addr_pending;
    qemu_irq         irq;
    MemoryRegion*    dma_mr;
    AddressSpace     dma_as;
    MemoryRegion     container;
    MemoryRegion     hsotg;
    MemoryRegion     fifos;

    union
    {
#define DWC2_GLBREG_SIZE 0x70
        uint32_t glbreg[DWC2_GLBREG_SIZE / sizeof(uint32_t)];
        struct
        {
            uint32_t gotgctl;   /* 00 */
            uint32_t gotgint;   /* 04 */
            uint32_t gahbcfg;   /* 08 */
            uint32_t gusbcfg;   /* 0c */
            uint32_t grstctl;   /* 10 */
            uint32_t gintsts;   /* 14 */
            uint32_t gintmsk;   /* 18 */
            uint32_t grxstsr;   /* 1c */
            uint32_t grxstsp;   /* 20 */
            uint32_t grxfsiz;   /* 24 */
            uint32_t gnptxfsiz; /* 28 */
            uint32_t gnptxsts;  /* 2c */
            uint32_t gi2cctl;   /* 30 */
            uint32_t gpvndctl;  /* 34 */
            uint32_t ggpio;     /* 38 */
            uint32_t guid;      /* 3c */
            uint32_t gsnpsid;   /* 40 */
            uint32_t ghwcfg1;   /* 44 */
            uint32_t ghwcfg2;   /* 48 */
            uint32_t ghwcfg3;   /* 4c */
            uint32_t ghwcfg4;   /* 50 */
            uint32_t glpmcfg;   /* 54 */
            uint32_t gpwrdn;    /* 58 */
            uint32_t gdfifocfg; /* 5c */
            uint32_t gadpctl;   /* 60 */
            uint32_t grefclk;   /* 64 */
            uint32_t gintmsk2;  /* 68 */
            uint32_t gintsts2;  /* 6c */
        };
    };

    union
    {
#define DWC2_FSZREG_SIZE 0x04
        uint32_t fszreg[DWC2_FSZREG_SIZE / sizeof(uint32_t)];
        struct
        {
            uint32_t hptxfsiz; /* 100 */
        };
    };
    union
    {
#define DWC2_DFSZREG_SIZE 0x3c
        uint32_t dfszreg[DWC2_DFSZREG_SIZE / sizeof(uint32_t)];
        struct
        {
            uint32_t dptxfsiz[DWC2_NB_EP]; /* 104 */
        };
    };

    union
    {
#define DWC2_HREG0_SIZE 0x44
        uint32_t hreg0[DWC2_HREG0_SIZE / sizeof(uint32_t)];
        struct
        {
            uint32_t hcfg;     /* 400 */
            uint32_t hfir;     /* 404 */
            uint32_t hfnum;    /* 408 */
            uint32_t rsvd0;    /* 40c */
            uint32_t hptxsts;  /* 410 */
            uint32_t haint;    /* 414 */
            uint32_t haintmsk; /* 418 */
            uint32_t hflbaddr; /* 41c */
            uint32_t rsvd1[8]; /* 420-43c */
            uint32_t hprt0;    /* 440 */
        };
    };

#define DWC2_HREG1_SIZE (0x20 * DWC2_NB_CHAN)
    uint32_t hreg1[DWC2_HREG1_SIZE / sizeof(uint32_t)];

#define hcchar(_ch)   hreg1[((_ch) << 3) + 0] /* 500, 520, ... */
#define hcsplt(_ch)   hreg1[((_ch) << 3) + 1] /* 504, 524, ... */
#define hcint(_ch)    hreg1[((_ch) << 3) + 2] /* 508, 528, ... */
#define hcintmsk(_ch) hreg1[((_ch) << 3) + 3] /* 50c, 52c, ... */
#define hctsiz(_ch)   hreg1[((_ch) << 3) + 4] /* 510, 530, ... */
#define hcdma(_ch)    hreg1[((_ch) << 3) + 5] /* 514, 534, ... */
#define hcdmab(_ch)   hreg1[((_ch) << 3) + 7] /* 51c, 53c, ... */

    union
    {
#define DWC2_DREG_SIZE 0x38
        uint32_t dreg[DWC2_DREG_SIZE / sizeof(uint32_t)];
        struct
        {
            uint32_t dcfg;       /* 800 */
            uint32_t dctl;       /* 804 */
            uint32_t dsts;       /* 808 */
            uint32_t rsvd2;      /* 80c */
            uint32_t diepmsk;    /* 810 */
            uint32_t doepmsk;    /* 814 */
            uint32_t daint;      /* 818 */
            uint32_t daintmsk;   /* 81c */
            uint32_t dtknqr1;    /* 820 */
            uint32_t dtknqr2;    /* 824 */
            uint32_t dvbusdis;   /* 828 */
            uint32_t dvbuspulse; /* 82c */
            uint32_t dtknqr3;    /* 830 */
            uint32_t dtknqr4;    /* 834 */
        };
    };

#define DWC2_DIEPREG_SIZE (0x20 * DWC2_NB_EP)
    uint32_t diepreg[DWC2_DIEPREG_SIZE / sizeof(uint32_t)];

#define diepctl(_ch)  diepreg[((_ch) << 3) + 0] /* 900, 920, ... */
#define diepint(_ch)  diepreg[((_ch) << 3) + 2] /* 908, 928, ... */
#define dieptsiz(_ch) diepreg[((_ch) << 3) + 4] /* 910, 930, ... */
#define diepdma(_ch)  diepreg[((_ch) << 3) + 5] /* 914, 934, ... */
#define dtxfsts(_ch)  diepreg[((_ch) << 3) + 6] /* 918, 938, ... */

#define DWC2_DOEPREG_SIZE (0x20 * DWC2_NB_EP)
    uint32_t doepreg[DWC2_DIEPREG_SIZE / sizeof(uint32_t)];

#define doepctl(_ch)  doepreg[((_ch) << 3) + 0] /* b00, b20, ... */
#define doepint(_ch)  doepreg[((_ch) << 3) + 2] /* b08, b28, ... */
#define doeptsiz(_ch) doepreg[((_ch) << 3) + 4] /* b10, b30, ... */
#define doepdma(_ch)  doepreg[((_ch) << 3) + 5] /* b14, b34, ... */

    union
    {
#define DWC2_PCGREG_SIZE 0x08
        uint32_t pcgreg[DWC2_PCGREG_SIZE / sizeof(uint32_t)];
        struct
        {
            uint32_t pcgctl;   /* e00 */
            uint32_t pcgcctl1; /* e04 */
        };
    };

    /* TODO - implement FIFO registers for slave mode */
#define DWC2_HFIFO_SIZE (0x1000 * DWC2_NB_CHAN)
    /*
     *  Internal state
     */
    QEMUTimer* eof_timer;
    QEMUTimer* frame_timer;
    QEMUBH*    async_bh;
    QEMUBH*    device_async_bh;
    int64_t    sof_time;
    int64_t    usb_frame_time;
    int64_t    usb_bit_time;
    uint32_t   usb_version;
    uint16_t   frame_number;
    uint16_t   fi;
    uint16_t   next_chan;
    bool       working;
    USBPort    uport;
    DWC2Packet packet[DWC2_NB_CHAN]; /* one packet per chan */
    union
    {
        uint8_t usb_buf[DWC2_NB_CHAN][DWC2_MAX_XFER_SIZE]; /* one buffer per chan */
        uint8_t fifos_buf[DWC2_HFIFO_SIZE];
    };
};

#define TYPE_DWC2_USB_DEVICE "dwc2-usb-device"
OBJECT_DECLARE_SIMPLE_TYPE(DWC2DeviceState, DWC2_USB_DEVICE)

#define TYPE_DWC2_USB "dwc2-usb"
OBJECT_DECLARE_SIMPLE_TYPE(DWC2State, DWC2_USB)
