#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "app_uart.h"
#include "crc16.h"
#include "nrf_assert.h"
#include "app_util.h"
#include "nrf_section_iter.h"
#include "app_timer.h"
#include "app_scheduler.h"

#define NRF_LOG_MODULE_NAME cmd
#define NRF_LOG_LEVEL 3
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
NRF_LOG_MODULE_REGISTER();

#include "app_cmd.h"

#define SKIP_CRC_CHECK              true

#define CMD_PACKET_LENGTH           1040
#define CMD_POOL_DEPTH              2

/* Time between requset sent to response received is:
 * slave process request, slave send response data,
 * host must set a long enough time to wait it, and
 * there should be a timer refresh feature, to avoid
 * timeout is triggered during UART is in active  */
#define WAIT_RSP_TIMEOUT            APP_TIMER_TICKS(4000)

/* This delay is only used to jump out of interrupt
 * context. app_sched library is a good option too,
 * but app_sched_execute must be inserted in the main()
 * which I don't want to do.
 */
#define PROC_REQ_DELAY              APP_TIMER_TICKS(5)
#define PROC_RSP_DELAY              APP_TIMER_TICKS(5)

typedef struct
{
    uint8_t*   p_data;                 /* Pointer of data */
    uint16_t   length;                 /* Data length of the buffer */
    uint16_t   offset;
} buffer_t;

static cmd_context_t    m_cmd_ctx;

static uint8_t          m_rx_pool[CMD_PACKET_LENGTH];
static uint8_t          m_tx_pool[CMD_PACKET_LENGTH];

static buffer_t         m_rx_buff;
static buffer_t         m_tx_buff;

static cmd_event_cb_t   m_event_cb;

NRF_SECTION_DEF(cmd_cb_list, cmd_cb_t);

NRF_BALLOC_DEF(m_cmd_pool, CMD_PACKET_LENGTH, CMD_POOL_DEPTH);
APP_TIMER_DEF(m_tmr_wait_rsp);

static void proc_req_handler(void * p_event_data, uint16_t event_size);
static void proc_rsp_handler(void * p_event_data, uint16_t event_size);

/* string names of each cmd state */
const static char* cmd_state_str[] = 
{
    "idle", "req_sending", "req_sent",
    "req_receiving", "req_received",
    "rsp_sending", "rsp_sent",
    "rsp_receiving", "rsp_received",
    "err_timeout", "err_sending",
    "err_receiving",
};

const static char* cmd_mode_str[] = 
{
    "idle", "host", "slave"
};

static void state_handler(cmd_context_t* p_cmd_ctx);
static uint32_t buff_to_cmd(buffer_t* p_buff, app_cmd_t* p_cmd);
static uint32_t app_cmd_respond(uint8_t* p_data, uint16_t length);

// -----------------

void event_cb_dummy(cmd_event_t* p_event) {;}

static bool crc16_check(uint8_t* p_data, uint16_t length, uint16_t crc_target)
{
    uint16_t crc16;

#ifdef SKIP_CRC_CHECK
    if (SKIP_CRC_CHECK) 
	{
        return true;
    }
#endif

    crc16 = 0;     // Must set init value to 0
    crc16 = crc16_compute(p_data, length, &crc16);

    return crc16 == crc_target;
}

// There can be better solution for this function
static void buff_alloc(uint8_t* p_pool, buffer_t* p_buff)
{
    ASSERT(p_pool != NULL);
    ASSERT(p_buff != NULL);

    memset(p_pool, 0, CMD_PACKET_LENGTH);

    p_buff->p_data = p_pool;
    p_buff->length = 0;
    p_buff->offset = 0;
}

static void buff_free(buffer_t* p_buff)
{
    memset(p_buff, 0, sizeof(buffer_t));
    p_buff->p_data = NULL;
}

static void state_set(cmd_context_t* p_cmd_ctx, cmd_state_t new_state)
{
    cmd_state_t old_state = p_cmd_ctx->state;
    if (old_state != new_state) 
	{
		p_cmd_ctx->state = new_state;

		NRF_LOG_DEBUG("State: %s -> %s",     \
			cmd_state_str[old_state],        \
			cmd_state_str[new_state]);

		state_handler(p_cmd_ctx);
	}
}

