/**
 * @file  app_serial_commands.c
 * @brief Serial Commands
 */

#include <jendefs.h>
#include <string.h>

/* Application */
#include "app_doorbell.h"
#include "app_lamp.h"
#include "app_main.h"
#include "app_sensor.h"
#include "app_serial_commands.h"
#include "app_uart.h"

/* SDK JN-SW-4170 */
#include "AppHardwareApi.h"
#include "PDM.h"
#include "ZQueue.h"
#include "bdb_api.h"
#include "dbg.h"
#include "zps_apl_zdo.h"

#ifndef TRACE_SERIAL
#define TRACE_SERIAL FALSE
#endif

#define SL_START_CHAR     0x01U
#define SL_ESC_CHAR       0x02U
#define SL_END_CHAR       0x03U
#define SL_HEADER_SIZE    4U
#define SL_FRAME_MIN_SIZE 5U
#define SL_FRAME_MAX_SIZE 16U
#define SL_RX_IDLE        0xFFU

#define SERIAL_RX_PROCESS_LIMIT 32U

#define JSON_LINE_MAX_SIZE 128U

/* Serial link message types */
enum {
    E_SC_MSG_RESET = 0x0011,
    E_SC_MSG_ERASE_PERSISTENT_DATA = 0x0012
};

PRIVATE void APP_vProcessRxChar(uint8 u8Char);
PRIVATE void APP_vProcessJsonChar(uint8 u8Char);
PRIVATE void APP_vProcessJsonLine(const char *pcLine);
PRIVATE bool_t APP_bGetJsonInt(const char *pcLine, const char *pcKey, uint16 *pu16Value);
PRIVATE void APP_vSendIeeeLine(void);
PRIVATE void APP_vProcessFrame(uint16 u16Type, const uint8 *pu8Payload, uint8 u8Length);
PRIVATE void APP_vProcessCommand(uint8 u8Command);

/**
 * @brief Process queued serial receive data
 * @details Bytes go to both parsers: 0x01-frames to the jntool
 * serial-link parser, '{...}' lines to the JSON parser.
 */
PUBLIC void APP_vProcessSerialRx(void)
{
    uint8 u8RxByte;
    uint8 u8BytesProcessed = 0U;

    while ((u8BytesProcessed < SERIAL_RX_PROCESS_LIMIT) &&
           ZQ_bQueueReceive(&APP_msgSerialRx, &u8RxByte)) {
        APP_vProcessRxChar(u8RxByte);
        APP_vProcessJsonChar(u8RxByte);
        u8BytesProcessed++;
    }
}

/**
 * @brief Send a serial message and wait for transmission to complete
 */
PUBLIC void APP_vSendSerialMessage(const char *pcMessage)
{
    DBG_vPrintf(TRACE_SERIAL, "Serial: TX message=\"%s\"\n", pcMessage);

    for (; *pcMessage != '\0'; pcMessage++) {
        UART_vWriteByte((uint8)*pcMessage);
    }

    UART_vWaitForTxComplete();
}

/**
 * @brief Send a text line (LF terminated) and wait for completion
 */
PUBLIC void APP_vSendSerialLine(const char *pcLine)
{
    DBG_vPrintf(TRACE_SERIAL, "Serial: TX line=\"%s\"\n", pcLine);

    for (; *pcLine != '\0'; pcLine++) {
        UART_vWriteByte((uint8)*pcLine);
    }

    UART_vWriteByte('\n');
    UART_vWaitForTxComplete();
}

/**
 * @brief Collect one byte of a '{...}' JSON line
 * @details A '{' byte restarts the line (resync). Control bytes that
 * cannot appear in a JSON line abort it. Overflow drops the line.
 */
PRIVATE void APP_vProcessJsonChar(uint8 u8Char)
{
    static char acLine[JSON_LINE_MAX_SIZE];
    static uint8 u8Length = 0U;

    if (u8Char == '{') {
        u8Length = 0U;
        acLine[u8Length++] = (char)u8Char;
        return;
    }

    if (u8Length == 0U) {
        return;
    }

    if ((u8Char == SL_START_CHAR) || (u8Char == SL_ESC_CHAR) || (u8Char == SL_END_CHAR)) {
        u8Length = 0U;
        return;
    }

    if (u8Char == '\n') {
        acLine[u8Length] = '\0';
        u8Length = 0U;
        APP_vProcessJsonLine(acLine);
        return;
    }

    if (u8Char == '\r') {
        return;
    }

    if (u8Length >= (JSON_LINE_MAX_SIZE - 1U)) {
        u8Length = 0U;
        return;
    }

    acLine[u8Length++] = (char)u8Char;
}

/**
 * @brief Dispatch a complete JSON line from the host
 * @details Recognised keys: onoff, level, r, g, b (lamp, EP1);
 * lux (sensor, EP2); play, volume, melody (doorbell, EP3);
 * reset, erase_pdm (actions). Device groups are independent and may
 * be combined in one line. Unknown keys and malformed lines are ignored.
 */
