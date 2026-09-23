/**
 * @file  app_lamp.h
 * @brief Virtual RGB lamp bridged to a host controller over UART (JSON)
 *
 * @details The lamp has no physical output (no GPIO/PWM). Its state
 * (on/off, level, CIE XY colour) lives in the ZCL attributes of the
 * Lamp endpoint. Every state change coming from the Zigbee network is
 * forwarded to the host controller over UART as a JSON line, and every
 * JSON state line received from the host is written into the ZCL
 * attributes so that the standard ZCL reporting engine delivers it to
 * the network. The host works purely with RGB; the module converts
 * RGB to CIE XY for the ZCL attributes and back.
 *
 * Module -> host (one line per event, LF terminated):
 *   {"cmd":"on","onoff":1,"level":254,"r":255,"g":255,"b":255}
 * cmd is one of: on, off, toggle, level, color_xy, color_hs, move,
 * write, update, resetting, erasing.
 *
 * Host -> module (fields optional, unknown keys ignored):
 *   {"onoff":0,"r":24,"g":32,"b":65}
 *   {"level":127}
 *   {"reset":1}
 *   {"erase_pdm":1}
 * Colour is applied only as a full r/g/b triple; a partial triple is
 * ignored. Omitted fields keep their current values.
 */

#ifndef APP_LAMP_H
#define APP_LAMP_H

#include <jendefs.h>

#include "app_serial_commands.h"

/* Lamp command codes (the "cmd" field of MODULE_STATE lines) */
enum {
    LAMP_CMD_OFF = 0x00,
    LAMP_CMD_ON = 0x01,
    LAMP_CMD_TOGGLE = 0x02,
    LAMP_CMD_SET_LEVEL = 0x03,
    LAMP_CMD_SET_COLOUR_XY = 0x04,
    LAMP_CMD_SET_COLOUR_HS = 0x05,
    LAMP_CMD_MOVE_STEP_STOP = 0x06,
    LAMP_CMD_ATTRIBUTE_WRITE = 0x07,
    LAMP_CMD_CLUSTER_UPDATE = 0x08
};

/* ZCL CurrentLevel range is 1..254 */
#define APP_LAMP_LEVEL_MAX 0xFEU

typedef struct {
    bool_t bOnOff;
    uint8 u8Level;
    uint16 u16X;
    uint16 u16Y;
} APP_tsLampState;

PUBLIC void APP_LAMP_vInit(void);
PUBLIC void APP_LAMP_vFactoryReset(void);
PUBLIC void APP_LAMP_vApplyToAttributes(void);

/* Outgoing path: Zigbee network -> host controller (JSON lines) */
PUBLIC void APP_LAMP_vSendStateToHost(uint8 u8Command);
PUBLIC void APP_LAMP_vOnOffCommand(uint8 u8ZclCommandId);
PUBLIC void APP_LAMP_vLevelCommand(uint8 u8ZclCommandId, uint8 u8TargetLevel, bool_t bWithOnOff);
PUBLIC void APP_LAMP_vColourXyCommand(uint16 u16X, uint16 u16Y);
PUBLIC void APP_LAMP_vColourHsCommand(uint8 u8Hue, uint8 u8Saturation);
PUBLIC void APP_LAMP_vTransitionUpdate(void);
PUBLIC void APP_LAMP_vAttributeWritten(void);

/* Incoming path: host controller -> Zigbee network (JSON lines) */
PUBLIC void APP_LAMP_vHandleHostJson(const APP_tsHostJson *psHost);

/* Colour conversions (host RGB <-> ZCL CIE XY) */
PUBLIC void APP_LAMP_vXyToRgb(uint16 u16X, uint16 u16Y, uint8 *pu8R, uint8 *pu8G, uint8 *pu8B);
PUBLIC void APP_LAMP_vRgbToXy(uint8 u8R, uint8 u8G, uint8 u8B, uint16 *pu16X, uint16 *pu16Y);
PUBLIC void APP_LAMP_vHsToRgb(uint8 u8Hue, uint8 u8Sat, uint8 *pu8R, uint8 *pu8G, uint8 *pu8B);

#endif /* APP_LAMP_H */
