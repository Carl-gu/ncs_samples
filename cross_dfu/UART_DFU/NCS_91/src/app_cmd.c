#include <string.h>
#include <zephyr.h>
#include <device.h>
#include <sys/byteorder.h>
#include <sys/crc.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(cmd, 3);

#include "app_cmd.h"
#include "app_uart.h"

#define SKIP_CRC_CHECK                  false            /* Switch option to skip CRC check or not */

#define CMD_PACKET_LENGTH               1040
#define CMD_POOL_DEPTH                  2
#define CMD_CB_LIST_LEN                 20

#define crc16_compute(p_data, len)      crc16_itu_t(0x0, p_data, len)
#define uint16_decode(p_data)           sys_get_le16(p_data)
#define uint16_encode(value, p_data)    sys_put_le16(value, p_data)

/* Time between requset sent to response received is:
 * slave process request, slave send response data.
 * Host must set a long enough time to wait it, and
 * there should be a timer refresh feature, to avoid
 * timeout is triggered during UART is in active.
 *
 * Note, this value can't be small, some activities such 
 * as flash erase will cost several seconds
 */
#define WAIT_RSP_TIMEOUT                K_MSEC(10000)

 /* string names of each cmd state */
const static char* cmd_state_str[] = {
    "idle", "req_sending", "req_sent",
    "req_receiving", "req_received",
    "rsp_sending", "rsp_sent",
    "rsp_receiving", "rsp_received",
    "err_timeout", "err_sending",
    "err_receiving",
};

const static char* cmd_mode_str[] = {
    "idle", "host", "slave"
};

typedef struct
{
    uint8_t*   p_data;                 /* Pointer of data */
    uint16_t   length;                 /* Data length of the buffer */
    uint16_t   offset;                 /* Datat offset of the buffer */
} buffer_t;

static uint8_t          m_rx_pool[CMD_PACKET_LENGTH];
static uint8_t          m_tx_pool[CMD_PACKET_LENGTH];

static cmd_context_t    m_cmd_ctx;

static buffer_t         m_rx_buff;
static buffer_t         m_tx_buff;

static cmd_event_cb_t   m_event_cb;

static struct k_work    wk_proc_req;               /* A k_work to process request */
static struct k_work    wk_proc_rsp;               /* A k_work to process response */

static cmd_cb_t m_cb_list[CMD_CB_LIST_LEN];    /* User cmd list */

/* Function declaration */
static int req_cb_ping(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond);
static void rsp_cb_ping(uint8_t* p_rsp, uint16_t rsp_len);
static void rsp_cb_raw_data(uint8_t* p_rsp, uint16_t rsp_len);
static int req_cb_raw_data(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond);
static void state_handler(cmd_context_t* p_cmd_ctx);
static int buff_to_cmd(buffer_t* p_buff, app_cmd_t* p_cmd);
static int app_cmd_respond(uint8_t* p_data, uint16_t length);
static void tmr_rsp_timeout_handler(struct k_timer* timer);

/* Timer for waiting response */
K_TIMER_DEFINE(tmr_wait_rsp, tmr_rsp_timeout_handler, NULL);


/**@brief Dummy function of cmd event callback */
static void event_cb_dummy(cmd_event_t* p_event) {;}

/**@brief Check the crc of data sequence with a target crc
 *
 * @param[in] p_data: pointer of the data
 * @param[in] length: data length
 * @param[in] crc_target: target crc
 *
 * @return true: crc matches
 *         false: crc doesn't match
 */
static bool crc16_check(uint8_t* p_data, uint16_t length, uint16_t crc_target)
{
#ifdef SKIP_CRC_CHECK
    if (SKIP_CRC_CHECK) {
        return true;
    }
#endif

    return crc16_compute(p_data, length) == crc_target;
}

/**@brief Allocate a buffer from the pool
 *
 * @note There may be better solution
 *
 * @param[in] p_pool: pointer of data pool
 * @param[in] p_buff: pointer of buffer
 */
static void buff_alloc(uint8_t* p_pool, buffer_t* p_buff)
{
    __ASSERT_NO_MSG(p_pool != NULL);
    __ASSERT_NO_MSG(p_buff != NULL);

    p_buff->p_data = p_pool;
    p_buff->length = 0;
    p_buff->offset = 0;
}

