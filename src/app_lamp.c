/**
 * @file  app_lamp.c
 * @brief Virtual RGB lamp bridged to a host controller over UART
 */

#include <jendefs.h>
#include <math.h>
#include <string.h>

/* Application */
#include "PDM_IDs.h"
#include "app_lamp.h"
#include "app_serial_commands.h"
#include "app_zcl_task.h"
#include "zcl_options.h"

/* SDK JN-SW-4170 */
#include "PDM.h"
#include "dbg.h"

#ifndef TRACE_LAMP
#define TRACE_LAMP FALSE
#endif

#define APP_LAMP_STATE_MAGIC 0x5A524C01UL /* ZR (Zigbee Router) + L (Lamp) + revision 1 */

/* Power-on defaults: off, full level, D65 white (x=0.3127, y=0.3290) */
#define APP_LAMP_DEFAULT_ONOFF FALSE
#define APP_LAMP_DEFAULT_LEVEL 0xFEU
#define APP_LAMP_DEFAULT_X     20413U /* 0.3127 * 65279 */
#define APP_LAMP_DEFAULT_Y     21477U /* 0.3290 * 65279 */

/* ZCL CIE xy range is 0x0000-0xFEFF */
#define APP_LAMP_XY_MAX 65279U

typedef struct {
    uint32 u32Magic;
    uint8 u8OnOff;
    uint8 u8Level;
    uint16 u16X;
    uint16 u16Y;
} APP_tsLampStateRecord;

PRIVATE APP_tsLampState sState;

PRIVATE void APP_LAMP_vSaveState(void);
PRIVATE void APP_LAMP_vSendSnapshot(uint8 u8Command, bool_t bOnOff, uint8 u8Level, uint16 u16X, uint16 u16Y);
PRIVATE void APP_LAMP_vReadAttributes(bool_t *pbOnOff, uint8 *pu8Level, uint16 *pu16X, uint16 *pu16Y);
PRIVATE const char *APP_LAMP_pcCommandName(uint8 u8Command);
PRIVATE uint8 APP_LAMP_u8AppendText(const char *pcText, char *pcOut);
PRIVATE uint8 APP_LAMP_u8AppendDec(uint8 u8Value, char *pcOut);
PRIVATE uint8 APP_LAMP_u8AppendDec32(uint32 u32Value, char *pcOut);

/**
 * @brief Restores the lamp state from PDM (or defaults) and applies it to ZCL
 * @note  Must be called after the Lamp endpoint is registered with ZCL.
 */
PUBLIC void APP_LAMP_vInit(void)
{
    APP_tsLampStateRecord sRecord;
    uint16 u16RecordLength;
    uint16 u16BytesRead = 0;
    bool_t bRestored = FALSE;

    /* JN516x PDM reads the entire record without enforcing the buffer size.
     * Check the stored length before reading to prevent a buffer overflow. */
    if (PDM_bDoesDataExist(PDM_ID_APP_LAMP_STATE, &u16RecordLength) &&
        (u16RecordLength == sizeof(sRecord))) {
        if ((PDM_eReadDataFromRecord(PDM_ID_APP_LAMP_STATE, &sRecord, sizeof(sRecord), &u16BytesRead) ==
             PDM_E_STATUS_OK) &&
            (u16BytesRead == sizeof(sRecord)) &&
            (sRecord.u32Magic == APP_LAMP_STATE_MAGIC)) {
            sState.bOnOff = (sRecord.u8OnOff != 0U) ? TRUE : FALSE;
            sState.u8Level = sRecord.u8Level;
            sState.u16X = sRecord.u16X;
            sState.u16Y = sRecord.u16Y;
            bRestored = TRUE;
        }
    }

    if (!bRestored) {
        DBG_vPrintf(TRACE_LAMP, "Lamp: No valid state record, using defaults\n");
        sState.bOnOff = APP_LAMP_DEFAULT_ONOFF;
        sState.u8Level = APP_LAMP_DEFAULT_LEVEL;
        sState.u16X = APP_LAMP_DEFAULT_X;
        sState.u16Y = APP_LAMP_DEFAULT_Y;
        APP_LAMP_vSaveState();
    }

    APP_LAMP_vApplyToAttributes();
}

