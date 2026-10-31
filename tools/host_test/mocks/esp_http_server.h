// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include <map>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>

typedef void* httpd_handle_t;

typedef enum {
    HTTP_GET = 1,
    HTTP_POST = 2,
    HTTP_DELETE = 3,
    HTTP_OPTIONS = 6,
} httpd_method_t;

#define HTTPD_400_BAD_REQUEST           "400 Bad Request"
#define HTTPD_403_FORBIDDEN             "403 Forbidden"
#define HTTPD_404_NOT_FOUND             "404 Not Found"
#define HTTPD_429_TOO_MANY_REQUESTS     "429 Too Many Requests"
#define HTTPD_500_INTERNAL_SERVER_ERROR "500 Internal Server Error"

#define HTTPD_SOCK_ERR_TIMEOUT -1

#ifndef HTTPD_MAX_URI_LEN
#define HTTPD_MAX_URI_LEN 512
#endif

typedef struct httpd_req {
    httpd_handle_t handle;
    int method;
    const char uri[HTTPD_MAX_URI_LEN];
    size_t content_len;
    void* aux;
    void* user_ctx;
} httpd_req_t;

inline esp_err_t httpd_resp_set_status(httpd_req_t*, const char*) {
    return ESP_OK;
}
inline esp_err_t httpd_resp_set_type(httpd_req_t*, const char*) {
    return ESP_OK;
}
inline esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) {
    return ESP_OK;
}
inline esp_err_t httpd_resp_send(httpd_req_t*, const char*, ssize_t) {
    return ESP_OK;
}
inline esp_err_t httpd_resp_sendstr(httpd_req_t*, const char*) {
    return ESP_OK;
}
inline esp_err_t httpd_resp_send_err(httpd_req_t*, const char*, const char*) {
    return ESP_OK;
}
inline esp_err_t httpd_resp_send_chunk(httpd_req_t*, const char*, ssize_t) {
    return ESP_OK;
}
inline int httpd_req_recv(httpd_req_t* req, char* buf, size_t max_len) {
    if (req != nullptr && req->aux != nullptr) {
        const char* src = static_cast<const char*>(req->aux);
        size_t len = strlen(src);
        if (len > max_len) {
            len = max_len;
        }
        memcpy(buf, src, len);
        req->aux = const_cast<char*>(src + len);
        return static_cast<int>(len);
    }
    return 0;
}
inline esp_err_t httpd_req_get_url_query_str(httpd_req_t*, char*, size_t) {
    return ESP_FAIL;
}

inline size_t httpd_req_get_hdr_value_len(httpd_req_t*, const char*) {
    return 0;
}
inline esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*, const char*, char*, size_t) {
    return ESP_ERR_NOT_FOUND;
}

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

inline httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t, int) {
    return HTTPD_WS_CLIENT_WEBSOCKET;
}
inline int httpd_req_to_sockfd(httpd_req_t*) {
    return 1;
}
inline esp_err_t httpd_get_client_list(httpd_handle_t, size_t* numClients, int*) {
    if (numClients)
        *numClients = 0;
    return ESP_OK;
}
inline esp_err_t httpd_sess_trigger_close(httpd_handle_t, int) {
    return ESP_OK;
}
inline esp_err_t httpd_ws_send_frame_async(httpd_handle_t, int, httpd_ws_frame_t*) {
    return ESP_OK;
}
inline esp_err_t httpd_ws_recv_frame(httpd_req_t*, httpd_ws_frame_t*, size_t) {
    return ESP_OK;
}
inline esp_err_t httpd_ws_send_frame(httpd_req_t*, httpd_ws_frame_t*) {
    return ESP_OK;
}
inline esp_err_t httpd_query_key_value(const char* qry, const char* key, char* val, size_t val_size) {
    if (!qry || !key || !val || val_size == 0)
        return ESP_FAIL;
    const char* p = strstr(qry, key);
    if (!p)
        return ESP_FAIL;
    p += strlen(key);
    if (*p != '=')
        return ESP_FAIL;
    p++;
    size_t i = 0;
    while (*p && *p != '&' && i < val_size - 1) {
        val[i++] = *p++;
    }
    val[i] = '\0';
    return ESP_OK;
}

typedef struct httpd_config {
    unsigned stack_size;
    unsigned max_uri_handlers;
    int max_open_sockets;
    int backlog_conn;
    bool lru_purge_enable;
    int recv_wait_timeout;
    int send_wait_timeout;
    bool enable_so_linger;
    int linger_timeout;
    bool (*uri_match_fn)(const char*, const char*, size_t);
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() {4096, 8, 4, 4, false, 5, 5, false, 0, nullptr}

inline bool httpd_uri_match_wildcard(const char*, const char*, size_t) {
    return true;
}

inline esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t*) {
    if (handle != nullptr) {
        *handle = reinterpret_cast<httpd_handle_t>(0x1234);
    }
    return ESP_OK;
}

inline esp_err_t httpd_stop(httpd_handle_t) {
    return ESP_OK;
}

typedef struct httpd_uri {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t*);
    void* user_ctx;
    bool is_websocket;
} httpd_uri_t;

inline esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t*) {
    return ESP_OK;
}
