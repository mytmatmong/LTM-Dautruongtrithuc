#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SCHEMA_SQL =
    "PRAGMA foreign_keys = ON;\n"
    "CREATE TABLE IF NOT EXISTS users (\n"
    "    user_id       INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    username      TEXT NOT NULL COLLATE NOCASE,\n"
    "    password_hash TEXT NOT NULL,\n"
    "    created_at    DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),\n"
    "    CONSTRAINT uq_username UNIQUE (username),\n"
    "    CONSTRAINT chk_username_len CHECK (length(username) >= 3 AND length(username) <= 32)\n"
    ");\n"
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_users_username ON users(username);\n"
    "CREATE TABLE IF NOT EXISTS login_sessions (\n"
    "    session_id  TEXT PRIMARY KEY,\n"
    "    user_id     INTEGER NOT NULL,\n"
    "    login_at    DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),\n"
    "    expires_at  DATETIME NOT NULL,\n"
    "    is_active   INTEGER NOT NULL DEFAULT 1,\n"
    "    CONSTRAINT fk_sessions_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,\n"
    "    CONSTRAINT chk_session_is_active CHECK (is_active IN (0, 1))\n"
    ");\n"
    "CREATE INDEX IF NOT EXISTS idx_sessions_auth ON login_sessions(session_id, is_active);\n"
    "CREATE INDEX IF NOT EXISTS idx_sessions_user ON login_sessions(user_id, is_active);\n"
    "CREATE TABLE IF NOT EXISTS questions (\n"
    "    question_id    INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    content        TEXT NOT NULL,\n"
    "    options_json   TEXT NOT NULL,\n"
    "    correct_choice TEXT NOT NULL,\n"
    "    is_active      INTEGER NOT NULL DEFAULT 1,\n"
    "    created_at     DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),\n"
    "    CONSTRAINT chk_question_active CHECK (is_active IN (0, 1))\n"
    ");\n"
    "CREATE INDEX IF NOT EXISTS idx_questions_active ON questions(is_active);\n"
    "CREATE TABLE IF NOT EXISTS matches (\n"
    "    match_id       INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    status         TEXT NOT NULL DEFAULT 'ACTIVE',\n"
    "    started_at     DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),\n"
    "    ended_at       DATETIME,\n"
    "    winner_user_id INTEGER,\n"
    "    CONSTRAINT fk_matches_winner FOREIGN KEY (winner_user_id) REFERENCES users(user_id) ON DELETE SET NULL\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS match_players (\n"
    "    match_id    INTEGER NOT NULL,\n"
    "    user_id     INTEGER NOT NULL,\n"
    "    role        TEXT NOT NULL DEFAULT 'NONE',\n"
    "    status      TEXT NOT NULL DEFAULT 'ACTIVE',\n"
    "    score_units INTEGER NOT NULL DEFAULT 1000,\n"
    "    skip_count  INTEGER NOT NULL DEFAULT 0,\n"
    "    PRIMARY KEY (match_id, user_id),\n"
    "    CONSTRAINT fk_mp_match FOREIGN KEY (match_id) REFERENCES matches(match_id) ON DELETE CASCADE,\n"
    "    CONSTRAINT fk_mp_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,\n"
    "    CONSTRAINT chk_mp_score CHECK (score_units >= 0),\n"
    "    CONSTRAINT chk_mp_skip CHECK (skip_count >= 0 AND skip_count <= 2)\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS rounds (\n"
    "    round_id     INTEGER NOT NULL,\n"
    "    match_id     INTEGER NOT NULL,\n"
    "    question_id  INTEGER,\n"
    "    round_type   TEXT NOT NULL DEFAULT 'MAIN',\n"
    "    main_user_id INTEGER,\n"
    "    status       TEXT NOT NULL DEFAULT 'EVALUATED',\n"
    "    created_at   DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),\n"
    "    PRIMARY KEY (match_id, round_id),\n"
    "    CONSTRAINT fk_rounds_match FOREIGN KEY (match_id) REFERENCES matches(match_id) ON DELETE CASCADE,\n"
    "    CONSTRAINT fk_rounds_question FOREIGN KEY (question_id) REFERENCES questions(question_id) ON DELETE SET NULL,\n"
    "    CONSTRAINT fk_rounds_main FOREIGN KEY (main_user_id) REFERENCES users(user_id) ON DELETE SET NULL\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS answers (\n"
    "    round_id    INTEGER NOT NULL,\n"
    "    match_id    INTEGER NOT NULL,\n"
    "    user_id     INTEGER NOT NULL,\n"
    "    choice      TEXT NOT NULL,\n"
    "    received_at DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),\n"
    "    is_correct  INTEGER NOT NULL DEFAULT 0,\n"
    "    PRIMARY KEY (match_id, round_id, user_id),\n"
    "    CONSTRAINT fk_answers_round FOREIGN KEY (match_id, round_id) REFERENCES rounds(match_id, round_id) ON DELETE CASCADE,\n"
    "    CONSTRAINT fk_answers_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS score_transfers (\n"
    "    transfer_id  INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    round_id     INTEGER NOT NULL,\n"
    "    match_id     INTEGER NOT NULL,\n"
    "    from_user_id INTEGER,\n"
    "    to_user_id   INTEGER,\n"
    "    amount_units INTEGER NOT NULL,\n"
    "    reason       TEXT NOT NULL,\n"
    "    created_at   DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),\n"
    "    CONSTRAINT fk_st_round FOREIGN KEY (match_id, round_id) REFERENCES rounds(match_id, round_id) ON DELETE CASCADE\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS game_logs (\n"
    "    log_id      INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    match_id    INTEGER NOT NULL,\n"
    "    event_type  TEXT NOT NULL,\n"
    "    description TEXT NOT NULL,\n"
    "    created_at  DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),\n"
    "    CONSTRAINT fk_gl_match FOREIGN KEY (match_id) REFERENCES matches(match_id) ON DELETE CASCADE\n"
    ");\n";

