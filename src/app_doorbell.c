/**
 * @file  app_doorbell.c
 * @brief Virtual doorbell (play/mute + volume + melody) on EP3
 */

#include <jendefs.h>
#include <string.h>

/* Generated */
#include "zps_gen.h"

/* Application */
#include "PDM_IDs.h"
#include "app_doorbell.h"
#include "app_reporting.h"
#include "app_serial_commands.h"
#include "app_zcl_task.h"
#include "zcl_options.h"

/* SDK JN-SW-4170 */
#include "ColourControl.h"
#include "LevelControl.h"
#include "MultistateOutputBasic.h"
#include "OnOff.h"
#include "PDM.h"
#include "dbg.h"
#include "zcl.h"

#ifndef TRACE_DOORBELL
#define TRACE_DOORBELL FALSE
#endif

#define APP_DOORBELL_STATE_MAGIC 0x5A524401UL /* ZR (Zigbee Router) + D (Doorbell) + revision 1 */

/* Power-on defaults: stopped, full volume, melody 0 */
#define APP_DOORBELL_DEFAULT_VOLUME 0xFEU
#define APP_DOORBELL_DEFAULT_MELODY 0U

/* Doorbell snapshot commands */
enum {
    DOORBELL_CMD_RING = 0x00,
    DOORBELL_CMD_MUTE = 0x01,
    DOORBELL_CMD_VOLUME = 0x02,
    DOORBELL_CMD_MELODY = 0x03,
    DOORBELL_CMD_WRITE = 0x04,
    DOORBELL_CMD_UPDATE = 0x05
};

typedef struct {
    uint32 u32Magic;
    uint8 u8Volume;
    uint16 u16Melody;
} APP_tsDoorbellStateRecord;

PRIVATE APP_tsDoorbellState sState;

PRIVATE void APP_DOORBELL_vSaveState(void);
PRIVATE void APP_DOORBELL_vSendSnapshot(uint8 u8Command);
PRIVATE void APP_DOORBELL_vReportAll(bool_t bWithAck);
PRIVATE const char *APP_DOORBELL_pcCommandName(uint8 u8Command);
PRIVATE uint8 APP_DOORBELL_u8AppendText(const char *pcText, char *pcOut);
PRIVATE uint8 APP_DOORBELL_u8AppendDec(uint8 u8Value, char *pcOut);
PRIVATE uint8 APP_DOORBELL_u8AppendDec16(uint16 u16Value, char *pcOut);
PRIVATE uint8 APP_DOORBELL_u8AppendDec32(uint32 u32Value, char *pcOut);

/**
 * @brief Restores the doorbell state from PDM (or defaults)
 * @note  Must be called after the Doorbell endpoint is registered.
 *        Playback never survives a reboot (starts stopped).
 */
PUBLIC void APP_DOORBELL_vInit(void)
{
    APP_tsDoorbellStateRecord sRecord;
    uint16 u16RecordLength;
    uint16 u16BytesRead = 0;
    bool_t bRestored = FALSE;

    /* JN516x PDM reads the entire record without enforcing the buffer size.
     * Check the stored length before reading to prevent a buffer overflow. */
    if (PDM_bDoesDataExist(PDM_ID_APP_DOORBELL_STATE, &u16RecordLength) &&
        (u16RecordLength == sizeof(sRecord))) {
        if ((PDM_eReadDataFromRecord(PDM_ID_APP_DOORBELL_STATE, &sRecord, sizeof(sRecord), &u16BytesRead) ==
             PDM_E_STATUS_OK) &&
            (u16BytesRead == sizeof(sRecord)) &&
            (sRecord.u32Magic == APP_DOORBELL_STATE_MAGIC)) {
            sState.u8Volume = sRecord.u8Volume;
            sState.u16Melody = sRecord.u16Melody;
            bRestored = TRUE;
        }
    }

    if (!bRestored) {
        DBG_vPrintf(TRACE_DOORBELL, "Doorbell: No valid state record, using defaults\n");
        sState.u8Volume = APP_DOORBELL_DEFAULT_VOLUME;
        sState.u16Melody = APP_DOORBELL_DEFAULT_MELODY;
        APP_DOORBELL_vSaveState();
    }

    sState.bPlay = FALSE;

    sDoorbell.sMultistateOutputServerCluster.u16NumberOfStates = APP_DOORBELL_NUMBER_OF_STATES;
    sDoorbell.sMultistateOutputServerCluster.bOutOfService = FALSE;
    sDoorbell.sMultistateOutputServerCluster.u8StatusFlags = 0;

    APP_DOORBELL_vApplyToAttributes();
}

/**
 * @brief Resets the doorbell state to defaults (used on factory reset)
 */