static void mode_set(cmd_context_t* p_cmd_ctx, cmd_mode_t new_mode)
{
    cmd_mode_t old_mode = p_cmd_ctx->mode;
    if (old_mode != new_mode) 
	{
		p_cmd_ctx->mode = new_mode;

		NRF_LOG_DEBUG("Mode: %s -> %s",     \
			cmd_mode_str[old_mode],         \
			cmd_mode_str[new_mode]);
	}
}

static cmd_mode_t mode_get(cmd_context_t* p_cmd_ctx)
{
    return p_cmd_ctx->mode;
}

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
        mode_set(p_cmd_ctx, CMD_MODE_IDLE);
        break;

    case CMD_STATE_REQ_SENDING:
        mode_set(p_cmd_ctx, CMD_MODE_HOST);
        break;

    case CMD_STATE_REQ_SENT:
        app_timer_start(m_tmr_wait_rsp, WAIT_RSP_TIMEOUT, NULL);
        break;

    case CMD_STATE_RSP_RECEIVING:
        /* TODO: Refresh timer count */
        break;

    case CMD_STATE_RSP_RECEIVED:
        app_timer_stop(m_tmr_wait_rsp);
//        app_timer_start(m_tmr_proc_rsp, PROC_RSP_DELAY, NULL);
        app_sched_event_put(NULL, 0, proc_rsp_handler);

        break;

    case CMD_STATE_REQ_RECEIVING:
        mode_set(p_cmd_ctx, CMD_MODE_SLAVE);
        break;

    case CMD_STATE_REQ_RECEIVED:
        app_sched_event_put(NULL, 0, proc_req_handler);
//        app_timer_start(m_tmr_proc_req, PROC_REQ_DELAY, NULL);
        break;

    case CMD_STATE_ERR_TIMEOUT:
        state_set(p_cmd_ctx, CMD_STATE_IDLE);
        break;

    case CMD_STATE_ERR_RECEIVE:
        if (mode_get(&m_cmd_ctx) != CMD_MODE_HOST)
        {
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

static void on_cmd_send_start(void)
{
    NRF_LOG_DEBUG(__func__);

    if (mode_get(&m_cmd_ctx) == CMD_MODE_SLAVE)
    {
        state_set(&m_cmd_ctx, CMD_STATE_RSP_SENDING);
    }
    else
    {
        state_set(&m_cmd_ctx, CMD_STATE_REQ_SENDING);
    }
}

static void on_cmd_send_complete(void)
{
    NRF_LOG_DEBUG(__func__);

    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST)
    {
        state_set(&m_cmd_ctx, CMD_STATE_REQ_SENT);
    }
    else if (mode_get(&m_cmd_ctx) == CMD_MODE_SLAVE)
    {
        state_set(&m_cmd_ctx, CMD_STATE_RSP_SENT);
    }
    else
    {
        NRF_LOG_ERROR("Should not come here");
    }
}

static void on_cmd_send_error(void)
{
    NRF_LOG_DEBUG(__func__);

    state_set(&m_cmd_ctx, CMD_STATE_ERR_SEND);
}

static void on_cmd_receive_start(void)
{
    NRF_LOG_DEBUG(__func__);

    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST)
    {
        state_set(&m_cmd_ctx, CMD_STATE_RSP_RECEIVING);
    }
    else
    {
        state_set(&m_cmd_ctx, CMD_STATE_REQ_RECEIVING);
    }
}

static void on_cmd_receive_complete(void)
{
    NRF_LOG_DEBUG(__func__);

    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST)
    {
        state_set(&m_cmd_ctx, CMD_STATE_RSP_RECEIVED);
    }
    else if (mode_get(&m_cmd_ctx) == CMD_MODE_SLAVE)
    {
        state_set(&m_cmd_ctx, CMD_STATE_REQ_RECEIVED);
    }
    else
    {
        NRF_LOG_WARNING("Should not come here");
    }
}

