/**
 * @file  app_zcl_task.c
 * @brief ZCL Interface
 */

#include <jendefs.h>
#include <string.h>

/* Generated */
#include "pdum_gen.h"
#include "zps_gen.h"

/* Application */
#include "app_doorbell.h"
#include "app_lamp.h"
#include "app_main.h"
#include "app_reporting.h"
#include "app_sensor.h"
#include "app_zcl_task.h"
#include "zcl_options.h"

/* SDK JN-SW-4170 */
#include "Basic.h"
#include "Identify.h"
#include "OnOff.h"
#include "LevelControl.h"
#include "ColourControl.h"
#include "MultistateOutputBasic.h"
#include "IlluminanceMeasurement.h"
#include "ZTimer.h"
#include "dbg.h"
#include "zcl.h"

#ifndef TRACE_ZCL
#define TRACE_ZCL FALSE
#endif

#define ZCL_TICK_TIME ZTIMER_TIME_SEC(1)

PRIVATE void APP_ZCL_vTick(void);
PRIVATE void APP_ZCL_cbGeneralCallback(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_cbEndpointCallback(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleClusterCustomCommands(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleClusterUpdate(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleConfigureReportingRecord(tsZCL_CallBackEvent *psEvent);
PRIVATE teZCL_Status APP_ZCL_eRegisterEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack, APP_tsLumiRouter *psDeviceInfo);
PRIVATE teZCL_Status APP_ZCL_eRegisterSensorEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack,
                                                     APP_tsSensor *psDeviceInfo);
PRIVATE teZCL_Status APP_ZCL_eRegisterDoorbellEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack,
                                                       APP_tsDoorbell *psDeviceInfo);
PRIVATE void APP_ZCL_vDeviceSpecific_Init(void);
PRIVATE void APP_ZCL_vHandleLampCustomCommands(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleLampUpdate(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleDoorbellCustomCommands(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleDoorbellUpdate(tsZCL_CallBackEvent *psEvent);
PRIVATE bool_t APP_ZCL_bIsLampCluster(tsZCL_CallBackEvent *psEvent);
PRIVATE bool_t APP_ZCL_bIsDoorbellCluster(tsZCL_CallBackEvent *psEvent);

PUBLIC APP_tsLumiRouter sLumiRouter;
PUBLIC APP_tsSensor sSensor;
PUBLIC APP_tsDoorbell sDoorbell;

/* Ensure Basic cluster strings fit the tsCLD_Basic buffers (JN-SW-4170, Basic.h). */
#define APP_CHECK_BASIC_STRING(name, length, field) \
    typedef char name[((length) <= sizeof(sLumiRouter.sBasicServerCluster.field)) ? 1 : -1]

APP_CHECK_BASIC_STRING(APP_tManufacturerNameSizeCheck, CLD_BAS_MANUF_NAME_SIZE, au8ManufacturerName);
APP_CHECK_BASIC_STRING(APP_tModelIdentifierSizeCheck, CLD_BAS_MODEL_ID_SIZE, au8ModelIdentifier);
APP_CHECK_BASIC_STRING(APP_tBuildDateSizeCheck, CLD_BAS_DATE_SIZE, au8DateCode);
APP_CHECK_BASIC_STRING(APP_tVersionSizeCheck, CLD_BAS_SW_BUILD_SIZE, au8SWBuildID);

#undef APP_CHECK_BASIC_STRING

/**
 * @brief Initialises ZCL, registers the application endpoint, and starts the tick timer
 */
PUBLIC void APP_ZCL_vInitialise(void)
{
    teZCL_Status eZCL_Status;

    /* Initialise ZCL */
    eZCL_Status = eZCL_Initialise(&APP_ZCL_cbGeneralCallback, apduZCL);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL, "ZCL Initialisation: Error status=%x\n", eZCL_Status);
    }

    /* Start the tick timer */
    ZTIMER_eStart(u8TimerTick, ZCL_TICK_TIME);

    /* Register EP1: router + virtual RGB lamp */
    eZCL_Status = APP_ZCL_eRegisterEndPoint(&APP_ZCL_cbEndpointCallback, &sLumiRouter);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Registration: Error status=%x\n", eZCL_Status);
    }

    /* Register EP2: virtual light sensor */
    eZCL_Status = APP_ZCL_eRegisterSensorEndPoint(&APP_ZCL_cbEndpointCallback, &sSensor);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL, "ZCL Sensor Endpoint Registration: Error status=%x\n", eZCL_Status);
    }

    /* Register EP3: virtual doorbell */
    eZCL_Status = APP_ZCL_eRegisterDoorbellEndPoint(&APP_ZCL_cbEndpointCallback, &sDoorbell);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL, "ZCL Doorbell Endpoint Registration: Error status=%x\n", eZCL_Status);
    }

    APP_ZCL_vDeviceSpecific_Init();

    /* Restore virtual device states after their cluster attributes exist. */
    APP_LAMP_vInit();
    APP_SENSOR_vInit();
    APP_DOORBELL_vInit();
}

