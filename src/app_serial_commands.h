/**
 * @file  app_serial_commands.h
 * @brief Serial Commands
 */

#ifndef APP_SERIAL_COMMANDS_H
#define APP_SERIAL_COMMANDS_H

#include <jendefs.h>

/* Parsed host->module JSON line; bHas* flags mark supplied fields */
typedef struct {
    /* Lamp (EP1) */
    bool_t bHasOnOff;
    bool_t bOnOff;
    bool_t bHasLevel;
    uint8 u8Level;
    bool_t bHasRgb;
    uint8 u8R;
    uint8 u8G;
    uint8 u8B;
    bool_t bHasCt;
    uint16 u16Ct;

    /* Sensor (EP2) */
    bool_t bHasLux;
    uint16 u16Lux;

    /* Doorbell (EP3) */
    bool_t bHasPlay;
    bool_t bPlay;
    bool_t bHasVolume;
    uint8 u8Volume;
    bool_t bHasMelody;
    uint16 u16Melody;

    /* Global actions */
    bool_t bReset;
    bool_t bErasePdm;
} APP_tsHostJson;

PUBLIC void APP_vProcessSerialRx(void);
PUBLIC void APP_vSendSerialMessage(const char *pcMessage);
PUBLIC void APP_vSendSerialLine(const char *pcLine);

/* Monotonic sequence number for module->host lines (wraps at 2^32) */
PUBLIC uint32 APP_u32NextSeq(void);

#endif /* APP_SERIAL_COMMANDS_H */