PRIVATE void APP_vProcessJsonLine(const char *pcLine)
{
    APP_tsHostJson sHost;
    uint16 u16Value;

    DBG_vPrintf(TRACE_SERIAL, "Serial: RX json=\"%s\"\n", pcLine);

    memset(&sHost, 0, sizeof(sHost));

    if (APP_bGetJsonInt(pcLine, "reset", &u16Value) && (u16Value != 0U)) {
        APP_vSendSerialLine("{\"cmd\":\"resetting\"}");
        vAHI_SwReset();
        return;
    }

    if (APP_bGetJsonInt(pcLine, "erase_pdm", &u16Value) && (u16Value != 0U)) {
        APP_vSendSerialLine("{\"cmd\":\"erasing\"}");
        PDM_vDeleteAllDataRecords();
        vAHI_SwReset();
        return;
    }

    if (APP_bGetJsonInt(pcLine, "getieee", &u16Value) && (u16Value != 0U)) {
        APP_vSendIeeeLine();
    }

    if (APP_bGetJsonInt(pcLine, "onoff", &u16Value)) {
        sHost.bHasOnOff = TRUE;
        sHost.bOnOff = (u16Value != 0U) ? TRUE : FALSE;
    }

    if (APP_bGetJsonInt(pcLine, "level", &u16Value)) {
        sHost.bHasLevel = TRUE;
        sHost.u8Level = (u16Value > 255U) ? 255U : (uint8)u16Value;
    }

    {
        uint16 u16R;
        uint16 u16G;
        uint16 u16B;

        /* Colour applies only as a full triple. */
        if (APP_bGetJsonInt(pcLine, "\"r\"", &u16R) && APP_bGetJsonInt(pcLine, "\"g\"", &u16G) &&
            APP_bGetJsonInt(pcLine, "\"b\"", &u16B)) {
            sHost.bHasRgb = TRUE;
            sHost.u8R = (u16R > 255U) ? 255U : (uint8)u16R;
            sHost.u8G = (u16G > 255U) ? 255U : (uint8)u16G;
            sHost.u8B = (u16B > 255U) ? 255U : (uint8)u16B;
        }
    }

    if (APP_bGetJsonInt(pcLine, "lux", &u16Value)) {
        sHost.bHasLux = TRUE;
        sHost.u16Lux = u16Value;
    }

    if (APP_bGetJsonInt(pcLine, "play", &u16Value)) {
        sHost.bHasPlay = TRUE;
        sHost.bPlay = (u16Value != 0U) ? TRUE : FALSE;
    }

    if (APP_bGetJsonInt(pcLine, "volume", &u16Value)) {
        sHost.bHasVolume = TRUE;
        sHost.u8Volume = (u16Value > 255U) ? 255U : (uint8)u16Value;
    }

    if (APP_bGetJsonInt(pcLine, "melody", &u16Value)) {
        sHost.bHasMelody = TRUE;
        sHost.u16Melody = u16Value;
    }

    if (sHost.bHasOnOff || sHost.bHasLevel || sHost.bHasRgb) {
        APP_LAMP_vHandleHostJson(&sHost);
    }

    if (sHost.bHasLux) {
        APP_SENSOR_vHandleLux(sHost.u16Lux);
    }

    if (sHost.bHasPlay || sHost.bHasVolume || sHost.bHasMelody) {
        APP_DOORBELL_vHandleHostJson(&sHost);
    }
}

/**
 * @brief Replies with the module IEEE address and network state
 * @details Line format:
 * {"cmd":"ieee","ieee":"00158D00030B20CD","joined":1}
 */
PRIVATE void APP_vSendIeeeLine(void)
{
    uint64 u64Ieee = ZPS_u64AplZdoGetIeeeAddr();
    char acLine[64];
    uint8 u8Length = 0U;
    uint8 u8Shift;
    static const char acPrefix[] = "{\"cmd\":\"ieee\",\"ieee\":\"";
    static const char acMiddle[] = "\",\"joined\":";
    const char *pcText;
    uint8 u8Nibble;

    for (pcText = acPrefix; *pcText != '\0'; pcText++) {
        acLine[u8Length++] = *pcText;
    }

    for (u8Shift = 16U; u8Shift > 0U; u8Shift--) {
        u8Nibble = (uint8)((u64Ieee >> ((u8Shift - 1U) * 4U)) & 0xFU);
        acLine[u8Length++] = (char)((u8Nibble < 10U) ? ('0' + u8Nibble) : ('A' + u8Nibble - 10U));
    }

    for (pcText = acMiddle; *pcText != '\0'; pcText++) {
        acLine[u8Length++] = *pcText;
    }

    acLine[u8Length++] = (sBDB.sAttrib.bbdbNodeIsOnANetwork != FALSE) ? '1' : '0';
    acLine[u8Length++] = '}';
    acLine[u8Length] = '\0';

    DBG_vPrintf(TRACE_SERIAL, "Serial: TX line=\"%s\"\n", acLine);

    APP_vSendSerialLine(acLine);
}

/**
 * @brief Extract an unsigned integer value for a JSON key
 * @param pcKey Key with quotes, e.g. "\"level\""
 * @return TRUE when the key with a valid value was found
 */