/**@brief Set cmd state */
static void state_set(cmd_context_t* p_cmd_ctx, cmd_state_t new_state)
{
    cmd_state_t old_state = p_cmd_ctx->state;

    if (old_state != new_state) {
        p_cmd_ctx->state = new_state;

        // Always don't enable this print because it may cause 
        // UART data loss when receiving
        LOG_DBG("State: %s -> %s",      \
            cmd_state_str[old_state],   \
            cmd_state_str[new_state]);

        state_handler(p_cmd_ctx);
    }
}

/**@brief Set a new cmd mode */
static void mode_set(cmd_context_t* p_cmd_ctx, cmd_mode_t new_mode)
{
    cmd_mode_t old_mode = p_cmd_ctx->mode;
    if (old_mode != new_mode) {
        p_cmd_ctx->mode = new_mode;

        // Always don't enable this print because it may cause 
        // UART data loss when receiving
        LOG_DBG("Mode: %s -> %s", \
            cmd_mode_str[old_mode], \
            cmd_mode_str[new_mode]);
    }
}

/**@brief Get current cmd mode */
static cmd_mode_t mode_get(cmd_context_t* p_cmd_ctx)
{
    return p_cmd_ctx->mode;
}

/**@brief Get current cmd state */
static cmd_state_t state_get(cmd_context_t* p_cmd_ctx)
{
    return p_cmd_ctx->state;
}

/**@brief State change handler */
static void state_handler(cmd_context_t* p_cmd_ctx)
{
    cmd_state_t state = p_cmd_ctx->state;

    switch (state) {

    case CMD_STATE_IDLE:
        app_uart_rx_reset();
        mode_set(p_cmd_ctx, CMD_MODE_IDLE);
        LOG_DBG("---------\n");
        break;

    case CMD_STATE_REQ_SENDING:
        mode_set(p_cmd_ctx, CMD_MODE_HOST);
        break;

    case CMD_STATE_REQ_SENT:
        /* Issue an one shot timer */
        k_timer_start(&tmr_wait_rsp, WAIT_RSP_TIMEOUT, K_NO_WAIT);
        break;

    case CMD_STATE_RSP_RECEIVING:
        /* TODO: Refresh timer count, to avoid
         * timeout during UART receiving. Now I 
         * set a very long timeout value so it's 
         * safe. */
        break;

    case CMD_STATE_RSP_RECEIVED:
        k_timer_stop(&tmr_wait_rsp);
        k_work_submit(&wk_proc_rsp);
        break;

    case CMD_STATE_REQ_RECEIVING:
        mode_set(p_cmd_ctx, CMD_MODE_SLAVE);
        break;

    case CMD_STATE_REQ_RECEIVED:
        k_work_submit(&wk_proc_req);
        break;

    case CMD_STATE_ERR_TIMEOUT:
        state_set(p_cmd_ctx, CMD_STATE_IDLE);
        break;

    case CMD_STATE_ERR_RECEIVE:
        if (mode_get(&m_cmd_ctx) != CMD_MODE_HOST) {
            state_set(p_cmd_ctx, CMD_STATE_IDLE);
        }
        break;

    case CMD_STATE_RSP_SENT:
    case CMD_STATE_ERR_SEND:
        state_set(p_cmd_ctx, CMD_STATE_IDLE);
        break;

    case CMD_STATE_RSP_SENDING:
    default:
        /* Do nothing */
        break;
    }
}

/**@brief Handler for sending cmd start */
static void on_cmd_send_start(void)
{
    LOG_DBG("%s", __func__);

    if (mode_get(&m_cmd_ctx) == CMD_MODE_SLAVE) {
        state_set(&m_cmd_ctx, CMD_STATE_RSP_SENDING);
    }
    else {
        state_set(&m_cmd_ctx, CMD_STATE_REQ_SENDING);
    }
}

/**@brief Handler for sending cmd complete */
static void on_cmd_send_complete(void)
{
    LOG_DBG("%s", __func__);

    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST) {
        state_set(&m_cmd_ctx, CMD_STATE_REQ_SENT);
    }
    else if (mode_get(&m_cmd_ctx) == CMD_MODE_SLAVE) {
        state_set(&m_cmd_ctx, CMD_STATE_RSP_SENT);
    }
    else {
        LOG_ERR("Should not come here");
    }
}

