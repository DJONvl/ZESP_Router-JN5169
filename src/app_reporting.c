/**
 * @file  app_reporting.c
 * @brief Reporting functionality
 */

#include <jendefs.h>
#include <string.h>

/* Generated */
#include "zps_gen.h"

/* Application */
#include "PDM_IDs.h"
#include "app_reporting.h"
#include "app_serial_commands.h"
#include "zcl_options.h"

/* SDK JN-SW-4170 */
#include "OnOff.h"
#include "LevelControl.h"
#include "ColourControl.h"
#include "PDM.h"
#include "bdb_api.h"
#include "dbg.h"
#include "pdum_apl.h"
#include "zcl.h"
#include "zcl_customcommand.h"

#ifndef TRACE_REPORT
#define TRACE_REPORT FALSE
#endif

#define APP_REPORTS_MAGIC        0x5A525204UL /* ZR (Zigbee Router) + R (Reports) + revision 4 */
#define APP_REPORT_INDEX_INVALID 0xFF

#define LAMP_MIN_REPORT_INTERVAL_SECONDS 1
#define LAMP_MAX_REPORT_INTERVAL_SECONDS 3600
#define LAMP_LEVEL_MINIMUM_REPORTABLE_CHANGE 0x01
#define LAMP_COLOUR_MINIMUM_REPORTABLE_CHANGE 0x10

typedef struct {
    uint8 u8EndPoint;
    uint16 u16ClusterID;
    tsZCL_AttributeReportingConfigurationRecord sAttributeReportingConfigurationRecord;
} APP_tsReports;

typedef struct {
    uint32 u32Magic;
    APP_tsReports asReports[NUMBER_OF_REPORTS];
} APP_tsReportsRecord;

PRIVATE void APP_vSaveReportsRecord(void);
PRIVATE uint8 APP_u8GetRecordIndex(uint16 u16ClusterID, uint16 u16AttributeEnum);
PRIVATE void APP_vPrintReportRecord(APP_tsReports *psReport);

/* Saved reporting configurations */
PRIVATE APP_tsReports asSavedReports[NUMBER_OF_REPORTS];

/* Define the default reports */
PRIVATE APP_tsReports asDefaultReports[NUMBER_OF_REPORTS] = {
    {
        LUMIROUTER_APPLICATION_ENDPOINT,
        GENERAL_CLUSTER_ID_ONOFF,
        {
            0,
            E_ZCL_BOOL,
            E_CLD_ONOFF_ATTR_ID_ONOFF,
            LAMP_MIN_REPORT_INTERVAL_SECONDS,
            LAMP_MAX_REPORT_INTERVAL_SECONDS,
            0,
            {0},
        },
    },
    {
        LUMIROUTER_APPLICATION_ENDPOINT,
        GENERAL_CLUSTER_ID_LEVEL_CONTROL,
        {
            0,
            E_ZCL_UINT8,
            E_CLD_LEVELCONTROL_ATTR_ID_CURRENT_LEVEL,
            LAMP_MIN_REPORT_INTERVAL_SECONDS,
            LAMP_MAX_REPORT_INTERVAL_SECONDS,
            0,
            {.zuint8ReportableChange = LAMP_LEVEL_MINIMUM_REPORTABLE_CHANGE},
        },
    },
    {
        LUMIROUTER_APPLICATION_ENDPOINT,
        LIGHTING_CLUSTER_ID_COLOUR_CONTROL,
        {
            0,
            E_ZCL_UINT16,
            E_CLD_COLOURCONTROL_ATTR_CURRENT_X,
            LAMP_MIN_REPORT_INTERVAL_SECONDS,
            LAMP_MAX_REPORT_INTERVAL_SECONDS,
            0,
            {.zuint16ReportableChange = LAMP_COLOUR_MINIMUM_REPORTABLE_CHANGE},
        },
    },
    {
        LUMIROUTER_APPLICATION_ENDPOINT,
        LIGHTING_CLUSTER_ID_COLOUR_CONTROL,
        {
            0,
            E_ZCL_UINT16,
            E_CLD_COLOURCONTROL_ATTR_CURRENT_Y,
            LAMP_MIN_REPORT_INTERVAL_SECONDS,
            LAMP_MAX_REPORT_INTERVAL_SECONDS,
            0,
            {.zuint16ReportableChange = LAMP_COLOUR_MINIMUM_REPORTABLE_CHANGE},
        },
    },
    {
        LUMIROUTER_APPLICATION_ENDPOINT,
        LIGHTING_CLUSTER_ID_COLOUR_CONTROL,
        {
            0,
            E_ZCL_UINT16,
            E_CLD_COLOURCONTROL_ATTR_COLOUR_TEMPERATURE_MIRED,
            LAMP_MIN_REPORT_INTERVAL_SECONDS,
            LAMP_MAX_REPORT_INTERVAL_SECONDS,
            0,
            {.zuint16ReportableChange = LAMP_COLOUR_MINIMUM_REPORTABLE_CHANGE},
        },
    },
};