static void on_cmd_receive_error(void)
{
    NRF_LOG_DEBUG(__func__);

    state_set(&m_cmd_ctx, CMD_STATE_ERR_RECEIVE);
}

// ------------------

static uint32_t cmd_cb_get(uint8_t op_code, cmd_cb_t* p_cmd_cb)
{
    uint32_t err_code;
    uint16_t count;

    err_code = NRF_ERROR_NOT_FOUND;
    count = NRF_SECTION_ITEM_COUNT(cmd_cb_list, cmd_cb_t);

    for (uint16_t i = 0; i < count; i++)
    {
        cmd_cb_t* p_cb = NRF_SECTION_ITEM_GET(cmd_cb_list, cmd_cb_t, i);
        if (op_code == p_cb->op_code)
        {
            p_cmd_cb->proc_req = p_cb->proc_req;
            p_cmd_cb->proc_rsp = p_cb->proc_rsp;

            err_code = NRF_SUCCESS;
            break;
        }
    }

    return err_code;
}

static void tmr_rsp_timeout_handler(void * p_context)
{
    NRF_LOG_DEBUG(__func__);

    uint32_t  err_code;
    app_cmd_t cmd;
    cmd_cb_t  cmd_cb;
    cmd_event_t event;

    err_code = buff_to_cmd(&m_tx_buff, &cmd);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Buffer error");
        return;
    }

    err_code = cmd_cb_get(cmd.op_code, &cmd_cb);
    if (err_code == NRF_SUCCESS)
    {
        uint8_t p_rsp[] = CMD_RSP_TIMEOUT;

        if (cmd_cb.proc_rsp)
        {
            cmd_cb.proc_rsp(p_rsp, sizeof(p_rsp));
        }

        event.op_code = cmd.op_code;
        event.p_data  = p_rsp;
        event.length  = sizeof(p_rsp);
        event.timeout = true;

        m_event_cb(&event);
    }
    else
    {
        // Should not come here
        NRF_LOG_ERROR("op is unregisterd(wait rsp)");
    }

    state_set(&m_cmd_ctx, CMD_STATE_IDLE);
}

static void proc_req_handler(void * p_event_data, uint16_t event_size)
{
    NRF_LOG_DEBUG(__func__);

    uint32_t err_code;
    app_cmd_t cmd;
    cmd_cb_t  cmd_cb;
    cmd_event_t event;

    err_code = buff_to_cmd(&m_rx_buff, &cmd);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Buffer error");
        return;
    }

    err_code = cmd_cb_get(cmd.op_code, &cmd_cb);
    if (err_code == NRF_SUCCESS)
    {
        if (cmd_cb.proc_req)
        {
            cmd_cb.proc_req(cmd.p_data, cmd.length, app_cmd_respond);
        }
        else
        {
            app_cmd_respond(NULL, 0);
        }

        event.op_code = cmd.op_code;
        event.p_data  = cmd.p_data;
        event.length  = cmd.length;
        event.timeout = false;

        m_event_cb(&event);
    }
    else
    {
        NRF_LOG_ERROR("op is unregisterd(proc req)");
        uint8_t p_rsp[] = CMD_RSP_UNREG;

        app_cmd_respond(p_rsp, sizeof(p_rsp));
    }
}

static void proc_rsp_handler(void * p_event_data, uint16_t event_size)
{
    NRF_LOG_DEBUG(__func__);

    uint32_t err_code;
    app_cmd_t cmd;
    cmd_cb_t  cmd_cb;
    cmd_event_t event;

    state_set(&m_cmd_ctx, CMD_STATE_IDLE);

    err_code = buff_to_cmd(&m_rx_buff, &cmd);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Buffer error");
        return;
    }

    err_code = cmd_cb_get(cmd.op_code, &cmd_cb);
    if (err_code == NRF_SUCCESS)
    {
        if (cmd_cb.proc_rsp)
        {
            cmd_cb.proc_rsp(cmd.p_data, cmd.length);
        }

        event.op_code = cmd.op_code;
        event.p_data  = cmd.p_data;
        event.length  = cmd.length;
        event.timeout = false;

        m_event_cb(&event);
    }
    else
    {
        // Should not come here
        NRF_LOG_ERROR("op is unregisterd(proc rsp)");
    }
}

