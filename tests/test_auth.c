#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth_crypto.h"
#include "auth_msg.h"
#include "db.h"

static void test_crypto(void) {
    printf("[TEST] Testing auth_crypto password hashing & verification...\n");
    char hash1[256] = {0};
    char hash2[256] = {0};

    int rc = auth_hash_password("MyPassword123!", hash1, sizeof(hash1));
    assert(rc == 0);
    assert(strlen(hash1) > 0);

    /* Cùng password nhưng hash lại phải khác nhau vì có salt ngẫu nhiên */
    rc = auth_hash_password("MyPassword123!", hash2, sizeof(hash2));
    assert(rc == 0);
    assert(strcmp(hash1, hash2) != 0);

    /* Kiểm tra verify thành công */
    assert(auth_verify_password("MyPassword123!", hash1) == 1);
    assert(auth_verify_password("MyPassword123!", hash2) == 1);

    /* Kiểm tra verify thất bại với sai password */
    assert(auth_verify_password("WrongPassword", hash1) == 0);
    assert(auth_verify_password("", hash1) == 0);

    printf("[PASS] Password hashing & verification passed.\n");
}

static void test_msg_parsing(void) {
    printf("[TEST] Testing auth message parser and json extraction...\n");

    const char *reg_json = "{\"type\":\"REGISTER\",\"payload\":{\"username\":\"bob123\",\"password\":\"secretPass\"}}\n";
    assert(parse_message_type(reg_json) == MSG_TYPE_REGISTER);

    char u[64] = {0};
    char p[64] = {0};
    assert(json_extract_string(reg_json, "username", u, sizeof(u)) == 0);
    assert(json_extract_string(reg_json, "password", p, sizeof(p)) == 0);
    assert(strcmp(u, "bob123") == 0);
    assert(strcmp(p, "secretPass") == 0);

    const char *login_json = "{\"type\":\"LOGIN\",\"payload\":{\"username\":\"bob123\",\"password\":\"secretPass\"}}\n";
    assert(parse_message_type(login_json) == MSG_TYPE_LOGIN);

    const char *logout_json = "{\"type\":\"LOGOUT\",\"session_id\":\"sess_12345\"}\n";
    assert(parse_message_type(logout_json) == MSG_TYPE_LOGOUT);
    char sess[64] = {0};
    assert(json_extract_string(logout_json, "session_id", sess, sizeof(sess)) == 0);
    assert(strcmp(sess, "sess_12345") == 0);

    /* Kiểm tra build message */
    char out_buf[256];
    assert(build_register_result(true, 10, "bob123", NULL, NULL, out_buf, sizeof(out_buf)) == 0);
    assert(strstr(out_buf, "\"status\":\"ok\"") != NULL);
    assert(strstr(out_buf, "\"username\":\"bob123\"") != NULL);

    assert(build_register_result(false, 0, "bob123", "USER_EXISTS", "Trùng tên", out_buf, sizeof(out_buf)) == 0);
    assert(strstr(out_buf, "\"status\":\"error\"") != NULL);
    assert(strstr(out_buf, "\"error_code\":\"USER_EXISTS\"") != NULL);

    printf("[PASS] Auth message parser & builder passed.\n");
}

static void test_auth_flow_simulation(void) {
    printf("[TEST] Testing end-to-end Auth lifecycle simulation in SQLite...\n");

    sqlite3 *db = NULL;
    assert(db_init(":memory:", &db) == DB_SUCCESS);

    /* 1. Đăng ký tài khoản */
    char hashed[256];
    assert(auth_hash_password("PassWord@2026", hashed, sizeof(hashed)) == 0);

    int64_t uid = 0;
    assert(db_user_create(db, "player_one", hashed, &uid) == DB_SUCCESS);
    assert(uid > 0);

    /* Đăng ký trùng phải trả về DB_ERR_EXISTS */
    assert(db_user_create(db, "PLAYER_ONE", hashed, NULL) == DB_ERR_EXISTS);

    /* 2. Đăng nhập sai mật khẩu */
    int64_t get_uid = 0;
    char get_hash[256];
    assert(db_user_get_by_username(db, "player_one", &get_uid, get_hash, sizeof(get_hash)) == DB_SUCCESS);
    assert(auth_verify_password("WrongPassword", get_hash) == 0);

    /* 3. Đăng nhập đúng mật khẩu */
    assert(auth_verify_password("PassWord@2026", get_hash) == 1);
    const char *token1 = "sess_player_one_token_1";
    assert(db_session_create(db, token1, get_uid, 3600) == DB_SUCCESS);

    /* 4. Xác thực session hợp lệ (yêu cầu game) */
    int64_t val_uid = 0;
    char val_user[64];
    assert(db_session_validate(db, token1, &val_uid, val_user, sizeof(val_user)) == DB_SUCCESS);
    assert(val_uid == get_uid);
    assert(strcmp(val_user, "player_one") == 0);

    /* 5. Gửi request với session sai hoặc hết hạn */
    assert(db_session_validate(db, "fake_token", &val_uid, val_user, sizeof(val_user)) == DB_ERR_NOTFOUND);

    /* 6. Đăng nhập lại tạo session mới -> session cũ bị vô hiệu hóa */
    const char *token2 = "sess_player_one_token_2";
    assert(db_session_create(db, token2, get_uid, 3600) == DB_SUCCESS);
    assert(db_session_validate(db, token1, &val_uid, val_user, sizeof(val_user)) == DB_ERR_NOTFOUND);
    assert(db_session_validate(db, token2, &val_uid, val_user, sizeof(val_user)) == DB_SUCCESS);

    /* 7. Đăng xuất (LOGOUT) */
    assert(db_session_invalidate(db, token2) == DB_SUCCESS);
    assert(db_session_validate(db, token2, &val_uid, val_user, sizeof(val_user)) == DB_ERR_NOTFOUND);

    db_close(db);
    printf("[PASS] Auth lifecycle flow passed successfully.\n");
}

int main(void) {
    printf("=== RUNNING AUTH & SESSION TESTS ===\n");
    test_crypto();
    test_msg_parsing();
    test_auth_flow_simulation();
    printf("=== ALL AUTH TESTS PASSED! ===\n");
    return 0;
}