int db_init(const char *db_path, sqlite3 **out_db) {
    if (!db_path || !out_db) return DB_ERR_GENERIC;

    sqlite3 *db = NULL;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] Cannot open database %s: %s\n", db_path, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        *out_db = NULL;
        return DB_ERR_GENERIC;
    }

    /* Bật WAL mode cho SQLite để hỗ trợ concurrency tốt hơn */
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB WARN] Failed to set WAL mode: %s\n", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
        err_msg = NULL;
    }

    rc = sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] Failed to initialize schema: %s\n", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
        sqlite3_close(db);
        *out_db = NULL;
        return DB_ERR_GENERIC;
    }

    *out_db = db;
    return DB_SUCCESS;
}

void db_close(sqlite3 *db) {
    if (db) {
        sqlite3_close(db);
    }
}

int db_user_create(sqlite3 *db, const char *username, const char *password_hash, int64_t *out_user_id) {
    if (!db || !username || !password_hash) return DB_ERR_GENERIC;

    size_t ulen = strlen(username);
    if (ulen < 3 || ulen > 32) {
        return DB_ERR_GENERIC;
    }

    const char *sql = "INSERT INTO users (username, password_hash) VALUES (?, ?);";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_user_create failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password_hash, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int result = DB_SUCCESS;

    if (rc == SQLITE_DONE) {
        if (out_user_id) {
            *out_user_id = (int64_t)sqlite3_last_insert_rowid(db);
        }
    } else if (rc == SQLITE_CONSTRAINT) {
        result = DB_ERR_EXISTS;
    } else {
        fprintf(stderr, "[DB ERROR] step db_user_create failed: %s\n", sqlite3_errmsg(db));
        result = DB_ERR_GENERIC;
    }

    sqlite3_finalize(stmt);
    return result;
}