/**
 * @brief Restores the reporting configuration from PDM if the record is valid.
 */
PUBLIC bool_t APP_bRestoreReports(void)
{
    APP_tsReportsRecord sRecord;
    uint16 u16RecordLength;
    uint16 u16BytesRead = 0;

    /* JN516x PDM reads the entire record without enforcing the buffer size.
     * Check the stored length before reading to prevent a buffer overflow. */
    if (!PDM_bDoesDataExist(PDM_ID_APP_REPORTS, &u16RecordLength)) {
        DBG_vPrintf(TRACE_REPORT, "PDM: Reports record not found, using defaults\n");
        return FALSE;
    }

    if (u16RecordLength != sizeof(sRecord)) {
        DBG_vPrintf(TRACE_REPORT, "PDM: Unexpected reports record length=%u\n", u16RecordLength);
        return FALSE;
    }

    PDM_teStatus eStatus = PDM_eReadDataFromRecord(PDM_ID_APP_REPORTS, &sRecord, sizeof(sRecord), &u16BytesRead);
    if ((eStatus != PDM_E_STATUS_OK) || (u16BytesRead != sizeof(sRecord))) {
        DBG_vPrintf(TRACE_REPORT, "PDM: Reports read failed, status=%d length=%u\n", eStatus, u16BytesRead);
        return FALSE;
    }

    if (sRecord.u32Magic != APP_REPORTS_MAGIC) {
        DBG_vPrintf(TRACE_REPORT, "PDM: Invalid reports record magic=%08lx\n", (unsigned long)sRecord.u32Magic);
        return FALSE;
    }

    memcpy(asSavedReports, sRecord.asReports, sizeof(asSavedReports));

    return TRUE;
}

/**
 * @brief Applies the reporting configuration to ZCL.
 */
PUBLIC void APP_vApplyReportingConfig(void)
{
    uint8 i;
    uint16 u16AttributeEnum;
    uint16 u16ClusterId;
    tsZCL_AttributeReportingConfigurationRecord *psAttributeReportingConfigurationRecord;

    DBG_vPrintf(TRACE_REPORT, "Reporting: Apply configuration records=%d\n", NUMBER_OF_REPORTS);

    for (i = 0; i < NUMBER_OF_REPORTS; i++) {
        u16AttributeEnum = asSavedReports[i].sAttributeReportingConfigurationRecord.u16AttributeEnum;
        u16ClusterId = asSavedReports[i].u16ClusterID;
        psAttributeReportingConfigurationRecord = &(asSavedReports[i].sAttributeReportingConfigurationRecord);
        APP_vPrintReportRecord(&asSavedReports[i]);
        eZCL_SetReportableFlag(asSavedReports[i].u8EndPoint, u16ClusterId, TRUE, FALSE, u16AttributeEnum);
        eZCL_CreateLocalReport(asSavedReports[i].u8EndPoint,
                               u16ClusterId,
                               0,
                               TRUE,
                               psAttributeReportingConfigurationRecord);
    }
}

/**
 * @brief Loads a default configuration
 */
PUBLIC void APP_vLoadDefaultReports(void)
{
    uint8 i;

    DBG_vPrintf(TRACE_REPORT, "Reporting: Load default configuration\n");

    for (i = 0; i < NUMBER_OF_REPORTS; i++) {
        asSavedReports[i] = asDefaultReports[i];
        APP_vPrintReportRecord(&asSavedReports[i]);
    }

    APP_vSaveReportsRecord();
}

