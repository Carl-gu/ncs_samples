#ifndef APP_FLASH_CMD_H__
#define APP_FLASH_CMD_H__

#include <zephyr.h>
#include "app_cmd.h"

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

/**@brief Register flash related commands
 *
 * @param cb: event callback
 */
void app_flash_cmd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FLASH_CMD_H__ */
