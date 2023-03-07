## Cross DFU Project (91 part)

This repo is the 91 part of cross DFU project.

A whole cross DFU demo contains:

- cross DFU (91 part)
- cross DFU (52 part)
- web server
- Android App

This repo contains nRF9160 firmware and web server project.

### Project `cross_dfu`

It can download DFU bin file and do DFU to itself, nRF52 DK and modem.

This project should co-work with `cross_dfu_52` project. Take a nRF9160 DK and nRF52832 DK and connect them with UART wires.

52 DK can do DFU to 91 or modem as well.

The DFU file is stored in bank 1 of the flash.

### How to assign DFU file

When booting up, it will try to connect to NB-IoT automatically.

It download DFU file from web server, and store in bank 1 of the flash.



The host name is assigned by variable `m_http_host` of main.c.

52 DFU file path is assigned by  variable `m_http_file_52` of main.c

91 DFU file path is assigned by  variable `m_http_file_91` of main.c

User can change it to own paths.

### LEDs and buttons

LED 1: indicate NB-IoT is connected

LED 2: indicate DFU file is downloaded or received

Switch 1 + switch 2 to left: download 52 DFU file

Switch 1 + switch 2 to right: download 91 DFU file

Button 1: start to download DFU file

Button 2: start to do DFU

### How to get DFU file

Build the project by command: `west build`, it will generate the DFU bin file at: `build\zephyr\app_update.bin`

### How to program 91 DK

Use command: 

```
west build --board nrf9160dk_nrf9160ns
west build
west flash
```

### Project `nrf91_server`

Deploy it to a remote server. 

User can upload a bin file to the server and get a public access URL for that file, and then download it by 91.

The usage of how to develop and deploy a web server project is out of this project.