PUBLIC void APP_DOORBELL_vFactoryReset(void)
{
    sState.bPlay = FALSE;
    sState.u8Volume = APP_DOORBELL_DEFAULT_VOLUME;
    sState.u16Melody = APP_DOORBELL_DEFAULT_MELODY;

    APP_DOORBELL_vSaveState();
    APP_DOORBELL_vApplyToAttributes();
}

/**
 * @brief Writes the RAM mirror state into the ZCL cluster attributes
 */
PUBLIC void APP_DOORBELL_vApplyToAttributes(void)
{
    sDoorbell.sOnOffServerCluster.bOnOff = sState.bPlay;
    sDoorbell.sLevelControlServerCluster.u8CurrentLevel = sState.u8Volume;
    sDoorbell.sMultistateOutputServerCluster.u16PresentValue = sState.u16Melody;
}

/**
 * @brief Handles an On/Off cluster command from the Zigbee network
 * @note  Play state is derived from the command itself: with LevelControl
 *        present the stack defers the OnOff attribute (the transition
 *        engine sets it on completion), so the attribute lags here.
 *        The LevelControl transition started by the stack (lamp semantics:
 *        Off fades brightness to minimum) is cancelled: a doorbell keeps
 *        volume independent from play/stop.
 */
PUBLIC void APP_DOORBELL_vPlayCommand(uint8 u8ZclCommandId)
{
    uint8 u8Command;

    switch (u8ZclCommandId) {
    case 0x00: /* E_CLD_ONOFF_CMD_OFF */
        sState.bPlay = FALSE;
        u8Command = DOORBELL_CMD_MUTE;
        break;

    case 0x01: /* E_CLD_ONOFF_CMD_ON */
        sState.bPlay = TRUE;
        u8Command = DOORBELL_CMD_RING;
        break;

    default: /* E_CLD_ONOFF_CMD_TOGGLE and others: invert */
        sState.bPlay = sState.bPlay ? FALSE : TRUE;
        u8Command = sState.bPlay ? DOORBELL_CMD_RING : DOORBELL_CMD_MUTE;
        break;
    }

    /* Force the attribute (the engine lags) and decouple volume:
     * kill any transition and clear the WithOnOff latch, otherwise
     * every ZCL tick forces OnOff back from the level. */
    sDoorbell.sOnOffServerCluster.bOnOff = sState.bPlay;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.eTransition =
        E_CLD_LEVELCONTROL_TRANSITION_NONE;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.bWithOnOff = FALSE;
    sDoorbell.sLevelControlServerCluster.u8CurrentLevel = sState.u8Volume;

    DBG_vPrintf(TRACE_DOORBELL, "Doorbell: Play command=%02x play=%d\n", u8ZclCommandId, sState.bPlay);

    APP_DOORBELL_vSendSnapshot(u8Command);
    APP_DOORBELL_vReportAll(TRUE);
}

/**
 * @brief Handles a Level Control MoveToLevel command (volume)
 * @note  Volume never touches playback state: unlike a lamp, muting and
 *        volume are independent here, so the WithOnOff linkage is ignored.
 */
PUBLIC void APP_DOORBELL_vVolumeCommand(uint8 u8TargetLevel, bool_t bWithOnOff)
{
    (void)bWithOnOff;

    sState.u8Volume = (u8TargetLevel > APP_DOORBELL_VOLUME_MAX) ? APP_DOORBELL_VOLUME_MAX : u8TargetLevel;

    /* Short-circuit the transition: the host applies the target.
     * Neutralise the stack engine as well, otherwise it overwrites the
     * attribute from its own (possibly hours-long with transtime 0xFFFF)
     * ramp on the next ticks. */
    sDoorbell.sLevelControlServerCluster.u8CurrentLevel = sState.u8Volume;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.eTransition =
        E_CLD_LEVELCONTROL_TRANSITION_NONE;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.iCurrentLevel =
        (int)sState.u8Volume * 100;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.iTargetLevel =
        (int)sState.u8Volume * 100;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.iPreviousLevel =
        (int)sState.u8Volume * 100;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.u32Time = 0;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.iStepSize = 0;
    sDoorbell.sLevelControlServerCustomDataStructure.sTransition.bWithOnOff = FALSE;

    DBG_vPrintf(TRACE_DOORBELL, "Doorbell: Volume target=%d\n", sState.u8Volume);

    APP_DOORBELL_vSaveState();
    APP_DOORBELL_vSendSnapshot(DOORBELL_CMD_VOLUME);
    APP_DOORBELL_vReportAll(FALSE);
}

/**
 * @brief Forwards live attribute values after a stack transition step
 */
