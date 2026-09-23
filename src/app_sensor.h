/**
 * @file  app_sensor.h
 * @brief Virtual light sensor (illuminance) on EP2
 *
 * @details The host pushes raw MeasuredValue readings with {"lux":N}.
 * Every push is written into the ZCL attribute and immediately sent
 * as a unicast Report Attributes to the coordinator: no reporting
 * configuration, no bindings, no PDM. The coordinator may also read
 * the attribute directly at any time.
 */

#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include <jendefs.h>

PUBLIC void APP_SENSOR_vInit(void);
PUBLIC void APP_SENSOR_vHandleLux(uint16 u16Lux);

#endif /* APP_SENSOR_H */
