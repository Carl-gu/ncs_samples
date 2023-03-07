"""
Description: Perform general DFU programming as below:

1. Genrate bootloader settings
2. Merge bootloader hex and bootloader settings hex
3. Erase
4. Program softdevice
5. Program application
6. Program bootloader merged hex
7. Reset
"""
import sys
from os import path
from os import mkdir
from lib_run_cmd import run_cmd
from lib_dk_snr import DK_SNR


dk_snr = DK_SNR

# Base paths
out_file_path = path.relpath('./out_files')
app_build_path = path.relpath('../sdk16.0/_project/cross_dfu_52/pca10040/s132/ses/Output/Debug/Exe')
bl_build_path = path.relpath('../sdk16.0/_project/serial_bootloader/secure_bootloader/pca10040_uart/ses/Output/Release/Exe')
sd_path = path.relpath('../sdk16.0/components/softdevice/s132/hex')

# File names
application_file_name = 'cross_dfu_52.hex'
bootloader_file_name = 'secure_bootloader_uart_mbr_pca10040.hex'
softdevice_file_name = 's132_nrf52_7.0.1_softdevice.hex'
bl_settings_file_name = 'bl_settings.hex'
bl_merged_file_name = 'bl_merged.hex'

# File paths
application_file_path = path.join(app_build_path, application_file_name)
bootloader_file_path = path.join(bl_build_path, bootloader_file_name)
softdevice_file_path = path.join(sd_path, softdevice_file_name)
bl_settings_file_path = path.join(out_file_path, bl_settings_file_name)
bl_merged_file_path = path.join(out_file_path, bl_merged_file_name)

# Device family
device_family_52832 = 'NRF52'
device_family_52840 = 'NRF52840'

if __name__ == '__main__':

    # Make a `out_files` folder
    if not path.exists(out_file_path):
        mkdir(out_file_path)

    print('Generate bootloader settings...')
    cmd = '''nrfutil settings generate
                        --family {family}
                        --application "{app_file}"
                        --application-version 1
                        --bootloader-version 1
                        --bl-settings-version 2
                        "{settings_file}"
    '''.format(family = device_family_52832,
        app_file = application_file_path,
        settings_file = bl_settings_file_path)

    run_cmd(cmd)

    print('Merge bootloader and settings...')
    cmd = '''mergehex -m "{bl_file}" "{settings_file}" -o "{merged_file}"
    '''.format(bl_file = bootloader_file_path,
        settings_file = bl_settings_file_path,
        merged_file = bl_merged_file_path)

    run_cmd(cmd)

    print('Erase...')
    cmd = '''nrfjprog -s {snr} -e
    '''.format(snr = dk_snr)

    run_cmd(cmd)

    print('Program softdevice...')
    cmd = '''nrfjprog -s {snr} --program "{hex}"
    '''.format(snr = dk_snr, hex = softdevice_file_path)

    run_cmd(cmd)

    print('Program application file...')
    cmd = '''nrfjprog -s {snr} --program "{hex}" --sectorerase
    '''.format(snr = dk_snr, hex = application_file_path)

    run_cmd(cmd)

    print('Program merged bootloader file...')
    cmd = '''nrfjprog -s {snr} --program "{hex}"
    '''.format(snr = dk_snr, hex = bl_merged_file_path)

    run_cmd(cmd)

    print('Reset...')
    cmd = '''nrfjprog -s {snr} -r
    '''.format(snr = dk_snr)

    run_cmd(cmd)

