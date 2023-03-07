"""
Description: Generate a DFU bin file(*.bin) by script `dfu_zip_to_bin.py`
"""
import sys
from os import path
from os import mkdir
from lib_run_cmd import run_cmd


# Base paths
in_file_path = path.relpath('./in_files')
out_file_path = path.relpath('./out_files')
script_path = path.relpath('.')

# File names
dfu_package_file_name = 'dfu_pkg.zip'
dfu_bin_file_name = 'dfu_bin.bin'
dfu_zip_to_bin_script_name = 'dfu_zip_to_bin.py'

# File paths
dfu_package_file_path = path.join(out_file_path, dfu_package_file_name)
dfu_bin_file_path = path.join(out_file_path, dfu_bin_file_name)

# Script paths
dfu_zip_to_bin_script_path = path.join(script_path, dfu_zip_to_bin_script_name)


if __name__ == '__main__':

    if not path.exists(in_file_path):
        print('Input file directory is invalid, exit')
        exit(1)

    # Make a `out_files` folder
    if not path.exists(out_file_path):
        mkdir(out_file_path)

    print('Convert DFU zip to bin...')
    cmd = '''python "{script}" "{dfu_pkg}" "{dfu_bin}"
    '''.format(script = dfu_zip_to_bin_script_path,
        dfu_pkg = dfu_package_file_path,
        dfu_bin = dfu_bin_file_path)

    run_cmd(cmd)

    if path.exists(dfu_bin_file_path):
        print('DFU bin file is generated:\n{}'.format(path.abspath(dfu_bin_file_path)))
