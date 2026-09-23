# ZESP Router

Firmware for the __Zigbee__ JN5169 chip in __Xiaomi DGNWG05LM__ and __Aqara ZHWG11LM__ gateways.
Based on [Lumi Router](https://github.com/igorlistopad/Lumi-Router-JN5169):
the gateway works as a Zigbee router in any Zigbee network instead of using
the stock coordinator firmware for the proprietary Xiaomi Mi Home network.

On top of routing, this firmware exposes three __virtual devices__ (no GPIO,
everything is bridged to the host controller over UART as JSON lines):

| EP | Device | Clusters |
|---|---|---|
| 1 | Extended Colour Light (virtual RGB lamp) + router | Basic, Identify, OnOff, LevelControl, ColourControl |
| 2 | Light Sensor (virtual) | Illuminance Measurement |
| 3 | Combined Interface (virtual doorbell) | OnOff (play/stop), LevelControl (volume), Multistate Output (melody) |

Full UART protocol reference: [LAMP_PROTOCOL.md](LAMP_PROTOCOL.md).

Quick peek at the exchange (`UART0`, `115200 8N1`, LF-terminated lines):

```json
{"cmd":"on","onoff":1,"level":254,"r":255,"g":255,"b":255}
{"cmd":"ring","play":1,"volume":132,"melody":3}
```

```json
{"onoff":1,"level":254,"r":255,"g":0,"b":0}
{"lux":12500}
{"play":1}
{"volume":132}
{"melody":3}
{"getieee":1}
```

The device identifies as manufacturer `VLK_SW`, model `ZESP_Router`
(hardware version `2` on DGNWG05LM, `3` on ZHWG11LM).

---

These instructions assume that alternative __OpenWrt__ firmware is already installed on the gateway. If it is not, follow the guide at [https://openlumi.github.io](https://openlumi.github.io).

## Firmware

Take `LumiRouter-<BOARD>.bin` from the [Releases page](../../releases) or
from a successful [CI run](../../actions) (artifacts, both boards).

**Web interface**

1. Go to `LuCI -> System -> Zigbee Tools`
2. Click the `Upload Firmware…` button.
3. Select the firmware file to upload.
4. Click the `Upload` button.

**Command line**

1. Connect to the device via SSH.
2. Copy the firmware over and flash it:

```shell
scp LumiRouter-DGNWG05LM.bin root@192.168.1.1:/tmp/LumiRouter.bin
ssh root@192.168.1.1 jnflash /tmp/LumiRouter.bin
```

(`scp` is needed because the gateway has no SFTP server; adjust the address.)

## Reset and pairing

Erase the PDM data to reset the device and start joining a new Zigbee network.

**Web interface**

Go to `LuCI -> System -> Zigbee Tools` and click the `Erase PDM` button.

**Command line (either works)**

```shell
jntool erase_pdm
echo '{"erase_pdm":1}' > /dev/ttymxc1
```

## Restart

**Web interface**

Go to `LuCI -> System -> Zigbee Tools` and click the `Soft reset` button.

**Command line (either works)**

```shell
jntool soft_reset
echo '{"reset":1}' > /dev/ttymxc1
```

## Identify the module

Ask the module for its IEEE address and network state:

```shell
echo '{"getieee":1}' > /dev/ttymxc1
# {"cmd":"ieee","ieee":"00158D00030B20CD","joined":1}
```

## Building firmware

Use GitHub Codespaces or VS Code Dev Containers for a preconfigured environment,
or follow the local build instructions below. Every push to `main` is also
built by CI for both boards.

[![Open in GitHub Codespaces](https://img.shields.io/static/v1?style=for-the-badge&label=GitHub+Codespaces&message=Open&color=lightgrey&logo=github)](https://codespaces.new/DJONvl/ZESP_Router-JN5169)
[![Open in Dev Container](https://img.shields.io/static/v1?style=for-the-badge&label=Dev%20Containers&message=Open&color=blue)](https://vscode.dev/redirect?url=vscode://ms-vscode-remote.remote-containers/cloneInVolume?url=https://github.com/DJONvl/ZESP_Router-JN5169)

### Local setup

Supported platforms:

- macOS: AMD64, ARM64
- Linux: AMD64, ARM64
- Windows: AMD64 (MSYS2)

Prerequisites:

- Git, make, and curl
- Python 3.5 or later

Clone the repository and install the SDK and toolchain.
Everything is mirrored, no third-party downloads required:

```shell
git clone --recurse-submodules https://github.com/DJONvl/ZESP_Router-JN5169.git
cd ZESP_Router-JN5169
make install
```

### Build

Build the firmware with `BOARD=DGNWG05LM` for Xiaomi or
`BOARD=ZHWG11LM` for Aqara:

```shell
make BOARD=DGNWG05LM
```

The firmware is generated as `build/LumiRouter-<BOARD>.bin`.

Run `make clean` before switching boards. This also deletes previously
generated firmware files.