static uint16_t cmd_len_get(buffer_t* p_buff)
{
    uint8_t* p_data;
    uint16_t cmd_len;

    p_data = p_buff->p_data;
    if (p_buff->length == CMD_FMT_SIZE_START + CMD_FMT_SIZE_LEN)
    {
        cmd_len = CMD_FMT_OFFSET_OPCODE + CMD_FMT_SIZE_CRC +
                uint16_decode(&p_data[CMD_FMT_OFFSET_LEN]);
        return cmd_len;
    }

    return 0;
}

static uint32_t format_check(buffer_t* p_buff, uint8_t op_code, cmd_mode_t mode)
{
    uint16_t cmd_len;
    uint16_t cmd_crc;
    bool crc_ok;

    uint8_t* p_data = p_buff->p_data;
    uint16_t length = p_buff->length;

    // Check start flag
    if (mode == CMD_MODE_HOST &&
            p_data[CMD_FMT_OFFSET_START] != CMD_FMT_START_RSP)
    {
        NRF_LOG_ERROR("Invalid cmd format: start");
        return NRF_ERROR_INVALID_DATA;
    }
    else if (mode == CMD_MODE_SLAVE &&
            p_data[CMD_FMT_OFFSET_START] != CMD_FMT_START_REQ)
    {
        NRF_LOG_ERROR("Invalid cmd format: start");
        return NRF_ERROR_INVALID_DATA;
    }

    // Check length
    cmd_len = uint16_decode(&p_data[CMD_FMT_OFFSET_LEN]) +
            CMD_FMT_OFFSET_OPCODE + CMD_FMT_SIZE_CRC;
    if (length != cmd_len)
    {
        NRF_LOG_ERROR("Invalid cmd format: length");
        return NRF_ERROR_INVALID_DATA;
    }

    // Check op code
    if (mode == CMD_MODE_HOST &&
            p_data[CMD_FMT_OFFSET_OPCODE] != op_code)
    {
        NRF_LOG_ERROR("Invalid cmd format: op code");
        return NRF_ERROR_INVALID_DATA;
    }

    // Check CRC
    cmd_crc = uint16_decode(&p_data[cmd_len - CMD_FMT_SIZE_CRC]);
    crc_ok = crc16_check(&p_data[CMD_FMT_OFFSET_LEN],
            cmd_len - CMD_FMT_SIZE_START - CMD_FMT_SIZE_CRC,
            cmd_crc);
    if (!crc_ok)
    {
        NRF_LOG_ERROR("Invalid cmd format: crc");
        return NRF_ERROR_INVALID_DATA;
    }

    return NRF_SUCCESS;
}

static uint32_t op_code_get(buffer_t* p_buff, uint8_t* op_code)
{
    if (p_buff == NULL || p_buff->p_data == NULL)
    {
        return NRF_ERROR_INVALID_DATA;
    }

    // Assume it's a valid buffer, so skip format check
    *op_code = p_buff->p_data[CMD_FMT_OFFSET_OPCODE];

    return NRF_SUCCESS;
}

/** Alloc a buffer and fill with cmd data */
static void cmd_to_buff(app_cmd_t* p_cmd, buffer_t* p_buff)
{
    uint32_t err_code;
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
    crc16 = 0;
    crc16 = crc16_compute(&p_packet[CMD_FMT_OFFSET_LEN],
            CMD_FMT_SIZE_LEN + CMD_FMT_SIZE_OPCODE + pdu_len,
            &crc16);
    uint16_encode(crc16, &p_packet[CMD_FMT_OFFSET_PDU + pdu_len]);

    /* Packet length */
    pkt_len = CMD_FMT_OFFSET_PDU + pdu_len + CMD_FMT_SIZE_CRC;
    pkt_len = MIN(pkt_len, CMD_PACKET_LENGTH);

    p_buff->length = pkt_len;
}

