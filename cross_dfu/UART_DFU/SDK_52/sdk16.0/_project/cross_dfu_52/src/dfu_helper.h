#ifndef DFU_HELPER_H__
#define DFU_HELPER_H__

#include <stdint.h>
#include "ble.h"

#ifdef __cplusplus
extern "C" {
#endif


// app flash commands: 0x20 - 0x2F
#define CMD_OP_FLASH        0x20
#define CMD_OP_FLASH_INFO   (CMD_OP_FLASH + 1)
#define CMD_OP_FLASH_READ   (CMD_OP_FLASH + 2)
#define CMD_OP_FLASH_WRITE  (CMD_OP_FLASH + 3)
#define CMD_OP_FLASH_ERASE  (CMD_OP_FLASH + 4)
#define CMD_OP_FLASH_CRC    (CMD_OP_FLASH + 5)
#define CMD_OP_FLASH_START  (CMD_OP_FLASH + 6)
#define CMD_OP_FLASH_DONE   (CMD_OP_FLASH + 7)

// Control commands: 0x30 - 0x3F
#define CMD_OP_CONTROL      0x30
#define CMD_OP_ENTER_BL     (CMD_OP_CONTROL + 1)
#define CMD_OP_PING_APP     (CMD_OP_CONTROL + 2)


/**@brief Init dfu helper module.
 *
 * @param[in] p_nus: pointer of nus instance.
 */
void dfu_helper_init(ble_nus_t * p_nus);


#ifdef __cplusplus
}
#endif

#endif /* DFU_HELPER_H__ */
