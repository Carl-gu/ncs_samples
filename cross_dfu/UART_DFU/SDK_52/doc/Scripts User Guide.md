## User Guide

### What is copy_dfu_file_to_central.py

This script is used to covert dfu bin file to a hex format, and program it to the flash of 52840.

In order help firmware to know the bin file size, it will also write file size info to the certain place.

Usage:

```
python copy_dfu_file_to_central.py <bin-file> <hex-address-offset>

@Param
bin-file: DFU bin file path
hex-address-offset: Address of the hex file

@example
python copy_dfu_file_to_central.py dfu-bin.bin 0x40000
```

When cross_dfu_52_c is programed to a 52840 DK, it will print a log of `bank 1 start address`, hex-address-offset must be larger than or equal to it.

For example, bank 1 start address is 0x40000, hex-address-offset must be 0x40000 or bigger value.

hex-address-offset should be flash page aligned(4096 byte aligned).

Suppose hex-address-offset is 0x40000, this script will write two words at 0x40000:

```
magic number[4]: 0x49535951
file size[4]: len(dfu_bin_91.bin)
```

cross_dfu_52_c will check the magic number and get the file size of the dfu bin file.

The bin file content is written from `hex-address-offset + 4096`.

### What is bin_to_hex.py

It's from Python library `intelhex > bin2hex` from github [[link](https://github.com/python-intelhex/intelhex)].

It's used to convert a bin file to intel hex format.

We can program a valid hex file to a nRF device.

Usage:

```
python bin_to_hex.py <in-file> --offset=<address-offset>
```

### What is lib_dk_snr.py

Fetch serial number(SNR) of a connected NRF DK.

It's a base library for other scripts.

### What is lib_run_cmd.py

Run a console command.

It's a base library for other scripts.

### What is make_dfu_zip.py

Generate a application hex to DFU package.

It uses nrfutil to implement.

Usage:

```
Verify the variable of this script:

	- new_application_file_name
	- private_key_file_name
	- dfu_package_file_name

Then: 
python make_dfu_zip.py
```

### What is dfu_zip_to_bin.py

Convert a DFU package to bin format.

### What is make_dfu_bin.py

Convert a DFU package to bin format.

It uses `dfu_zip_to_bin.py`.

Usage:

```
Verify the variable of this script:

	- dfu_package_file_name
	- dfu_bin_file_name
	- dfu_zip_to_bin_script_name

Then: 
python make_dfu_bin.py
```

### What is prog_sd_app_bl.py

Program softdevice + application + bootloader + bootloader settings to 52832 DK.

Usage

```
python prog_sd_app_bl.py
```





### How to use 52840 as BLE central

1. Program `sdk16.0\_project\cross_dfu_52_c` to 52840 DK. 
   No need bootloader.
   It will scan the device named *Cross_DFU_52* and connect to it automatically.

2. Prepare your DFU bin file, and write it to bank 1 of 52840 DK.

   ```
   python copy_dfu_file_to_central.py <bin-file> <address-offset>
   ```

   By default, address-offset should be set to 0x40000. In RTT log, it print the start address of bank 1, this address-offset must be equal to or bigger than it, and address-offset must be 4096 bytes aligned.

3. Check RTT log, when connection is ready, press button 1 to start DFU.
   DFU process will be printed in RTT log.

### How to generate DFU bin file

1. Prepare application hex file

2. Generate DFU zip file

   ```
   python make_dfu_zip.py
   ```

3. Convert to DFU bin file

   ```
   python make_dfu_bin.py
   ```

4. Get the dfu_bin file at: `out_files\dfu_bin.bin`

### How to program 52832 DK as cross DFU peripheral

1. Build application and bootloader projects
   app: `sdk16.0\_project\cross_dfu_52`
   bootloader: `sdk16.0\_project\serial_bootloader`

2. Program them

   ```
   python prog_sd_app_bl.py
   ```

   

