#ifndef SERIAL_DFU_H__
#define SERIAL_DFU_H__

#include <zephyr.h>
#include <device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Initialize serial dfu module
 *
 * @param[in] p_device: pointer of UART device
 *
 * @return 0: success
 * @return neg: error
 */
    int serial_dfu_init(struct device* p_device);

/**@brief Un-initialize serial dfu module */
void serial_dfu_uninit(void);

/**@brief Start to perform serial dfu */
void serial_dfu_start(void);

#ifdef __cplusplus
}
#endif

#endif