static uint32_t buff_to_cmd(buffer_t* p_buff, app_cmd_t* p_cmd)
{
    if (p_buff->p_data == NULL || p_cmd == NULL)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    uint8_t* p_data;
    uint16_t op_pdu_len;

    p_data = p_buff->p_data;
    op_pdu_len = uint16_decode(&p_data[CMD_FMT_OFFSET_LEN]);

    p_cmd->type = p_data[CMD_FMT_OFFSET_START];
    p_cmd->op_code = p_data[CMD_FMT_OFFSET_OPCODE];
    p_cmd->length = op_pdu_len - CMD_FMT_SIZE_OPCODE;
    p_cmd->p_data = &p_data[CMD_FMT_OFFSET_PDU];

    return NRF_SUCCESS;
}

static uint32_t buff_send(buffer_t* p_buff)
{
    uint32_t err_code;
    uint8_t  byte;

    if (p_buff->length == 0)
    {
        return NRF_ERROR_DATA_SIZE;
    }

    if (p_buff->offset == p_buff->length)
    {
        on_cmd_send_complete();

        return NRF_SUCCESS;
    }

    byte = p_buff->p_data[p_buff->offset];
    p_buff->offset++;

    err_code = app_uart_put(byte);
    if (err_code == NRF_SUCCESS)
    {
        if (m_tx_buff.offset == 1)
        {
            on_cmd_send_start();
        }

        return NRF_ERROR_BUSY;
    }
    else
    {
        on_cmd_send_error();

        return NRF_ERROR_INTERNAL;
    }
}

static uint32_t cmd_send(app_cmd_t* p_cmd)
{
    cmd_to_buff(p_cmd, &m_tx_buff);
    return buff_send(&m_tx_buff);
}

static uint32_t app_cmd_respond(uint8_t* p_data, uint16_t length)
{
    uint32_t err_code;
    uint8_t  op_code;
    app_cmd_t cmd;

    if (state_get(&m_cmd_ctx) != CMD_STATE_REQ_RECEIVED &&
        state_get(&m_cmd_ctx) != CMD_STATE_REQ_SENT)
    {
        NRF_LOG_ERROR("Invalid state for response:%d", state_get(&m_cmd_ctx));
        return NRF_ERROR_INVALID_STATE;
    }

    err_code = op_code_get(&m_rx_buff, &op_code);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("rx buffer is reset too early");
        on_cmd_send_error();
        return err_code;
    }

    cmd.type    = CMD_TYPE_RESPONSE;
    cmd.op_code = op_code;
    cmd.p_data  = p_data;
    cmd.length  = length;

    return cmd_send(&cmd);
}

uint32_t app_cmd_request(uint8_t op_code, uint8_t* p_data, uint16_t length)
{
    uint32_t err_code;
    if (mode_get(&m_cmd_ctx) != CMD_MODE_IDLE)
    {
        NRF_LOG_WARNING("Can't request now");
        return NRF_ERROR_INVALID_STATE;
    }

    app_cmd_t cmd =
    {
        .type = CMD_TYPE_REQUEST,
        .op_code = op_code,
        .p_data  = p_data,
        .length  = length,
    };

    mode_set(&m_cmd_ctx, CMD_MODE_HOST);

    return cmd_send(&cmd);
}

void app_cmd_event_cb_register(cmd_event_cb_t cb)
{
    if (cb != NULL)
    {
        m_event_cb = cb;
    }
}

static void on_uart_tx_empty(void)
{
    buff_send(&m_tx_buff);
}