/**@brief Handler for sending cmd error */
static void on_cmd_send_error(void)
{
    LOG_ERR("%s", __func__);

    state_set(&m_cmd_ctx, CMD_STATE_ERR_SEND);
}

/**@brief Handler for receiving cmd start */
static void on_cmd_receive_start(void)
{
    LOG_DBG("%s", __func__);

    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST) {
        state_set(&m_cmd_ctx, CMD_STATE_RSP_RECEIVING);
    }
    else {
        state_set(&m_cmd_ctx, CMD_STATE_REQ_RECEIVING);
    }
}

/**@brief Handler for receiving cmd complete */
static void on_cmd_receive_complete(void)
{
    LOG_DBG("%s", __func__);

    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST) {
        state_set(&m_cmd_ctx, CMD_STATE_RSP_RECEIVED);
    }
    else if (mode_get(&m_cmd_ctx) == CMD_MODE_SLAVE) {
        state_set(&m_cmd_ctx, CMD_STATE_REQ_RECEIVED);
    }
    else {
        LOG_WRN("Should not come here");
    }
}

/**@brief Handler for receiving cmd error */
static void on_cmd_receive_error(void)
{
    LOG_ERR("%s", __func__);

    state_set(&m_cmd_ctx, CMD_STATE_ERR_RECEIVE);
}

/**@brief Get element count of cmd list */
int cmd_cb_cnt(void)
{
    for (int i = 0; i < CMD_CB_LIST_LEN; i++) {
        if (m_cb_list[i].op_code == 0) {
            return i;
        }
    }

    return CMD_CB_LIST_LEN;
}

/**@brief Get element of cmd list by op code 
 *
 * @param[in] op_code: op code of cmd
 * @param[out] p_cmd_cb: pointer of cmd callback, it can be NULL
 *
 * @return -1: not found
 *         others(>=0): index of the cmd callback
 */
static int cmd_cb_get(uint8_t op_code, cmd_cb_t* p_cmd_cb)
{
    int rc;
    int count;

    rc = -1;
    count = cmd_cb_cnt();

    for (int i = 0; i < count; i++) {
        if (m_cb_list[i].op_code == op_code) {
            if (p_cmd_cb != NULL) {
                p_cmd_cb->proc_req = m_cb_list[i].proc_req;
                p_cmd_cb->proc_rsp = m_cb_list[i].proc_rsp;
            }

            rc = 0;
            break;
        }
    }

    return rc;
}

/**@brief Add a cmd to the list
 *
 * @param[in] op_code: op code of cmd
 * @param[in] req_cb: request callback function
 * @param[in] rsp_cb: response callback function
 *
 * @return 0: success
 * @return -1: cmd list is full
 * @return -2: op code is existed
 */
int app_cmd_add(uint8_t op_code, req_cb_t req_cb, rsp_cb_t rsp_cb)
{
    cmd_cb_t callback;
    int count;

    count = cmd_cb_cnt();
    if (count == CMD_CB_LIST_LEN) {
        LOG_ERR("Cmd list is full");
        return -1;
    }

    if (cmd_cb_get(op_code, NULL) == 0) {
        LOG_WRN("This op code is existed");
        return -2;
    }

    memset(&callback, 0, sizeof(cmd_cb_t));
    callback.op_code = op_code;
    callback.proc_req = req_cb;
    callback.proc_rsp = rsp_cb;

    memcpy(&m_cb_list[count], &callback, sizeof(cmd_cb_t));

    return 0;
}

