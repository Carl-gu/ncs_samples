#ifndef APP_UART_H__
#define APP_UART_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @typedef uart buffer type */
typedef struct
{
    u8_t*   p_data;					/* Pointer of data */
    u16_t 	length;					/* Data length of the buffer */
    u16_t 	max_len;				/* Max of data length */
} uart_buff_t;

/**@typedef uart receiving callback function type
 *
 * @param[in] p_buffer 	pointer of data buffer
 * @param[in] length 	data length
 */
typedef void (*uart_rx_cb)(u8_t* p_data, u16_t length);

/**@typedef uart sending callback function type 
 *
 * @param[in] event     tx event id. 0: success, -1: failed.
 */
typedef void (*uart_tx_cb)(int event);

/**@brief Initialize app_uart module
 *
 * @param[in] p_device 		pointer of UART device
 * @param[in] p_rx_buff		pointer of UART rx buffer
 * @param[in] rx_max_len	max length of rx buffer
 *
 * @return 0				success
 * @return -1				failed
 */
int app_uart_init(struct device* p_device, u8_t* p_rx_buff,
		u16_t rx_max_len);

/**@brief Un-initialize app_uart module */
void app_uart_uninit(void);

/**@brief Send uart data
 *
 * @param p_data		pointer of data to be sent
 * @param length		length of data to be sent
 *
 * @return 0			success
 * @return -1			failed
 */
int app_uart_send(const u8_t* p_data, u16_t length);

/**@brief Send uart data in synchronized mode
 *
 * @param p_device 		pointer of UART device
 * @param p_data		pointer of data to be sent
 * @param length		length of data to be sent
 *
 * @return 0			success
 * @return -1			failed
 */
int uart_send_sync(struct device* p_device, const u8_t* p_data, u16_t length);

/**@brief Rest a buffer data
 *
 * @param[in] p_buff: pointer of the buffer
 *
 * @return n/a
 */
void uart_buffer_reset(uart_buff_t* p_buff);

/**@brief Reset rx buffer */
void app_uart_rx_reset(void);

/**@brief Set rx data ready event callback */
void app_uart_rx_cb_set(uart_rx_cb cb);

/**@brief Set tx empty event callback */
void app_uart_tx_cb_set(uart_tx_cb cb);


#ifdef __cplusplus
}
#endif

#endif /* APP_UART_H__ */