int db_user_get_by_username(sqlite3 *db, const char *username, int64_t *out_user_id, char *out_hash_buf, size_t hash_buf_size) {
    if (!db || !username || !out_user_id || !out_hash_buf || hash_buf_size == 0) return DB_ERR_GENERIC;

    const char *sql = "SELECT user_id, password_hash FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_user_get_by_username failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int result = DB_SUCCESS;

    if (rc == SQLITE_ROW) {
        *out_user_id = sqlite3_column_int64(stmt, 0);
        const unsigned char *hash = sqlite3_column_text(stmt, 1);
        if (hash) {
            snprintf(out_hash_buf, hash_buf_size, "%s", (const char *)hash);
        } else {
            out_hash_buf[0] = '\0';
        }
    } else if (rc == SQLITE_DONE) {
        result = DB_ERR_NOTFOUND;
    } else {
        fprintf(stderr, "[DB ERROR] step db_user_get_by_username failed: %s\n", sqlite3_errmsg(db));
        result = DB_ERR_GENERIC;
    }

    sqlite3_finalize(stmt);
    return result;
}

int db_session_create(sqlite3 *db, const char *session_id, int64_t user_id, int duration_seconds) {
    if (!db || !session_id || user_id <= 0 || duration_seconds <= 0) return DB_ERR_GENERIC;

    /* Bước 1: Vô hiệu hóa các session cũ của user_id */
    db_session_invalidate_all_for_user(db, user_id);

    /* Bước 2: Tạo session mới */
    const char *sql = "INSERT INTO login_sessions (session_id, user_id, expires_at, is_active) "
                      "VALUES (?, ?, datetime('now', printf('+%d seconds', ?), 'localtime'), 1);";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_session_create failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, user_id);
    sqlite3_bind_int(stmt, 3, duration_seconds);

    rc = sqlite3_step(stmt);
    int result = (rc == SQLITE_DONE) ? DB_SUCCESS : DB_ERR_GENERIC;
    if (result != DB_SUCCESS) {
        fprintf(stderr, "[DB ERROR] step db_session_create failed: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return result;
}

int db_session_validate(sqlite3 *db, const char *session_id, int64_t *out_user_id, char *out_username, size_t username_size) {
    if (!db || !session_id || !out_user_id) return DB_ERR_GENERIC;

    const char *sql = "SELECT s.user_id, u.username FROM login_sessions s "
                      "JOIN users u ON s.user_id = u.user_id "
                      "WHERE s.session_id = ? AND s.is_active = 1 "
                      "AND s.expires_at > datetime('now', 'localtime');";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_session_validate failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int result = DB_SUCCESS;

    if (rc == SQLITE_ROW) {
        *out_user_id = sqlite3_column_int64(stmt, 0);
        if (out_username && username_size > 0) {
            const unsigned char *uname = sqlite3_column_text(stmt, 1);
            if (uname) {
                snprintf(out_username, username_size, "%s", (const char *)uname);
            } else {
                out_username[0] = '\0';
            }
        }
    } else if (rc == SQLITE_DONE) {
        result = DB_ERR_NOTFOUND;
    } else {
        fprintf(stderr, "[DB ERROR] step db_session_validate failed: %s\n", sqlite3_errmsg(db));
        result = DB_ERR_GENERIC;
    }

    sqlite3_finalize(stmt);
    return result;
}

int db_session_invalidate(sqlite3 *db, const char *session_id) {
    if (!db || !session_id) return DB_ERR_GENERIC;

    const char *sql = "UPDATE login_sessions SET is_active = 0 WHERE session_id = ?;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_session_invalidate failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int result = (rc == SQLITE_DONE) ? DB_SUCCESS : DB_ERR_GENERIC;

    sqlite3_finalize(stmt);
    return result;
}

int db_session_invalidate_all_for_user(sqlite3 *db, int64_t user_id) {
    if (!db || user_id <= 0) return DB_ERR_GENERIC;

    const char *sql = "UPDATE login_sessions SET is_active = 0 WHERE user_id = ? AND is_active = 1;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_session_invalidate_all_for_user failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    sqlite3_bind_int64(stmt, 1, user_id);

    rc = sqlite3_step(stmt);
    int result = (rc == SQLITE_DONE) ? DB_SUCCESS : DB_ERR_GENERIC;

    sqlite3_finalize(stmt);
    return result;
}

int db_question_add(sqlite3 *db, const char *content, const char *options_json,
                    const char *correct_choice, int64_t *out_question_id) {
    if (!db || !content || !options_json || !correct_choice) return DB_ERR_GENERIC;

    const char *sql = "INSERT INTO questions (content, options_json, correct_choice, is_active) "
                      "VALUES (?, ?, ?, 1);";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_question_add failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    sqlite3_bind_text(stmt, 1, content, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, options_json, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, correct_choice, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int result = DB_SUCCESS;

    if (rc == SQLITE_DONE) {
        if (out_question_id) {
            *out_question_id = (int64_t)sqlite3_last_insert_rowid(db);
        }
    } else {
        fprintf(stderr, "[DB ERROR] step db_question_add failed: %s\n", sqlite3_errmsg(db));
        result = DB_ERR_GENERIC;
    }

    sqlite3_finalize(stmt);
    return result;
}

static void extract_question_row(sqlite3_stmt *stmt, DbQuestion *out_q) {
    out_q->question_id = sqlite3_column_int64(stmt, 0);

    const unsigned char *txt_content = sqlite3_column_text(stmt, 1);
    snprintf(out_q->content, sizeof(out_q->content), "%s", txt_content ? (const char *)txt_content : "");

    const unsigned char *txt_opts = sqlite3_column_text(stmt, 2);
    snprintf(out_q->options_json, sizeof(out_q->options_json), "%s", txt_opts ? (const char *)txt_opts : "{}");

    const unsigned char *txt_correct = sqlite3_column_text(stmt, 3);
    snprintf(out_q->correct_choice, sizeof(out_q->correct_choice), "%s", txt_correct ? (const char *)txt_correct : "");

    out_q->is_active = sqlite3_column_int(stmt, 4);
}

int db_question_get_by_id(sqlite3 *db, int64_t question_id, DbQuestion *out_q) {
    if (!db || question_id <= 0 || !out_q) return DB_ERR_GENERIC;

    const char *sql = "SELECT question_id, content, options_json, correct_choice, is_active "
                      "FROM questions WHERE question_id = ?;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_question_get_by_id failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    sqlite3_bind_int64(stmt, 1, question_id);

    rc = sqlite3_step(stmt);
    int result = DB_SUCCESS;

    if (rc == SQLITE_ROW) {
        extract_question_row(stmt, out_q);
    } else if (rc == SQLITE_DONE) {
        result = DB_ERR_NOTFOUND;
    } else {
        fprintf(stderr, "[DB ERROR] step db_question_get_by_id failed: %s\n", sqlite3_errmsg(db));
        result = DB_ERR_GENERIC;
    }

    sqlite3_finalize(stmt);
    return result;
}

int db_question_get_random(sqlite3 *db, DbQuestion *out_q) {
    if (!db || !out_q) return DB_ERR_GENERIC;

    const char *sql = "SELECT question_id, content, options_json, correct_choice, is_active "
                      "FROM questions WHERE is_active = 1 ORDER BY RANDOM() LIMIT 1;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] prepare db_question_get_random failed: %s\n", sqlite3_errmsg(db));
        return DB_ERR_GENERIC;
    }

    rc = sqlite3_step(stmt);
    int result = DB_SUCCESS;

    if (rc == SQLITE_ROW) {
        extract_question_row(stmt, out_q);
    } else if (rc == SQLITE_DONE) {
        result = DB_ERR_NOTFOUND;
    } else {
        fprintf(stderr, "[DB ERROR] step db_question_get_random failed: %s\n", sqlite3_errmsg(db));
        result = DB_ERR_GENERIC;
    }

    sqlite3_finalize(stmt);
    return result;
}

