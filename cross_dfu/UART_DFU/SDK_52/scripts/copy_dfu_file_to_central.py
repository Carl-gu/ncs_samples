"""
Description: Copy the DFU file to the flash of BLE central
"""
import sys
from os import stat
from os import path
from os import mkdir
from lib_run_cmd import run_cmd
from lib_dk_snr import DK_SNR


dk_snr = DK_SNR

# Base paths
out_file_path = path.relpath('./out_files')

# File names
dfu_bin_to_hex_name = 'dfu_bin_to_hex.hex'

# File paths
dfu_bin_to_hex_path = path.join(out_file_path, dfu_bin_to_hex_name)


magic_number = 0x49535951


if __name__ == '__main__':

    if len(sys.argv) != 3:
        print('usage: python copy_dfu_file_to_central.py <bin-file> <addr-offset>')
        exit(1)

    bin_file = sys.argv[1]
    address_offset = sys.argv[2]

    meta_info_address = int(address_offset, 16)
    hex_file_address = int(address_offset, 16) + 4096
    bin_file_size = stat(path.abspath(bin_file)).st_size

    print('Convert DFU bin to hex...')
    cmd = '''python bin_to_hex.py --offset={offset} "{bin_file}" "{hex_file}"
    '''.format(offset = hex_file_address, bin_file = bin_file, hex_file = dfu_bin_to_hex_path)

    run_cmd(cmd)

    if path.exists(dfu_bin_to_hex_path):
        print('DFU bin_to_hex file is generated:\n{}'.format(path.abspath(dfu_bin_to_hex_path)))

    print('Write file meta info to flash(address: 0x{:x})...'.format(meta_info_address))
    cmd = '''nrfjprog -s {snr} --erasepage 0x{page_addr:x}
    '''.format(snr = dk_snr, page_addr = meta_info_address)

    run_cmd(cmd)

    cmd = '''nrfjprog -s {snr} --memwr 0x{page_addr:x} --val 0x{magic:x}
    '''.format(snr = dk_snr, page_addr = meta_info_address, magic = magic_number)

    run_cmd(cmd)

    cmd = '''nrfjprog -s {snr} --memwr 0x{page_addr:x} --val {file_size}
    '''.format(snr = dk_snr, page_addr = meta_info_address + 4, file_size = bin_file_size)

    run_cmd(cmd)

    print('Write hex to flash(address: 0x{:x})...'.format(hex_file_address))
    cmd = '''nrfjprog -s {snr} --program "{hex_file}" --sectorerase
    '''.format(snr = dk_snr, hex_file = dfu_bin_to_hex_path)

    run_cmd(cmd)

    print('Please reset your 52840 DK manually')

