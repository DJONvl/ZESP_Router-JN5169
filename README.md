# Lumi Router

This firmware replaces the original firmware for the __Zigbee__ JN5169 chip in __Xiaomi DGNWG05LM__ and __Aqara ZHWG11LM__ gateways. It allows the gateway to operate as a Zigbee router in any Zigbee network instead of using the stock coordinator firmware for the proprietary Xiaomi Mi Home network.

---

These instructions assume that alternative __OpenWrt__ firmware is already installed on the gateway. If it is not, follow the guide at [https://openlumi.github.io](https://openlumi.github.io).

## Firmware

**Web interface**

1. Go to `LuCI -> System -> Zigbee Tools`
2. Click the `Upload Firmware…` button.
3. Select the firmware file to upload.
4. Click the `Upload` button.

**Command line**

1. Connect to the device via SSH.
2. Run the following commands:

```shell
wget https://github.com/igorlistopad/Lumi-Router-JN5169/releases/latest/download/LumiRouter.bin -P /tmp
jnflash /tmp/LumiRouter.bin
```

## Reset and pairing

Erase the PDM data to reset the device and start joining a new Zigbee network.

**Web interface**

Go to `LuCI -> System -> Zigbee Tools` and click the `Erase PDM` button.

**Command line**

```shell
jntool erase_pdm
```

## Restart

**Web interface**

Go to `LuCI -> System -> Zigbee Tools` and click the `Soft reset` button.

**Command line**

```shell
jntool soft_reset
```

## Building firmware

Use GitHub Codespaces or VS Code Dev Containers for a preconfigured environment,
or follow the local build instructions below.

[![Open in GitHub Codespaces](https://img.shields.io/static/v1?style=for-the-badge&label=GitHub+Codespaces&message=Open&color=lightgrey&logo=github)](https://codespaces.new/igorlistopad/Lumi-Router-JN5169)
[![Open in Dev Container](https://img.shields.io/static/v1?style=for-the-badge&label=Dev%20Containers&message=Open&color=blue)](https://vscode.dev/redirect?url=vscode://ms-vscode-remote.remote-containers/cloneInVolume?url=https://github.com/igorlistopad/Lumi-Router-JN5169)

### Local setup

Supported platforms:

- macOS: AMD64, ARM64
- Linux: AMD64, ARM64
- Windows: AMD64 (MSYS2)

Prerequisites:

- Git, make, and curl
- Python 3.5 or later

Clone the repository and install the SDK and toolchain:

```shell
git clone --recurse-submodules https://github.com/igorlistopad/Lumi-Router-JN5169.git
cd Lumi-Router-JN5169
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
