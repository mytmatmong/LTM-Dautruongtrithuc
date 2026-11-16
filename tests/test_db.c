#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "db.h"

static void test_db_init_and_close(void) {
    printf("[TEST] Testing db_init with in-memory DB...\n");
    sqlite3 *db = NULL;
    int rc = db_init(":memory:", &db);
    assert(rc == DB_SUCCESS);
    assert(db != NULL);

    /* Kiểm tra bảng tồn tại bằng cách truy vấn sqlite_master */
    sqlite3_stmt *stmt = NULL;
    const char *check_sql = "SELECT count(*) FROM sqlite_master WHERE type='table' AND name IN ('users', 'login_sessions');";
    rc = sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    assert(rc == SQLITE_OK);
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    int table_count = sqlite3_column_int(stmt, 0);
    assert(table_count == 2);
    sqlite3_finalize(stmt);

    db_close(db);
    printf("[PASS] db_init and table creation passed.\n");
}

static void test_user_operations(void) {
    printf("[TEST] Testing users table CRUD & constraints...\n");
    sqlite3 *db = NULL;
    int rc = db_init(":memory:", &db);
    assert(rc == DB_SUCCESS);

    /* 1. Tạo user hợp lệ */
    int64_t user1_id = 0;
    rc = db_user_create(db, "alice", "hashed_secret_alice", &user1_id);
    assert(rc == DB_SUCCESS);
    assert(user1_id > 0);

    /* 2. Thử tạo trùng username (phải thất bại vì UNIQUE, không phân biệt hoa thường) */
    int64_t user2_id = 0;
    rc = db_user_create(db, "Alice", "another_hash", &user2_id);
    assert(rc == DB_ERR_EXISTS);

    /* 3. Thử tạo username quá ngắn (< 3 ký tự) - CHECK constraint */
    rc = db_user_create(db, "ab", "hash", &user2_id);
    assert(rc != DB_SUCCESS);

    /* 4. Truy vấn user thành công */
    int64_t found_id = 0;
    char found_hash[128];
    rc = db_user_get_by_username(db, "ALICE", &found_id, found_hash, sizeof(found_hash));
    assert(rc == DB_SUCCESS);
    assert(found_id == user1_id);
    assert(strcmp(found_hash, "hashed_secret_alice") == 0);

    /* 5. Truy vấn user không tồn tại */
    rc = db_user_get_by_username(db, "bob", &found_id, found_hash, sizeof(found_hash));
    assert(rc == DB_ERR_NOTFOUND);

    db_close(db);
    printf("[PASS] User operations passed.\n");
}

static void test_session_operations(void) {
    printf("[TEST] Testing login_sessions lifecycle & validation...\n");
    sqlite3 *db = NULL;
    int rc = db_init(":memory:", &db);
    assert(rc == DB_SUCCESS);

    int64_t user1_id = 0;
    rc = db_user_create(db, "charlie", "hash_charlie", &user1_id);
    assert(rc == DB_SUCCESS);

    const char *sess1 = "sess-token-1111-aaaa";
    const char *sess2 = "sess-token-2222-bbbb";

    /* 1. Tạo session 1 với hạn 3600s */
    rc = db_session_create(db, sess1, user1_id, 3600);
    assert(rc == DB_SUCCESS);

    /* 2. Validate session 1 hợp lệ */
    int64_t val_user_id = 0;
    char val_username[64];
    rc = db_session_validate(db, sess1, &val_user_id, val_username, sizeof(val_username));
    assert(rc == DB_SUCCESS);
    assert(val_user_id == user1_id);
    assert(strcmp(val_username, "charlie") == 0);

    /* 3. Tạo session 2 cho cùng user -> session 1 phải tự động bị vô hiệu (single session) */
    rc = db_session_create(db, sess2, user1_id, 3600);
    assert(rc == DB_SUCCESS);

    /* Kiểm tra session 1 không còn hợp lệ */
    rc = db_session_validate(db, sess1, &val_user_id, val_username, sizeof(val_username));
    assert(rc == DB_ERR_NOTFOUND);

    /* Session 2 đang hợp lệ */
    rc = db_session_validate(db, sess2, &val_user_id, val_username, sizeof(val_username));
    assert(rc == DB_SUCCESS);

    /* 4. Logout / Hủy session 2 */
    rc = db_session_invalidate(db, sess2);
    assert(rc == DB_SUCCESS);

    rc = db_session_validate(db, sess2, &val_user_id, val_username, sizeof(val_username));
    assert(rc == DB_ERR_NOTFOUND);

    /* 5. Khóa ngoại: Thử tạo session cho user_id không tồn tại */
    rc = db_session_create(db, "fake-sess", 999999, 3600);
    assert(rc != DB_SUCCESS);

    db_close(db);
    printf("[PASS] Session operations passed.\n");
}

