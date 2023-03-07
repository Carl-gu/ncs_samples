#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "app_uart.h"
#include "crc16.h"
#include "nrf_assert.h"
#include "app_util.h"
#include "app_timer.h"
#include "ble.h"
#include "ble_nus.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_soc.h"
#include "app_scheduler.h"

#define NRF_LOG_MODULE_NAME dfu_helper
#define NRF_LOG_LEVEL 3
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
NRF_LOG_MODULE_REGISTER();

#include "app_cmd.h"
#include "dfu_helper.h"

#define IMG_PACKET_SIZE          128
#define PKTS_PER_BLOCK           8
#define IMG_BLOCK_SIZE           (IMG_PACKET_SIZE * PKTS_PER_BLOCK)

#define CMD_WRITE_ADDR_SIZE      4
#define CMD_WRITE_LEN_SIZE       4
#define CMD_WRITE_HEADER_SIZE    (CMD_WRITE_ADDR_SIZE + CMD_WRITE_LEN_SIZE)
#define CMD_BUFFER_SIZE          (IMG_BLOCK_SIZE + CMD_WRITE_HEADER_SIZE)       // address + length + image block data

#define REQ_START_DFU            0x00
#define REQ_GET_IMG_SIZE         0x01       // image size
#define REQ_GET_IMG_DATA         0x02       // a packet of data

#define BLE_NOTIFY_DELAY         APP_TIMER_TICKS(5)
#define ENTER_BL_DELAY           APP_TIMER_TICKS(100)

/* GPREGRET macro is copied from nrf_bootloader_info.h */
#define BOOTLOADER_DFU_GPREGRET             (0xB0)      /**< Magic pattern written to GPREGRET register to signal between main app and DFU. The 3 lower bits are assumed to be used for signalling purposes.*/
#define BOOTLOADER_DFU_START_BIT_MASK       (0x01)      /**< Bit mask to signal from main application to enter DFU mode using a buttonless service. */
#define BOOTLOADER_DFU_START                (BOOTLOADER_DFU_GPREGRET | BOOTLOADER_DFU_START_BIT_MASK)

static uint8_t    m_cmd_buffer[CMD_BUFFER_SIZE];

static uint16_t   m_conn_handle;
static ble_nus_t* m_nus_handle;

static uint32_t   m_img_size = 100;
static uint32_t   m_img_offset = 0;

APP_TIMER_DEF(m_tmr_ble_notify);
APP_TIMER_DEF(m_tmr_enter_bootloader);

static uint32_t ble_send_req(uint8_t req);
static void on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context);

NRF_SDH_BLE_OBSERVER(dfu_helper_obs, BLE_NUS_BLE_OBSERVER_PRIO, on_ble_evt, NULL);

/**@brief Request to get flash info of nrf9160 device.
 */
uint32_t cmd_request_flash_info(void)
{
    NRF_LOG_INFO(__func__);

    return app_cmd_request(CMD_OP_FLASH_INFO, NULL, 0);
}

/**@brief Request to write flash of nrf9160 device.
 *
 * @param p_data: pointer of data to be written, containing:
 *                address[4], length[4], data[length - 8].
 * @param length: length of data.
 */
uint32_t cmd_request_flash_write(uint8_t* p_data, uint16_t length)
{
    NRF_LOG_INFO(__func__);

    return app_cmd_request(CMD_OP_FLASH_WRITE, p_data, length);
}

/**@brief Request to read flash of nrf9160 device.
 *
 * @param address: Address of data to be read.
 * @param length: length of data.
 */
uint32_t cmd_request_flash_read(uint32_t address, uint16_t length)
{
    NRF_LOG_INFO(__func__);

    uint8_t p_data[8];
    uint32_encode(address, &p_data[0]);
    uint32_encode(length, &p_data[4]);

    return app_cmd_request(CMD_OP_FLASH_READ, p_data, sizeof(p_data));
}

/**@brief Request to erase flash pages of nrf9160 device.
 *
 * @param address: Address of data to be read.
 * @param count: count of pages, 1 page = 4096 bytes.
 */
uint32_t cmd_request_flash_erase(uint32_t address, uint16_t count)
{
    NRF_LOG_INFO(__func__);

    uint8_t p_data[8];
    uint32_encode(address, &p_data[0]);
    uint32_encode(count, &p_data[4]);

    return app_cmd_request(CMD_OP_FLASH_ERASE, p_data, sizeof(p_data));
}

