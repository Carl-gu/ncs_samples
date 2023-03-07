#ifndef DFU_FILE_H__
#define DFU_FILE_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAGIC_NUMBER_MCUBOOT	0x96f3b83d
#define MAGIC_NUMBER_SDK_DFU    0x49535951
#define MAGIC_NUMBER_MODEM      0x7544656d

#define IMAGE_TYPE_NRF52		0           /* SDK DFU file for nrf52 firmware */
#define IMAGE_TYPE_NRF91		1           /* Mcuboot file for nrf91 firmware */
#define IMAGE_TYPE_MODEM		2           /* Mcuboot file for nrf91 modem */
#define IMAGE_TYPE_ERROR		(-1)        /* Unknown file type */

/**@brief Get DFU file type
 *
 * @return IMAGE_TYPE_NRF91: Mcuboot file for nrf91
 * @return IMAGE_TYPE_NRF52: SDK DFU file for nrf52
 * @return IMAGE_TYPE_ERROR: unknown file type
 */
int dfu_file_type(void);

/**@brief Get DFU file info
 *
 * @details init packet address[4], init packet size[4],
 * firmware bin address[4], firmware bin size[4]
 *
 * @return n/a
 */
void dfu_file_info(u32_t* ip_addr, u32_t* ip_size,
    u32_t* fw_addr, u32_t* fw_size);

#ifdef __cplusplus
}
#endif

#endif /* DFU_FILE_H__ */
