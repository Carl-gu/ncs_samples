#ifndef BUTTON_H__
#define BUTTON_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*button_handler)(void);

/**@brief Initialize button module */
int button_init(void);

/**@brief Assign interrupt handler to a button
 *
 * @param[in] button_id: one of {"button_1", "button_2", "switch_1", "switch_2" }
 * @param[in] handler: interrupt handler of button
 *
 * @return 0: success
 * @return -1: error
 */
int button_handler_assign(char* button_id, button_handler handler);

/**@brief Read status of a button
 *
 * @param[in] button_id: one of {"button_1", "button_2", "switch_1", "switch_2" }
 *
 * @return 0: success
 * @return -1: error
 */
int button_read(char* button_id);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H__ */