/**
 * @brief Dispatches a Zigbee stack event to ZCL
 */
PUBLIC void APP_ZCL_vEventHandler(ZPS_tsAfEvent *psStackEvent)
{
    tsZCL_CallBackEvent sCallBackEvent;
    sCallBackEvent.pZPSevent = psStackEvent;

    DBG_vPrintf(TRACE_ZCL, "ZCL Stack Event: type=%d\n", psStackEvent->eType);
    sCallBackEvent.eEventType = E_ZCL_CBET_ZIGBEE_EVENT;
    vZCL_EventHandler(&sCallBackEvent);
}

/**
 * @brief Handles expiration of the ZCL tick timer
 */
PUBLIC void APP_cbTimerZclTick(void *pvParam)
{
    (void)pvParam;

    /* Notify ZCL of the one-second tick, then re-arm the application timer. */
    APP_ZCL_vTick();
    ZTIMER_eStart(u8TimerTick, ZCL_TICK_TIME);
}

/**
 * @brief Dispatches a timer tick event to ZCL
 */
PRIVATE void APP_ZCL_vTick(void)
{
    tsZCL_CallBackEvent sCallBackEvent;

    sCallBackEvent.pZPSevent = NULL;
    sCallBackEvent.eEventType = E_ZCL_CBET_TIMER;
    vZCL_EventHandler(&sCallBackEvent);
}

/**
 * @brief General callback for ZCL events
 */
PRIVATE void APP_ZCL_cbGeneralCallback(tsZCL_CallBackEvent *psEvent)
{
    switch (psEvent->eEventType) {
    case E_ZCL_CBET_ERROR:
        DBG_vPrintf(TRACE_ZCL, "ZCL General Callback: Error status=%x\n", psEvent->eZCL_Status);
        break;

    case E_ZCL_CBET_UNHANDLED_EVENT:
        DBG_vPrintf(TRACE_ZCL, "ZCL General Callback: Unhandled event\n");
        break;

    default:
        DBG_vPrintf(TRACE_ZCL, "ZCL General Callback: Unexpected event type=%d\n", psEvent->eEventType);
        break;
    }
}

/**
 * @brief Endpoint specific callback for ZCL events
 */
