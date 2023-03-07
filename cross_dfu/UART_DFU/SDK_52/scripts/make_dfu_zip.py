"""
Description: Generate a DFU package file(*.zip) by `nrfutil pkg generate` command
"""
import sys
from os import path
from os import mkdir
from lib_run_cmd import run_cmd


# Base paths
in_file_path = path.relpath('./in_files')
out_file_path = path.relpath('./out_files')

# File names
new_application_file_name = 'ble_app_hrs_pca10040_s132.hex'
private_key_file_name = 'private_key.pem'
dfu_package_file_name = 'dfu_pkg.zip'

# File paths
new_application_file_path = path.join(in_file_path, new_application_file_name)
private_key_file_path = path.join(in_file_path, private_key_file_name)
dfu_package_file_path = path.join(out_file_path, dfu_package_file_name)

# sd-req value, by command: `nrfutil pkg generate --help`
sd_req_52832 = 0xCB
sd_req_52840 = 0xCA


if __name__ == '__main__':

    if not path.exists(in_file_path):
        print('Input file directory is invalid, exit')
        exit(1)

    # Make a `out_files` folder
    if not path.exists(out_file_path):
        mkdir(out_file_path)

    print('Generate DFU zip file...')
    cmd = '''nrfutil pkg generate
                        --application "{new_app}"
                        --application-version 1
                        --hw-version 52
                        --sd-req {sd_req}
                        --key-file "{priv_key}"
                        "{dfu_pkg}"
    '''.format(new_app = new_application_file_path,
            sd_req = sd_req_52832,
            priv_key = private_key_file_path,
            dfu_pkg = dfu_package_file_path)

    run_cmd(cmd)

    if path.exists(dfu_package_file_path):
        print('DFU package file is generated:\n{}'.format(path.abspath(dfu_package_file_path)))