/**@brief Handler for waiting response timeout */
static void tmr_rsp_timeout_handler(struct k_timer* timer)
{
    LOG_DBG("%s", __func__);

    uint32_t  err_code;
    app_cmd_t cmd;
    cmd_cb_t  cmd_cb;
    cmd_event_t event;

    err_code = buff_to_cmd(&m_tx_buff, &cmd);
    if (err_code != 0) {
        LOG_ERR("Buffer error");
        return;
    }

    err_code = cmd_cb_get(cmd.op_code, &cmd_cb);
    if (err_code == 0) {
        uint8_t p_rsp[] = CMD_RSP_TIMEOUT;

        if (cmd_cb.proc_rsp) {
            cmd_cb.proc_rsp(p_rsp, sizeof(p_rsp));
        }

        event.op_code = cmd.op_code;
        event.p_data = p_rsp;
        event.length = sizeof(p_rsp);
        event.timeout = true;

        m_event_cb(&event);
    }
    else {
        // Should not come here
        LOG_ERR("op is unregisterd(wait rsp)");
    }

    state_set(&m_cmd_ctx, CMD_STATE_IDLE);
}

/**@brief Handler for processing request */
static void wk_proc_req_handler(struct k_work* unused)
{
    LOG_DBG("%s", __func__);

    int err_code;
    app_cmd_t cmd;
    cmd_cb_t  cmd_cb;
    cmd_event_t event;

    err_code = buff_to_cmd(&m_rx_buff, &cmd);
    if (err_code != 0) {
        LOG_ERR("Buffer error");
        return;
    }

    err_code = cmd_cb_get(cmd.op_code, &cmd_cb);
    if (err_code == 0) {
        if (cmd_cb.proc_req) {
            cmd_cb.proc_req(cmd.p_data, cmd.length, app_cmd_respond);
        }
        else {
            app_cmd_respond(NULL, 0);
        }

        event.op_code = cmd.op_code;
        event.p_data = cmd.p_data;
        event.length = cmd.length;
        event.timeout = false;

        m_event_cb(&event);
    }
    else {
        LOG_ERR("op is unregisterd(proc req)");
        uint8_t p_rsp[] = CMD_RSP_UNREG;

        app_cmd_respond(p_rsp, sizeof(p_rsp));
    }
}

/**@brief Handler for processing response */
static void wk_proc_rsp_handler(struct k_work* unused)
{
    LOG_DBG("%s", __func__);

    int err_code;
    app_cmd_t cmd;
    cmd_cb_t  cmd_cb;
    cmd_event_t event;

    state_set(&m_cmd_ctx, CMD_STATE_IDLE);

    err_code = buff_to_cmd(&m_rx_buff, &cmd);
    if (err_code != 0) {
        LOG_ERR("Buffer error");
        return;
    }

    err_code = cmd_cb_get(cmd.op_code, &cmd_cb);
    if (err_code == 0) {
        if (cmd_cb.proc_rsp) {
            cmd_cb.proc_rsp(cmd.p_data, cmd.length);
        }

        event.op_code = cmd.op_code;
        event.p_data = cmd.p_data;
        event.length = cmd.length;
        event.timeout = false;

        m_event_cb(&event);
    }
    else {
        // Should not come here
        LOG_ERR("op is unregisterd(proc rsp): %d", cmd.op_code);
    }
}

/**@brief Get cmd length from cmd data buffer
 *
 * @param[in] p_buff: pointer of cmd data buffer
 *
 * @return 0: not found
 *         others: cmd length
 */
static uint16_t cmd_len_get(buffer_t* p_buff)
{
    uint8_t* p_data;
    uint16_t cmd_len;

    cmd_len = 0;
    p_data = p_buff->p_data;
    if (p_buff->length == CMD_FMT_SIZE_START + CMD_FMT_SIZE_LEN) {
        cmd_len = CMD_FMT_OFFSET_OPCODE + CMD_FMT_SIZE_CRC +
            uint16_decode(&p_data[CMD_FMT_OFFSET_LEN]);
    }

    return cmd_len;
}

/**@brief Validate format of a cmd
 *
 * @param[in] p_buff: pointer of cmd data buffer
 * @param[in] op_code: op code of the request in host mode,
 *  in slave mode, it's not used
 *
 * @return 0: format is ok
 *         -1: format is wrong
 */