/**
 * @brief Save reportable record (EP1 application endpoint only)
 * @details EP2/EP3 virtual devices use explicit unicast reports and
 * never persist reporting configurations.
 */
PUBLIC void
APP_vSaveReportableRecord(uint8 u8EndPointID,
                          uint16 u16ClusterID,
                          tsZCL_AttributeReportingConfigurationRecord *psAttributeReportingConfigurationRecord)
{
    /* Save only outgoing report configurations (direction 0). */
    if (psAttributeReportingConfigurationRecord->u8DirectionIsReceived != 0) {
        return;
    }

    if (u8EndPointID != LUMIROUTER_APPLICATION_ENDPOINT) {
        return;
    }

    uint8 u8Index = APP_u8GetRecordIndex(u16ClusterID, psAttributeReportingConfigurationRecord->u16AttributeEnum);
    if (u8Index == APP_REPORT_INDEX_INVALID) {
        return;
    }

    DBG_vPrintf(TRACE_REPORT, "Reporting: Save record index=%d\n", u8Index);

    /* Update the reportable record with new configuration */
    asSavedReports[u8Index].u16ClusterID = u16ClusterID;
    asSavedReports[u8Index].sAttributeReportingConfigurationRecord = *psAttributeReportingConfigurationRecord;
    APP_vPrintReportRecord(&asSavedReports[u8Index]);
    APP_vSaveReportsRecord();
}

/**
 * @brief Restore default record (EP1 application endpoint only)
 */
PUBLIC void
APP_vRestoreDefaultRecord(uint8 u8EndPointID,
                          uint16 u16ClusterID,
                          tsZCL_AttributeReportingConfigurationRecord *psAttributeReportingConfigurationRecord)
{
    uint8 u8Index;

    if (u8EndPointID != LUMIROUTER_APPLICATION_ENDPOINT) {
        return;
    }

    u8Index = APP_u8GetRecordIndex(u16ClusterID, psAttributeReportingConfigurationRecord->u16AttributeEnum);
    if (u8Index == APP_REPORT_INDEX_INVALID) {
        return;
    }

    teZCL_Status eStatus = eZCL_CreateLocalReport(u8EndPointID,
                                                  u16ClusterID,
                                                  0,
                                                  TRUE,
                                                  &asDefaultReports[u8Index].sAttributeReportingConfigurationRecord);
    if (eStatus != E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_REPORT, "Reporting: Failed to restore default record index=%d status=%d\n", u8Index, eStatus);
        return;
    }

    DBG_vPrintf(TRACE_REPORT, "Reporting: Restore default record index=%d\n", u8Index);

    asSavedReports[u8Index] = asDefaultReports[u8Index];
    APP_vPrintReportRecord(&asSavedReports[u8Index]);
    APP_vSaveReportsRecord();
}

/**
 * @brief Saves the reporting configuration to PDM.
 */
PRIVATE void APP_vSaveReportsRecord(void)
{
    APP_tsReportsRecord sRecord = {.u32Magic = APP_REPORTS_MAGIC};

    memcpy(sRecord.asReports, asSavedReports, sizeof(asSavedReports));

    PDM_teStatus eStatus = PDM_eSaveRecordData(PDM_ID_APP_REPORTS, &sRecord, sizeof(sRecord));
    if (eStatus != PDM_E_STATUS_OK) {
        DBG_vPrintf(TRACE_REPORT, "PDM: Failed to save reports, status=%d\n", eStatus);
    }
}

/**
 * @brief Get record index
 */