PUBLIC void APP_DOORBELL_vVolumeUpdate(void)
{
    sState.u8Volume = sDoorbell.sLevelControlServerCluster.u8CurrentLevel;
    sState.bPlay = sDoorbell.sOnOffServerCluster.bOnOff ? TRUE : FALSE;

    APP_DOORBELL_vSendSnapshot(DOORBELL_CMD_UPDATE);
    APP_DOORBELL_vReportAll(FALSE);
}

/**
 * @brief Forwards attribute values after a Write Attributes request
 * @details Covers melody selection (PresentValue) and direct writes.
 */
PUBLIC void APP_DOORBELL_vAttributeWritten(void)
{
    sState.bPlay = sDoorbell.sOnOffServerCluster.bOnOff ? TRUE : FALSE;
    sState.u8Volume = sDoorbell.sLevelControlServerCluster.u8CurrentLevel;
    sState.u16Melody = sDoorbell.sMultistateOutputServerCluster.u16PresentValue;

    if (sState.u16Melody > APP_DOORBELL_MELODY_MAX) {
        sState.u16Melody = APP_DOORBELL_MELODY_MAX;
        sDoorbell.sMultistateOutputServerCluster.u16PresentValue = sState.u16Melody;
    }

    DBG_vPrintf(TRACE_DOORBELL,
                "Doorbell: Attributes written play=%d volume=%d melody=%d\n",
                sState.bPlay,
                sState.u8Volume,
                sState.u16Melody);

    APP_DOORBELL_vSaveState();
    APP_DOORBELL_vSendSnapshot(DOORBELL_CMD_WRITE);
    APP_DOORBELL_vReportAll(FALSE);
}

/**
 * @brief Applies an authoritative state line received from the host
 */
PUBLIC void APP_DOORBELL_vHandleHostJson(const APP_tsHostJson *psHost)
{
    bool_t bChanged = FALSE;

    if (psHost->bHasPlay) {
        sState.bPlay = psHost->bPlay ? TRUE : FALSE;
        bChanged = TRUE;
    }

    if (psHost->bHasVolume) {
        sState.u8Volume =
            (psHost->u8Volume > APP_DOORBELL_VOLUME_MAX) ? APP_DOORBELL_VOLUME_MAX : psHost->u8Volume;
        bChanged = TRUE;
    }

    if (psHost->bHasMelody) {
        sState.u16Melody =
            (psHost->u16Melody > APP_DOORBELL_MELODY_MAX) ? APP_DOORBELL_MELODY_MAX : psHost->u16Melody;
        bChanged = TRUE;
    }

    if (!bChanged) {
        return;
    }

    DBG_vPrintf(TRACE_DOORBELL,
                "Doorbell: Host json play=%d volume=%d melody=%d\n",
                sState.bPlay,
                sState.u8Volume,
                sState.u16Melody);

    APP_DOORBELL_vApplyToAttributes();
    APP_DOORBELL_vSaveState();

    /* Echo the applied state back so the host sees the result. */
    APP_DOORBELL_vSendSnapshot(DOORBELL_CMD_WRITE);
    APP_DOORBELL_vReportAll(FALSE);
}

/**
 * @brief Persists volume and melody to PDM (playback is never stored)
 */
PRIVATE void APP_DOORBELL_vSaveState(void)
{
    APP_tsDoorbellStateRecord sRecord;

    sRecord.u32Magic = APP_DOORBELL_STATE_MAGIC;
    sRecord.u8Volume = sState.u8Volume;
    sRecord.u16Melody = sState.u16Melody;

    if (PDM_eSaveRecordData(PDM_ID_APP_DOORBELL_STATE, &sRecord, sizeof(sRecord)) != PDM_E_STATUS_OK) {
        DBG_vPrintf(TRACE_DOORBELL, "Doorbell: Failed to save state\n");
    }
}

/**
 * @brief Builds a state JSON line and sends it to the host
 * @details Line format:
 * {"cmd":"ring","play":1,"volume":132,"melody":3}
 * Consecutive duplicates are dropped; seq advances only on sends.
 */