static int format_check(buffer_t* p_buff, uint8_t op_code, cmd_mode_t mode)
{
    uint16_t cmd_len;
    uint16_t cmd_crc;
    bool crc_ok;

    uint8_t* p_data = p_buff->p_data;
    uint16_t length = p_buff->length;

    // Check start flag
    if (mode == CMD_MODE_HOST &&
        p_data[CMD_FMT_OFFSET_START] != CMD_FMT_START_RSP) {
        LOG_ERR("Invalid cmd format: start(host)");
        return -1;
    }
    else if (mode == CMD_MODE_SLAVE &&
        p_data[CMD_FMT_OFFSET_START] != CMD_FMT_START_REQ) {
        LOG_ERR("Invalid cmd format: start(slave)");
        return -1;
    }

    // Check length
    cmd_len = uint16_decode(&p_data[CMD_FMT_OFFSET_LEN]) +
        CMD_FMT_OFFSET_OPCODE + CMD_FMT_SIZE_CRC;
    if (length != cmd_len) {
        LOG_ERR("Invalid cmd format: length");
        return -1;
    }

    // Check op code
    if (mode == CMD_MODE_HOST &&
        p_data[CMD_FMT_OFFSET_OPCODE] != op_code) {
        LOG_ERR("Invalid cmd format: op code");
        return -1;
    }

    // Check CRC
    cmd_crc = uint16_decode(&p_data[cmd_len - CMD_FMT_SIZE_CRC]);
    crc_ok = crc16_check(&p_data[CMD_FMT_OFFSET_LEN],
        cmd_len - CMD_FMT_SIZE_START - CMD_FMT_SIZE_CRC,
        cmd_crc);
    if (!crc_ok) {
        LOG_ERR("Invalid cmd format: crc");
        return -1;
    }

    return 0;
}
/**@brief Get op code from a cmd data buffer */
static int op_code_get(buffer_t* p_buff, uint8_t* op_code)
{
    if (p_buff == NULL || p_buff->p_data == NULL) {
        return -1;
    }

    // Assume it's a valid buffer, so skip format check
    *op_code = p_buff->p_data[CMD_FMT_OFFSET_OPCODE];

    return 0;
}

/**@brief Build a data buffer from a cmd structure
 *
 * @note It will allocate memory for buffer
 *
 * @param[in] p_cmd: pointer of cmd
 * @param[out] p_buff: cmd data buffer
 *
 * @return 0: ok
 */
static int cmd_to_buff(app_cmd_t* p_cmd, buffer_t* p_buff)
{
    __ASSERT_NO_MSG(p_cmd != NULL);

    uint16_t crc16;
    uint16_t pdu_len;
    uint8_t* p_packet;
    uint16_t pkt_len;

    buff_alloc(m_tx_pool, p_buff);

    p_packet = p_buff->p_data;
    pdu_len = p_cmd->length;

    /* Start flag */
    p_packet[CMD_FMT_OFFSET_START] =
        (p_cmd->type == CMD_TYPE_RESPONSE) ?
        CMD_FMT_START_RSP :
        CMD_FMT_START_REQ;

    /* Length */
    uint16_encode(CMD_FMT_SIZE_OPCODE + pdu_len,
        &p_packet[CMD_FMT_OFFSET_LEN]);

    /* OP code */
    p_packet[CMD_FMT_OFFSET_OPCODE] = p_cmd->op_code;

    /* PDU */
    if (pdu_len > 0 && p_cmd->p_data != NULL) {
        memcpy(&p_packet[CMD_FMT_OFFSET_PDU], p_cmd->p_data, pdu_len);
    }

    /* CRC: crc16 init value must be 0 */
    crc16 = crc16_compute(&p_packet[CMD_FMT_OFFSET_LEN],
        CMD_FMT_SIZE_LEN + CMD_FMT_SIZE_OPCODE + pdu_len);
    uint16_encode(crc16, &p_packet[CMD_FMT_OFFSET_PDU + pdu_len]);

    /* Packet length */
    pkt_len = CMD_FMT_OFFSET_PDU + pdu_len + CMD_FMT_SIZE_CRC;
    pkt_len = MIN(pkt_len, CMD_PACKET_LENGTH);

    p_buff->length = pkt_len;

    return 0;
}

/**@brief Get a cmd from a data buff 
 * 
 * @param[in] p_buff: cmd data buffer
 * @param[out] p_cmd: pointer of cmd
 *
 * @return 0: ok
 *         -1: pointer is NULL
 */
