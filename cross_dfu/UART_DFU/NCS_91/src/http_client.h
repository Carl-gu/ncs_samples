#ifndef HTTP_CLIENT_H__
#define HTTP_CLIENT_H__

#include <zephyr.h>
#include <net/fota_download.h>

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Initialize http client module 
 * 
 * @param[in] download_client: fota download callback 
 *
 * @return 0: success
 * @return neg: error
 */
int http_client_init(fota_download_callback_t download_callback);

/**@brief Start to connect LTE network */
int http_client_connect(void);

/**@brief Start to HTTP download 
 *
 * @param[in] host: host name
 * @param[in] file: file path
 *
 * @return 0: success
 * @return neg: error
 */
int http_client_download(char* host, char* file);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_H__ */