/**@brief Request to indicate image transferring done.
 */
uint32_t cmd_request_flash_done(void)
{
    NRF_LOG_INFO(__func__);

    uint8_t p_data[4];
    uint32_encode(m_img_size, &p_data[0]);

    return app_cmd_request(CMD_OP_FLASH_DONE, p_data, sizeof(p_data));
}

/**@brief Callback function for flash_info response.
 *
 * @param p_rsp: response contains: address[4], page_count[4],
 *               first blank page[4].
 */
static void rsp_cb_flash_info(uint8_t* p_rsp, uint16_t rsp_len)
{
    NRF_LOG_INFO(__func__);

    ASSERT(rsp_len == 12);

    uint32_t flash_addr;
    uint32_t page_count;
    uint32_t first_blank;

    flash_addr  = uint32_decode(&p_rsp[0]);
    page_count  = uint32_decode(&p_rsp[4]);
    first_blank = uint32_decode(&p_rsp[8]);

    NRF_LOG_INFO("Addr: 0x%08x, pages: %d, offset: %08x", flash_addr, page_count, first_blank);

    // The last page is reserved for mcuboot flag
    if (m_img_size > (page_count - 1) * 0x1000)
    {
        NRF_LOG_ERROR("Image size is too big.");
    }
    else
    {
        cmd_request_flash_erase(0, CEIL_DIV(m_img_size, 0x1000));
    }
}

/**@brief Callback function for flash erase response.
 *
 * @param p_rsp: response contains: "ok".
 */
static void rsp_cb_flash_erase(uint8_t* p_rsp, uint16_t rsp_len)
{
    NRF_LOG_INFO(__func__);

    uint8_t p_ok[] = CMD_RSP_OK;

    if (memcmp(p_rsp, p_ok, sizeof(p_ok)) == 0)
    {
        ble_send_req(REQ_GET_IMG_DATA);
    }
    else
    {
        NRF_LOG_ERROR("Timeout");
    }
}

/**@brief Callback function for flash write response.
 *
 * @param p_rsp: response contains: "ok".
 */
static void rsp_cb_flash_write(uint8_t* p_rsp, uint16_t rsp_len)
{
    NRF_LOG_INFO(__func__);

    if (m_img_offset == m_img_size)
    {
        NRF_LOG_INFO("Image is finished");
        m_img_offset = 0;

        cmd_request_flash_done();
    }
    else if (m_img_offset > 0)
    {
        ble_send_req(REQ_GET_IMG_DATA);
    }
}

/**@brief Callback function for flash done response.
 *
 * @param p_rsp: response contains: "ok".
 */
static void rsp_cb_flash_done(uint8_t* p_rsp, uint16_t rsp_len)
{
    NRF_LOG_INFO(__func__);
}

/**@brief Enter bootloader handler of app_timer.
 *
 * @details Copied from ble_dfu.c.
 */
static void tmr_enter_bootloader_handler(void * p_context)
{
    uint32_t err_code;

    NRF_LOG_INFO("Enter bootloader");

    err_code = sd_power_gpregret_clr(0, 0xffffffff);
    APP_ERROR_CHECK(err_code);

    err_code = sd_power_gpregret_set(0, BOOTLOADER_DFU_START);
    APP_ERROR_CHECK(err_code);

    // Signal that DFU mode is to be enter to the power management module
    nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_DFU);
}

/**@brief Callback function for entering bootloader request.
 */
static int req_cb_enter_bootloader(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond)
{
    NRF_LOG_INFO(__func__);

    uint8_t p_rsp[] = {'o', 'k'};
    respond(p_rsp, sizeof(p_rsp));

    app_timer_create(&m_tmr_enter_bootloader, APP_TIMER_MODE_SINGLE_SHOT, tmr_enter_bootloader_handler);
    app_timer_start(m_tmr_enter_bootloader, ENTER_BL_DELAY, NULL);

    return 0;
}

static int req_cb_ping_app(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond)
{
    NRF_LOG_INFO(__func__);

    uint8_t p_rsp[] = {'o', 'k'};
    respond(p_rsp, sizeof(p_rsp));

    return 0;
}