static int buff_to_cmd(buffer_t* p_buff, app_cmd_t* p_cmd)
{
    if (p_buff->p_data == NULL || p_cmd == NULL) {
        return -1;
    }

    uint8_t* p_data;
    uint16_t op_pdu_len;

    p_data = p_buff->p_data;
    op_pdu_len = uint16_decode(&p_data[CMD_FMT_OFFSET_LEN]);

    p_cmd->type = p_data[CMD_FMT_OFFSET_START];
    p_cmd->op_code = p_data[CMD_FMT_OFFSET_OPCODE];
    p_cmd->length = op_pdu_len - CMD_FMT_SIZE_OPCODE;
    p_cmd->p_data = &p_data[CMD_FMT_OFFSET_PDU];

    return 0;
}

/**@brief Send a cmd by UART */
static int cmd_send(app_cmd_t* p_cmd)
{
    on_cmd_send_start();

    cmd_to_buff(p_cmd, &m_tx_buff);
    return app_uart_send(m_tx_buff.p_data, m_tx_buff.length);
}

/**@brief Send a response */
static int app_cmd_respond(uint8_t* p_data, uint16_t length)
{
    int err_code;
    uint8_t  op_code;
    app_cmd_t cmd;

    if (state_get(&m_cmd_ctx) != CMD_STATE_REQ_RECEIVED &&
        state_get(&m_cmd_ctx) != CMD_STATE_REQ_SENT) {
        LOG_ERR("Invalid state for response:%d", state_get(&m_cmd_ctx));
        return -1;
    }

    err_code = op_code_get(&m_rx_buff, &op_code);
    if (err_code != 0) {
        LOG_ERR("rx buffer is reset too early");
        on_cmd_send_error();
        return err_code;
    }

    cmd.type = CMD_TYPE_RESPONSE;
    cmd.op_code = op_code;
    cmd.p_data = p_data;
    cmd.length = length;

    return cmd_send(&cmd);
}

/**@brief Send a request cmd
 *
 * @param[in] op_code: cmd op code
 * @param[in] p_data: pointer of cmd data
 * @param[in] length: length of cmd data
 *
 * @return 0: success
 * @return -1: error
 */
uint32_t app_cmd_request(uint8_t op_code, uint8_t* p_data, uint16_t length)
{
    if (mode_get(&m_cmd_ctx) != CMD_MODE_IDLE) {
        return -1;
    }

    app_cmd_t cmd =
    {
        .type = CMD_TYPE_REQUEST,
        .op_code = op_code,
        .p_data = p_data,
        .length = length,
    };

    mode_set(&m_cmd_ctx, CMD_MODE_HOST);

    return cmd_send(&cmd);
}

/**@brief Register a callback for request and response
 *
 * @note This functions is duplicated to callback functions
 * (req_proc, rsp_proc), but it's useful 
 *
 * @todo Check to remove rsp_proc
 */
void app_cmd_event_cb_register(cmd_event_cb_t event_cb)
{
    if (event_cb != NULL) {
        m_event_cb = event_cb;
    }
}

/**@brief Handler for UART tx complete */
static void on_uart_tx_empty(int event)
{
    LOG_DBG("%s", __func__);
    if (event == 0) {
        on_cmd_send_complete();
    }
    else {
        on_cmd_send_error();
    }
}

/**@brief Handler for UART rx data ready */
static void on_uart_rx_ready(uint8_t* p_data, uint16_t length)
{
    int err_code;
    uint8_t  req_op_code;
    static bool rx_started;
    static uint16_t cmd_len;

    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST) {
        if (m_cmd_ctx.state != CMD_STATE_REQ_SENT &&
            m_cmd_ctx.state != CMD_STATE_RSP_RECEIVING) {
            LOG_WRN("Invalid state for rx(host)");
            return;
        }
    }

    if (mode_get(&m_cmd_ctx) == CMD_MODE_SLAVE) {
        if (m_cmd_ctx.state != CMD_STATE_REQ_RECEIVING) {
            LOG_WRN("Invalid state for rx(slave)");
            return;
        }
    }

    if (!rx_started) {
        rx_started = true;
        buff_alloc(m_rx_pool, &m_rx_buff);
        cmd_len = 0;
        on_cmd_receive_start();
    }

    m_rx_buff.length = length;

    if (cmd_len == 0) {

        cmd_len = cmd_len_get(&m_rx_buff);

        if (cmd_len > CMD_PACKET_LENGTH) {
            on_cmd_receive_error();
        }

        if (cmd_len == 0) {
            return;
        }
    }

    if (m_rx_buff.length < cmd_len) {
        return;
    }

    LOG_HEXDUMP_DBG(m_rx_buff.p_data, m_rx_buff.length, "RX");

    req_op_code = 0;
    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST) {
        op_code_get(&m_tx_buff, &req_op_code);
    }
    err_code = format_check(&m_rx_buff, req_op_code, mode_get(&m_cmd_ctx));

    if (err_code == 0) {
        on_cmd_receive_complete();
    }
    // Invalid format
    else if (err_code == -1) {
        on_cmd_receive_error();
    }
    // On going receiving
    else {
        LOG_ERR("Should not come here(%d)", err_code);
        on_cmd_receive_error();
    }

    rx_started = false;
}

