/**
 * @file  app_sensor.c
 * @brief Virtual light sensor (illuminance) on EP2
 */

#include <jendefs.h>

/* Generated */
#include "zps_gen.h"

/* Application */
#include "app_reporting.h"
#include "app_sensor.h"
#include "app_zcl_task.h"
#include "zcl_options.h"

/* SDK JN-SW-4170 */
#include "IlluminanceMeasurement.h"
#include "dbg.h"
#include "zcl.h"

#ifndef TRACE_SENSOR
#define TRACE_SENSOR FALSE
#endif

/**
 * @brief Initialises the sensor attributes to sane defaults
 * @note  Must be called after the Sensor endpoint is registered with ZCL.
 */
PUBLIC void APP_SENSOR_vInit(void)
{
    sSensor.sIlluminanceMeasurementServerCluster.u16MeasuredValue = 0;
    sSensor.sIlluminanceMeasurementServerCluster.u16MinMeasuredValue = 0;
    sSensor.sIlluminanceMeasurementServerCluster.u16MaxMeasuredValue = 0xFFFE;
}

/**
 * @brief Applies a host lux push: writes the attribute and reports it
 * @details The value is forwarded verbatim (no conversion); the report
 * goes unicast to the coordinator without any reporting configuration.
 */
PUBLIC void APP_SENSOR_vHandleLux(uint16 u16Lux)
{
    DBG_vPrintf(TRACE_SENSOR, "Sensor: lux=%u\n", u16Lux);

    sSensor.sIlluminanceMeasurementServerCluster.u16MeasuredValue = u16Lux;

    APP_vSendUnicastReport(LUMIROUTER_SENSOR_ENDPOINT,
                           MEASUREMENT_AND_SENSING_CLUSTER_ID_ILLUMINANCE_MEASUREMENT,
                           E_CLD_ILLMEAS_ATTR_ID_MEASURED_VALUE,
                           FALSE);
}
