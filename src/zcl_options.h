/**
 * @file  zcl_options.h
 * @brief Options Header for ZigBee Cluster Library functions
 */

#ifndef ZCL_OPTIONS_H
#define ZCL_OPTIONS_H

#include <jendefs.h>

/* Use the NXP manufacturer code */
#define ZCL_MANUFACTURER_CODE 0x1037

/* Number of endpoints supported by this device */
#define ZCL_NUMBER_OF_ENDPOINTS 3

/* Keep default responses enabled: the coordinator waits for them and
 * retries commands without an answer (duplicate snapshots). */
#define ZCL_DISABLE_DEFAULT_RESPONSES (FALSE)

/* ZCL global attribute commands supported by the server */
#define ZCL_ATTRIBUTE_READ_SERVER_SUPPORTED
#define ZCL_ATTRIBUTE_WRITE_SERVER_SUPPORTED
#define ZCL_ATTRIBUTE_DISCOVERY_SERVER_SUPPORTED

/* Configuring Attribute Reporting */
#define ZCL_ATTRIBUTE_REPORTING_SERVER_SUPPORTED
#define ZCL_CONFIGURE_ATTRIBUTE_REPORTING_SERVER_SUPPORTED
#define ZCL_READ_ATTRIBUTE_REPORTING_CONFIGURATION_SERVER_SUPPORTED

/* Reporting related configuration (lamp attributes on EP1) */
enum {
    REPORT_LAMP_ONOFF_SLOT = 0,
    REPORT_LAMP_LEVEL_SLOT,
    REPORT_LAMP_CURRENT_X_SLOT,
    REPORT_LAMP_CURRENT_Y_SLOT,
    REPORT_LAMP_COLOUR_TEMP_SLOT,
    NUMBER_OF_REPORTS
};

#define ZCL_NUMBER_OF_REPORTS NUMBER_OF_REPORTS

/* Enable wild card profile */
#define ZCL_ALLOW_WILD_CARD_PROFILE

/* Enable ZCL clusters and their client/server roles */
#define CLD_BASIC
#define BASIC_SERVER
#define CLD_IDENTIFY
#define IDENTIFY_SERVER

/* Virtual RGB lamp on EP1, virtual sensor on EP2, virtual doorbell
 * on EP3 (server clusters, no GPIO) */
#define CLD_ONOFF
#define ONOFF_SERVER
#define CLD_LEVEL_CONTROL
#define LEVEL_CONTROL_SERVER
#define CLD_COLOUR_CONTROL
#define COLOUR_CONTROL_SERVER
#define CLD_MULTISTATE_OUTPUT_BASIC
#define MULTISTATE_OUTPUT_BASIC_SERVER
#define CLD_ILLUMINANCE_MEASUREMENT
#define ILLUMINANCE_MEASUREMENT_SERVER

/* Colour capabilities: Hue/Saturation + CIE XY + colour temperature */
#define CLD_COLOURCONTROL_COLOUR_CAPABILITIES \
    (COLOUR_CAPABILITY_HUE_SATURATION_SUPPORTED | COLOUR_CAPABILITY_XY_SUPPORTED | \
     COLOUR_CAPABILITY_COLOUR_TEMPERATURE_SUPPORTED)

/* Basic cluster optional attributes */
#define CLD_BAS_ATTR_MANUFACTURER_NAME
#define CLD_BAS_ATTR_MODEL_IDENTIFIER
#define CLD_BAS_ATTR_DATE_CODE
#define CLD_BAS_ATTR_SW_BUILD_ID
#define CLD_BAS_ATTR_HARDWARE_VERSION

#define BAS_MANUF_NAME_STRING "VLK_SW"

#ifdef BOARD_DGNWG05LM
#define BAS_MODEL_ID_STRING   "ZESP_Router"
#define BAS_HARDWARE_VERSION  2U
#endif

#ifdef BOARD_ZHWG11LM
#define BAS_MODEL_ID_STRING   "ZESP_Router"
#define BAS_HARDWARE_VERSION  3U
#endif

#define BAS_DATE_STRING       BUILD_DATE_STRING
#define BAS_SW_BUILD_STRING   VERSION_STRING

#define CLD_BAS_MANUF_NAME_SIZE  (sizeof(BAS_MANUF_NAME_STRING) - 1U)
#define CLD_BAS_MODEL_ID_SIZE    (sizeof(BAS_MODEL_ID_STRING) - 1U)
#define CLD_BAS_DATE_SIZE        (sizeof(BAS_DATE_STRING) - 1U)
#define CLD_BAS_SW_BUILD_SIZE    (sizeof(BAS_SW_BUILD_STRING) - 1U)
#define CLD_BAS_POWER_SOURCE     E_CLD_BAS_PS_SINGLE_PHASE_MAINS

#endif /* ZCL_OPTIONS_H */