PRIVATE uint8 APP_u8GetRecordIndex(uint16 u16ClusterID, uint16 u16AttributeEnum)
{
    if ((u16ClusterID == GENERAL_CLUSTER_ID_ONOFF) &&
        (u16AttributeEnum == E_CLD_ONOFF_ATTR_ID_ONOFF)) {
        return REPORT_LAMP_ONOFF_SLOT;
    }

    if ((u16ClusterID == GENERAL_CLUSTER_ID_LEVEL_CONTROL) &&
        (u16AttributeEnum == E_CLD_LEVELCONTROL_ATTR_ID_CURRENT_LEVEL)) {
        return REPORT_LAMP_LEVEL_SLOT;
    }

    if ((u16ClusterID == LIGHTING_CLUSTER_ID_COLOUR_CONTROL) &&
        (u16AttributeEnum == E_CLD_COLOURCONTROL_ATTR_CURRENT_X)) {
        return REPORT_LAMP_CURRENT_X_SLOT;
    }

    if ((u16ClusterID == LIGHTING_CLUSTER_ID_COLOUR_CONTROL) &&
        (u16AttributeEnum == E_CLD_COLOURCONTROL_ATTR_CURRENT_Y)) {
        return REPORT_LAMP_CURRENT_Y_SLOT;
    }

    if ((u16ClusterID == LIGHTING_CLUSTER_ID_COLOUR_CONTROL) &&
        (u16AttributeEnum == E_CLD_COLOURCONTROL_ATTR_COLOUR_TEMPERATURE_MIRED)) {
        return REPORT_LAMP_COLOUR_TEMP_SLOT;
    }

    return APP_REPORT_INDEX_INVALID;
}

/**
 * @brief Print report record for debugging
 */
PRIVATE void APP_vPrintReportRecord(APP_tsReports *psReport)
{
    DBG_vPrintf(TRACE_REPORT,
                "Reporting: Record endpoint=%d cluster=%04x attribute=%04x type=%d "
                "min=%d max=%d timeout=%d direction=%d change=%d\n",
                psReport->u8EndPoint,
                psReport->u16ClusterID,
                psReport->sAttributeReportingConfigurationRecord.u16AttributeEnum,
                psReport->sAttributeReportingConfigurationRecord.eAttributeDataType,
                psReport->sAttributeReportingConfigurationRecord.u16MinimumReportingInterval,
                psReport->sAttributeReportingConfigurationRecord.u16MaximumReportingInterval,
                psReport->sAttributeReportingConfigurationRecord.u16TimeoutPeriodField,
                psReport->sAttributeReportingConfigurationRecord.u8DirectionIsReceived,
                psReport->sAttributeReportingConfigurationRecord.uAttributeReportableChange.zint16ReportableChange);
}

/**
 * @brief Sends an explicit attribute report unicast to the coordinator
 * @details Used by EP2/EP3 virtual devices instead of the reporting
 * engine: no Configure Reporting, no bindings, no persisted slots.
 * The APDU instance is consumed by the stack on success; on early
 * errors it is released here (transmit errors are released inside).
 */
PUBLIC void APP_vSendUnicastReport(uint8 u8SrcEndPoint,
                                   uint16 u16ClusterId,
                                   uint16 u16AttributeId,
                                   bool_t bWithAck)
{
    PDUM_thAPduInstance hAPduInst;
    tsZCL_Address sAddress;
    teZCL_Status eStatus;

    /* Nothing to send to while off the network. */
    if (!sBDB.sAttrib.bbdbNodeIsOnANetwork) {
        return;
    }

    hAPduInst = hZCL_AllocateAPduInstance();
    if (hAPduInst == PDUM_INVALID_HANDLE) {
        DBG_vPrintf(TRACE_REPORT, "Reporting: No APDU instance\n");
        return;
    }

    sAddress.eAddressMode = bWithAck ? E_ZCL_AM_SHORT : E_ZCL_AM_SHORT_NO_ACK;
    sAddress.uAddress.u16DestinationAddress = 0x0000;

    eStatus = eZCL_ReportAttribute(&sAddress, u16ClusterId, u16AttributeId, u8SrcEndPoint, 1, hAPduInst);
    if ((eStatus != E_ZCL_SUCCESS) && (eStatus != E_ZCL_ERR_ZTRANSMIT_FAIL)) {
        /* Pre-transmit failure: the instance was not consumed. */
        PDUM_eAPduFreeAPduInstance(hAPduInst);
        DBG_vPrintf(TRACE_REPORT,
                    "Reporting: Unicast report failed ep=%d cluster=%04x attr=%04x status=%d\n",
                    u8SrcEndPoint,
                    u16ClusterId,
                    u16AttributeId,
                    eStatus);
    }
}
