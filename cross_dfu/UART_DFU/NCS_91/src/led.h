#ifndef LED_H__
#define LED_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Initialize led module */
void led_init(void);

/**@brief Set on/off of LED 1 */
void led1_set(int value);

/**@brief Set on/off of LED 2 */
void led2_set(int value);

/**@brief Set on/off of LED 3 */
void led3_set(int value);

/**@brief Set on/off of LED 4 */
void led4_set(int value);

#ifdef __cplusplus
}
#endif

#endif /* LED_H__ */