/**
 * @brief Resets the lamp state to defaults (used on factory reset)
 */
PUBLIC void APP_LAMP_vFactoryReset(void)
{
    sState.bOnOff = APP_LAMP_DEFAULT_ONOFF;
    sState.u8Level = APP_LAMP_DEFAULT_LEVEL;
    sState.u16X = APP_LAMP_DEFAULT_X;
    sState.u16Y = APP_LAMP_DEFAULT_Y;

    APP_LAMP_vSaveState();
    APP_LAMP_vApplyToAttributes();
}

/**
 * @brief Writes the RAM mirror state into the ZCL cluster attributes
 */
PUBLIC void APP_LAMP_vApplyToAttributes(void)
{
    sLumiRouter.sOnOffServerCluster.bOnOff = sState.bOnOff;
    sLumiRouter.sLevelControlServerCluster.u8CurrentLevel = sState.u8Level;
    sLumiRouter.sColourControlServerCluster.u16CurrentX = sState.u16X;
    sLumiRouter.sColourControlServerCluster.u16CurrentY = sState.u16Y;
}

/**
 * @brief Handles an On/Off cluster command from the Zigbee network
 * @note  Power state is derived from the command itself: with LevelControl
 *        present the stack defers the OnOff attribute (the transition
 *        engine sets it on completion), so the attribute lags here.
 *        OnOff commands never touch brightness: any level transition
 *        the stack started is cancelled and the level is restored.
 */
PUBLIC void APP_LAMP_vOnOffCommand(uint8 u8ZclCommandId)
{
    uint8 u8Command;

    switch (u8ZclCommandId) {
    case 0x00: /* E_CLD_ONOFF_CMD_OFF */
        sState.bOnOff = FALSE;
        u8Command = LAMP_CMD_OFF;
        break;

    case 0x01: /* E_CLD_ONOFF_CMD_ON */
        sState.bOnOff = TRUE;
        u8Command = LAMP_CMD_ON;
        break;

    default: /* E_CLD_ONOFF_CMD_TOGGLE and others: invert */
        sState.bOnOff = sState.bOnOff ? FALSE : TRUE;
        u8Command = LAMP_CMD_TOGGLE;
        break;
    }

    /* Force the attribute (the engine lags) and decouple brightness. */
    sLumiRouter.sOnOffServerCluster.bOnOff = sState.bOnOff;
    sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.eTransition =
        E_CLD_LEVELCONTROL_TRANSITION_NONE;
    sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.bWithOnOff = FALSE;
    sLumiRouter.sLevelControlServerCluster.u8CurrentLevel = sState.u8Level;

    DBG_vPrintf(TRACE_LAMP, "Lamp: OnOff command=%02x state=%d\n", u8ZclCommandId, sState.bOnOff);

    APP_LAMP_vSaveState();
    APP_LAMP_vSendSnapshot(u8Command, sState.bOnOff, sState.u8Level, sState.u16X, sState.u16Y);
}

/**
 * @brief Handles a Level Control cluster command from the Zigbee network
 * @param u8TargetLevel Level payload of MoveToLevel commands, ignored otherwise
 * @note  Level commands never touch OnOff, even the WithOnOff variants:
 *        brightness and power are fully independent here.
 */
