#include <stdio.h>
#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(button, 3);

#include "button.h"

#define BUTTON_PORT_DEV		DT_GPIO_LABEL(DT_ALIAS(sw0), gpios)

#define BUTTON_1	        DT_ALIAS(sw0)
#define BUTTON_2	        DT_ALIAS(sw1)
#define SWITCH_1	        DT_ALIAS(sw2)
#define SWITCH_2	        DT_ALIAS(sw3)

#define GET_PIN(x)	        DT_GPIO_PIN(x, gpios)
#define GET_FLAGS(x)	    (GPIO_INPUT | DT_GPIO_FLAGS(x, gpios))

#define BUTTON_DEBOUNCE		10000 // 10ms
#define SWITCH_DEBOUNCE		30000 // 30ms

static struct device *button_port;
static struct gpio_callback gpio_cb;    
static button_handler button1_pressed_handler;
static button_handler button2_pressed_handler;
static button_handler switch1_toggled_handler;
static button_handler switch2_toggled_handler;

void button_handler_placeholder(void) { ; }

/**@brief Interrupt handler of button */
static void button_evt_handler(struct device *dev, struct gpio_callback *cb, u32_t pins)
{
	int rc;

    if (pins & BIT(GET_PIN(BUTTON_1))) {
		k_busy_wait(BUTTON_DEBOUNCE);

		if (gpio_pin_get(button_port, GET_PIN(BUTTON_1)) == 0) {
			return;
		}

        button1_pressed_handler();
    }
    else if (pins & BIT(GET_PIN(BUTTON_2))) {
		k_busy_wait(BUTTON_DEBOUNCE);

		if (gpio_pin_get(button_port, GET_PIN(BUTTON_2)) == 0) {
			return;
		}
		
        button2_pressed_handler();
    }
	else if (pins & BIT(GET_PIN(SWITCH_1))) {
		rc = gpio_pin_interrupt_configure(button_port, GET_PIN(SWITCH_1), GPIO_INT_DISABLE);
		if (rc != 0) {
			LOG_ERR("Unable to enable interrupt!\n");
		}

		k_busy_wait(SWITCH_DEBOUNCE);

        if (gpio_pin_get(button_port, GET_PIN(SWITCH_1)) == 0) {

            rc = gpio_pin_interrupt_configure(button_port, GET_PIN(SWITCH_1), GPIO_INT_EDGE_TO_ACTIVE);
            if (rc != 0) {
                LOG_ERR("Unable to enable interrupt!\n");
            }
        }
        else {
            rc = gpio_pin_interrupt_configure(button_port, GET_PIN(SWITCH_1), GPIO_INT_EDGE_TO_INACTIVE);
            if (rc != 0) {
                LOG_ERR("Unable to enable interrupt!\n");
            }
        }

		switch1_toggled_handler();
	}
	else if (pins & BIT(GET_PIN(SWITCH_2))) {
		rc = gpio_pin_interrupt_configure(button_port, GET_PIN(SWITCH_2), GPIO_INT_DISABLE);
		if (rc != 0) {
			LOG_ERR("Unable to enable interrupt!\n");
		}

		k_busy_wait(SWITCH_DEBOUNCE);

		if (gpio_pin_get(button_port, GET_PIN(SWITCH_2)) == 0) {

			rc = gpio_pin_interrupt_configure(button_port, GET_PIN(SWITCH_2), GPIO_INT_EDGE_TO_ACTIVE);
			if (rc != 0) {
				LOG_ERR("Unable to enable interrupt!\n");
			}
		}
		else {
			rc = gpio_pin_interrupt_configure(button_port, GET_PIN(SWITCH_2), GPIO_INT_EDGE_TO_INACTIVE);
			if (rc != 0) {
				LOG_ERR("Unable to enable interrupt!\n");
			}
		}

		switch2_toggled_handler();
	}
}

