#include <string.h>
#include <zephyr.h>
#include <net/fota_download.h>
#include <power/reboot.h>
#include <sys/byteorder.h>
#include <dfu/dfu_target.h>
#include <dfu/mcuboot.h>
#include <modem/modem_info.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(main, 3);


#include "button.h"
#include "led.h"
#include "http_client.h"
#include "app_flash.h"
#include "app_cmd.h"
#include "app_flash_cmd.h"
#include "serial_dfu/serial_dfu.h"
#include "serial_dfu/dfu_host.h"
#include "serial_dfu/dfu_file.h"

#define UPDATE_APP 					false

#define IMAGE_FROM_HTTP				1
#define IMAGE_FROM_SERIAL			2

#define UART_DEVICE_LABLE 			"UART_3"

#define CMD_OP_ENTER_BL				0x31
#define CMD_OP_PING_APP				0x32

#define ENTER_BL_DELAY				1000000			/* 1 second */
#define MODEM_DATA_UNIT_LEN			256

static struct device*				m_uart_dev;

static char* m_http_host = "106.13.189.165";
static char* m_http_file_52 = "dfu_file_52";
static char* m_http_file_91 = "dfu_file_91";

static bool 	m_download_busy;
static uint8_t  m_image_channel;
static uint8_t  m_image_file_type;
static uint32_t m_image_file_size;

static struct k_work wk_serial_dfu;               /* k_work to perform DFU */
static struct k_work wk_update_mcuboot_flag;

static char m_modem_version[MODEM_INFO_MAX_RESPONSE_SIZE];

/**@brief copy image to modem core */
static void copy_image_to_modem(void)
{
	int rc;

	uint8_t p_data[MODEM_DATA_UNIT_LEN];
	uint32_t len = 0;
	uint32_t offset = 0;

	while (true) {
		if (offset == m_image_file_size) {
			break;
		}

		if (offset + MODEM_DATA_UNIT_LEN <= m_image_file_size) {
			len = MODEM_DATA_UNIT_LEN;
		}
		else {
			len = m_image_file_size - offset;
		}

		rc = app_flash_read(offset, p_data, len);
		if (rc != 0) {
			LOG_ERR("Read for modem error: %d", rc);
		}

		rc = dfu_target_write(p_data, len);
		if (rc != 0) {
			LOG_ERR("Write for modem error: %d", rc);
		}

		offset += len;
	}

}

/**@brief Start to download a file */
static void http_download_start(char* file_path)
{
	m_image_channel = IMAGE_FROM_HTTP;
	m_download_busy = true;

	http_client_download(m_http_host, file_path);
}

/**@brief Do serial dfu for 52 */
static void do_serial_dfu(void)
{
	int rc;
	bool bl_mode;

	// Uninit app_cmd as it's use the same UART 
	// component as serial DFU
	app_cmd_uninit();

	rc = serial_dfu_init(m_uart_dev);
	if (rc) {
		LOG_ERR("Serial DFD init failed");
		return;
	}

	bl_mode = dfu_host_bl_mode_check();

	if (bl_mode) {
		LOG_INF("Found bootloader, start DFU");
		serial_dfu_start();
	}
	else {
		LOG_WRN("No bootloader, quit");
	}

	// Give back UART component
	serial_dfu_uninit();
	app_cmd_init(m_uart_dev);
}

/**@brief Do mcuboot dfu for 91 */
static void do_mcuboot_dfu(void)
{
	LOG_INF("Start to reboot...");

	sys_reboot(SYS_REBOOT_WARM);
}

/**@brief Callback of k_work to perform DFU */
static void wk_serial_dfu_handler(struct k_work* unused)
{
	do_serial_dfu();
}

/**@brief A dummy callback function */
void dfu_target_cb_dummy(enum dfu_target_evt_id evt_id) {;}

/**@brief k_work handler for updating mcuboot flag */
static void wk_update_mcuboot_flag_handler(struct k_work* unused)
{
	if (m_image_file_type == IMAGE_TYPE_NRF52) {
		app_flash_erase_from_end(1);
	}
	else if (m_image_file_type == IMAGE_TYPE_NRF91) {
		dfu_target_init(DFU_TARGET_IMAGE_TYPE_MCUBOOT, 0, dfu_target_cb_dummy);	
		dfu_target_done(true);			
	}
	else if (m_image_file_type == IMAGE_TYPE_MODEM) {
		if (m_image_file_size == 0) {
			LOG_ERR("Inavlid image file size");
			return;
		}

		dfu_target_init(DFU_TARGET_IMAGE_TYPE_MODEM_DELTA, 0, dfu_target_cb_dummy);	
	
		LOG_INF("Start to copy image file to modem space");
		copy_image_to_modem();

		dfu_target_done(true);	
	}
	else {
		LOG_ERR("Inavlid image file type");
	}
}