/**@brief Initialize app cmd module
 *
 * @param[in] p_device: pointer of UART device
 * @param[in] evt_cb: callback of cmd event
 *
 * @return 0: success
 * @return neg: error
 */
int app_cmd_init(struct device* p_device)
{
    int rc;
    static bool initialized = false;

    rc = app_uart_init(p_device, m_rx_pool, CMD_PACKET_LENGTH);
    if (rc != 0) {
        LOG_ERR("UART device init failed");
        return -ENXIO;
    }

    state_set(&m_cmd_ctx, CMD_STATE_IDLE);

    memset(&m_cmd_ctx.cmd, 0, sizeof(app_cmd_t));

    memset(&m_rx_pool, 0, sizeof(m_rx_pool));
    memset(&m_tx_pool, 0, sizeof(m_tx_pool));
    memset(&m_rx_buff, 0, sizeof(buffer_t));
    memset(&m_tx_buff, 0, sizeof(buffer_t));

    app_uart_rx_cb_set(on_uart_rx_ready);
    app_uart_tx_cb_set(on_uart_tx_empty);

    if (!initialized) {
        initialized = true;

        m_event_cb = event_cb_dummy;

        memset(&m_cb_list, 0, sizeof(m_cb_list));

        k_work_init(&wk_proc_req, wk_proc_req_handler);
        k_work_init(&wk_proc_rsp, wk_proc_rsp_handler);

        app_cmd_add(CMD_OP_PING, req_cb_ping, rsp_cb_ping);
        app_cmd_add(CMD_OP_RAW_DATA, req_cb_raw_data, rsp_cb_raw_data);
    }

    return 0;
}

/**@brief Un-initialize app cmd module */
void app_cmd_uninit(void)
{
    app_uart_uninit();

    k_timer_stop(&tmr_wait_rsp);
}

/**@brief Callback function for ping request. */
static int req_cb_ping(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond)
{
    LOG_INF("%s", __func__);

    char* rsp = "ok";
    respond(rsp, strlen(rsp));

    return 0;
}

/**@brief Callback function for ping response. */
static void rsp_cb_ping(uint8_t* p_rsp, uint16_t rsp_len)
{
    LOG_INF("%s", __func__);
}

/**@brief Callback function for raw_data request. */
static int req_cb_raw_data(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond)
{
    LOG_DBG("%s", __func__);
    char* rsp = "ok";

    // TODO: fill user function to handle raw data
    if (req_len > 0) {
        LOG_HEXDUMP_INF(p_req, MIN(req_len, 8), "raw data:");
    }

    respond(rsp, strlen(rsp));

    return 0;
}

/**@brief Callback function for mtu_get response. */
static void rsp_cb_raw_data(uint8_t* p_rsp, uint16_t rsp_len)
{
    LOG_DBG("%s", __func__);

    if (rsp_len > 0) {
        LOG_HEXDUMP_INF(p_rsp, MIN(rsp_len, 8), "raw data:");
    }
}

/**@brief Send a ping request */
void cmd_request_ping(void)
{
    app_cmd_request(CMD_OP_PING, "yq", 2);
}

/**@brief Send a raw data request */
void cmd_request_raw_data(uint8_t* p_data, uint16_t length)
{
    app_cmd_request(CMD_OP_RAW_DATA, p_data, length);
}
