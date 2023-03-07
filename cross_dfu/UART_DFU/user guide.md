## User Guide

This is a file package for customers to test cross DFU project.

### File Introduction

- `91_cross_dfu.zip`

  It contains nRF9160 project code and web server code.

  Read its `Readme.md` file first.

  To use it, `nrf connect sdk(ncs)` is required. Read this document [link](http://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/index.html) to install ncs.

  Put this project to `ncs\nrf\samples\nrf9160\cross_dfu`.

  Build: `west build -b nrf9160dk_nrf9160ns`

  Flash: `west flash`

- `52_cross_dfu.zip`

  It contains nRF52832 project(peripheral) and nRF52840 project(central), and a Android App(My_Toolbox.apk).

  Read its `Readme.md` file first.

  It contains a complete SDK 16.0, put it to any place.

  Use Segger Embedded Studio to build projects.

- `cross_dfu.pptx`

  It introduces the design idea and how to use the project.