PUBLIC void APP_LAMP_vLevelCommand(uint8 u8ZclCommandId, uint8 u8TargetLevel, bool_t bWithOnOff)
{
    bool_t bOnOff;
    uint8 u8Level;
    uint8 u8Command = LAMP_CMD_SET_LEVEL;

    (void)bWithOnOff;

    /* Discrete target commands carry their final level in the payload.
     * Continuous commands (Move/Step/Stop) mirror the live attribute value;
     * the stack transition engine streams progress via cluster updates. */
    switch (u8ZclCommandId) {
    case 0x00: /* E_CLD_LEVELCONTROL_CMD_MOVE_TO_LEVEL */
    case 0x04: /* E_CLD_LEVELCONTROL_CMD_MOVE_TO_LEVEL_WITH_ON_OFF */
        u8Level = (u8TargetLevel > APP_LAMP_LEVEL_MAX) ? APP_LAMP_LEVEL_MAX : u8TargetLevel;
        sState.u8Level = u8Level;

        /* Short-circuit the transition: a virtual lamp has no hardware
         * ramp, the host applies the target immediately. Neutralise the
         * stack engine too, otherwise it overwrites the attribute from
         * its own (possibly hours-long) ramp on the next ticks. */
        sLumiRouter.sLevelControlServerCluster.u8CurrentLevel = u8Level;
        sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.eTransition =
            E_CLD_LEVELCONTROL_TRANSITION_NONE;
        sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.bWithOnOff = FALSE;
        sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.iCurrentLevel =
            (int)u8Level * 100;
        sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.iTargetLevel =
            (int)u8Level * 100;
        sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.iPreviousLevel =
            (int)u8Level * 100;
        sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.u32Time = 0;
        sLumiRouter.sLevelControlServerCustomDataStructure.sTransition.iStepSize = 0;
        break;

    default:
        u8Command = LAMP_CMD_MOVE_STEP_STOP;
        APP_LAMP_vReadAttributes(&bOnOff, &u8Level, NULL, NULL);
        sState.u8Level = u8Level;
        break;
    }

    DBG_vPrintf(TRACE_LAMP, "Lamp: Level command=%02x level=%d\n", u8ZclCommandId, sState.u8Level);

    APP_LAMP_vSaveState();
    APP_LAMP_vSendSnapshot(u8Command, sState.bOnOff, sState.u8Level, sState.u16X, sState.u16Y);
}

/**
 * @brief Handles a MoveToColour command from the Zigbee network
 */
PUBLIC void APP_LAMP_vColourXyCommand(uint16 u16X, uint16 u16Y)
{
    sState.u16X = u16X;
    sState.u16Y = u16Y;

    /* Short-circuit the transition (see APP_LAMP_vLevelCommand) and
     * neutralise the colour engine, it also ramps from stale state. */
    sLumiRouter.sColourControlServerCluster.u16CurrentX = u16X;
    sLumiRouter.sColourControlServerCluster.u16CurrentY = u16Y;
    sLumiRouter.sColourControlServerCustomDataStructure.sTransition.eCommand =
        E_CLD_COLOURCONTROL_CMD_NONE;

    DBG_vPrintf(TRACE_LAMP, "Lamp: MoveToColour x=%u y=%u\n", u16X, u16Y);

    APP_LAMP_vSaveState();
    APP_LAMP_vSendSnapshot(LAMP_CMD_SET_COLOUR_XY, sState.bOnOff, sState.u8Level, u16X, u16Y);
}

/**
 * @brief Handles a MoveToHueAndSaturation command from the Zigbee network
 */
PUBLIC void APP_LAMP_vColourHsCommand(uint8 u8Hue, uint8 u8Saturation)
{
    uint8 u8R;
    uint8 u8G;
    uint8 u8B;

    APP_LAMP_vHsToRgb(u8Hue, u8Saturation, &u8R, &u8G, &u8B);
    APP_LAMP_vRgbToXy(u8R, u8G, u8B, &sState.u16X, &sState.u16Y);

    sLumiRouter.sColourControlServerCluster.u16CurrentX = sState.u16X;
    sLumiRouter.sColourControlServerCluster.u16CurrentY = sState.u16Y;
    sLumiRouter.sColourControlServerCustomDataStructure.sTransition.eCommand =
        E_CLD_COLOURCONTROL_CMD_NONE;

    DBG_vPrintf(TRACE_LAMP,
                "Lamp: MoveToHueSat hue=%u sat=%u -> x=%u y=%u\n",
                u8Hue,
                u8Saturation,
                sState.u16X,
                sState.u16Y);

    APP_LAMP_vSaveState();
    APP_LAMP_vSendSnapshot(LAMP_CMD_SET_COLOUR_HS, sState.bOnOff, sState.u8Level, sState.u16X, sState.u16Y);
}

