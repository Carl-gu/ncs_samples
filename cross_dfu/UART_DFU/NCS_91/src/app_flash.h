#ifndef APP_FLASH_H__
#define APP_FLASH_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PM_MCUBOOT_SECONDARY_ID
#define APP_FLASH_BANK_ID			PM_MCUBOOT_SECONDARY_ID
#else
#define APP_FLASH_BANK_ID			FLASH_AREA_ID(image_1)		// 3: flash0 -> "image-1"
#endif // PM_MCUBOOT_SECONDARY_ID

int app_flash_info(u8_t* p_data);
int app_flash_read(u32_t offset, u8_t* p_data, u32_t length);
int app_flash_write(u32_t offset, u8_t* p_data, u32_t length);
int app_flash_erase_page(u32_t offset, u32_t count);
int app_flash_erase_from_end(u32_t count);
int app_flash_crc(u32_t offset, u32_t length, u32_t* crc32);

#ifdef __cplusplus
}
#endif

#endif /* APP_FLASH_H__ */
