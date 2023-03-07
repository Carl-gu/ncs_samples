#include <string.h>
#include <stdio.h>
#include <zephyr.h>
#include <device.h>
#include <bsd.h>
#include <modem/lte_lc.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>
#include <modem/bsdlib.h>
#include <net/fota_download.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(http_client, 3);

#include "http_client.h"

#define HTTP_HOST_LEN_MAX   30
#define HTTP_FILE_LEN_MAX   30

static struct k_work	 wk_http_download;

fota_download_callback_t m_user_download_cb;

static char m_http_host[HTTP_HOST_LEN_MAX];
static char m_http_file[HTTP_FILE_LEN_MAX];

static bool downloading;

/**@brief Handler for HTTP download worker */
static void http_download_handler(struct k_work* unused)
{
    int rc;
    int sec_tag = -1;
    char* apn = NULL;
    int port = 0;       // HTTP, not HTTPS

    rc = fota_download_start(m_http_host, m_http_file, sec_tag, port, apn);
    if (rc) {
        LOG_ERR("Download file error, %d", rc);
    }
}

/**@brief Start to HTTP download */
int http_client_download(char* host, char* file)
{
    if (downloading) {
        return 0;
    }

    if (strlen(file) > HTTP_FILE_LEN_MAX) {
        LOG_ERR("File path len should be less than %d bytes", HTTP_FILE_LEN_MAX);
        return -1;
    }

    if (strlen(host) > HTTP_HOST_LEN_MAX) {
        LOG_ERR("Host len should be less than %d bytes", HTTP_HOST_LEN_MAX);
        return -1;
    }

    memcpy(m_http_host, host, strlen(host));
    memcpy(m_http_file, file, strlen(file));

    k_work_submit(&wk_http_download);

    downloading = true;

    return 0;
}

/**@brief HTTP download event handler */
static void download_handler(const struct fota_download_evt* evt)
{
    switch (evt->id) {
    case FOTA_DOWNLOAD_EVT_FINISHED:
        LOG_DBG("Download event finished");
        downloading = false;
        break;

    case FOTA_DOWNLOAD_EVT_ERROR:
        LOG_ERR("Download event error");
        downloading = false;
        break;

    case FOTA_DOWNLOAD_EVT_PROGRESS:
    case FOTA_DOWNLOAD_EVT_ERASE_PENDING:
    case FOTA_DOWNLOAD_EVT_ERASE_DONE:
    default:
        LOG_DBG("Download event: %d", evt->id);
        break;
    }

    if (m_user_download_cb) {
        m_user_download_cb(evt);
    }
}

/**@brief Start to connect LTE network */
int http_client_connect(void)
{
    return lte_lc_init_and_connect();
}

/**@brief Initialize http client module  */
int http_client_init(fota_download_callback_t download_callback)
{
    int rc;
    
    downloading = false;
    m_user_download_cb = download_callback;

    rc = bsdlib_init();
	switch (rc) {
	case MODEM_DFU_RESULT_OK:
		LOG_INF("Modem firmware update successful!\n");
		LOG_INF("Modem will run the new firmware after reboot\n");
        // TODO: reset?
		k_thread_suspend(k_current_get());
		break;
	case MODEM_DFU_RESULT_UUID_ERROR:
	case MODEM_DFU_RESULT_AUTH_ERROR:
		LOG_ERR("Modem firmware update failed\n");
		LOG_ERR("Modem will run non-updated firmware on reboot.\n");
		break;
	case MODEM_DFU_RESULT_HARDWARE_ERROR:
	case MODEM_DFU_RESULT_INTERNAL_ERROR:
		LOG_ERR("Modem firmware update failed\n");
		LOG_ERR("Fatal error.\n");
		break;
	case -1:
		LOG_ERR("Could not initialize bsdlib.\n");
		LOG_ERR("Fatal error.\n");
	default:
		break;
	}

    if (rc) {
        LOG_ERR("BSD library error.");

        return rc;
    }

    rc = at_notif_init();
    if (rc) {
        LOG_ERR("AT Notify error.");

        return rc;
    }

    rc = at_cmd_init();
    if (rc) {
        LOG_ERR("AT CMD error.");

        return rc;
    }

    rc = fota_download_init(download_handler);
    if (rc) {
        LOG_ERR("FOTA init error.");
        return rc;
    }

    k_work_init(&wk_http_download, http_download_handler);

    return rc;
}
