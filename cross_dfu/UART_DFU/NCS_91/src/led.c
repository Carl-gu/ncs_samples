#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(led, 3);

#include "led.h"

#define LED_PORT_LABEL		DT_GPIO_LABEL(DT_ALIAS(led0), gpios)

#define LED_1				DT_ALIAS(led0)
#define LED_2				DT_ALIAS(led1)
#define LED_3				DT_ALIAS(led2)
#define LED_4				DT_ALIAS(led3)

#define GET_PIN(x)	        DT_GPIO_PIN(x, gpios)
#define GET_FLAGS(x)	    (GPIO_OUTPUT_INACTIVE | DT_GPIO_FLAGS(x, gpios))

static struct device *m_device;

/**@brief Initialize led module */
void led_init(void)
{
	m_device = device_get_binding(LED_PORT_LABEL);
	if (m_device == NULL) {
		LOG_ERR("Unable to bind GPIO device!\n");
		return;
	}

	gpio_pin_configure(m_device, GET_PIN(LED_1), GET_FLAGS(LED_1));
	gpio_pin_configure(m_device, GET_PIN(LED_2), GET_FLAGS(LED_2));
	gpio_pin_configure(m_device, GET_PIN(LED_3), GET_FLAGS(LED_3));
	gpio_pin_configure(m_device, GET_PIN(LED_4), GET_FLAGS(LED_4));
}

/**@brief Set on/off of LED 1 */
void led1_set(int value)
{
	gpio_pin_set(m_device, GET_PIN(LED_1), value);
}

/**@brief Set on/off of LED 2 */
void led2_set(int value)
{
	gpio_pin_set(m_device, GET_PIN(LED_2), value);
}

/**@brief Set on/off of LED 3 */
void led3_set(int value)
{
	gpio_pin_set(m_device, GET_PIN(LED_3), value);
}

/**@brief Set on/off of LED 4 */
void led4_set(int value)
{
	gpio_pin_set(m_device, GET_PIN(LED_4), value);
}