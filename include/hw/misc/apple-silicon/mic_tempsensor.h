/*
 * Apple Mic/ICA60 Temp Sensor.
 *
 * Copyright (c) 2026 Visual Ehrmanntraut (VisualEhrmanntraut).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HW_MISC_APPLE_SILICON_MIC_TEMPSENSOR_H
#define HW_MISC_APPLE_SILICON_MIC_TEMPSENSOR_H

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"

#define TYPE_APPLE_MIC_TEMP_SENSOR "apple-mic-temp-sensor"
OBJECT_DECLARE_SIMPLE_TYPE(AppleMicTempSensorState, APPLE_MIC_TEMP_SENSOR);

I2CSlave *apple_mic_temp_sensor_create(uint8_t addr, uint8_t product_id,
                                       uint8_t vendor_id, uint8_t revision,
                                       uint8_t fab_id);
#endif /* HW_MISC_APPLE_SILICON_MIC_TEMPSENSOR_H */
