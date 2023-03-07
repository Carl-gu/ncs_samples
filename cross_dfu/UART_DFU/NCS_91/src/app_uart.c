#include <zephyr.h>
#include <kernel.h>
#include <drivers/uart.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(app_uart, 3);

#include "app_uart.h"

#define FIFO_ELEM_LEN 				32

/** @typedef fifo data element type, specially used for k_fifo functions */
typedef struct
{
	void* fifo_reserved;			/* This item is necessary! */
	u8_t    p_data[FIFO_ELEM_LEN];
	u16_t   length;
} fifo_elem_t;

static uart_buff_t m_rx_buff;		/* RX buffer */
static uart_rx_cb  m_rx_cb;			/* RX ready interrupt callback */
static uart_tx_cb  m_tx_cb;			/* TX empty interrupt callback */

static struct device* m_device;		/* Current UART device */

static K_FIFO_DEFINE(m_tx_fifo);	/* Fifo for tx processing */

/**@brief Handle uart rx interrupt */
static void rx_handler(struct device* p_device, uart_buff_t* p_buff)
{
	int read_len;

	while (true) {
		/* As for nrfx drivers, it always reads 1 byte each time */
		read_len = uart_fifo_read(p_device,
				&p_buff->p_data[p_buff->length],
				p_buff->max_len - p_buff->length);

		if (read_len <= 0) {
			break;
		}

		p_buff->length += read_len;

		if (m_rx_cb)
		{
			m_rx_cb(p_buff->p_data, p_buff->length);
		}
	}
}

/**@brief Handle uart tx interrupt */
static void tx_handler(struct device* p_device, struct k_fifo* p_fifo)
{
	u16_t offset;
	u16_t fill_len;
	fifo_elem_t* p_elem;

	p_elem = k_fifo_get(p_fifo, K_NO_WAIT);
	if (!p_elem) {

		uart_irq_tx_disable(p_device);

		if (m_tx_cb) {
			m_tx_cb(-1);
		}
		return;
	}

	offset = 0;
	while (true) {
		fill_len = uart_fifo_fill(p_device, 
				p_elem->p_data + offset, 
				p_elem->length - offset);

		offset += fill_len;
		if (offset == p_elem->length) {
			break;
		}
	}

	while (!uart_irq_tx_complete(p_device)) {
		/* Wait for the last byte to get shifted out of the module */
	}

	if (k_fifo_is_empty(p_fifo)) {

		uart_irq_tx_disable(p_device);

		if (m_tx_cb) {
			m_tx_cb(0);
		}
	}

	k_free(p_elem);
}

/**@brief Send uart data in asynchronized mode
 *
 * @param p_device 		pointer of UART device
 * @param p_fifo		pointer of the fifo
 * @param p_data		pointer of data to be sent
 * @param length		length of data to be sent
 *
 * @return 0			success
 * @return -1			failed
 */
static int uart_send_asyn(struct device* p_device, struct k_fifo* p_fifo,
		const u8_t *p_data, u16_t length)
{
	uint16_t offset;
	fifo_elem_t* p_elem;

	__ASSERT_NO_MSG(p_data != NULL);
	__ASSERT_NO_MSG(length > 0);

	offset = 0;
	while (true) {
		p_elem = k_malloc(sizeof(fifo_elem_t));

		if (!p_elem) {
			LOG_ERR("Insufficient memory");
			return -ENOSR;
		}

		p_elem->length = MIN((length - offset), FIFO_ELEM_LEN);
		memcpy(p_elem->p_data, &p_data[offset], p_elem->length);

		offset += p_elem->length;
		k_fifo_put(p_fifo, p_elem);

		if (offset >= length) {
			break;
		}
	}

	uart_irq_tx_enable(p_device);

	return 0;
}

/**@brief Send uart data in synchronized mode
 *
 * @param p_device 		pointer of UART device
 * @param p_data		pointer of data to be sent
 * @param length		length of data to be sent
 *
 * @return 0			success
 * @return -1			failed
 */
int uart_send_sync(struct device* p_device, const u8_t* p_data, 
		u16_t length)
{
	if (!p_device) {
		return -1;
	}

	while (length--) {
		uart_poll_out(p_device, *p_data++);
	}

	return 0;
}

/**@brief Send uart data
 *
 * @param p_data		pointer of data to be sent
 * @param length		length of data to be sent
 *
 * @return 0			success
 * @return -1			failed
 */
int app_uart_send(const u8_t *p_data, u16_t length)
{
	if (m_device == NULL) {
		return -1;
	}

	if (p_data != NULL && length != 0) {
		uart_send_asyn(m_device, &m_tx_fifo, p_data, length);
	}

	return 0;
}

/** @brief UART interrupt handler */
static void uart_isr(struct device *p_device)
{
	uart_irq_update(p_device);

	if (uart_irq_is_pending(p_device)) {

		if (uart_irq_rx_ready(p_device)) {
			rx_handler(p_device, &m_rx_buff);
		}
		if (uart_irq_tx_ready(p_device)) {
			tx_handler(p_device, &m_tx_fifo);
		}
	}
}

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
		u16_t rx_max_len)
{
	u8_t byte;

	if (p_device == NULL) {
		return -ENXIO;
	}

	m_device = p_device;

	m_rx_buff.p_data = p_rx_buff;
	m_rx_buff.max_len = rx_max_len;
	m_rx_buff.length = 0;

	m_rx_cb = NULL;
	m_tx_cb = NULL;

	uart_irq_rx_disable(p_device);
	uart_irq_tx_disable(p_device);

	/* Drain the rx buffer */
	while (uart_fifo_read(p_device, &byte, 1)) {
		continue;
	}

	uart_irq_callback_set(p_device, uart_isr);
	uart_irq_rx_enable(p_device);

	return 0;
}

/**@brief Un-initialize app_uart module */
void app_uart_uninit(void)
{
	fifo_elem_t* p_elem;

	if (m_device == NULL) {
		return;
	}

	uart_irq_rx_disable(m_device);
	uart_irq_tx_disable(m_device);

	app_uart_rx_cb_set(NULL);
	app_uart_tx_cb_set(NULL);

	while (true) {
		p_elem = k_fifo_get(&m_tx_fifo, K_NO_WAIT);
		if (p_elem) {
			k_free(p_elem);
		}
		else {
			break;
		}
	}
}

/**@brief Rest a buffer data
 *
 * @param[in] p_buff: pointer of the buffer
 *
 * @return n/a
 */
void uart_buffer_reset(uart_buff_t* p_buff)
{
	// TODO: review
//	memset(p_buff->p_data, 0, p_buff->max_len);
	p_buff->length = 0;
}

/**@brief Reset rx buffer */
void app_uart_rx_reset(void)
{
	uart_buffer_reset(&m_rx_buff);
}

/**@brief Set rx data ready event callback */
void app_uart_rx_cb_set(uart_rx_cb cb)
{
	m_rx_cb = cb;
}

/**@brief Set tx empty event callback */
void app_uart_tx_cb_set(uart_tx_cb cb)
{
	m_tx_cb = cb;
}
