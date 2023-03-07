#include <stdio.h>
#include <zephyr.h>
#include <device.h>
#include <sys/util.h>
#include <sys/byteorder.h>
#include <sys/printk.h>
#include <storage/flash_map.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(dfu_serial_file, 3);

#include "crc32.h"
#include "dfu_file.h"

#ifdef PM_MCUBOOT_SECONDARY_ID
#define DFU_FLASH_AREA_ID			PM_MCUBOOT_SECONDARY_ID
#else
#include "app_flash.h"
#define DFU_FLASH_AREA_ID			APP_FLASH_BANK_ID
#endif // PM_MCUBOOT_SECONDARY_ID

#define FILE_HEADER_LEN             128
#define FILE_HEADER_OFFSET          0

// Offset of file header elements
#define FILE_OFFSET_IP_ADDR         20
#define FILE_OFFSET_IP_SIZE         24
#define FILE_OFFSET_FW_ADDR         28
#define FILE_OFFSET_FW_SIZE         32

static u32_t fa_addr_base;

/**@brief Read flash
 *
 * @param[in] offset: offset from the start address
 * @param[in] p_data: pointer of the data
 * @param[in] length: length of the data
 *
 * @return 0: success
 * @return neg: error
 */
static int _flash_read(u32_t offset, u8_t* p_data, 
        u16_t length)
{
    int rc;
    const struct flash_area* fa;

    rc = flash_area_open(DFU_FLASH_AREA_ID, &fa);
    if (rc) {
        LOG_ERR("Flash area open error");
        return rc;
    }

    fa_addr_base = fa->fa_off;

    rc = flash_area_read(fa, offset, p_data, length);
    if (rc != 0) {
        LOG_ERR("Flash area read error");
    }

    flash_area_close(fa);

    return rc;
}

/**@brief Get DFU file type
 *
 * @return IMAGE_TYPE_NRF91: Mcuboot file for nrf91
 * @return IMAGE_TYPE_NRF52: SDK DFU file for nrf52
 * @return IMAGE_TYPE_MODEM: SDK DFU file for nrf91 modem
 * @return IMAGE_TYPE_ERROR: unknown file type
 */
int dfu_file_type(void)
{
    u8_t  p_data[8];
    u32_t magic_number_1;
    u32_t magic_number_2;

    _flash_read(0, p_data, sizeof(p_data));

    magic_number_1 = sys_get_le32(&p_data[0]);
    magic_number_2 = sys_get_le32(&p_data[4]);

    if (magic_number_1 == MAGIC_NUMBER_MCUBOOT)
    {
        if (magic_number_2 == MAGIC_NUMBER_SDK_DFU) {
            return IMAGE_TYPE_NRF52;
        }
        else {
            return IMAGE_TYPE_NRF91;
        }        
    }
    else
    {
        if (magic_number_2 == MAGIC_NUMBER_MODEM) {
            return IMAGE_TYPE_MODEM;
        }
        else {
            LOG_HEXDUMP_INF(p_data, sizeof(p_data), "File header is: ");
            return IMAGE_TYPE_ERROR;
        }   
    }
}

/**@brief Get DFU file info
 *
 * @details init packet address[4], init packet size[4],
 * firmware bin address[4], firmware bin size[4]
 *
 * @param[out] ip_addr: init packet address
 * @param[out] ip_size: init packet size
 * @param[out] fw_addr: firmware address
 * @param[out] fw_size: firmware size
 *
 * @return n/a
 */
void dfu_file_info(u32_t* ip_addr, u32_t* ip_size, 
        u32_t* fw_addr, u32_t* fw_size)
{
    u8_t p_file_header[FILE_HEADER_LEN];
    memset(p_file_header, 0, FILE_HEADER_LEN);

    _flash_read(FILE_HEADER_OFFSET, p_file_header, FILE_HEADER_LEN);

    *ip_addr = fa_addr_base + sys_get_le32(&p_file_header[FILE_OFFSET_IP_ADDR]);
    *fw_addr = fa_addr_base + sys_get_le32(&p_file_header[FILE_OFFSET_FW_ADDR]);

    *ip_size = sys_get_le32(&p_file_header[FILE_OFFSET_IP_SIZE]);
    *fw_size = sys_get_le32(&p_file_header[FILE_OFFSET_FW_SIZE]);

    LOG_DBG("ip addr: %08x", *ip_addr);
    LOG_DBG("ip size: %08x", *ip_size);
    LOG_DBG("fw addr: %08x", *fw_addr);
    LOG_DBG("fw size: %08x", *fw_size);
}

