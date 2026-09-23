/**
 * @file  app_doorbell.h
 * @brief Virtual doorbell (play/mute + volume + melody) on EP3
 *
 * @details All objects are virtual (no GPIO). State lives in the ZCL
 * attributes of the Doorbell endpoint: OnOff (play/stop), LevelControl
 * (volume 0-254), Multistate Output (melody index). Zigbee commands and
 * host JSON lines both update the attributes; every change is forwarded
 * to the host as a JSON line and to the coordinator as an explicit
 * unicast report (no reporting configuration, no bindings).
 *
 * Module -> host:
 *   {"cmd":"ring","play":1,"volume":132,"melody":3}
 *   {"cmd":"mute","play":0,"volume":132,"melody":3}
 * cmd is one of: ring, mute, volume, melody, write, update.
 *
 * Host -> module (fields optional):
 *   {"play":1}
 *   {"volume":132}
 *   {"melody":3}
 */

#ifndef APP_DOORBELL_H
#define APP_DOORBELL_H

#include <jendefs.h>

#include "app_serial_commands.h"

/* Full uint16 melody range (PresentValue 0..0xFFFE, 0xFFFF reserved) */
#define APP_DOORBELL_NUMBER_OF_STATES 0xFFFFU
#define APP_DOORBELL_MELODY_MAX       0xFFFEU

/* ZCL CurrentLevel range is 1..254 */
#define APP_DOORBELL_VOLUME_MAX 0xFEU

typedef struct {
    bool_t bPlay;
    uint8 u8Volume;
    uint16 u16Melody;
} APP_tsDoorbellState;

PUBLIC void APP_DOORBELL_vInit(void);
PUBLIC void APP_DOORBELL_vFactoryReset(void);
PUBLIC void APP_DOORBELL_vApplyToAttributes(void);

/* Outgoing path: Zigbee network -> host controller */
PUBLIC void APP_DOORBELL_vPlayCommand(uint8 u8ZclCommandId);
PUBLIC void APP_DOORBELL_vVolumeCommand(uint8 u8TargetLevel, bool_t bWithOnOff);
PUBLIC void APP_DOORBELL_vVolumeUpdate(void);
PUBLIC void APP_DOORBELL_vAttributeWritten(void);

/* Incoming path: host controller -> Zigbee network */
PUBLIC void APP_DOORBELL_vHandleHostJson(const APP_tsHostJson *psHost);

#endif /* APP_DOORBELL_H */