static void test_question_operations(void) {
    printf("[TEST] Testing questions table, secret keeping & client formatting...\n");
    sqlite3 *db = NULL;
    int rc = db_init(":memory:", &db);
    assert(rc == DB_SUCCESS);

    /* 1. Ngân hàng trống: lấy random phải trả về DB_ERR_NOTFOUND */
    DbQuestion q_empty;
    rc = db_question_get_random(db, &q_empty);
    assert(rc == DB_ERR_NOTFOUND);

    /* 2. Thêm câu hỏi 1 */
    int64_t q1_id = 0;
    const char *content1 = "Thủ đô của Việt Nam là gì?";
    const char *opts1 = "{\"A\":\"Hà Nội\",\"B\":\"TP.HCM\",\"C\":\"Đà Nẵng\",\"D\":\"Cần Thơ\"}";
    const char *ans1 = "A";

    rc = db_question_add(db, content1, opts1, ans1, &q1_id);
    assert(rc == DB_SUCCESS);
    assert(q1_id > 0);

    /* 3. Thêm câu hỏi 2 */
    int64_t q2_id = 0;
    const char *content2 = "Giao thức truyền tải tin cậy của tầng Transport là gì?";
    const char *opts2 = "{\"A\":\"UDP\",\"B\":\"TCP\",\"C\":\"IP\",\"D\":\"ICMP\"}";
    const char *ans2 = "B";

    rc = db_question_add(db, content2, opts2, ans2, &q2_id);
    assert(rc == DB_SUCCESS);
    assert(q2_id > 0);

    /* 4. Truy vấn câu hỏi theo ID và kiểm tra server lưu giữ đúng đáp án */
    DbQuestion q_found;
    rc = db_question_get_by_id(db, q1_id, &q_found);
    assert(rc == DB_SUCCESS);
    assert(q_found.question_id == q1_id);
    assert(strcmp(q_found.content, content1) == 0);
    assert(strcmp(q_found.options_json, opts1) == 0);
    assert(strcmp(q_found.correct_choice, ans1) == 0); /* Server có đáp án đúng */
    assert(q_found.is_active == 1);

    /* 5. Lấy câu hỏi ngẫu nhiên từ DB */
    DbQuestion q_rand;
    rc = db_question_get_random(db, &q_rand);
    assert(rc == DB_SUCCESS);
    assert(q_rand.question_id == q1_id || q_rand.question_id == q2_id);
    if (q_rand.question_id == q1_id) {
        assert(strcmp(q_rand.correct_choice, ans1) == 0);
    } else {
        assert(strcmp(q_rand.correct_choice, ans2) == 0);
    }

    /* 6. Kiểm tra đóng gói JSON gửi cho Client: TUYỆT ĐỐI KHÔNG CHỨA ĐÁP ÁN ĐÚNG! */
    char client_json[1024];
    rc = db_question_format_client_json(&q_found, client_json, sizeof(client_json));
    assert(rc == 0);

    /* Xác minh trường correct_choice không xuất hiện trong chuỗi JSON gửi client */
    assert(strstr(client_json, "correct_choice") == NULL);
    assert(strstr(client_json, "\"correct\"") == NULL);
    /* Kiểm tra có đầy đủ question_id, content và options */
    assert(strstr(client_json, "\"question_id\":1") != NULL);
    assert(strstr(client_json, "Hà Nội") != NULL);
    assert(strstr(client_json, "TP.HCM") != NULL);

    /* 7. Truy vấn ID không tồn tại */
    rc = db_question_get_by_id(db, 99999, &q_found);
    assert(rc == DB_ERR_NOTFOUND);

    db_close(db);
    printf("[PASS] Question bank & security isolation passed.\n");
}

/* =========================================================================
 * Unit tests cho P9.3: Ghi lịch sử theo giao dịch (Atomic Transaction & Rollback)
 * ========================================================================= */

