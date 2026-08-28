#pragma once

/* Misc. things related to the system emulator.  */

#include "qemu/timer.h"
#include "qemu/notify.h"
#include "qemu/uuid.h"

/* vl.c */

extern const char* qemu_name;
extern QemuUUID    qemu_uuid;
extern bool        qemu_uuid_set;

const char* qemu_get_vm_name(void);

/* Exit notifiers will run with BQL held. */
void qemu_add_exit_notifier(Notifier* notify);
void qemu_remove_exit_notifier(Notifier* notify);

void qemu_add_machine_init_done_notifier(Notifier* notify);
void qemu_remove_machine_init_done_notifier(Notifier* notify);

void configure_rtc(QemuOpts* opts);

void qemu_init_subsystems(void);

extern int autostart;

extern int           graphic_width;
extern int           graphic_height;
extern int           graphic_depth;
extern const char*   keyboard_layout;
extern uint8_t*      boot_splash_filedata;
extern bool          enable_cpu_pm;
extern QEMUClockType rtc_clock;

typedef enum
{
    MLOCK_OFF = 0,
    MLOCK_ON,
    MLOCK_ON_FAULT,
} MlockState;

bool should_mlock(MlockState);
bool is_mlock_on_fault(MlockState);

extern MlockState mlock_state;

#define MAX_OPTION_ROMS 16
typedef struct QEMUOptionRom
{
    const char* name;
    int32_t     bootindex;
} QEMUOptionRom;
extern QEMUOptionRom option_rom[MAX_OPTION_ROMS];
extern int           nb_option_roms;

#define MAX_PROM_ENVS 128
extern const char*  prom_envs[MAX_PROM_ENVS];
extern unsigned int nb_prom_envs;

/* serial ports */

/* Return the Chardev for serial port i, or NULL if none */
Chardev* serial_hd(int i);

void qemu_init(int argc, char** argv);
int  qemu_main_loop(void);
void qemu_cleanup(int);

extern QemuOptsList qemu_legacy_drive_opts;
extern QemuOptsList qemu_common_drive_opts;
extern QemuOptsList qemu_drive_opts;
extern QemuOptsList bdrv_runtime_opts;
extern QemuOptsList qemu_chardev_opts;
extern QemuOptsList qemu_device_opts;
extern QemuOptsList qemu_netdev_opts;
extern QemuOptsList qemu_nic_opts;
extern QemuOptsList qemu_net_opts;
extern QemuOptsList qemu_global_opts;