int db_question_format_client_json(const DbQuestion *q, char *out_buf, size_t out_size) {
    if (!q || !out_buf || out_size == 0) return -1;

    /*
     * Định dạng gửi client: tuyệt đối KHÔNG chứa trường `correct_choice`!
     * Format: {"question_id":%lld,"content":"...","options":%s}
     */
    int written = snprintf(out_buf, out_size,
                           "{\"question_id\":%lld,\"content\":\"%s\",\"options\":%s}",
                           (long long)q->question_id,
                           q->content,
                           q->options_json[0] ? q->options_json : "{}");

    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }

    return 0;
}

/* =========================================================================
 * P9.3 — Ghi lịch sử lượt thi theo giao dịch logic nhất quán (Atomic Transaction)
 * ========================================================================= */

int db_round_record_history_tx(sqlite3 *db, const DbRoundHistoryTx *tx) {
    if (!db || !tx || tx->match_id <= 0 || tx->round_id <= 0) {
        return DB_ERR_GENERIC;
    }

    char *err_msg = NULL;
    /* 1. Mở giao dịch IMMEDIATE để giành quyền ghi, tránh deadlock giữa nhiều luồng */
    int rc = sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] BEGIN TRANSACTION failed: %s\n", err_msg ? err_msg : "");
        sqlite3_free(err_msg);
        return DB_ERR_GENERIC;
    }

    /* 2. Đảm bảo bản ghi matches tồn tại cho match_id */
    const char *sql_match = "INSERT OR IGNORE INTO matches (match_id, status) VALUES (?, 'ACTIVE');";
    sqlite3_stmt *stmt_match = NULL;
    rc = sqlite3_prepare_v2(db, sql_match, -1, &stmt_match, NULL);
    if (rc != SQLITE_OK) {
        goto rollback;
    }
    sqlite3_bind_int64(stmt_match, 1, tx->match_id);
    rc = sqlite3_step(stmt_match);
    sqlite3_finalize(stmt_match);
    if (rc != SQLITE_DONE) {
        goto rollback;
    }

    /* 3. Ghi thông tin lượt thi vào bảng rounds */
    const char *sql_round = "INSERT OR REPLACE INTO rounds "
                            "(round_id, match_id, question_id, round_type, main_user_id, status) "
                            "VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt_round = NULL;
    rc = sqlite3_prepare_v2(db, sql_round, -1, &stmt_round, NULL);
    if (rc != SQLITE_OK) {
        goto rollback;
    }
    sqlite3_bind_int(stmt_round, 1, tx->round_id);
    sqlite3_bind_int64(stmt_round, 2, tx->match_id);
    if (tx->question_id > 0) {
        sqlite3_bind_int64(stmt_round, 3, tx->question_id);
    } else {
        sqlite3_bind_null(stmt_round, 3);
    }
    sqlite3_bind_text(stmt_round, 4, tx->round_type ? tx->round_type : "MAIN", -1, SQLITE_STATIC);
    if (tx->main_user_id > 0) {
        sqlite3_bind_int64(stmt_round, 5, tx->main_user_id);
    } else {
        sqlite3_bind_null(stmt_round, 5);
    }
    sqlite3_bind_text(stmt_round, 6, tx->status ? tx->status : "EVALUATED", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt_round);
    sqlite3_finalize(stmt_round);
    if (rc != SQLITE_DONE) {
        goto rollback;
    }

    /* 4. Ghi tất cả câu trả lời của các người chơi vào bảng answers */
    if (tx->answer_count > 0) {
        const char *sql_ans = "INSERT OR REPLACE INTO answers "
                              "(round_id, match_id, user_id, choice, is_correct) "
                              "VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt *stmt_ans = NULL;
        rc = sqlite3_prepare_v2(db, sql_ans, -1, &stmt_ans, NULL);
        if (rc != SQLITE_OK) {
            goto rollback;
        }

        for (int i = 0; i < tx->answer_count; i++) {
            const DbRoundAnswerRecord *ans = &tx->answers[i];
            sqlite3_reset(stmt_ans);
            sqlite3_clear_bindings(stmt_ans);

            sqlite3_bind_int(stmt_ans, 1, tx->round_id);
            sqlite3_bind_int64(stmt_ans, 2, tx->match_id);
            sqlite3_bind_int64(stmt_ans, 3, ans->user_id);
            sqlite3_bind_text(stmt_ans, 4, ans->choice, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt_ans, 5, ans->is_correct ? 1 : 0);

            rc = sqlite3_step(stmt_ans);
            if (rc != SQLITE_DONE) {
                sqlite3_finalize(stmt_ans);
                goto rollback;
            }
        }
        sqlite3_finalize(stmt_ans);
    }

    /* 5. Ghi các bản ghi chuyển dịch điểm vào bảng score_transfers */
    if (tx->transfer_count > 0) {
        const char *sql_tr = "INSERT INTO score_transfers "
                             "(round_id, match_id, from_user_id, to_user_id, amount_units, reason) "
                             "VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt *stmt_tr = NULL;
        rc = sqlite3_prepare_v2(db, sql_tr, -1, &stmt_tr, NULL);
        if (rc != SQLITE_OK) {
            goto rollback;
        }

        for (int i = 0; i < tx->transfer_count; i++) {
            const DbScoreTransferRecord *tr = &tx->transfers[i];
            sqlite3_reset(stmt_tr);
            sqlite3_clear_bindings(stmt_tr);

            sqlite3_bind_int(stmt_tr, 1, tx->round_id);
            sqlite3_bind_int64(stmt_tr, 2, tx->match_id);
            if (tr->from_user_id > 0) {
                sqlite3_bind_int64(stmt_tr, 3, tr->from_user_id);
            } else {
                sqlite3_bind_null(stmt_tr, 3);
            }
            if (tr->to_user_id > 0) {
                sqlite3_bind_int64(stmt_tr, 4, tr->to_user_id);
            } else {
                sqlite3_bind_null(stmt_tr, 4);
            }
            sqlite3_bind_int(stmt_tr, 5, tr->amount_units);
            sqlite3_bind_text(stmt_tr, 6, tr->reason, -1, SQLITE_STATIC);

            rc = sqlite3_step(stmt_tr);
            if (rc != SQLITE_DONE) {
                sqlite3_finalize(stmt_tr);
                goto rollback;
            }
        }
        sqlite3_finalize(stmt_tr);
    }

    /* 6. Ghi nhật ký sự kiện vào bảng game_logs */
    if (tx->event_type && tx->event_description) {
        const char *sql_log = "INSERT INTO game_logs (match_id, event_type, description) VALUES (?, ?, ?);";
        sqlite3_stmt *stmt_log = NULL;
        rc = sqlite3_prepare_v2(db, sql_log, -1, &stmt_log, NULL);
        if (rc != SQLITE_OK) {
            goto rollback;
        }

        sqlite3_bind_int64(stmt_log, 1, tx->match_id);
        sqlite3_bind_text(stmt_log, 2, tx->event_type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_log, 3, tx->event_description, -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt_log);
        sqlite3_finalize(stmt_log);
        if (rc != SQLITE_DONE) {
            goto rollback;
        }
    }

    /* 7. Xác nhận giao dịch COMMIT */
    rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] COMMIT failed: %s\n", err_msg ? err_msg : "");
        sqlite3_free(err_msg);
        goto rollback;
    }

    return DB_SUCCESS;

rollback:
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    fprintf(stderr, "[DB WARN] Giao dịch ghi lịch sử lượt thi thất bại -> ĐÃ ROLLBACK toàn bộ.\n");
    return DB_ERR_GENERIC;
}
