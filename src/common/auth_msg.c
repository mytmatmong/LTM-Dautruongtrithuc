#include "auth_msg.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int json_extract_string(const char *json_str, const char *key, char *out_val, size_t out_size) {
    if (!json_str || !key || !out_val || out_size == 0) return -1;

    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char *p = strstr(json_str, search_pattern);
    if (!p) return -1;

    p += strlen(search_pattern);
    /* Bỏ qua khoảng trắng và dấu hai chấm ':' */
    while (*p && (isspace((unsigned char)*p) || *p == ':')) {
        p++;
    }

    if (*p != '\"') return -1;
    p++; /* Bỏ qua dấu mở ngoặc kép '\"' */

    size_t idx = 0;
    while (*p && *p != '\"' && idx + 1 < out_size) {
        if (*p == '\\' && *(p + 1)) {
            p++; /* Bỏ qua escape backslash */
        }
        out_val[idx++] = *p++;
    }
    out_val[idx] = '\0';

    if (*p != '\"') return -1; /* Thiếu dấu đóng ngoặc kép */
    return 0;
}

AuthMsgType parse_message_type(const char *json_str) {
    char type_str[64];
    if (json_extract_string(json_str, "type", type_str, sizeof(type_str)) != 0) {
        return MSG_TYPE_UNKNOWN;
    }

    if (strcmp(type_str, "REGISTER") == 0) return MSG_TYPE_REGISTER;
    if (strcmp(type_str, "REGISTER_RESULT") == 0) return MSG_TYPE_REGISTER_RESULT;
    if (strcmp(type_str, "LOGIN") == 0) return MSG_TYPE_LOGIN;
    if (strcmp(type_str, "LOGIN_RESULT") == 0) return MSG_TYPE_LOGIN_RESULT;
    if (strcmp(type_str, "LOGOUT") == 0) return MSG_TYPE_LOGOUT;
    if (strcmp(type_str, "JOIN_LOBBY") == 0) return MSG_TYPE_JOIN_LOBBY;
    if (strcmp(type_str, "JOIN_LOBBY_RESULT") == 0) return MSG_TYPE_JOIN_LOBBY_RESULT;
    if (strcmp(type_str, "LOBBY_UPDATED") == 0) return MSG_TYPE_LOBBY_UPDATED;
    if (strcmp(type_str, "START_GAME") == 0) return MSG_TYPE_START_GAME;
    if (strcmp(type_str, "START_GAME_RESULT") == 0) return MSG_TYPE_START_GAME_RESULT;
    if (strcmp(type_str, "GAME_STARTED") == 0) return MSG_TYPE_GAME_STARTED;
    if (strcmp(type_str, "SELECTION_QUESTION") == 0) return MSG_TYPE_SELECTION_QUESTION;
    if (strcmp(type_str, "SELECTION_ANSWER") == 0) return MSG_TYPE_SELECTION_ANSWER;
    if (strcmp(type_str, "SELECTION_ANSWER_RESULT") == 0) return MSG_TYPE_SELECTION_ANSWER_RESULT;
    if (strcmp(type_str, "ROLE_ASSIGNED") == 0) return MSG_TYPE_ROLE_ASSIGNED;
    if (strcmp(type_str, "ROUND_STARTED") == 0) return MSG_TYPE_ROUND_STARTED;
    if (strcmp(type_str, "ANSWER_SUBMIT") == 0) return MSG_TYPE_ANSWER_SUBMIT;
    if (strcmp(type_str, "ANSWER_ACK") == 0) return MSG_TYPE_ANSWER_ACK;
    if (strcmp(type_str, "ROUND_RESULT") == 0) return MSG_TYPE_ROUND_RESULT;
    if (strcmp(type_str, "SCORE_UPDATED") == 0) return MSG_TYPE_SCORE_UPDATED;
    if (strcmp(type_str, "GAME_FINISHED") == 0) return MSG_TYPE_GAME_FINISHED;
    if (strcmp(type_str, "ERROR") == 0) return MSG_TYPE_ERROR;

    return MSG_TYPE_UNKNOWN;
}

int build_register_result(bool success, int64_t user_id, const char *username, 
                          const char *err_code, const char *err_msg, 
                          char *out_buf, size_t out_size) {
    if (!out_buf || out_size == 0) return -1;

    int written = 0;
    if (success) {
        written = snprintf(out_buf, out_size,
            "{\"type\":\"REGISTER_RESULT\",\"status\":\"ok\",\"payload\":{\"user_id\":%ld,\"username\":\"%s\"}}\n",
            (long)user_id, username ? username : "");
    } else {
        written = snprintf(out_buf, out_size,
            "{\"type\":\"REGISTER_RESULT\",\"status\":\"error\",\"error_code\":\"%s\",\"payload\":{\"message\":\"%s\"}}\n",
            err_code ? err_code : "INTERNAL_ERROR", err_msg ? err_msg : "Error");
    }

    return (written > 0 && (size_t)written < out_size) ? 0 : -1;
}

int build_login_result(bool success, const char *session_id, int64_t user_id, 
                       const char *username, const char *err_code, const char *err_msg, 
                       char *out_buf, size_t out_size) {
    if (!out_buf || out_size == 0) return -1;

    int written = 0;
    if (success) {
        written = snprintf(out_buf, out_size,
            "{\"type\":\"LOGIN_RESULT\",\"status\":\"ok\",\"session_id\":\"%s\",\"payload\":{\"user_id\":%ld,\"username\":\"%s\"}}\n",
            session_id ? session_id : "", (long)user_id, username ? username : "");
    } else {
        written = snprintf(out_buf, out_size,
            "{\"type\":\"LOGIN_RESULT\",\"status\":\"error\",\"error_code\":\"%s\",\"payload\":{\"message\":\"%s\"}}\n",
            err_code ? err_code : "AUTH_FAILED", err_msg ? err_msg : "Authentication failed");
    }

    return (written > 0 && (size_t)written < out_size) ? 0 : -1;
}

int build_error_msg(const char *err_code, const char *err_msg, char *out_buf, size_t out_size) {
    if (!out_buf || out_size == 0) return -1;

    int written = snprintf(out_buf, out_size,
        "{\"type\":\"ERROR\",\"status\":\"error\",\"error_code\":\"%s\",\"payload\":{\"message\":\"%s\"}}\n",
        err_code ? err_code : "ERROR", err_msg ? err_msg : "Unknown error");

    return (written > 0 && (size_t)written < out_size) ? 0 : -1;
}

