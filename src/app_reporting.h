/**
 * @file  app_reporting.h
 * @brief Reporting functionality
 */

#ifndef APP_REPORTING_H
#define APP_REPORTING_H

#include <jendefs.h>

/* SDK JN-SW-4170 */
#include "zcl.h"

PUBLIC bool_t APP_bRestoreReports(void);
PUBLIC void APP_vApplyReportingConfig(void);
PUBLIC void APP_vLoadDefaultReports(void);
PUBLIC void
APP_vSaveReportableRecord(uint8 u8EndPointID,
                          uint16 u16ClusterID,
                          tsZCL_AttributeReportingConfigurationRecord *psAttributeReportingConfigurationRecord);
PUBLIC void
APP_vRestoreDefaultRecord(uint8 u8EndPointID,
                          uint16 u16ClusterID,
                          tsZCL_AttributeReportingConfigurationRecord *psAttributeReportingConfigurationRecord);

/* Explicit unicast report to the coordinator, no reporting configuration */
PUBLIC void APP_vSendUnicastReport(uint8 u8SrcEndPoint,
                                   uint16 u16ClusterId,
                                   uint16 u16AttributeId,
                                   bool_t bWithAck);

#endif /* APP_REPORTING_H */
