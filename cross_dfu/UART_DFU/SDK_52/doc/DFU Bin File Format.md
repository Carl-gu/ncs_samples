## DFU Bin File

### 1. Format

DFU bin file is a binary file format by flattening a standard Nordic DFU package file(*.zip).

A DFU package contains init packet, firmware bin and manifest.json.

DFU bin file join three parts to one binary file.

It adds a `file_info` part as the file header to record file sizes and offset addresses of init packet and firmware bin. It has the same effect of manifest.json.

The format structure of bin file is designed as below:

```
+--------------+
| File_info    | 
+--------------+
| Init_packet  | 
+--------------+
| Firmware_bin | 
+--------------+
| File_crc 	   | 
+--------------+

```

The length of each part is:

```
+--------------+------------+-----------------+
|     Part     | Length     | Variable-Length |
+--------------+------------+-----------------+
| File_info    | 128        | No              |
| Init_packet  | 512        | No              |
| Firmware_bin | x          | Yes             |
| File_crc	   | 4          | No              |
+--------------+------------+-----------------+
```

Details of file info:

```
+---------------+----------------------+--------------+----------------------+
| Magic_1[4]    | Magic_2[4]     	   | File_Size[4] | Image_Count[4]       |
+---------------+----------------------+--------------+----------------------+
| Image_Type[4] | IP_Addr[4]     	   | IP_Size[4]   | FW_Addr[4]           |
+---------------+----------------------+--------------+----------------------+
| FW_Size[4]    | Padding to 128 bytes								 		 |
+---------------+----------------------+--------------+----------------------+
```

Details of init packet:

```
+---------------+----------------------+
| IP_Data[x]    | Padding to 512 bytes |
+---------------+----------------------+
```

Details of firmware bin:

```
+---------------+-------------------------+
| FW_Data[x]    | Padding to word aligned |
+---------------+-------------------------+
```

Details of CRC:

```
+---------------+
| CRC32[4]      |
+---------------+
```

Note: 

- All elements use little endian
- Padding byte is 0xFF
- If file_info length is less than 128 bytes, padding to 128.
- If init packet length is less than 512 bytes, padding to 512.
- If firmware bin length is not word(4-byte) aligned, padding to make it word aligned, for example, if len(fw) = 14, padding 2 bytes.



In order to keep things simple, it only supports one step DFU:

- Upgrade application
- Upgrade bootloader + softdevice

It doesn't support two step DFU(app + bl + sd).

### 2. Generate

```python
$ python dfu_zip_to_bin.py <in: dfu-zip-file> <out: dfu-bin-file>
        
Example: python dfu_zip_to_bin.py dfu_pkg.zip dfu_bin.bin
```