/**
 * @brief Forwards live attribute values after a stack transition step
 */
PUBLIC void APP_LAMP_vTransitionUpdate(void)
{
    bool_t bOnOff;
    uint8 u8Level;
    uint16 u16X;
    uint16 u16Y;

    APP_LAMP_vReadAttributes(&bOnOff, &u8Level, &u16X, &u16Y);

    /* RAM mirror follows without wearing out the PDM record. */
    sState.bOnOff = bOnOff;
    sState.u8Level = u8Level;
    sState.u16X = u16X;
    sState.u16Y = u16Y;

    APP_LAMP_vSendSnapshot(LAMP_CMD_CLUSTER_UPDATE, bOnOff, u8Level, u16X, u16Y);
}

/**
 * @brief Forwards live attribute values after a Write Attributes request
 */
PUBLIC void APP_LAMP_vAttributeWritten(void)
{
    bool_t bOnOff;
    uint8 u8Level;
    uint16 u16X;
    uint16 u16Y;

    APP_LAMP_vReadAttributes(&bOnOff, &u8Level, &u16X, &u16Y);

    sState.bOnOff = bOnOff;
    sState.u8Level = u8Level;
    sState.u16X = u16X;
    sState.u16Y = u16Y;

    DBG_vPrintf(TRACE_LAMP, "Lamp: Attributes written on=%d level=%d x=%u y=%u\n", bOnOff, u8Level, u16X, u16Y);

    APP_LAMP_vSaveState();
    APP_LAMP_vSendSnapshot(LAMP_CMD_ATTRIBUTE_WRITE, bOnOff, u8Level, u16X, u16Y);
}

/**
 * @brief Sends a live-attribute snapshot to the host controller
 */
PUBLIC void APP_LAMP_vSendStateToHost(uint8 u8Command)
{
    bool_t bOnOff;
    uint8 u8Level;
    uint16 u16X;
    uint16 u16Y;

    APP_LAMP_vReadAttributes(&bOnOff, &u8Level, &u16X, &u16Y);
    APP_LAMP_vSendSnapshot(u8Command, bOnOff, u8Level, u16X, u16Y);
}

/**
 * @brief Applies an authoritative state line received from the host
 * @details Supplied fields are written into the ZCL attributes (colour
 * as a full r/g/b triple is converted to CIE XY); omitted fields keep
 * their values. The reporting engine delivers the change to the
 * Zigbee network.
 */
PUBLIC void APP_LAMP_vHandleHostJson(const APP_tsHostJson *psHost)
{
    if (psHost->bHasOnOff) {
        sState.bOnOff = psHost->bOnOff ? TRUE : FALSE;
    }

    if (psHost->bHasLevel) {
        sState.u8Level = (psHost->u8Level > APP_LAMP_LEVEL_MAX) ? APP_LAMP_LEVEL_MAX : psHost->u8Level;
    }

    if (psHost->bHasRgb) {
        APP_LAMP_vRgbToXy(psHost->u8R, psHost->u8G, psHost->u8B, &sState.u16X, &sState.u16Y);
    }

    DBG_vPrintf(TRACE_LAMP,
                "Lamp: Host json on=%d level=%d x=%u y=%u\n",
                sState.bOnOff,
                sState.u8Level,
                sState.u16X,
                sState.u16Y);

    APP_LAMP_vApplyToAttributes();
    APP_LAMP_vSaveState();

    /* Echo the applied state back so the host sees the result. */
    APP_LAMP_vSendSnapshot(LAMP_CMD_ATTRIBUTE_WRITE,
                           sState.bOnOff,
                           sState.u8Level,
                           sState.u16X,
                           sState.u16Y);
}