PRIVATE void APP_ZCL_cbEndpointCallback(tsZCL_CallBackEvent *psEvent)
{
    switch (psEvent->eEventType) {
    case E_ZCL_CBET_READ_REQUEST:
        /* Use the current attribute values; do not refresh them before the read. */
        break;

    case E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Write range check cluster=%04x attribute=%04x\n",
                    psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum,
                    psEvent->uMessage.sIndividualAttributeResponse.u16AttributeEnum);
        break;

    case E_ZCL_CBET_WRITE_INDIVIDUAL_ATTRIBUTE:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Write attribute cluster=%04x attribute=%04x status=%02x\n",
                    psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum,
                    psEvent->uMessage.sIndividualAttributeResponse.u16AttributeEnum,
                    psEvent->uMessage.sIndividualAttributeResponse.eAttributeStatus);

        if (APP_ZCL_bIsLampCluster(psEvent)) {
            APP_LAMP_vAttributeWritten();
        }
        else if (APP_ZCL_bIsDoorbellCluster(psEvent)) {
            APP_DOORBELL_vAttributeWritten();
        }
        break;

    case E_ZCL_CBET_WRITE_ATTRIBUTES:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Write attributes request processed cluster=%04x status=%02x\n",
                    psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum,
                    psEvent->eZCL_Status);
        break;

    case E_ZCL_CBET_REPORT_INDIVIDUAL_ATTRIBUTES_CONFIGURE:
        APP_ZCL_vHandleConfigureReportingRecord(psEvent);
        break;

    case E_ZCL_CBET_REPORT_ATTRIBUTES_CONFIGURE:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Configure reporting complete cluster=%04x\n",
                    psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum);
        break;

    case E_ZCL_CBET_REPORT_REQUEST:
        /* Use the current attribute values; do not refresh them before reporting. */
        break;

    case E_ZCL_CBET_CLUSTER_CUSTOM:
        if (APP_ZCL_bIsLampCluster(psEvent)) {
            APP_ZCL_vHandleLampCustomCommands(psEvent);
        }
        else if (APP_ZCL_bIsDoorbellCluster(psEvent)) {
            APP_ZCL_vHandleDoorbellCustomCommands(psEvent);
        }
        else {
            APP_ZCL_vHandleClusterCustomCommands(psEvent);
        }
        break;

    case E_ZCL_CBET_CLUSTER_UPDATE:
        if (APP_ZCL_bIsLampCluster(psEvent)) {
            APP_ZCL_vHandleLampUpdate(psEvent);
        }
        else if (APP_ZCL_bIsDoorbellCluster(psEvent)) {
            APP_ZCL_vHandleDoorbellUpdate(psEvent);
        }
        else {
            APP_ZCL_vHandleClusterUpdate(psEvent);
        }
        break;

    case E_ZCL_CBET_DEFAULT_RESPONSE:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Default response command=%02x status=%02x\n",
                    psEvent->uMessage.sDefaultResponse.u8CommandId,
                    psEvent->uMessage.sDefaultResponse.u8StatusCode);
        break;

    case E_ZCL_CBET_ERROR:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Error status=%x endpoint=%d\n",
                    psEvent->eZCL_Status,
                    psEvent->u8EndPoint);
        break;

    case E_ZCL_CBET_UNHANDLED_EVENT:
        DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Callback: Unhandled event\n");
        break;

    default:
        DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Callback: Unexpected event type=%d\n", psEvent->eEventType);
        break;
    }
}

/**
 * @brief Handles cluster-specific commands
 */
PRIVATE void APP_ZCL_vHandleClusterCustomCommands(tsZCL_CallBackEvent *psEvent)
{
    if (psEvent->uMessage.sClusterCustomMessage.u16ClusterId == GENERAL_CLUSTER_ID_IDENTIFY) {
        tsCLD_IdentifyCallBackMessage *psMessage =
            (tsCLD_IdentifyCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        switch (psMessage->u8CommandId) {
        case E_CLD_IDENTIFY_CMD_IDENTIFY:
            /* This module has no physical indicator, so no indication is started or stopped. */
            DBG_vPrintf(TRACE_ZCL,
                        "ZCL Endpoint Callback: Identify time=%d\n",
                        psMessage->uMessage.psIdentifyRequestPayload->u16IdentifyTime);
            break;

        case E_CLD_IDENTIFY_CMD_IDENTIFY_QUERY:
            DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Callback: Identify query\n");
            break;
        }
    }
}

/**
 * @brief Handles cluster state updates
 */
PRIVATE void APP_ZCL_vHandleClusterUpdate(tsZCL_CallBackEvent *psEvent)
{
    if (psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum == GENERAL_CLUSTER_ID_IDENTIFY) {
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Identify time remaining=%d\n",
                    ((tsCLD_Identify *)psEvent->psClusterInstance->pvEndPointSharedStructPtr)->u16IdentifyTime);
    }
}

/**
 * @brief Handles lamp cluster-specific commands from the Zigbee network
 * @note  The stack has already applied discrete commands to the attributes;
 *        continuous commands (Move/Step/Stop) are mirrored live while the
 *        stack transition engine streams progress via cluster updates.
 */
