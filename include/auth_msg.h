#ifndef AUTH_MSG_H
#define AUTH_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MSG_TYPE_UNKNOWN = 0,
    MSG_TYPE_REGISTER,
    MSG_TYPE_REGISTER_RESULT,
    MSG_TYPE_LOGIN,
    MSG_TYPE_LOGIN_RESULT,
    MSG_TYPE_LOGOUT,
    MSG_TYPE_JOIN_LOBBY,
    MSG_TYPE_JOIN_LOBBY_RESULT,
    MSG_TYPE_LOBBY_UPDATED,
    MSG_TYPE_START_GAME,
    MSG_TYPE_START_GAME_RESULT,
    MSG_TYPE_GAME_STARTED,
    MSG_TYPE_SELECTION_QUESTION,
    MSG_TYPE_SELECTION_ANSWER,
    MSG_TYPE_SELECTION_ANSWER_RESULT,
    MSG_TYPE_ROLE_ASSIGNED,
    MSG_TYPE_ROUND_STARTED,
    MSG_TYPE_ANSWER_SUBMIT,
    MSG_TYPE_ANSWER_ACK,
    MSG_TYPE_ROUND_RESULT,
    MSG_TYPE_SCORE_UPDATED,
    MSG_TYPE_GAME_FINISHED,
    MSG_TYPE_ERROR
} AuthMsgType;

/**
 * Trích xuất giá trị trường chuỗi từ chuỗi JSON đơn giản.
 * Hỗ trợ định dạng {"key": "value"} hoặc {"nested": {"key": "value"}}.
 * 
 * @param json_str Chuỗi JSON đầu vào
 * @param key Khóa cần tìm
 * @param out_val Buffer nhận kết quả
 * @param out_size Kích thước buffer
 * @return 0 nếu tìm thấy, -1 nếu không có hoặc lỗi cú pháp
 */
int json_extract_string(const char *json_str, const char *key, char *out_val, size_t out_size);

/**
 * Phân tích loại thông điệp từ trường "type" của gói tin JSON.
 */
AuthMsgType parse_message_type(const char *json_str);

/**
 * Đóng gói phản hồi REGISTER_RESULT (JSON chuỗi kết thúc bởi '\n').
 */
int build_register_result(bool success, int64_t user_id, const char *username, 
                          const char *err_code, const char *err_msg, 
                          char *out_buf, size_t out_size);

/**
 * Đóng gói phản hồi LOGIN_RESULT (JSON chuỗi kết thúc bởi '\n').
 */
int build_login_result(bool success, const char *session_id, int64_t user_id, 
                       const char *username, const char *err_code, const char *err_msg, 
                       char *out_buf, size_t out_size);

/**
 * Đóng gói phản hồi lỗi chung ERROR (kết thúc bởi '\n').
 */
int build_error_msg(const char *err_code, const char *err_msg, char *out_buf, size_t out_size);

#endif /* AUTH_MSG_H */

