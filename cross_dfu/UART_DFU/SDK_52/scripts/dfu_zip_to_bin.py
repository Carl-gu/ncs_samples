import zipfile
import os
from crc.crc import Crc32, CrcCalculator
from enum import IntEnum, unique
import json
import sys


@unique
class DfuImageType(IntEnum):
    application = 0
    bootloader = 1
    softdevice = 2
    softdevice_bootloader = 3


class DfuImage:
    def __init__(self):
        self.type = DfuImageType.application
        self.size = 0

        self.init_packet_name = ''
        self.init_packet_addr = 0
        self.init_packet_size = 0
        self.init_packet_data = []

        self.firmware_name = ''
        self.firmware_addr = 0
        self.firmware_size = 0
        self.firmware_data = []

    def __repr__(self):
        ret_msg = 'Image info:\n'
        ret_msg += 'Type: {}\n'.format(str(DfuImageType(self.type)))
        # ret_msg += 'Size(ip + fw): {:d} bytes\n'.format(self.size)
        ret_msg += 'Init packet\n name: {}\n addr: 0x{:08X}\n size: {} bytes\n'.format(self.init_packet_name, self.init_packet_addr, self.init_packet_size)
        ret_msg += 'Firmware\n name: {}\n addr: 0x{:08X}\n size: {} bytes\n'.format(self.firmware_name, self.firmware_addr, self.firmware_size)
        return ret_msg


class DfuFlatFile:
    magic_number_mcuboot = 0x96F3B83D
    magic_number_dfufile = 0x49535951
    file_header_size_max = 128
    init_packet_size_max = 512

    def __init__(self):
        self.file_name = ''
        self.file_size = 0
        self.file_data = []
        self.file_crc = 0
        self.images = []

    def make(self, zip_file_path, out_file_path):
        if not os.path.exists(zip_file_path):
            print('Input file is not existed')
            exit(1)

        self.file_name = out_file_path

        dfu_zip_file = zipfile.ZipFile(zip_file_path)
        if not len(dfu_zip_file.infolist()) == 3:
            print('Only support one file upgrading in this version')
            exit(1)

        manifest = json.loads(dfu_zip_file.read('manifest.json'))
        if manifest is None:
            print('Invalid NRF Secure DFU package')
            exit(1)

        self.file_size += DfuFlatFile.file_header_size_max

        if len(manifest['manifest']) is not 1:
            print('Warning: it only supports one step of update now')
            exit(1)

        for image_type in ('application', 'softdevice', 'bootloader', 'softdevice_bootloader'):
            if image_type in manifest['manifest']:
                ip_file_name = manifest['manifest'][image_type]['dat_file']
                fw_file_name = manifest['manifest'][image_type]['bin_file']

                image = DfuImage()

                image.init_packet_name = ip_file_name
                image.init_packet_addr = self.file_size
                image.init_packet_size = dfu_zip_file.getinfo(ip_file_name).file_size
                image.init_packet_data = dfu_zip_file.read(ip_file_name)

                image.firmware_name = fw_file_name
                image.firmware_addr = image.init_packet_addr + DfuFlatFile.init_packet_size_max
                image.firmware_size = dfu_zip_file.getinfo(fw_file_name).file_size
                image.firmware_data = dfu_zip_file.read(fw_file_name)

                image.type = DfuImageType[image_type]

                # Make the size of firmware word-aligned, so the following elements
                # can also get a word-aligned address
                if image.firmware_size % 4 == 0:
                    image.size = DfuFlatFile.init_packet_size_max + image.firmware_size
                else:
                    padding_size = 4 - (image.firmware_size & 3)
                    image.size = DfuFlatFile.init_packet_size_max + image.firmware_size + padding_size

                print(image)

                self.images.append(image)

                self.file_size += image.size

        self.file_size += 4     # len(CRC32)

        #  Fill file header
        self.file_data.extend(DfuFlatFile.get_le32(DfuFlatFile.magic_number_mcuboot))
        self.file_data.extend(DfuFlatFile.get_le32(DfuFlatFile.magic_number_dfufile))
        self.file_data.extend(DfuFlatFile.get_le32(self.file_size))
        self.file_data.extend(DfuFlatFile.get_le32(len(self.images)))

        # Fill image info
        for img in self.images:
            self.file_data.extend(DfuFlatFile.get_le32(img.type))
            self.file_data.extend(DfuFlatFile.get_le32(img.init_packet_addr))
            self.file_data.extend(DfuFlatFile.get_le32(img.init_packet_size))
            self.file_data.extend(DfuFlatFile.get_le32(img.firmware_addr))
            self.file_data.extend(DfuFlatFile.get_le32(img.firmware_size))

        # Fill padding with 0xFF
        left = DfuFlatFile.file_header_size_max - len(self.file_data)
        self.file_data.extend([0xFF] * left)

        # print('-----------------')
        # print(' '.join(['{:02X}'.format(x) for x in self.file_data]))

        # Fill image data
        for img in self.images:
            self.file_data.extend(img.init_packet_data)
            # Fill padding with 0xFF
            if img.init_packet_size < DfuFlatFile.init_packet_size_max:
                left = DfuFlatFile.init_packet_size_max - img.init_packet_size
                self.file_data.extend([0xFF] * left)

            self.file_data.extend(img.firmware_data)
            # Fill padding with 0xFF
            if not img.firmware_size % 4 == 0:
                left = 4 - (image.firmware_size & 3)
                self.file_data.extend([0xFF] * left)


        # Fill CRC32
        self.file_crc = CrcCalculator(Crc32.CRC32).calculate_checksum(self.file_data)
        self.file_data.extend(DfuFlatFile.get_le32(self.file_crc))

        try:
            out_file = open(self.file_name, 'wb+')
            out_file.write(bytes(self.file_data))
        finally:
            out_file.close()

    @staticmethod
    def get_le32(number: int):
        return number.to_bytes(4, 'little')



if __name__ == '__main__':
    """
    Usage: python dfu_zip_to_bin.py dfu_pkg.zip dfu_bin.bin
    """
    dfu_bin = DfuFlatFile()

    if len(sys.argv) == 3:
        in_file = sys.argv[1]
        if not os.path.exists(in_file):
            print('error: invalid file name')
            exit(1)

        out_file = sys.argv[2]
    else:
        print('error. usage: python dfu_zip_to_bin.py <zip_file> <bin_file>')
        exit(1)

    dfu_bin.make(in_file, out_file)

    exit(0)
