#include "handler.h"
#include "timer.h"

#include <sys/param.h>

#include "nvs.h"
#include "router_globals.h"

static const char *TAG = "LockHandler";

bool locked = false;

bool isLocked()
{
    return locked;
}
void lockUI()
{
    locked = true;
}
esp_err_t unlock_handler(httpd_req_t *req)
{

    httpd_req_to_sockfd(req);

    size_t content_len = req->content_len;
    char buf[content_len];

    if (fill_post_buffer(req, buf, content_len) == ESP_OK)
    {

        char unlockParam[req->content_len];
        readUrlParameterIntoBuffer(buf, "unlock", unlockParam, req->content_len);

        if (strlen(unlockParam) > 0)
        {
            char *lock;
            get_config_param_str("lock_pass", &lock);
            if (strcmp(lock, unlockParam) == 0)
            {
                locked = false;
                httpd_resp_set_status(req, "302 Found");
                httpd_resp_set_hdr(req, "Location", "/");
                return httpd_resp_send(req, NULL, 0);
            }
        }
    }
    if (req->method == HTTP_GET) // Relock if called
    {
        locked = true;
        ESP_LOGI(TAG, "UI relocked");
    }
    extern const char ul_start[] asm("_binary_unlock_html_start");

    closeHeader(req);
    return httpd_resp_send(req, ul_start, HTTPD_RESP_USE_STRLEN);
}

esp_err_t redirectToLock(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/unlock");
    return httpd_resp_send(req, NULL, 0);
}
esp_err_t lock_handler(httpd_req_t *req)
{
    if (locked)
    {
        return redirectToLock(req);
    }
    httpd_req_to_sockfd(req);

    if (req->method == HTTP_POST) // Relock if called
    {
        int ret, remaining = req->content_len;
        char buf[req->content_len];

        while (remaining > 0)
        {
            /* Read the data for the request */
            if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0)
            {
                if (ret == HTTPD_SOCK_ERR_TIMEOUT)
                {
                    continue;
                }
                ESP_LOGE(TAG, "Timeout occured");
                return ESP_FAIL;
            }

            remaining -= ret;
        }

        char passParam[req->content_len], pass2Param[req->content_len];

        readUrlParameterIntoBuffer(buf, "lockpass", passParam, req->content_len);
        readUrlParameterIntoBuffer(buf, "lockpass2", pass2Param, req->content_len);
        ESP_LOGI(TAG, "Found pass2 parameter => %s", pass2Param);
        if (strlen(passParam) == strlen(pass2Param) && strcmp(passParam, pass2Param) == 0)
        {
            ESP_LOGI(TAG, "Passes are equal. Password will be changed.");
            if (strlen(passParam) == 0)
            {
                ESP_LOGI(TAG, "Pass will be removed");
            }
            nvs_handle_t nvs;
            nvs_open(PARAM_NAMESPACE, NVS_READWRITE, &nvs);
            nvs_set_str(nvs, "lock_pass", passParam);
            nvs_commit(nvs);
            nvs_close(nvs);
            httpd_resp_set_status(req, "302 Found");
            if (strlen(passParam) > 0)
            {
                httpd_resp_set_hdr(req, "Location", "/lock");
                lockUI();
            }
            else
            {
                httpd_resp_set_hdr(req, "Location", "/");
            }
            return httpd_resp_send(req, NULL, 0);
        }
        else
        {
            ESP_LOGI(TAG, "Passes are not equal.");
        }
    }

    extern const char l_start[] asm("_binary_lock_html_start");
    extern const char l_end[] asm("_binary_lock_html_end");
    const size_t l_html_size = (l_end - l_start);

    char *display = NULL;

    char *lock_pass = NULL;
    get_config_param_str("lock_pass", &lock_pass);
    if (lock_pass != NULL && strlen(lock_pass) > 0)
    {
        display = "block";
    }
    else
    {
        display = "none";
    }

    char *lock_page = malloc(l_html_size + strlen(display) + 1);

    sprintf(lock_page, l_start, display);

    closeHeader(req);

    esp_err_t out = httpd_resp_send(req, lock_page, HTTPD_RESP_USE_STRLEN);
    free(lock_page);
    return out;
}