PRIVATE void APP_ZCL_vHandleLampCustomCommands(tsZCL_CallBackEvent *psEvent)
{
    uint16 u16ClusterId = psEvent->uMessage.sClusterCustomMessage.u16ClusterId;

    if (u16ClusterId == GENERAL_CLUSTER_ID_ONOFF) {
        tsCLD_OnOffCallBackMessage *psMessage =
            (tsCLD_OnOffCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        APP_LAMP_vOnOffCommand(psMessage->u8CommandId);
    }
    else if (u16ClusterId == GENERAL_CLUSTER_ID_LEVEL_CONTROL) {
        tsCLD_LevelControlCallBackMessage *psMessage =
            (tsCLD_LevelControlCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        switch (psMessage->u8CommandId) {
        case E_CLD_LEVELCONTROL_CMD_MOVE_TO_LEVEL:
            APP_LAMP_vLevelCommand(psMessage->u8CommandId,
                                   psMessage->uMessage.psMoveToLevelCommandPayload->u8Level,
                                   FALSE);
            break;

        case E_CLD_LEVELCONTROL_CMD_MOVE_TO_LEVEL_WITH_ON_OFF:
            APP_LAMP_vLevelCommand(psMessage->u8CommandId,
                                   psMessage->uMessage.psMoveToLevelCommandPayload->u8Level,
                                   TRUE);
            break;

        default:
            /* Move/Step/Stop (with or without On/Off): forward live values,
             * the transition engine reports progress via cluster updates. */
            APP_LAMP_vLevelCommand(psMessage->u8CommandId, 0U, FALSE);
            break;
        }
    }
    else if (u16ClusterId == LIGHTING_CLUSTER_ID_COLOUR_CONTROL) {
        tsCLD_ColourControlCallBackMessage *psMessage =
            (tsCLD_ColourControlCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        switch (psMessage->u8CommandId) {
        case E_CLD_COLOURCONTROL_CMD_MOVE_TO_COLOUR:
            APP_LAMP_vColourXyCommand(psMessage->uMessage.psMoveToColourCommandPayload->u16ColourX,
                                      psMessage->uMessage.psMoveToColourCommandPayload->u16ColourY);
            break;

        case E_CLD_COLOURCONTROL_CMD_MOVE_TO_HUE_AND_SATURATION:
            APP_LAMP_vColourHsCommand(
                psMessage->uMessage.psMoveToHueAndSaturationCommandPayload->u8Hue,
                psMessage->uMessage.psMoveToHueAndSaturationCommandPayload->u8Saturation);
            break;

        default:
            APP_LAMP_vSendStateToHost(LAMP_CMD_MOVE_STEP_STOP);
            break;
        }
    }
    else {
        DBG_vPrintf(TRACE_ZCL, "ZCL Lamp: Unhandled cluster command cluster=%04x\n", u16ClusterId);
    }
}

/**
 * @brief Handles lamp cluster state updates (transition engine progress)
 */
PRIVATE void APP_ZCL_vHandleLampUpdate(tsZCL_CallBackEvent *psEvent)
{
    uint16 u16ClusterId = psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum;

    if ((u16ClusterId == GENERAL_CLUSTER_ID_ONOFF) ||
        (u16ClusterId == GENERAL_CLUSTER_ID_LEVEL_CONTROL) ||
        (u16ClusterId == LIGHTING_CLUSTER_ID_COLOUR_CONTROL)) {
        APP_LAMP_vTransitionUpdate();
    }
}

/**
 * @brief Checks whether the event targets a lamp cluster on EP1
 */
PRIVATE bool_t APP_ZCL_bIsLampCluster(tsZCL_CallBackEvent *psEvent)
{
    uint16 u16ClusterId;

    if (psEvent->u8EndPoint != LUMIROUTER_APPLICATION_ENDPOINT) {
        return FALSE;
    }

    u16ClusterId = psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum;

    return ((u16ClusterId == GENERAL_CLUSTER_ID_ONOFF) ||
            (u16ClusterId == GENERAL_CLUSTER_ID_LEVEL_CONTROL) ||
            (u16ClusterId == LIGHTING_CLUSTER_ID_COLOUR_CONTROL))
        ? TRUE
        : FALSE;
}

/**
 * @brief Checks whether the event targets a doorbell cluster on EP3
 */
PRIVATE bool_t APP_ZCL_bIsDoorbellCluster(tsZCL_CallBackEvent *psEvent)
{
    uint16 u16ClusterId;

    if (psEvent->u8EndPoint != LUMIROUTER_DOORBELL_ENDPOINT) {
        return FALSE;
    }

    u16ClusterId = psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum;

    return ((u16ClusterId == GENERAL_CLUSTER_ID_ONOFF) ||
            (u16ClusterId == GENERAL_CLUSTER_ID_LEVEL_CONTROL) ||
            (u16ClusterId == GENERAL_CLUSTER_ID_MULTISTATE_OUTPUT_BASIC))
        ? TRUE
        : FALSE;
}

/**
 * @brief Handles doorbell cluster-specific commands from the Zigbee network
 */
PRIVATE void APP_ZCL_vHandleDoorbellCustomCommands(tsZCL_CallBackEvent *psEvent)
{
    uint16 u16ClusterId = psEvent->uMessage.sClusterCustomMessage.u16ClusterId;

    if (u16ClusterId == GENERAL_CLUSTER_ID_ONOFF) {
        tsCLD_OnOffCallBackMessage *psMessage =
            (tsCLD_OnOffCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        APP_DOORBELL_vPlayCommand(psMessage->u8CommandId);
    }
    else if (u16ClusterId == GENERAL_CLUSTER_ID_LEVEL_CONTROL) {
        tsCLD_LevelControlCallBackMessage *psMessage =
            (tsCLD_LevelControlCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        switch (psMessage->u8CommandId) {
        case E_CLD_LEVELCONTROL_CMD_MOVE_TO_LEVEL:
            APP_DOORBELL_vVolumeCommand(psMessage->uMessage.psMoveToLevelCommandPayload->u8Level, FALSE);
            break;

        case E_CLD_LEVELCONTROL_CMD_MOVE_TO_LEVEL_WITH_ON_OFF:
            APP_DOORBELL_vVolumeCommand(psMessage->uMessage.psMoveToLevelCommandPayload->u8Level, TRUE);
            break;

        default:
            /* Move/Step/Stop: forward live values, the transition engine
             * reports progress via cluster updates. */
            APP_DOORBELL_vVolumeUpdate();
            break;
        }
    }
    else {
        DBG_vPrintf(TRACE_ZCL, "ZCL Doorbell: Unhandled cluster command cluster=%04x\n", u16ClusterId);
    }
}

/**
 * @brief Handles doorbell cluster state updates (transition engine progress)
 */
PRIVATE void APP_ZCL_vHandleDoorbellUpdate(tsZCL_CallBackEvent *psEvent)
{
    uint16 u16ClusterId = psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum;

    if ((u16ClusterId == GENERAL_CLUSTER_ID_ONOFF) ||
        (u16ClusterId == GENERAL_CLUSTER_ID_LEVEL_CONTROL)) {
        APP_DOORBELL_vVolumeUpdate();
    }
}

/**
 * @brief Handles a Configure Reporting record
 */
PRIVATE void APP_ZCL_vHandleConfigureReportingRecord(tsZCL_CallBackEvent *psEvent)
{
    tsZCL_AttributeReportingConfigurationRecord *psRecord = &psEvent->uMessage.sAttributeReportingConfigurationRecord;
    uint16 u16ClusterId = psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum;

    if (psEvent->eZCL_Status == E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Configure reporting "
                    "cluster=%04x attribute=%04x type=%d min=%d max=%d\n",
                    u16ClusterId,
                    psRecord->u16AttributeEnum,
                    psRecord->eAttributeDataType,
                    psRecord->u16MinimumReportingInterval,
                    psRecord->u16MaximumReportingInterval);

        APP_vSaveReportableRecord(psEvent->u8EndPoint, u16ClusterId, psRecord);
    }
    else if (psEvent->eZCL_Status == E_ZCL_RESTORE_DEFAULT_REPORT_CONFIGURATION) {
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Configure reporting restore default "
                    "cluster=%04x attribute=%04x\n",
                    u16ClusterId,
                    psRecord->u16AttributeEnum);

        APP_vRestoreDefaultRecord(psEvent->u8EndPoint, u16ClusterId, psRecord);
    }
    else {
        /* An empty request may leave the reporting record uninitialized. */
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Configure reporting failed "
                    "cluster=%04x status=%x\n",
                    u16ClusterId,
                    psEvent->eZCL_Status);
    }
}

/**
 * @brief Creates cluster instances and registers the application endpoint with ZCL
 */
PRIVATE teZCL_Status APP_ZCL_eRegisterEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack, APP_tsLumiRouter *psDeviceInfo)
{
    teZCL_Status eZCL_Status;

    /* Fill in end point details */
    psDeviceInfo->sEndPoint.u8EndPointNumber = LUMIROUTER_APPLICATION_ENDPOINT;
    psDeviceInfo->sEndPoint.u16ManufacturerCode = ZCL_MANUFACTURER_CODE;
    psDeviceInfo->sEndPoint.u16ProfileEnum = HA_PROFILE_ID;
    psDeviceInfo->sEndPoint.bIsManufacturerSpecificProfile = FALSE;
    psDeviceInfo->sEndPoint.u16NumberOfClusters =
        sizeof(APP_tsLumiRouterClusterInstances) / sizeof(tsZCL_ClusterInstance);
    psDeviceInfo->sEndPoint.psClusterInstance = (tsZCL_ClusterInstance *)&psDeviceInfo->sClusterInstance;
    psDeviceInfo->sEndPoint.bDisableDefaultResponse = ZCL_DISABLE_DEFAULT_RESPONSES;
    psDeviceInfo->sEndPoint.pCallBackFunctions = cbCallBack;

    eZCL_Status = eCLD_BasicCreateBasic(&psDeviceInfo->sClusterInstance.sBasicServer,
                                        TRUE,
                                        &sCLD_Basic,
                                        &psDeviceInfo->sBasicServerCluster,
                                        &au8BasicClusterAttributeControlBits[0]);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    eZCL_Status = eCLD_IdentifyCreateIdentify(&psDeviceInfo->sClusterInstance.sIdentifyServer,
                                              TRUE,
                                              &sCLD_Identify,
                                              &psDeviceInfo->sIdentifyServerCluster,
                                              &au8IdentifyAttributeControlBits[0],
                                              &psDeviceInfo->sIdentifyServerCustomDataStructure);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    eZCL_Status = eCLD_OnOffCreateOnOff(&psDeviceInfo->sClusterInstance.sOnOffServer,
                                        TRUE,
                                        &sCLD_OnOff,
                                        &psDeviceInfo->sOnOffServerCluster,
                                        &au8OnOffAttributeControlBits[0],
                                        &psDeviceInfo->sOnOffServerCustomDataStructure);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    eZCL_Status = eCLD_LevelControlCreateLevelControl(&psDeviceInfo->sClusterInstance.sLevelControlServer,
                                                      TRUE,
                                                      &sCLD_LevelControl,
                                                      &psDeviceInfo->sLevelControlServerCluster,
                                                      &au8LevelControlAttributeControlBits[0],
                                                      &psDeviceInfo->sLevelControlServerCustomDataStructure);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    eZCL_Status = eCLD_ColourControlCreateColourControl(&psDeviceInfo->sClusterInstance.sColourControlServer,
                                                        TRUE,
                                                        &sCLD_ColourControl,
                                                        &psDeviceInfo->sColourControlServerCluster,
                                                        &au8ColourControlAttributeControlBits[0],
                                                        &psDeviceInfo->sColourControlServerCustomDataStructure);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    return eZCL_Register(&psDeviceInfo->sEndPoint);
}

/**
 * @brief Creates cluster instances and registers the sensor endpoint with ZCL
 */
PRIVATE teZCL_Status APP_ZCL_eRegisterSensorEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack,
                                                     APP_tsSensor *psDeviceInfo)
{
    teZCL_Status eZCL_Status;

    /* Fill in end point details */
    psDeviceInfo->sEndPoint.u8EndPointNumber = LUMIROUTER_SENSOR_ENDPOINT;
    psDeviceInfo->sEndPoint.u16ManufacturerCode = ZCL_MANUFACTURER_CODE;
    psDeviceInfo->sEndPoint.u16ProfileEnum = HA_PROFILE_ID;
    psDeviceInfo->sEndPoint.bIsManufacturerSpecificProfile = FALSE;
    psDeviceInfo->sEndPoint.u16NumberOfClusters =
        sizeof(APP_tsSensorClusterInstances) / sizeof(tsZCL_ClusterInstance);
    psDeviceInfo->sEndPoint.psClusterInstance = (tsZCL_ClusterInstance *)&psDeviceInfo->sClusterInstance;
    psDeviceInfo->sEndPoint.bDisableDefaultResponse = ZCL_DISABLE_DEFAULT_RESPONSES;
    psDeviceInfo->sEndPoint.pCallBackFunctions = cbCallBack;

    eZCL_Status = eCLD_IlluminanceMeasurementCreateIlluminanceMeasurement(
        &psDeviceInfo->sClusterInstance.sIlluminanceMeasurementServer,
        TRUE,
        &sCLD_IlluminanceMeasurement,
        &psDeviceInfo->sIlluminanceMeasurementServerCluster,
        &au8IlluminanceMeasurementAttributeControlBits[0]);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    return eZCL_Register(&psDeviceInfo->sEndPoint);
}