// TODO: input param contains p_buff rather than m_rx_buff
static void on_uart_rx_ready(buffer_t* p_buffer, uint8_t byte)
{
    uint32_t err_code;
    uint8_t  req_op_code;
    static bool rx_started;
    static uint16_t cmd_len;

    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST)
    {
        if (m_cmd_ctx.state != CMD_STATE_REQ_SENT &&
                m_cmd_ctx.state != CMD_STATE_RSP_RECEIVING)
        {
            NRF_LOG_WARNING("Invalid state for rx(1)");
            return;
        }
    }

    if (mode_get(&m_cmd_ctx) == CMD_MODE_SLAVE)
    {
        if (m_cmd_ctx.state != CMD_STATE_REQ_RECEIVING)
        {
            NRF_LOG_WARNING("Invalid state for rx(2)");
            return;
        }
    }

    if (!rx_started)
    {
        rx_started = true;
        buff_alloc(m_rx_pool, p_buffer);
        cmd_len = 0;
    }

    p_buffer->p_data[p_buffer->length] = byte;
    p_buffer->length++;

    if (cmd_len == 0)
    {
        cmd_len = cmd_len_get(p_buffer);

        if (p_buffer->length == 1)
        {
            on_cmd_receive_start();
        }

        if (cmd_len > CMD_PACKET_LENGTH)
        {
            on_cmd_receive_error();
        }

        if (cmd_len == 0) 
        {
            return;
        }
    }

    if (p_buffer->length < cmd_len)
    {
        return;
    }
	
	req_op_code = 0;
    if (mode_get(&m_cmd_ctx) == CMD_MODE_HOST) 
	{
        op_code_get(&m_tx_buff, &req_op_code);
    }	

	// In host mode, op code of response should equal to the one of request
    err_code = format_check(p_buffer, req_op_code, mode_get(&m_cmd_ctx));

    if (err_code == NRF_SUCCESS)
    {
        on_cmd_receive_complete();
    }
    // Invalid format
    else if (err_code == NRF_ERROR_INVALID_DATA)
    {
        on_cmd_receive_error();
    }
    // On going receiving
    else
    {
        NRF_LOG_ERROR("Should not come here(%d)", err_code);
        on_cmd_receive_error();
    }

    rx_started = false;
}

void app_cmd_uart_event_handler(app_uart_evt_t * p_event)
{
    uint8_t byte;

    switch (p_event->evt_type)
    {
    case APP_UART_DATA_READY:
        while (true)
        {
            if (app_uart_get(&byte) == NRF_ERROR_NOT_FOUND)
            {
                break;
            }

            on_uart_rx_ready(&m_rx_buff, byte);
        }
        break;

    case APP_UART_TX_EMPTY:
        on_uart_tx_empty();
        break;

    default:
        break;
    }
}

uint32_t app_cmd_init(void)
{
    static bool init = false;

    mode_set(&m_cmd_ctx, CMD_MODE_IDLE);
    state_set(&m_cmd_ctx, CMD_STATE_IDLE);

    memset(&m_cmd_ctx.cmd, 0, sizeof(app_cmd_t));

    memset(&m_rx_pool, 0, sizeof(m_rx_pool));
    memset(&m_tx_pool, 0, sizeof(m_tx_pool));

    memset(&m_rx_buff, 0, sizeof(buffer_t));
    memset(&m_tx_buff, 0, sizeof(buffer_t));

    m_event_cb = event_cb_dummy;

    if (!init)
    {
        init = true;

        app_timer_create(&m_tmr_wait_rsp, APP_TIMER_MODE_SINGLE_SHOT, tmr_rsp_timeout_handler);
    }
}

//----------------------

void cmd_request_ping(void)
{
    app_cmd_request(CMD_OP_PING, "yq", 2);
}

static int req_cb_ping(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond)
{
    NRF_LOG_INFO(__func__);

    char* rsp = "ok";
    respond(rsp, strlen(rsp));

    return 0;
}

static void rsp_cb_ping(uint8_t* p_rsp, uint16_t rsp_len)
{
    NRF_LOG_INFO(__func__);

    if (rsp_len > 0)
    {
        uint8_t p_rsp_timeout[] = CMD_RSP_TIMEOUT;
        if (rsp_len == sizeof(p_rsp_timeout))
        {
            if (memcmp(p_rsp, p_rsp_timeout, rsp_len) == 0)
            {
                NRF_LOG_INFO("Ping response is timeout");
            }
        }
        else
        {
            NRF_LOG_INFO("Got ping response");
        }
    }
}

CMD_CALLBACK_REG(CMD_OP_PING, req_cb_ping, NULL);