static void test_round_history_transaction_commit(void) {
    printf("[TEST] Testing db_round_record_history_tx COMMIT success...\n");
    sqlite3 *db = NULL;
    int rc = db_init(":memory:", &db);
    assert(rc == DB_SUCCESS);

    /* Chuẩn bị user và question */
    int64_t u1 = 0, u2 = 0;
    db_user_create(db, "alice_main", "hash1", &u1);
    db_user_create(db, "bob_sub", "hash2", &u2);

    int64_t q1 = 0;
    db_question_add(db, "TCP or UDP?", "{\"A\":\"TCP\",\"B\":\"UDP\"}", "A", &q1);

    DbRoundHistoryTx tx;
    memset(&tx, 0, sizeof(tx));
    tx.match_id = 1;
    tx.round_id = 1;
    tx.question_id = q1;
    tx.main_user_id = u1;
    tx.round_type = "MAIN";
    tx.status = "EVALUATED";
    tx.event_type = "ROUND_EVALUATED";
    tx.event_description = "Alice answered correct";

    /* Answers */
    tx.answers[0].user_id = u1;
    strncpy(tx.answers[0].choice, "A", sizeof(tx.answers[0].choice));
    tx.answers[0].is_correct = 1;

    tx.answers[1].user_id = u2;
    strncpy(tx.answers[1].choice, "B", sizeof(tx.answers[1].choice));
    tx.answers[1].is_correct = 0;
    tx.answer_count = 2;

    /* Score transfer: Bob chuyển 1000 cho Alice */
    tx.transfers[0].from_user_id = u2;
    tx.transfers[0].to_user_id = u1;
    tx.transfers[0].amount_units = 1000;
    strncpy(tx.transfers[0].reason, "MAIN_CORRECT_TRANSFER", sizeof(tx.transfers[0].reason));
    tx.transfer_count = 1;

    rc = db_round_record_history_tx(db, &tx);
    assert(rc == DB_SUCCESS);

    /* Xác minh dữ liệu đã vào cả 4 bảng: rounds, answers, score_transfers, game_logs */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM rounds WHERE match_id=1 AND round_id=1;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 1);
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM answers WHERE match_id=1 AND round_id=1;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 2);
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM score_transfers WHERE match_id=1 AND round_id=1;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 1);
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM game_logs WHERE match_id=1;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 1);
    sqlite3_finalize(stmt);

    db_close(db);
    printf("[PASS] db_round_record_history_tx COMMIT passed.\n");
}

static void test_round_history_transaction_rollback(void) {
    printf("[TEST] Testing db_round_record_history_tx ROLLBACK on integrity error...\n");
    sqlite3 *db = NULL;
    int rc = db_init(":memory:", &db);
    assert(rc == DB_SUCCESS);

    int64_t u1 = 0;
    db_user_create(db, "alice_main", "hash1", &u1);

    DbRoundHistoryTx tx;
    memset(&tx, 0, sizeof(tx));
    tx.match_id = 2;
    tx.round_id = 1;
    tx.main_user_id = u1;
    tx.round_type = "MAIN";

    /* Cố ý gây vi phạm FOREIGN KEY ở bảng answers bằng cách đưa một user_id không tồn tại (999999) */
    tx.answers[0].user_id = 999999;
    strncpy(tx.answers[0].choice, "A", sizeof(tx.answers[0].choice));
    tx.answers[0].is_correct = 1;
    tx.answer_count = 1;

    rc = db_round_record_history_tx(db, &tx);
    /* Giao dịch phải thất bại do lỗi khóa ngoại */
    assert(rc != DB_SUCCESS);

    /* Xác minh tính nguyên tử: KHÔNG CÓ BẤT KỲ DỮ LIỆU NÀO ĐƯỢC LƯU LẠI (TOÀN BỘ ĐÃ ROLLBACK) */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM rounds WHERE match_id=2;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 0); /* rounds không được lưu */
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM answers WHERE match_id=2;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 0); /* answers không được lưu */
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM game_logs WHERE match_id=2;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 0); /* game_logs không được lưu */
    sqlite3_finalize(stmt);

    db_close(db);
    printf("[PASS] db_round_record_history_tx ROLLBACK passed.\n");
}

int main(void) {
    printf("=== RUNNING DB INITIALIZATION & QUERY TESTS ===\n");
    test_db_init_and_close();
    test_user_operations();
    test_session_operations();
    test_question_operations();
    test_round_history_transaction_commit();
    test_round_history_transaction_rollback();
    printf("=== ALL DB TESTS PASSED SUCCESSFULLY! ===\n");
    return 0;
}