/**
 * @brief Converts CIE XY (0-65279) to sRGB (0-255) for the host
 */
PUBLIC void APP_LAMP_vXyToRgb(uint16 u16X, uint16 u16Y, uint8 *pu8R, uint8 *pu8G, uint8 *pu8B)
{
    float fX;
    float fY;
    float fXyzX;
    float fXyzY;
    float fXyzZ;
    float fR;
    float fG;
    float fB;

    fX = (float)u16X / (float)APP_LAMP_XY_MAX;
    fY = (float)u16Y / (float)APP_LAMP_XY_MAX;

    if (fY < 0.001f) {
        fX = 0.3127f;
        fY = 0.3290f;
    }

    /* Chromaticity to XYZ (Y = 1) */
    fXyzX = fX / fY;
    fXyzY = 1.0f;
    fXyzZ = (1.0f - fX - fY) / fY;

    /* XYZ to linear sRGB (D65) */
    fR = 3.2406f * fXyzX - 1.5372f * fXyzY - 0.4986f * fXyzZ;
    fG = -0.9689f * fXyzX + 1.8758f * fXyzY + 0.0415f * fXyzZ;
    fB = 0.0557f * fXyzX - 0.2040f * fXyzY + 1.0570f * fXyzZ;

    /* Gamma encode with clamping */
    if (fR < 0.0f) {
        fR = 0.0f;
    }
    if (fG < 0.0f) {
        fG = 0.0f;
    }
    if (fB < 0.0f) {
        fB = 0.0f;
    }

    fR = (fR <= 0.0031308f) ? (12.92f * fR) : (1.055f * powf(fR, 1.0f / 2.4f) - 0.055f);
    fG = (fG <= 0.0031308f) ? (12.92f * fG) : (1.055f * powf(fG, 1.0f / 2.4f) - 0.055f);
    fB = (fB <= 0.0031308f) ? (12.92f * fB) : (1.055f * powf(fB, 1.0f / 2.4f) - 0.055f);

    /* Clamp both sides: matrix rounding overshoots 1.0 on primaries,
     * and a bare (uint8) cast would wrap instead of saturating. */
    if (fR < 0.0f) {
        fR = 0.0f;
    }
    else if (fR > 1.0f) {
        fR = 1.0f;
    }
    if (fG < 0.0f) {
        fG = 0.0f;
    }
    else if (fG > 1.0f) {
        fG = 1.0f;
    }
    if (fB < 0.0f) {
        fB = 0.0f;
    }
    else if (fB > 1.0f) {
        fB = 1.0f;
    }

    *pu8R = (uint8)(fR * 255.0f);
    *pu8G = (uint8)(fG * 255.0f);
    *pu8B = (uint8)(fB * 255.0f);
}

/**
 * @brief Converts sRGB (0-255) from the host to CIE XY (0-65279)
 */
PUBLIC void APP_LAMP_vRgbToXy(uint8 u8R, uint8 u8G, uint8 u8B, uint16 *pu16X, uint16 *pu16Y)
{
    float fR;
    float fG;
    float fB;
    float fX;
    float fY;
    float fZ;
    float fSum;

    if ((u8R == 0U) && (u8G == 0U) && (u8B == 0U)) {
        /* Black carries no chromaticity; fall back to D65 white. */
        *pu16X = APP_LAMP_DEFAULT_X;
        *pu16Y = APP_LAMP_DEFAULT_Y;
        return;
    }

    fR = (float)u8R / 255.0f;
    fG = (float)u8G / 255.0f;
    fB = (float)u8B / 255.0f;

    /* Gamma decode (sRGB) */
    fR = (fR <= 0.04045f) ? (fR / 12.92f) : powf((fR + 0.055f) / 1.055f, 2.4f);
    fG = (fG <= 0.04045f) ? (fG / 12.92f) : powf((fG + 0.055f) / 1.055f, 2.4f);
    fB = (fB <= 0.04045f) ? (fB / 12.92f) : powf((fB + 0.055f) / 1.055f, 2.4f);

    /* Linear sRGB to XYZ (D65) */
    fX = 0.4124f * fR + 0.3576f * fG + 0.1805f * fB;
    fY = 0.2126f * fR + 0.7152f * fG + 0.0722f * fB;
    fZ = 0.0193f * fR + 0.1192f * fG + 0.9505f * fB;

    fSum = fX + fY + fZ;
    if (fSum <= 0.0f) {
        *pu16X = APP_LAMP_DEFAULT_X;
        *pu16Y = APP_LAMP_DEFAULT_Y;
        return;
    }

    *pu16X = (uint16)((fX / fSum) * (float)APP_LAMP_XY_MAX);
    *pu16Y = (uint16)((fY / fSum) * (float)APP_LAMP_XY_MAX);
}

