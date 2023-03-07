## DFU File Transfer Protocol

App or 52840 works as BLE central.

52832 works as BLE peripheral.

52832 contains a BLE nus service.

#### 1. Start to do DFU

Central writes 0x00.

#### 2. Send DFU file size

Peripheral notifies 0x01

Central writes [0x01, 0xFF, 0xFF, 0xFF, 0xFF]. (The last 4 bytes are file size number in little endian)

#### 3. Send DFU file data

Peripheral notifies 0x02

Central writes [0x02, 0xFF, 0xFF, ... 0xFF]. (0xFF series are file content data, 128 bytes of 0xFF)

Central writes [0x02, 0xFF, 0xFF, ... 0xFF].

...

(repeat for 8 times, then central waits for peripheral's new notification)

(peripheral receives 8 x 128 = 1024 bytes of file data, and sends to 91 by UART, then notifies 0x02 again to receive next data block)