/**
 * @brief Creates cluster instances and registers the doorbell endpoint with ZCL
 */
PRIVATE teZCL_Status APP_ZCL_eRegisterDoorbellEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack,
                                                       APP_tsDoorbell *psDeviceInfo)
{
    teZCL_Status eZCL_Status;

    /* Fill in end point details */
    psDeviceInfo->sEndPoint.u8EndPointNumber = LUMIROUTER_DOORBELL_ENDPOINT;
    psDeviceInfo->sEndPoint.u16ManufacturerCode = ZCL_MANUFACTURER_CODE;
    psDeviceInfo->sEndPoint.u16ProfileEnum = HA_PROFILE_ID;
    psDeviceInfo->sEndPoint.bIsManufacturerSpecificProfile = FALSE;
    psDeviceInfo->sEndPoint.u16NumberOfClusters =
        sizeof(APP_tsDoorbellClusterInstances) / sizeof(tsZCL_ClusterInstance);
    psDeviceInfo->sEndPoint.psClusterInstance = (tsZCL_ClusterInstance *)&psDeviceInfo->sClusterInstance;
    psDeviceInfo->sEndPoint.bDisableDefaultResponse = ZCL_DISABLE_DEFAULT_RESPONSES;
    psDeviceInfo->sEndPoint.pCallBackFunctions = cbCallBack;

    eZCL_Status = eCLD_OnOffCreateOnOff(&psDeviceInfo->sClusterInstance.sOnOffServer,
                                        TRUE,
                                        &sCLD_OnOff,
                                        &psDeviceInfo->sOnOffServerCluster,
                                        &au8OnOffAttributeControlBits[0],
                                        &psDeviceInfo->sOnOffServerCustomDataStructure);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    eZCL_Status = eCLD_LevelControlCreateLevelControl(&psDeviceInfo->sClusterInstance.sLevelControlServer,
                                                      TRUE,
                                                      &sCLD_LevelControl,
                                                      &psDeviceInfo->sLevelControlServerCluster,
                                                      &au8LevelControlAttributeControlBits[0],
                                                      &psDeviceInfo->sLevelControlServerCustomDataStructure);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    eZCL_Status = eCLD_MultistateOutputBasicCreateMultistateOutputBasic(
        &psDeviceInfo->sClusterInstance.sMultistateOutputServer,
        TRUE,
        &sCLD_MultistateOutputBasic,
        &psDeviceInfo->sMultistateOutputServerCluster,
        &au8MultistateOutputBasicAttributeControlBits[0]);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    return eZCL_Register(&psDeviceInfo->sEndPoint);
}

/**
 * @brief Initialise ZCL device-specific attributes
 */
PRIVATE void APP_ZCL_vDeviceSpecific_Init(void)
{
    memcpy(sLumiRouter.sBasicServerCluster.au8ManufacturerName, BAS_MANUF_NAME_STRING, CLD_BAS_MANUF_NAME_SIZE);
    memcpy(sLumiRouter.sBasicServerCluster.au8ModelIdentifier, BAS_MODEL_ID_STRING, CLD_BAS_MODEL_ID_SIZE);
    memcpy(sLumiRouter.sBasicServerCluster.au8DateCode, BAS_DATE_STRING, CLD_BAS_DATE_SIZE);
    memcpy(sLumiRouter.sBasicServerCluster.au8SWBuildID, BAS_SW_BUILD_STRING, CLD_BAS_SW_BUILD_SIZE);
    sLumiRouter.sBasicServerCluster.u8HardwareVersion = BAS_HARDWARE_VERSION;
}
