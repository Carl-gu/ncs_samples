## Cross DFU Project (52 part)

This repo is the 52 part of cross DFU project.

A whole cross DFU contains:

- cross DFU (91 part)
- cross DFU (52 part)
- web server
- Android App

This repo contains three firmware projects and one Android App

- cross_dfu_52: BLE peripheral of cross DFU demo
- cross_dfu_52_c: BLE central of cross DFU demo
- serial_bootloader: secure serial DFU bootloader
- Android-nRF-Toolbox: modified Android App, named 'My Toolbox'

All firmware projects work on SDK v16.0.

### Project `cross_dfu_52`

Path: `sdk16.0\_project\cross_dfu_52`

DK: `52832 DK/PCA10040`

IDE: SES

It implements a BLE NUS peripheral, and connects to nRF91 DK with UART.

UART  X: P0.12, UART RX: P0.11. These pins are defined in pca10040.h.

It receives DFU data content from central by BLE, and sends to nRF91 by UART.

Advertising device name is: Cross_DFU_52.

LED 1 blinking means in advertising. After connected, LED 1 is keep on.

To port it to your own project, please compare the project with the original ble_app_uart project. You can see what are added or changed.

### Project `cross_dfu_52_c`

Path: `sdk16.0\_project\cross_dfu_52_c`

DK: `52840 DK/PCA10056`

IDE: SES

It implements a BLE NUS central. 

It will connect to a device named `Cross_DFU_52` automatically.

In order to test cross DFU demo, please copy a DFU bin file to the flash of 52840. It will read the bin file content and send to peripheral.

After connection is ready, press button 1 of 52840 DK will start to transfer DFU file.

To port it to your own project, please compare the project with the original ble_app_uart_c project. You can see what are added or changed.

### Project `serial_bootloader`

Path: `sdk16.0\_project\serial_bootloader`

DK: `52832 DK/PCA10040`

IDE: SES

It's the original bootloader project from SDK 16.0, except for using the new UART pins as `cross_dfu_52`.

### Keys 

Private key: `sdk16.0\_project\private_key.pem`

Public key: `sdk16.0\_project\serial_bootloader\secure_bootloader\dfu_public_key.c`

Please replace them with your key files.

You can get these key files by command line tool: `nrfutil`.

### App `Android-nRF-Toolbox`

Path: `app\Android-nRF-Toolbox`

HW: only test for one Huawei phone and one Redme phone

IDE: Android Studio 

Provide a pre-built APK for easy use: `app\My Toolbox.apk`

This App implements the same function as `cross_dfu_52_c`. Use can select a DFU bin file and send to peripheral with this App.

In order to port the sending file function to your own App, please clone the original nRF Toolbox repo, and compare two repos. There are a few of changes in the source code.

### How to program 52832

Program softdevice, application, bootloader and bootloader settings like the normal DFU procedure.

Or use script:

```
cd script
python prog_sd_app_bl.py
```

### How to make DFU bin file

Use script:

```
cd script
python make_dfu_zip.py
python make_dfu_bin.py
```

The dfu_bin.bin file will be generated and stored at: `script\out_files`

### How to program 52840

Use SES to program 52840. 

Bootloader is not required.

### How to copy DFU bin file to 52840

1. Run cross_dfu_52_c and check the RTT log. It prints the start address of bank 1:

```
00> <info> app: BLE DFU central started.
00> <info> app: scan_start
00> <info> app: Bank 1 start addr: 00038000
00> <info> app: Copy DFU bin file to an address bigger than it.
```

2. Select an address where bin file is stored. 
   In current firmware, it uses `0x40000`, if you want to use other values, please change `DFU_FILE_ADDR_DEFAULT` of the firmware as well.
3. Use script to copy the file to flash

```
python copy_dfu_file_to_central.py dfu-bin.bin 0x40000
```

4. Reset the 52840 DK.

### How DFU file is transferred from central to peripheral

1. C(central) tells P(peripheral) start to transfer file
   C writes a 0x00
2. P asks for file size from C
   P notifies a 0x01
   C writes a packet [0x01, 0xff, 0xff, 0xff, 0xff] (The last 4 bytes are file size in little endian)
3. P asks for file data from C
   P notifies a 0x02
   C writes a packet [0x02, 0xff, 0xff, ..., 0xff] (packet length is 129, the first byte is tag(0x02))
   C write a packet [0x02, 0xff, 0xff, ..., 0xff]
   ...
   (repeat for 8 times)
4. P receive 8 packets from C, total 128 x 8 = 1024 bytes, send it to nRF91 by UART, after getting response from nRF91, P asks for following file data from C in the same way.