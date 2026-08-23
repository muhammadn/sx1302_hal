	 / _____)             _              | |
	( (____  _____ ____ _| |_ _____  ____| |__
	 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
	 _____) ) ____| | | || |_| ____( (___| | | |
	(______/|_____)_|_|_| \__)_____)\____)_| |_|

MeshBeacon Uplink
==================

**MeshBeacon Uplink** is the gateway software for a Semtech Corecell SX1302
LoRa concentrator that acts as the **sink (PapaDuck / "SuperDuck")** for a
[ClusterDuck Protocol](https://github.com/Call-for-Code/ClusterDuckProtocol)
(CDP) mesh network. MamaDucks and DuckLinks in the field relay sensor and
message traffic over LoRa; this gateway receives every hop that reaches it,
de-duplicates and routes the packets, and bridges them out to an MQTT broker
for consumption by a hub / dashboard.

The project started as a fork of Semtech's reference SX1302 HAL and packet
forwarder, but the codebase has since moved away from that original
LoRaWAN-only design. The HAL is now the foundation for two independent
gateway daemons built on top of it:

* **`clusterduckd`** — the primary daemon. Implements the ClusterDuck
  Protocol PapaDuck role directly against the SX1302 HAL and forwards
  received mesh traffic to MQTT.
* **`meshbridge`** — an optional bridge that lets the same concentrator serve
  a [Meshtastic](https://meshtastic.org/) mesh instead of/alongside CDP.

The original Semtech `lora_pkt_fwd` LoRaWAN packet forwarder is still present
for anyone who needs a plain LoRaWAN Network Server uplink, but it is no
longer the primary use case of this repository.

## Contents

1. [Architecture](#1-architecture)
2. [Core library: libloragw](#2-core-library-libloragw)
3. [clusterduckd — the MeshBeacon Uplink gateway daemon](#3-clusterduckd--the-meshbeacon-uplink-gateway-daemon)
4. [meshbridge — Meshtastic mesh support](#4-meshbridge--meshtastic-mesh-support)
5. [Legacy: packet_forwarder (LoRaWAN)](#5-legacy-packet_forwarder-lorawan)
6. [Helper utilities](#6-helper-utilities)
7. [Hardware](#7-hardware)
8. [Build, install and run](#8-build-install-and-run)
9. [Continuous integration & releases](#9-continuous-integration--releases)
10. [Third party libraries](#10-third-party-libraries)
11. [Legal notice](#11-legal-notice)

## 1. Architecture

```
        MamaDuck            MamaDuck            DuckLink
       (field node)         (field node)        (field node)
             \                   |                   /
              \                  |                  /
               \_________________|_________________/
                                 LoRa
                                  |
                                  v
  +------------------------------------------------------------+
  |                    MeshBeacon Uplink gateway                |
  |                                                              |
  |   +--------------+   USB/SPI   +--------------------------+ |
  |   | SX1302        |<---------->| libloragw (HAL)          | |
  |   | Corecell      |            +-----------+--------------+ |
  |   | concentrator  |                        |                |
  |   +--------------+                         v                |
  |                              +--------------------------+    |
  |                              |  clusterduckd            |    |
  |                              |  - PapaDuck / CDP stack   |    |
  |                              |  - DuckRouter + bloom      |    |
  |                              |    filter de-dup          |    |
  |                              |  - ClusterDuckBridge       |    |
  |                              +-------------+--------------+    |
  |                                            | MQTT               |
  +--------------------------------------------|--------------------+
                                                v
                                    hub/event, hub/command,
                                     hub/response topics
                                                |
                                                v
                                        MQTT broker / dashboard
```

`meshbridge` (see [section 4](#4-meshbridge--meshtastic-mesh-support)) can run
against the same concentrator in place of, or alongside, `clusterduckd` when
the mesh is Meshtastic-based instead of CDP-based.

## 2. Core library: libloragw

Contains the sources of the library used to drive the Semtech LoRa SX1302
concentrator chip. Once compiled, the code is contained in `libloragw.a`,
which is statically linked into `clusterduckd`, `meshbridge`, the legacy
packet forwarder, and the helper utilities below.

The library also comes with basic test programs used to exercise its
sub-modules. Refer to the readme.md file located in the [libloragw](libloragw)
directory for more details.

## 3. clusterduckd — the MeshBeacon Uplink gateway daemon

`clusterduckd` (source in [clusterduck](clusterduck)) is a "SuperDuck": a
PapaDuck that talks to the SX1302 HAL directly instead of running on
Duck-native embedded hardware. It:

* Receives every LoRa packet reaching the concentrator and hands it to the
  CDP stack ([CDP.h](clusterduck/src/CDP.h), [Ducks](clusterduck/src/Ducks)).
* Routes and de-duplicates traffic using `DuckRouter` and a Bloom filter
  ([routing](clusterduck/src/routing)), same as any CDP node in the mesh.
* Recognizes the four Duck roles carried in each packet: `PAPA`, `MAMA`,
  `LINK` (DuckLink) and `DETECTOR` ([DuckTypes.h](clusterduck/src/Ducks/DuckTypes.h)).
* Bridges accepted uplinks to an MQTT broker, and forwards downlink commands
  from MQTT back out over LoRa, via the `ClusterDuckBridge` thread-safe C/C++
  bridge layer ([bridge](clusterduck/src/bridge)).

MQTT topics, message format and TLS/auth configuration are documented in
[clusterduck/MQTT_CONFIG.md](clusterduck/MQTT_CONFIG.md). By default the
gateway publishes received Duck traffic to `hub/event`, accepts downlink
commands on `hub/command`, and acknowledges them on `hub/response`.

Region/radio configuration for `clusterduckd` uses the same
`global_conf.json` format as the packet forwarder — see
`global_conf.json.clusterduck.AS923` for an example CDP configuration, plus
the per-region `global_conf.json.sx1250.*` / `global_conf.json.sx1255.*` /
`global_conf.json.sx1257.*` files for other supported bands.

## 4. meshbridge — Meshtastic mesh support

[meshtastic/meshbridge](meshtastic) is an alternative gateway daemon that
bridges the SX1302 concentrator to `meshtasticd` over a local IPC socket,
letting the same hardware serve a Meshtastic mesh instead of (or in addition
to) a ClusterDuck one. The current single-preset architecture and the
planned multi-preset design (running MediumFast/LongModerate/LongSlow
simultaneously from one concentrator) are documented in
[meshtastic/MULTI_PRESET_DESIGN.md](meshtastic/MULTI_PRESET_DESIGN.md).

## 5. Legacy: packet_forwarder (LoRaWAN)

[packet_forwarder](packet_forwarder) contains the original Semtech
`lora_pkt_fwd`, which forwards raw LoRaWAN uplinks from the concentrator to a
Network Server over UDP, and transmits downlinks scheduled by that server.
It is kept for anyone who needs a plain LoRaWAN uplink instead of, or
alongside, the ClusterDuck/Meshtastic gateways above. See
[packet_forwarder/readme.md](packet_forwarder/readme.md) and
[packet_forwarder/PROTOCOL.md](packet_forwarder/PROTOCOL.md) for details.

## 6. Helper utilities

These programs support development and bring-up of the concentrator; they
are unchanged from the original HAL project:

* **[util_net_downlink](util_net_downlink)** — a simple UDP downlink sender /
  uplink logger, useful for testing the packet forwarder without a full
  Network Server.
* **[util_chip_id](util_chip_id)** — reads back the SX1302 EUI, usable as a
  Gateway ID.
* **[util_boot](util_boot)** — switches a USB concentrator into DFU mode to
  reprogram its STM32 bridge MCU.
* **[util_spectral_scan](util_spectral_scan)** — scans the band using the
  auxiliary SX1261 radio on the Corecell reference design.
* **[tools/reset_lgw.sh](tools/reset_lgw.sh)** — performs GPIO reset/power-up
  of the SX1302 before any of the programs above access it. Must be located
  alongside the executable that uses it.

## 7. Hardware

MeshBeacon Uplink targets the **Semtech Corecell SX1302 LoRa concentrator**
reference design (SX1302 baseband chip paired with SX1250/SX125x radio
front-ends, and an optional SX1261 for spectral scan / Listen-Before-Talk).
The host communicates with the concentrator over **SPI or USB** — USB boards
carry an onboard STM32 MCU acting as a USB↔SPI bridge (firmware in
[mcu_bin](mcu_bin), flashed via `util_boot`).

Supported regions/radio combinations ship as `global_conf.json.*` files at
the root of [clusterduck](clusterduck) and [packet_forwarder](packet_forwarder):
EU868, US915, AS923, CN490 (including a full-duplex CN490 variant), on both
SPI and USB interfaces.

## 8. Build, install and run

All libraries and daemons are built from the root of the project.

### 8.1. Clean and compile everything

`make clean all`

This builds `libtools`, `libloragw`, `packet_forwarder`, `util_net_downlink`,
`util_chip_id`, `util_boot`, `util_spectral_scan` and `clusterduck`
(`clusterduckd`) in dependency order.

To build only the MeshBeacon Uplink daemon and its dependencies:

`make clusterduck`

### 8.2. Install executables on the gateway host

First edit the target.cfg file located in the root directory of the project
in order to configure where the executables have to be installed.

`TARGET_IP` : sets the IP address of the host of the gateway. In case the
project is compiled on the gateway host itself (Raspberry Pi...), this can
be left set to `localhost`.

`TARGET_DIR` : sets the directory on the gateway host file system in which
the executables must be copied. Note that the directory MUST exist when
invoking the install command.

`TARGET_USR` : sets the linux user to be used to perform the SSH/SCP command
for copying the executables.

In order to avoid entering the user password when installing the files, the
following steps have to be followed.

Lets say you want to copy between two hosts host_src and host_dest (they can
be the same). host_src is the host where you would run the scp command,
irrespective of the direction of the file copy!

* On host_src, run this command as the user that runs scp<br/>
`ssh-keygen -t rsa`

This will prompt for a passphrase. Just press the enter key. It'll then
generate an identification (private key) and a public key. Do not ever share
the private key with anyone! ssh-keygen shows where it saved the public key.
This is by default ~/.ssh/id_rsa.pub
* Transfer the id_rsa.pub file to host_dest<br/>
`ssh-copy-id -i ~/.ssh/id_rsa.pub user@host_dest`

You should be able to log on host_dest without being asked for a password.

Now that everything is set, the following command can be invoked:<br/>
`make install`

In order to also install the packet forwarder JSON configuration files:<br/>
`make install_conf`

### 8.3. Cross-compile from a PC

* Add the path to the binaries of the compiler corresponding to the target
platform to the `PATH` environment variable.
* set the `ARCH` environment variable to `arm`.
* set the `CROSS_COMPILE` environment variable to the prefix corresponding to
the compiler for the target platform.

An example for a Raspberry Pi target:

* `export PATH=[path]/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin`
* `export ARCH=arm`
* `export CROSS_COMPILE=arm-linux-gnueabihf-`

Then, from the same console where the previous environment variables have been
set, do:

`make clean all`

See [section 9](#9-continuous-integration--releases) for the arm64/mipsel
cross-compile invocations used in CI, which can be reproduced locally.

### 8.4. USB

This project provides support for both SPI or USB gateways. For USB interface,
the concentrator board has a STM32 MCU with which the linux host will
communicate to configure the sx1302 and the associated radios. The STM32 acts
as a USB <-> SPI bridge.

The STM32 MCU has to be programmed with the binary provided in the `mcu_bin`
directory of this project. For more details about how to flash it, please refer
to the `util_boot/readme.md` instructions.

Each daemon/utility of the project can be used using the `-u -d /dev/ttyACMx'
command line option, or with the proper configuration in the `global_conf.json`
file (`com_type` / `com_path`).

## 9. Continuous integration & releases

Pushing a `v*` tag triggers [.github/workflows/action.yaml](.github/workflows/action.yaml),
which cross-compiles `clusterduckd` for two deployment targets inside
`debian:bookworm` containers:

* **linux-arm64** (`aarch64-linux-gnu-`) — e.g. Raspberry Pi 64-bit hosts.
* **linux-mipsel-24kec** (`mipsel-linux-gnu-`, MIPS32r2/24KEc) — common
  router SoCs.

Both jobs install `libusb-1.0`, `libpaho-mqtt` and `libprotobuf` for the
target architecture, build with `make clusterduck`, and upload the resulting
`clusterduckd-<version>-<target>` binaries as release artifacts on a GitHub
Release created for the tag.

## 10. Third party libraries

In addition to the original HAL dependencies in the `libtools` directory
(`parson` JSON parser, `tinymt32` pseudo-random generator), `clusterduckd`
depends on:

* **Eclipse Paho MQTT C client** (`libpaho-mqtt`) — MQTT bridge to the hub.
* **Protocol Buffers** (`libprotobuf`) — Duck payload encoding
  ([duck_payloads.proto](clusterduck/src/payloads/duck_payloads.proto)).
* **libusb-1.0** — USB transport to Corecell USB boards.

## 11. Legal notice

MeshBeacon Uplink is built on top of Semtech's SX1302 CoreCell HAL and
packet forwarder reference design. The information presented in this
project documentation does not form part of any quotation or contract, is
believed to be accurate and reliable, and may be changed without notice. No
liability will be accepted by the publisher for any consequence of its use.
Publication does not convey nor imply any license under patent or other
industrial or intellectual property rights.

SEMTECH PRODUCTS ARE NOT DESIGNED, INTENDED, AUTHORIZED OR WARRANTED TO BE
SUITABLE FOR USE IN LIFE-SUPPORT APPLICATIONS, DEVICES OR SYSTEMS OR OTHER
CRITICAL APPLICATIONS. See [LICENSE.TXT](LICENSE.TXT) for full license terms.

*EOF*

<!-- Historical / superseded changelog below (Semtech SX1302 HAL, pre-fork) -->

## Appendix. Original Semtech HAL changelog

### v2.1.0 ###

> #### Updates

This release only targets USB Corecell (no change for SPI connexion type).
The USB-SPI bridge firmware, which runs on the STM32 MCU of the USB Corecell, has
been updated for API clean-up and robustness improvements.

> #### Changes

* MCU: USB-SPI bridge firmware binary v1.0.0.
	* Removed obsolete commands (ORDER_ID__REQ_SPI, ORDER_ID__ACK_SPI)
	* Command index shifted after obsolete commands removal (ORDER_ID__REQ_MULTIPLE_SPI, ORDER_ID__ACK_MULTIPLE_SPI)
	* Command parser sends ORDER_ID__UNKNOW_CMD in case of wrong command size.
	* Code clean-up (typo fixed, comments added...)
	* Implemented Error_Handler() function to reset the MCU in case of fatal error.
	* Fixed a potential roll-over issue in read_write_spi() function.
	* Increased delay tolerance for host feedback on USB transfers.
* HAL: Command interface updated for MCU firmware v1.0.0.
	* Removed obsolete commands from enum order_id_e
	* Shifted commands enum index according to USB-SPI bridge update.
	* Removed decode_ack_spi_access() unused function.
* HAL: Added timing debug information under DEBUG_MCU.

### v2.0.2 ###

> #### Updates

Fixed AGC firmware version check for sx1255/sx1257 based platforms (full-duplex
gateways...).

> #### Changes

* HAL: AGC firmware version for sx1255/sx1257 based gateways is v6.
* HAL: minor cosmetic changes & typo fixing.

### v2.0.1 ###

> #### Updates

The fine timestamping feature has been fully validated with this release.

> #### Changes

* HAL: Adjusted the freq_offset field of received packets, to take into account
the channel IF resolution error.
* HAL: Refined the fine timestamp offset compared to Gateway v2, by taking into
account the frequency offset of the received packet.
* HAL: Fixed the preamble length for FSK downlinks
* MCU: Removed the binary compiled in debug mode.
* util_spectral_scan: actually use the nb_scan input argument which was ignored.

### v2.0.0 ###

> #### New features

* Added support for USB interface between the HOST and the concentrator,
for sx1250 based concentrator only.
* Added support for Listen-Before-Talk for AS923 region, using the additional
sx1261 radio from the Semtech Corecell reference design v3.
* Added support for Spectral Scan with additional sx1261 radio from the Semtech
Corecell reference design v3.
* Added support for SX1303 chip, for further Fine Timestamping support.
* Merged the master-fdd-cn490 branch to bring support for CN490 Full-Duplex
reference design. It is an integration of the releases v1.1.0, v1.1.1, v1.1.2
described below.

> #### Changes

* HAL: Reworked the complete communication layer. A new loragw_com module has
been introduced to handle switching from a USB or a SPI communication interface,
aligned function prototypes for sx125x, sx1250 and sx1261 radios. For USB, a
mode has been added to group SPI write commands request to the STM32 MCU, in
order to optimize latency during time critical configuration phases.
* HAL: Added preliminary support for Fine Timestamping for TDOA localization.
* HAL: Updated AGC firmware to v6: add configurable delay for PA to start, add
Listen-Before-Talk support.
* HAL: Added new API function lgw_demod_setconf() to set global demodulator
settings.
* HAL: Added new API functions for Spectral Scan.
* Packet Forwarder: The type of interface is configurable in the
global_conf.json file: com_type can be "USB" or "SPI".
* Packet Forwarder: Changed the parameters to configure fine timestamping in the
global_conf.json.
* Packet Forwarder: Added sections to configure the spectral scan and
Listen-Before-Talk features.
* Packet Forwarder: Added a new thread for background spectral scan example,
to show how to use the spectral scan API provided by the HAL, without
interfering with the main tasks of the gateway (aka Receive uplinks and transmit
downlinks).
* Packet Forwarder: Added "nhdr" field parsing from "txpk" JSON downlink request
in order to be able to send beacon request from Network Server.
* Packet Forwarder: Added chan_multiSF_All in global_conf.json to choose which
spreading factors to enable for multi-sf demodulators.
* Packet Forwarder: Updated PROTOCOL.md to v1.6.
* Tools: added util_spectral_scan, a standalone spectral scanner utility.

> #### Notes

* This release has been validated on the Semtech Corecell reference design v3
with USB interface.

### v1.1.2 ###

> Integrated in ***v2.0.0*** from ***master-fdd-cn490*** branch.

* packet forwarder: updated global_conf.json.sx1255.CN490.full-duplex with RSSI
temperature compensation coefficients, and updated RSSI offset for radio 1.

### v1.1.1 ###

> Integrated in ***v2.0.0*** from ***master-fdd-cn490*** branch.

* HAL: Updated SX1302 LNA/PA LUT configuration for Full Duplex CN490 reference
design.
* test_loragw_hal_rx/tx: added --fdd option to enable Full Duplex
* packet forwarder: updated global_conf.json.sx1255.CN490.full-duplex for CN490
reference design.

### v1.1.0 ###

> Integrated in ***v2.0.0*** from ***master-fdd-cn490*** branch.

* HAL: Added support for CN490 full duplex reference design.

### v1.0.5 ###

* HAL: Fixed packet timestamp issue which was "jumping in time" in specific
conditions.
* HAL: Workaround hardware issue when reading 32-bits registers (timestamp, nb
bytes in RX buffer...)
* HAL: Fixed potential endless loop in sx1302_tx_abort() in SPI access fails.
* Packet Forwarder: Added global_conf.json.sx1250.US915 for US915 band
* test_hal_rx: added command line to specify RSSI offset to be applied

### v1.0.4 ###

* Added missing LICENSE.TXT file
* HAL & Packet Forwarder: added support for sx1250-based reference design for
CN490 region
* Packet Forwarder: disabled beaconing by default

### v1.0.3 ###

* HAL: Fixed scheduled downlink time precision by taking the tx start delay into
account.
* HAL: Fixed timestamp correction calculation for BW250 & BW500
* HAL: Fixed possible buffer overflow in lgw_receive() function
* HAL: Keep packet received in RX buffer when the buffer allocated to receive
the packets is too small. Remaining packets will be fetched on the next
lgw_receive calls (aligned on SX1301 behaviour).

### v1.0.2 ###

* Fixed compilation warnings reported by latest versions of GCC
* Reworked handling of temperature sensor
* Clean-up of unused files
* Added instructions and configuration files for packet forwarder auto-start
with systemd
* Added SX1250 radio calibration at startup

### v1.0.1 ###

* Packet Forwarder: Updated TX gain LUT in global_conf.json.sx1250 with proper
calibration

### v1.0.0 ###

* HAL: Initial official release for SX1302 CoreCell Reference Design.

### v0.0.1 ###

* HAL: Initial private release for TAP program

*(end of inherited Semtech HAL changelog — see [11. Legal notice](#11-legal-notice)
above for current license terms, and git tags/releases for MeshBeacon Uplink's
own changelog going forward)*