/**
 * @brief Converts Hue/Saturation (ZCL 0-254 ranges) to sRGB (0-255)
 */
PUBLIC void APP_LAMP_vHsToRgb(uint8 u8Hue, uint8 u8Sat, uint8 *pu8R, uint8 *pu8G, uint8 *pu8B)
{
    float fH;
    float fS;
    float fC;
    float fX;
    float fM;
    float fR = 0.0f;
    float fG = 0.0f;
    float fB = 0.0f;
    uint8 u8Sector;

    fH = ((float)u8Hue * 360.0f) / 254.0f;
    fS = (float)u8Sat / 254.0f;

    if (fH >= 360.0f) {
        fH = 0.0f;
    }

    fC = fS; /* Value = 1 */
    u8Sector = (uint8)(fH / 60.0f);

    /* |((H/60) mod 2) - 1| without libm */
    fX = (fH / 60.0f) - ((float)((uint8)(fH / 60.0f) / 2U) * 2.0f) - 1.0f;
    if (fX < 0.0f) {
        fX = -fX;
    }
    fX = fC * (1.0f - fX);

    switch (u8Sector) {
    case 0:
        fR = fC;
        fG = fX;
        break;

    case 1:
        fR = fX;
        fG = fC;
        break;

    case 2:
        fG = fC;
        fB = fX;
        break;

    case 3:
        fG = fX;
        fB = fC;
        break;

    case 4:
        fR = fX;
        fB = fC;
        break;

    default:
        fR = fC;
        fB = fX;
        break;
    }

    fM = 1.0f - fC;
    *pu8R = (uint8)((fR + fM) * 255.0f);
    *pu8G = (uint8)((fG + fM) * 255.0f);
    *pu8B = (uint8)((fB + fM) * 255.0f);
}

/**
 * @brief Persists the RAM mirror state to PDM
 */
PRIVATE void APP_LAMP_vSaveState(void)
{
    APP_tsLampStateRecord sRecord;

    sRecord.u32Magic = APP_LAMP_STATE_MAGIC;
    sRecord.u8OnOff = (sState.bOnOff != FALSE) ? 1U : 0U;
    sRecord.u8Level = sState.u8Level;
    sRecord.u16X = sState.u16X;
    sRecord.u16Y = sState.u16Y;

    if (PDM_eSaveRecordData(PDM_ID_APP_LAMP_STATE, &sRecord, sizeof(sRecord)) != PDM_E_STATUS_OK) {
        DBG_vPrintf(TRACE_LAMP, "Lamp: Failed to save state\n");
    }
}

/**
 * @brief Builds a state JSON line and sends it to the host
 * @details Line format (no stdio on this target, formatted by hand):
 * {"cmd":"on","onoff":1,"level":254,"r":255,"g":255,"b":255}
 * Consecutive duplicates are dropped (the stack often emits several
 * identical updates per command); seq advances only on actual sends.
 * Only the state is compared, not the command name: an echo and a
 * transition update carrying the same values are the same news twice.
 */