/**@brief Initialize button module */
int button_init(void)
{
	int rc;

	button_port = device_get_binding(BUTTON_PORT_DEV);
	if (button_port == NULL) {
		LOG_ERR("Unable to bind GPIO device!\n");
		return -1;
	}

    // Config Button 1
	rc = gpio_pin_configure(button_port, GET_PIN(BUTTON_1), GET_FLAGS(BUTTON_1));
	if (rc != 0) {
		LOG_ERR("Unable to configure gpio pin!\n");
		return -1;
	}

    rc = gpio_pin_interrupt_configure(button_port, GET_PIN(BUTTON_1), GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		LOG_ERR("Unable to enable interrupt!\n");
		return -1;
    }
    // End of button 1

    // Config button 2
	rc = gpio_pin_configure(button_port, GET_PIN(BUTTON_2), GET_FLAGS(BUTTON_2));
	if (rc != 0) {
		LOG_ERR("Unable to configure gpio pin!\n");
		return -1;
	}	

    rc = gpio_pin_interrupt_configure(button_port, GET_PIN(BUTTON_2), GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		LOG_ERR("Unable to enable interrupt!\n");
		return -1;
    }
    // End of button 2

	// Config switch 1
	rc = gpio_pin_configure(button_port, GET_PIN(SWITCH_1), GET_FLAGS(SWITCH_1));
	if (rc != 0) {
		LOG_ERR("Unable to configure gpio pin!\n");
		return -1;
	}

	rc = gpio_pin_interrupt_configure(button_port, GET_PIN(SWITCH_1), GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		LOG_ERR("Unable to enable interrupt!\n");
		return -1;
	}
	// End of switch 1

	// Config switch 2
	rc = gpio_pin_configure(button_port, GET_PIN(SWITCH_2), GET_FLAGS(SWITCH_2));
	if (rc != 0) {
		LOG_ERR("Unable to configure gpio pin!\n");
		return -1;
	}

	rc = gpio_pin_interrupt_configure(button_port, GET_PIN(SWITCH_2), GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		LOG_ERR("Unable to enable interrupt!\n");
		return -1;
	}
	// End of switch 2

    gpio_init_callback(&gpio_cb, button_evt_handler, 
		BIT(GET_PIN(BUTTON_1)) | BIT(GET_PIN(BUTTON_2)) | BIT(GET_PIN(SWITCH_1)) | BIT(GET_PIN(SWITCH_2)) );
    rc = gpio_add_callback(button_port, &gpio_cb);
	if (rc != 0) {
		LOG_ERR("Unable to assign callback function!\n");
		return -1;
	}

    button1_pressed_handler = button_handler_placeholder;
    button2_pressed_handler = button_handler_placeholder;
	switch1_toggled_handler = button_handler_placeholder;
	switch2_toggled_handler = button_handler_placeholder;

	return 0;
}

/**@brief Assign interrupt handler to a button
 *
 * @param[in] button_id: one of {"button_1", "button_2", "switch_1", "switch_2" }
 * @param[in] handler: interrupt handler of button
 *
 * @return 0: success
 * @return -1: error
 */
int button_handler_assign(char* button_id, button_handler handler)
{
	int rc = 0;

	if (strcmp(button_id, "button_1") == 0) {
		button1_pressed_handler = handler;
	}
	else if (strcmp(button_id, "button_2") == 0) {
		button2_pressed_handler = handler;
	}
	else if (strcmp(button_id, "switch_1") == 0) {
		switch1_toggled_handler = handler;
	}
	else if (strcmp(button_id, "switch_2") == 0) {
		switch2_toggled_handler = handler;
	}
	else {
		rc = -1;
	}

	return rc;
}

/**@brief Read status of a button
 *
 * @param[in] button_id: one of {"button_1", "button_2", "switch_1", "switch_2" }
 *
 * @return 0: success
 * @return -1: error
 */
int button_read(char* button_id)
{
	if (strcmp(button_id, "button_1") == 0) {
		return gpio_pin_get(button_port, GET_PIN(BUTTON_1));
	}
	else if (strcmp(button_id, "button_2") == 0) {
		return gpio_pin_get(button_port, GET_PIN(BUTTON_2));
	}
	else if (strcmp(button_id, "switch_1") == 0) {
		return gpio_pin_get(button_port, GET_PIN(SWITCH_1));
	}
	else if (strcmp(button_id, "switch_2") == 0) {
		return gpio_pin_get(button_port, GET_PIN(SWITCH_2));
	}
	else {
		return -1;
	}
}