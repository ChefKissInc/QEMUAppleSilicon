#pragma once

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

/* The QEMU SPMI implementation only supports transfers from master to slave
 * devices */

#define SPMI_CMD_EXT_WRITE 0x00
#define SPMI_CMD_RESET 0x10
#define SPMI_CMD_SLEEP 0x11
#define SPMI_CMD_SHUTDOWN 0x12
#define SPMI_CMD_WAKEUP 0x13
#define SPMI_CMD_AUTHENTICATE 0x14
#define SPMI_CMD_MSTR_READ 0x15
#define SPMI_CMD_MSTR_WRITE 0x16
#define SPMI_CMD_TRANSFER_BUS_OWNERSHIP 0x1A
#define SPMI_CMD_DDB_MASTER_READ 0x1B
#define SPMI_CMD_DDB_SLAVE_READ 0x1C
#define SPMI_CMD_EXT_READ 0x20
#define SPMI_CMD_EXT_WRITEL 0x30
#define SPMI_CMD_EXT_READL 0x38
#define SPMI_CMD_WRITE 0x40
#define SPMI_CMD_READ 0x60
#define SPMI_CMD_ZERO_WRITE 0x80


#define TYPE_SPMI_SLAVE "spmi-slave"
OBJECT_DECLARE_TYPE(SPMISlave, SPMISlaveClass, SPMI_SLAVE)

struct SPMISlaveClass {
    DeviceClass parent_class;

    /*
     * Master to slave.
     * Returns the number of bytes sent.
     */
    int (*send)(SPMISlave *s, uint8_t *data, uint8_t len);

    /*
     * Slave to master.
     * Returns the number of bytes received
     */
    int (*recv)(SPMISlave *s, uint8_t *data, uint8_t len);

    /*
     * Command frame.
     * Returns non-zero to NAK an operation.
     */
    int (*command)(SPMISlave *s, uint8_t opcode, uint16_t addr);

    void (*finish)(SPMISlave *s);
};

struct SPMISlave {
    DeviceState qdev;

    /* Remaining fields for internal use by the SPMI code.  */
    uint8_t sid;
};

#define TYPE_SPMI_BUS "spmi-bus"
OBJECT_DECLARE_SIMPLE_TYPE(SPMIBus, SPMI_BUS)

struct SPMIBus {
    BusState qbus;
    SPMISlave *current_dev;
    uint8_t saved_sid;
    bool broadcast;
};

SPMIBus *spmi_init_bus(DeviceState *parent, const char *name);
void spmi_set_slave_sid(SPMISlave *dev, uint8_t sid);
int spmi_bus_busy(SPMIBus *bus);
int spmi_start_transfer(SPMIBus *bus, uint8_t sid, uint8_t opcode,
                        uint16_t address);
void spmi_end_transfer(SPMIBus *bus);
int spmi_send_recv(SPMIBus *bus, uint8_t *data, uint8_t len, bool send);
int spmi_send(SPMIBus *bus, uint8_t *data, uint8_t len);
int spmi_recv(SPMIBus *bus, uint8_t *data, uint8_t len);

/**
 * Create an SPMI slave device on the heap.
 * @name: a device type name
 * @sid: SPMI slave id of the slave when put on a bus
 *
 * This only initializes the device state structure and allows
 * properties to be set. Type @name must exist. The device still
 * needs to be realized. See qdev-core.h.
 */
SPMISlave *spmi_slave_new(const char *name, uint8_t sid);

/**
 * Create and realize an SPMI slave device on the heap.
 * @bus: SPMI bus to put it on
 * @name: SPMI slave device type name
 * @sid: SPMI slave id of the slave when put on a bus
 *
 * Create the device state structure, initialize it, put it on the
 * specified @bus, and drop the reference to it (the device is realized).
 */
SPMISlave *spmi_slave_create_simple(SPMIBus *bus, const char *name,
                                    uint8_t sid);

/**
 * Realize and drop a reference an SPMI slave device
 * @dev: SPMI slave device to realize
 * @bus: SPMI bus to put it on
 * @sid: SPMI slave id of the slave on the bus
 * @errp: pointer to NULL initialized error object
 *
 * Returns: %true on success, %false on failure.
 *
 * Call 'realize' on @dev, put it on the specified @bus, and drop the
 * reference to it.
 *
 * This function is useful if you have created @dev via qdev_new(),
 * spmi_slave_new() or spmi_slave_try_new() (which take a reference to
 * the device it returns to you), so that you can set properties on it
 * before realizing it. If you don't need to set properties then
 * spmi_slave_create_simple() is probably better (as it does the create,
 * init and realize in one step).
 *
 * If you are embedding the SPMI slave into another QOM device and
 * initialized it via some variant on object_initialize_child() then
 * do not use this function, because that family of functions arrange
 * for the only reference to the child device to be held by the parent
 * via the child<> property, and so the reference-count-drop done here
 * would be incorrect.  (Instead you would want spmi_slave_realize(),
 * which doesn't currently exist but would be trivial to create if we
 * had any code that wanted it.)
 */
bool spmi_slave_realize_and_unref(SPMISlave *dev, SPMIBus *bus, Error **errp);