/**@brief Start to run DFU to 91 or 52 due to image type */
static void perform_dfu(void)
{
	int img_type;

	img_type = dfu_file_type();

	if (img_type == IMAGE_TYPE_NRF52) {
		// Check nRF52 in application mode or not
		app_cmd_request(CMD_OP_PING_APP, NULL, 0);
	}
	else {
		do_mcuboot_dfu();
	}
}

/**@brief Handler for button 1 is pressed */
void button1_pressed_handler(void)
{
	if (m_download_busy) {
		return;
	}

	// Turn off LED 2
	led2_set(0);

	// Flip both switches to **RIGHT** to download 91 image
	if ((button_read("switch_1") == 0) &&
		(button_read("switch_2") == 0)) {
		LOG_INF("Download 91 image file...");

		http_download_start(m_http_file_91);
	}
	// Flip both switches to **LEFT** to download 52 image
	else if ((button_read("switch_1") == 1) &&
		(button_read("switch_2") == 1)) {
		LOG_INF("Download 52 image file...");

		http_download_start(m_http_file_52);
	}
	else {
		LOG_INF("Download nothing");
	}
}

/**@brief Handler for button 2 is pressed */
void button2_pressed_handler(void)
{
	perform_dfu();
}

/**@brief Callback for enter_bootloader response. */
static void rsp_cb_enter_bl(u8_t* p_rsp, u16_t rsp_len)
{
	u8_t p_data[] = CMD_RSP_TIMEOUT;
	// Response is timeout, should not come here.
	// If come here, suppose 52 is in bootloader mode.
	if (memcmp(p_rsp, p_data, sizeof(p_data)) == 0) {
		LOG_ERR("cmd timeout: enter_bl");
	}
	// 52 is going to jump to bootloader mode,
	// this delay should be the same as 52 chip
	else {
		k_busy_wait(ENTER_BL_DELAY);
	}

	k_work_submit(&wk_serial_dfu);
}

/**@brief Callback for ping_app response. */
static void rsp_cb_ping_app(u8_t* p_rsp, u16_t rsp_len)
{
	u8_t p_data[] = CMD_RSP_TIMEOUT;
	// Response is timeout, which means 52 chip is 
	// in bootloader mode
	if (memcmp(p_rsp, p_data, sizeof(p_data)) == 0) {
		do_serial_dfu();
	}
	// Got response
	else {
		// If 52 chip is in application mode, 
		// drive it to bootloader mode
		app_cmd_request(CMD_OP_ENTER_BL, NULL, 0);
	}
}

/**@brief Handler for DFU file is downloaded or received */
static void dfu_file_ready(void)
{
	int img_type;

	img_type = dfu_file_type();

	if (img_type < 0) {
		LOG_ERR("DFU file is invalid, %d", img_type);
		return;
	}

	led2_set(1);

	/* If fetching image from http, fota_download library will
	 * take it as a mcuboot image, and adds a flag at the end of
	 * secondary bank, we need to remove it for 52 image to stop
	 * mcuboot behavior after reboot.
	 * If fetching image by UART fromrnrf52, we need to add a 
	 * mcuboot flag manually to make it a valid mcuboot image.
	 */
	if (m_image_channel == IMAGE_FROM_HTTP) {
		if (img_type == IMAGE_TYPE_NRF52) {
			// Remove the last page of secondary bank
			LOG_INF("Remove MCUboot flag for 52 image");

			m_image_file_type = IMAGE_TYPE_NRF52;

			k_work_submit(&wk_update_mcuboot_flag);
		}
		else {
			LOG_INF("Press button 2 to do DFU");
		}		
	}
	else if (m_image_channel == IMAGE_FROM_SERIAL) {
		if (img_type == IMAGE_TYPE_NRF91) {
			// Add mcuboot meta info
			LOG_INF("Add MCUboot flag for 91 application");

			m_image_file_type = IMAGE_TYPE_NRF91;

			k_work_submit(&wk_update_mcuboot_flag);
		}
		else if (img_type == IMAGE_TYPE_MODEM) {
			// Add mcuboot meta info
			LOG_INF("Add MCUboot flag for 91 modem");
			m_image_file_type = IMAGE_TYPE_MODEM;

			k_work_submit(&wk_update_mcuboot_flag);
		}

		LOG_INF("Press button 2 to do DFU");
	}
	else {
		// Should not come here
		LOG_ERR("Invalid image channel");
	}
}