PRIVATE void
APP_LAMP_vSendSnapshot(uint8 u8Command, bool_t bOnOff, uint8 u8Level, uint16 u16X, uint16 u16Y)
{
    uint8 u8R;
    uint8 u8G;
    uint8 u8B;
    char acLine[128];
    uint8 u8Length = 0U;

    static bool_t bLastOnOff = FALSE;
    static uint8 u8LastLevel = 0xFFU;
    static uint16 u16LastX = 0xFFFFU;
    static uint16 u16LastY = 0xFFFFU;

    if ((bOnOff == bLastOnOff) && (u8Level == u8LastLevel) &&
        (u16X == u16LastX) && (u16Y == u16LastY)) {
        return;
    }

    bLastOnOff = bOnOff;
    bLastOnOff = bOnOff;
    u8LastLevel = u8Level;
    u16LastX = u16X;
    u16LastY = u16Y;

    APP_LAMP_vXyToRgb(u16X, u16Y, &u8R, &u8G, &u8B);

    DBG_vPrintf(TRACE_LAMP,
                "Lamp: TX cmd=%s on=%d level=%d rgb=(%d,%d,%d)\n",
                APP_LAMP_pcCommandName(u8Command),
                (bOnOff != FALSE) ? 1 : 0,
                u8Level,
                u8R,
                u8G,
                u8B);

    u8Length += APP_LAMP_u8AppendText("{\"cmd\":\"", &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendText(APP_LAMP_pcCommandName(u8Command), &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendText("\",\"onoff\":", &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendDec((bOnOff != FALSE) ? 1U : 0U, &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendText(",\"level\":", &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendDec(u8Level, &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendText(",\"r\":", &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendDec(u8R, &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendText(",\"g\":", &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendDec(u8G, &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendText(",\"b\":", &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendDec(u8B, &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendText(",\"seq\":", &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendDec32(APP_u32NextSeq(), &acLine[u8Length]);
    u8Length += APP_LAMP_u8AppendText("}", &acLine[u8Length]);
    acLine[u8Length] = '\0';

    APP_vSendSerialLine(acLine);
}

/**
 * @brief Maps a lamp command code to its JSON name
 */
PRIVATE const char *APP_LAMP_pcCommandName(uint8 u8Command)
{
    switch (u8Command) {
    case LAMP_CMD_OFF:
        return "off";

    case LAMP_CMD_ON:
        return "on";

    case LAMP_CMD_TOGGLE:
        return "toggle";

    case LAMP_CMD_SET_LEVEL:
        return "level";

    case LAMP_CMD_SET_COLOUR_XY:
        return "color_xy";

    case LAMP_CMD_SET_COLOUR_HS:
        return "color_hs";

    case LAMP_CMD_MOVE_STEP_STOP:
        return "move";

    case LAMP_CMD_ATTRIBUTE_WRITE:
        return "write";

    default:
        return "update";
    }
}

/**
 * @brief Copies a string, returns its length
 */
PRIVATE uint8 APP_LAMP_u8AppendText(const char *pcText, char *pcOut)
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
PRIVATE uint8 APP_LAMP_u8AppendDec(uint8 u8Value, char *pcOut)
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
 * @brief Formats 0..4294967295 decimal, returns its length
 */
PRIVATE uint8 APP_LAMP_u8AppendDec32(uint32 u32Value, char *pcOut)
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

/**
 * @brief Reads the live ZCL attribute values of the lamp
 */
PRIVATE void APP_LAMP_vReadAttributes(bool_t *pbOnOff, uint8 *pu8Level, uint16 *pu16X, uint16 *pu16Y)
{
    if (pbOnOff != NULL) {
        *pbOnOff = sLumiRouter.sOnOffServerCluster.bOnOff ? TRUE : FALSE;
    }

    if (pu8Level != NULL) {
        *pu8Level = sLumiRouter.sLevelControlServerCluster.u8CurrentLevel;
    }

    if (pu16X != NULL) {
        *pu16X = sLumiRouter.sColourControlServerCluster.u16CurrentX;
    }

    if (pu16Y != NULL) {
        *pu16Y = sLumiRouter.sColourControlServerCluster.u16CurrentY;
    }
}