PRIVATE void APP_DOORBELL_vSendSnapshot(uint8 u8Command)
{
    char acLine[128];
    uint8 u8Length = 0U;

    static bool_t bLastPlay = FALSE;
    static uint8 u8LastVolume = 0xFFU;
    static uint16 u16LastMelody = 0xFFFFU;

    if ((sState.bPlay == bLastPlay) &&
        (sState.u8Volume == u8LastVolume) && (sState.u16Melody == u16LastMelody)) {
        return;
    }

    bLastPlay = sState.bPlay;
    u8LastVolume = sState.u8Volume;
    u16LastMelody = sState.u16Melody;

    u8Length += APP_DOORBELL_u8AppendText("{\"cmd\":\"", &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendText(APP_DOORBELL_pcCommandName(u8Command), &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendText("\",\"play\":", &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendDec((sState.bPlay != FALSE) ? 1U : 0U, &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendText(",\"volume\":", &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendDec(sState.u8Volume, &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendText(",\"melody\":", &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendDec16(sState.u16Melody, &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendText(",\"seq\":", &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendDec32(APP_u32NextSeq(), &acLine[u8Length]);
    u8Length += APP_DOORBELL_u8AppendText("}", &acLine[u8Length]);
    acLine[u8Length] = '\0';

    APP_vSendSerialLine(acLine);
}

/**
 * @brief Reports play, volume and melody to the coordinator (unicast)
 */
PRIVATE void APP_DOORBELL_vReportAll(bool_t bWithAck)
{
    APP_vSendUnicastReport(LUMIROUTER_DOORBELL_ENDPOINT,
                           GENERAL_CLUSTER_ID_ONOFF,
                           E_CLD_ONOFF_ATTR_ID_ONOFF,
                           bWithAck);
    APP_vSendUnicastReport(LUMIROUTER_DOORBELL_ENDPOINT,
                           GENERAL_CLUSTER_ID_LEVEL_CONTROL,
                           E_CLD_LEVELCONTROL_ATTR_ID_CURRENT_LEVEL,
                           FALSE);
    APP_vSendUnicastReport(LUMIROUTER_DOORBELL_ENDPOINT,
                           GENERAL_CLUSTER_ID_MULTISTATE_OUTPUT_BASIC,
                           E_CLD_MULTISTATE_OUTPUT_BASIC_ATTR_ID_PRESENT_VALUE,
                           FALSE);
}

/**
 * @brief Maps a doorbell command code to its JSON name
 */
PRIVATE const char *APP_DOORBELL_pcCommandName(uint8 u8Command)
{
    switch (u8Command) {
    case DOORBELL_CMD_RING:
        return "ring";

    case DOORBELL_CMD_MUTE:
        return "mute";

    case DOORBELL_CMD_VOLUME:
        return "volume";

    case DOORBELL_CMD_MELODY:
        return "melody";

    case DOORBELL_CMD_WRITE:
        return "write";

    default:
        return "update";
    }
}

/**
 * @brief Copies a string, returns its length
 */
PRIVATE uint8 APP_DOORBELL_u8AppendText(const char *pcText, char *pcOut)
{
    uint8 u8Length = 0U;

    while (*pcText != '\0') {
        *pcOut = *pcText;
        pcOut++;
        pcText++;
        u8Length++;
    }

    return u8Length;
}

/**
 * @brief Formats 0..255 decimal, returns its length
 */
PRIVATE uint8 APP_DOORBELL_u8AppendDec(uint8 u8Value, char *pcOut)
{
    uint8 u8Length = 0U;

    if (u8Value >= 100U) {
        *pcOut = (char)('0' + u8Value / 100U);
        pcOut++;
        u8Length++;
        u8Value %= 100U;
    }

    if ((u8Length > 0U) || (u8Value >= 10U)) {
        *pcOut = (char)('0' + u8Value / 10U);
        pcOut++;
        u8Length++;
        u8Value %= 10U;
    }

    *pcOut = (char)('0' + u8Value);
    return (uint8)(u8Length + 1U);
}

/**
 * @brief Formats 0..65535 decimal, returns its length
 */
PRIVATE uint8 APP_DOORBELL_u8AppendDec16(uint16 u16Value, char *pcOut)
{
    uint8 u8Length = 0U;
    uint16 u16Divisor = 10000U;
    bool_t bStarted = FALSE;

    while (u16Divisor > 0U) {
        uint8 u8Digit = (uint8)(u16Value / u16Divisor);

        if ((u8Digit != 0U) || bStarted || (u16Divisor == 1U)) {
            *pcOut = (char)('0' + u8Digit);
            pcOut++;
            u8Length++;
            bStarted = TRUE;
        }

        u16Value %= u16Divisor;
        u16Divisor /= 10U;
    }

    return u8Length;
}

/**
 * @brief Formats 0..4294967295 decimal, returns its length
 */
PRIVATE uint8 APP_DOORBELL_u8AppendDec32(uint32 u32Value, char *pcOut)
{
    uint8 u8Length = 0U;
    uint32 u32Divisor = 1000000000UL;
    bool_t bStarted = FALSE;

    while (u32Divisor > 0UL) {
        uint8 u8Digit = (uint8)(u32Value / u32Divisor);

        if ((u8Digit != 0U) || bStarted || (u32Divisor == 1UL)) {
            *pcOut = (char)('0' + u8Digit);
            pcOut++;
            u8Length++;
            bStarted = TRUE;
        }

        u32Value %= u32Divisor;
        u32Divisor /= 10UL;
    }

    return u8Length;
}