CMD_CALLBACK_REG(CMD_OP_FLASH_INFO, NULL, rsp_cb_flash_info);
CMD_CALLBACK_REG(CMD_OP_FLASH_WRITE, NULL, rsp_cb_flash_write);
CMD_CALLBACK_REG(CMD_OP_FLASH_ERASE, NULL, rsp_cb_flash_erase);
CMD_CALLBACK_REG(CMD_OP_FLASH_DONE, NULL, rsp_cb_flash_done);

CMD_CALLBACK_REG(CMD_OP_ENTER_BL, req_cb_enter_bootloader, NULL);
CMD_CALLBACK_REG(CMD_OP_PING_APP, req_cb_ping_app, NULL);

//---------------------------------------------------

/**@brief Handler of ble notifying.
 */
static void ble_send_req_handler(void * p_event_data, uint16_t event_size)
{
    uint8_t* p_byte = (uint8_t*)p_event_data;
    uint8_t  p_data[1] = {*p_byte};
    uint16_t len = 1;
    ble_nus_data_send(m_nus_handle, p_data, &len, m_conn_handle);
}

/**@brief Notify a request through ble notification to NUS central.
 */
static uint32_t ble_send_req(uint8_t req)
{
    static uint8_t data;
    data = req;

    app_sched_event_put(&data, sizeof(uint8_t), ble_send_req_handler);
}

/**@brief Handler of receiving NUS rx_handle data.
 *
 * @param[in] p_write_data: pointer to received write data.
 * @param[in] write_data_len: write data length.
 */
static void on_ble_write(const uint8_t* p_write_data, uint16_t write_data_len)
{
    ret_code_t err_code;
    static uint32_t buffer_len = 0;

    uint8_t  ble_data_flag = p_write_data[0];
    const uint8_t* p_img_data = &(p_write_data[1]);
    uint16_t img_data_len = write_data_len - 1;

    /* START_DFU content: flag[1] */
    if (ble_data_flag == REQ_START_DFU)
    {
        ble_send_req(REQ_GET_IMG_SIZE);
    }
    /* IMG_SIZE content: flag[1], image size[4] */
    else if (ble_data_flag == REQ_GET_IMG_SIZE)
    {
        m_img_size = uint32_decode(p_img_data);
        NRF_LOG_INFO("Image file size: %d", m_img_size);

        cmd_request_flash_info();
    }
    /* IMG_DATA content: flag[1], image data[IMG_UNIT_SIZE] */
    else if (ble_data_flag == REQ_GET_IMG_DATA)
    {
        // Reserve 8 bytes to store address[4] and img_data_len[4]
        memcpy(&m_cmd_buffer[8 + buffer_len], p_img_data, img_data_len);
        buffer_len += img_data_len;

        if (buffer_len == IMG_BLOCK_SIZE ||
            buffer_len == m_img_size - m_img_offset)
        {
            uint32_encode(m_img_offset, &m_cmd_buffer[0]);
            uint32_encode(buffer_len, &m_cmd_buffer[4]);

            cmd_request_flash_write(m_cmd_buffer, buffer_len + 8);

            m_img_offset += buffer_len;

            NRF_LOG_INFO("image(%d) %d/%d", buffer_len, m_img_offset, m_img_size);

            buffer_len = 0;
        }
    }
}

/**@brief Function for handling the BLE events.
 *
 * @param[in] p_ble_evt     Event received from the SoftDevice.
 * @param[in] p_context     Nordic UART Service structure.
 */
static void on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context)
{
    if (p_ble_evt == NULL)
    {
        return;
    }

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            break;

        case BLE_GATTS_EVT_WRITE:
        {
            ble_gatts_evt_write_t const * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

            // Only process rx_handle data
            if ((p_evt_write->handle == m_nus_handle->rx_handles.value_handle) &&
                (m_nus_handle->data_handler != NULL))
            {
                NRF_LOG_HEXDUMP_DEBUG(p_evt_write->data, MIN(p_evt_write->len, 8));
                on_ble_write(p_evt_write->data, p_evt_write->len);
            }
        } break;

        case BLE_GATTS_EVT_HVN_TX_COMPLETE:
        default:
            break;
    }
}

/**@brief Init dfu helper module.
 *
 * @param[in] p_nus: pointer of nus instance.
 */
void dfu_helper_init(ble_nus_t * p_nus)
{
    m_conn_handle = BLE_CONN_HANDLE_INVALID;
    m_nus_handle = p_nus;
}