/**@brief FOTA download event handler */
static void download_event_handler(const struct fota_download_evt* evt)
{
	switch (evt->id) {
	case FOTA_DOWNLOAD_EVT_FINISHED:
		LOG_INF("FOTA download finished");
		m_download_busy = false;
		dfu_file_ready();
		break;

	case FOTA_DOWNLOAD_EVT_ERROR:
		LOG_ERR("FOTA download error");
		m_download_busy = false;
		break;

	default:
		break;
	}
}

/**@brief Get modem version
 * @param[out] version of modem in string format
 * 
 * @return 0: success
 * 		 neg: failed
 */
static int modem_version_get(char* version_string)
{
	int rc;
	struct modem_param_info modem_info = {0};

	rc = modem_info_init();
	if (rc) {
		LOG_ERR("Could not initialize modem info module");
		return rc;
	}

	rc = modem_info_params_init(&modem_info);
	if (rc) {
		LOG_ERR("Could not initialize modem info parameters");
		return rc;
	}

	rc = modem_info_params_get(&modem_info);
	if (rc) {
		LOG_ERR("Could not obtain cell information");
		return rc;
	}

	strcpy(version_string, modem_info.device.modem_fw.value_string);

	return rc;
}

/**@brief app_cmd request & response event handler.
 *		 Note, don't run long-time task here. 
 */
static void app_cmd_event_handler(cmd_event_t* p_event)
{
	static u32_t start_time;
	u32_t total_time;
	u32_t speed_integer;
	u32_t speed_fraction;

	switch (p_event->op_code) {
	case CMD_OP_FLASH_INFO:
		LOG_INF("Start to receive DFU image by UART");
		start_time = k_uptime_get_32();

		break;

	case CMD_OP_FLASH_DONE:
		LOG_INF("DFU image is received by UART");
		m_image_file_size = sys_get_le32(p_event->p_data);
		LOG_INF("File size is: %d", m_image_file_size);	

		total_time = k_uptime_get_32() - start_time;
		speed_integer = m_image_file_size / total_time;
		speed_fraction = (m_image_file_size % total_time) * 100 / total_time;

		LOG_INF("UART transferring speed: %d.%d kB/s", speed_integer, speed_fraction);

		m_image_channel = IMAGE_FROM_SERIAL;
		dfu_file_ready();
		break;

	default:
		LOG_DBG("cmd op: 0x%02x", p_event->op_code);
		break;
	}
}

void main(void)
{
	int rc = 0;

#if UPDATE_APP
	LOG_INF("Cross DFU Demo(new)\n");
#else 
	LOG_INF("Cross DFU Demo(ori)\n");
#endif

	led_init();

#if UPDATE_APP
	led3_set(1);
	led4_set(1);
#endif

	rc = button_init();
	if (rc) {
		goto err;
	}
	button_handler_assign("button_1", button1_pressed_handler);
	button_handler_assign("button_2", button2_pressed_handler);

	rc = http_client_init(download_event_handler);
	if (rc) {
		LOG_ERR("HTTP Client init error.");
		goto err;
	}

	modem_version_get(m_modem_version);
	LOG_INF("modem version: %s", m_modem_version);

	LOG_INF("Connecting NBIOT...(don't run other tasks now)");
	rc = http_client_connect();
	if (rc) {
		LOG_ERR("Can't connect to NBIOT");
		goto err;
	}
	LOG_INF("NBIOT network is connected.");	

	// After NBIOT connection, LED 1 is on
	led1_set(1);	

	LOG_INF("Put switchs left:  download 52 DFU file");
	LOG_INF("Put switchs right: download 91 DFU file");

	k_work_init(&wk_serial_dfu, wk_serial_dfu_handler);
	k_work_init(&wk_update_mcuboot_flag, wk_update_mcuboot_flag_handler);

	m_uart_dev = device_get_binding(UART_DEVICE_LABLE);
	if (m_uart_dev == NULL) {
		LOG_ERR("Can't init UART component");
		goto err;
	}

	app_cmd_init(m_uart_dev);
	app_cmd_event_cb_register(app_cmd_event_handler);
	app_flash_cmd_init();
	
	// Add cmds
	app_cmd_add(CMD_OP_ENTER_BL, NULL, rsp_cb_enter_bl);
	app_cmd_add(CMD_OP_PING_APP, NULL, rsp_cb_ping_app);

	/* All initializations were successful mark image as working so that we
	 * will not revert upon reboot.
	 */
	boot_write_img_confirmed();

err:
	while (true) {
		k_sleep(K_SECONDS(1));
	}
}
