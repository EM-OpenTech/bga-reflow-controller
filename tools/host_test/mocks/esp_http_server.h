// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include <map>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <string.h>

typedef void* httpd_handle_t;

typedef enum {
    HTTP_GET = 1,
    HTTP_POST = 2,
    HTTP_DELETE = 3,
    HTTP_OPTIONS = 6,
} httpd_method_t;

#define HTTPD_400_BAD_REQUEST "400 Bad Request"
#define HTTPD_403_FORBIDDEN "403 Forbidden"
#define HTTPD_404_NOT_FOUND "404 Not Found"
#define HTTPD_429_TOO_MANY_REQUESTS "429 Too Many Requests"
#define HTTPD_500_INTERNAL_SERVER_ERROR "500 Internal Server Error"

#define HTTPD_SOCK_ERR_TIMEOUT -1

typedef struct httpd_req {
    httpd_handle_t handle;
    int method;
    const char* uri;
    size_t content_len;
    void* aux;
    void* user_ctx;
} httpd_req_t;

inline esp_err_t httpd_resp_set_status(httpd_req_t*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_set_type(httpd_req_t*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_send(httpd_req_t*, const char*, ssize_t) { return ESP_OK; }
inline esp_err_t httpd_resp_sendstr(httpd_req_t*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_send_err(httpd_req_t*, const char*, const char*) { return ESP_OK; }
inline int httpd_req_recv(httpd_req_t*, char*, size_t) { return 0; }
inline esp_err_t httpd_req_get_url_query_str(httpd_req_t*, char*, size_t) { return ESP_FAIL; }

inline size_t httpd_req_get_hdr_value_len(httpd_req_t*, const char*) { return 0; }
inline esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*, const char*, char*, size_t) { return ESP_ERR_NOT_FOUND; }

typedef enum {
    HTTPD_WS_CLIENT_INVALID = 0,
    HTTPD_WS_CLIENT_HTTP = 1,
    HTTPD_WS_CLIENT_WEBSOCKET = 2,
} httpd_ws_client_info_t;

typedef enum {
    HTTPD_WS_TYPE_TEXT = 1,
    HTTPD_WS_TYPE_PING = 2,
    HTTPD_WS_TYPE_PONG = 3,
    HTTPD_WS_TYPE_CLOSE = 4,
} httpd_ws_type_t;

typedef struct httpd_ws_frame {
    bool final;
    bool fragmented;
    httpd_ws_type_t type;
    uint8_t* payload;
    size_t len;
} httpd_ws_frame_t;

inline httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t, int) { return HTTPD_WS_CLIENT_WEBSOCKET; }
inline int httpd_req_to_sockfd(httpd_req_t*) { return 1; }
inline esp_err_t httpd_get_client_list(httpd_handle_t, size_t* numClients, int*) {
    if (numClients) *numClients = 0;
    return ESP_OK;
}
inline esp_err_t httpd_sess_trigger_close(httpd_handle_t, int) { return ESP_OK; }
inline esp_err_t httpd_ws_send_frame_async(httpd_handle_t, int, httpd_ws_frame_t*) { return ESP_OK; }
inline esp_err_t httpd_ws_recv_frame(httpd_req_t*, httpd_ws_frame_t*, size_t) { return ESP_OK; }
inline esp_err_t httpd_ws_send_frame(httpd_req_t*, httpd_ws_frame_t*) { return ESP_OK; }
inline esp_err_t httpd_query_key_value(const char* qry, const char* key, char* val, size_t val_size) {
    if (!qry || !key || !val || val_size == 0) return ESP_FAIL;
    const char* p = strstr(qry, key);
    if (!p) return ESP_FAIL;
    p += strlen(key);
    if (*p != '=') return ESP_FAIL;
    p++;
    size_t i = 0;
    while (*p && *p != '&' && i < val_size - 1) {
        val[i++] = *p++;
    }
    val[i] = '\0';
    return ESP_OK;
}