PRIVATE bool_t APP_bGetJsonInt(const char *pcLine, const char *pcKey, uint16 *pu16Value)
{
    const char *pcValue;
    uint16 u16Result = 0U;
    bool_t bFound = FALSE;
    char acQuoted[16];
    uint8 i = 0U;

    /* Plain keys ("reset") also match their quoted form. */
    if (pcKey[0] != '"') {
        acQuoted[i++] = '"';
        while ((i < (sizeof(acQuoted) - 2U)) && (pcKey[i - 1U] != '\0')) {
            acQuoted[i] = pcKey[i - 1U];
            i++;
        }
        acQuoted[i++] = '"';
        acQuoted[i] = '\0';
        pcKey = acQuoted;
    }

    pcValue = strstr(pcLine, pcKey);
    if (pcValue == NULL) {
        return FALSE;
    }

    pcValue = strchr(pcValue, ':');
    if (pcValue == NULL) {
        return FALSE;
    }
    pcValue++;

    while ((*pcValue == ' ') || (*pcValue == '\t')) {
        pcValue++;
    }

    while ((*pcValue >= '0') && (*pcValue <= '9')) {
        bFound = TRUE;
        u16Result = (uint16)(u16Result * 10U + (uint16)(*pcValue - '0'));
        pcValue++;
    }

    *pu16Value = u16Result;
    return bFound;
}

/**
 * @brief Process a byte from a serial-link frame.
 * @details Wire frame:
 * START | escaped(type[2], length[2], checksum, payload[0..11]) | END.
 * Bytes below 0x10 are escaped as ESC followed by the byte XOR 0x10.
 * Valid frames are dispatched by message type.
 */
PRIVATE void APP_vProcessRxChar(uint8 u8Char)
{
    static uint8 au8Frame[SL_FRAME_MAX_SIZE];
    static uint8 u8FrameLength = SL_RX_IDLE;
    static uint8 u8Checksum;
    static bool_t bEscaped = FALSE;

    if (u8Char == SL_START_CHAR) {
        u8FrameLength = 0U;
        u8Checksum = 0U;
        bEscaped = FALSE;
        return;
    }

    if (u8FrameLength == SL_RX_IDLE) {
        return;
    }

    if (u8Char == SL_END_CHAR) {
        if (!bEscaped &&
            (u8FrameLength >= SL_FRAME_MIN_SIZE) &&
            (u8Checksum == 0U) &&
            (au8Frame[0] == 0U) &&
            (au8Frame[2] == 0U) &&
            (au8Frame[3] == (u8FrameLength - SL_FRAME_MIN_SIZE))) {
            APP_vProcessFrame((((uint16)au8Frame[0]) << 8) | au8Frame[1],
                              &au8Frame[SL_FRAME_MIN_SIZE],
                              (uint8)(u8FrameLength - SL_FRAME_MIN_SIZE));
        }

        u8FrameLength = SL_RX_IDLE;
        bEscaped = FALSE;
        return;
    }

    if (u8Char == SL_ESC_CHAR) {
        bEscaped = TRUE;
        return;
    }

    if (bEscaped) {
        u8Char ^= 0x10U;
        bEscaped = FALSE;
    }

    if (u8FrameLength >= SL_FRAME_MAX_SIZE) {
        u8FrameLength = SL_RX_IDLE;
        bEscaped = FALSE;
        return;
    }

    au8Frame[u8FrameLength] = u8Char;

    /* A valid frame XOR is zero. */
    u8Checksum ^= u8Char;
    u8FrameLength++;
}

/**
 * @brief Dispatch a decoded serial-link frame by message type.
 */
PRIVATE void APP_vProcessFrame(uint16 u16Type, const uint8 *pu8Payload, uint8 u8Length)
{
    DBG_vPrintf(TRACE_SERIAL, "Serial: RX frame type=%04x length=%u\n", u16Type, u8Length);

    (void)pu8Payload;
    (void)u8Length;

    switch (u16Type) {
    case E_SC_MSG_RESET:
    case E_SC_MSG_ERASE_PERSISTENT_DATA:
        /* jntool commands carry no payload. */
        APP_vProcessCommand((uint8)u16Type);
        break;

    default:
        APP_vSendSerialMessage("Unknown command.");
        break;
    }
}

/**
 * @brief Process a decoded jntool command.
 */
PRIVATE void APP_vProcessCommand(uint8 u8Command)
{
    DBG_vPrintf(TRACE_SERIAL, "Serial: RX command=%02x\n", u8Command);

    switch (u8Command) {
    case E_SC_MSG_RESET:
        APP_vSendSerialMessage("Reset...........");
        vAHI_SwReset();
        break;

    case E_SC_MSG_ERASE_PERSISTENT_DATA:
        APP_vSendSerialMessage("Erase PDM.......");
        APP_vSendSerialMessage("Reset...........");
        PDM_vDeleteAllDataRecords();
        vAHI_SwReset();
        break;

    default:
        APP_vSendSerialMessage("Unknown command.");
        break;
    }
}
