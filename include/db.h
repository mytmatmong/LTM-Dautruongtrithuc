#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DB_SUCCESS 0
#define DB_ERR_GENERIC -1
#define DB_ERR_EXISTS -2
#define DB_ERR_NOTFOUND -3

/**
 * Khởi tạo cơ sở dữ liệu SQLite: mở kết nối, bật khóa ngoại,
 * tạo các bảng `users`, `login_sessions` và các index cần thiết.
 * 
 * @param db_path Đường dẫn file sqlite (ví dụ "server.db" hoặc ":memory:").
 * @param out_db Con trỏ nhận sqlite3* handle khi mở thành công.
 * @return DB_SUCCESS (0) nếu thành công, mã lỗi âm nếu thất bại.
 */
int db_init(const char *db_path, sqlite3 **out_db);

/**
 * Đóng kết nối cơ sở dữ liệu SQLite và giải phóng tài nguyên.
 * 
 * @param db Handle database cần đóng.
 */
void db_close(sqlite3 *db);

/**
 * Thêm người dùng mới vào bảng `users`.
 * 
 * @param db Database handle.
 * @param username Tên đăng nhập (3 - 32 ký tự).
 * @param password_hash Chuỗi băm mật khẩu.
 * @param out_user_id (Tùy chọn) Con trỏ nhận user_id tự sinh (nếu != NULL).
 * @return DB_SUCCESS (0), DB_ERR_EXISTS nếu trùng username, hoặc DB_ERR_GENERIC.
 */
int db_user_create(sqlite3 *db, const char *username, const char *password_hash, int64_t *out_user_id);

/**
 * Lấy thông tin xác thực của người dùng qua username.
 * 
 * @param db Database handle.
 * @param username Tên đăng nhập cần tra cứu.
 * @param out_user_id Con trỏ nhận user_id.
 * @param out_hash_buf Buffer lưu chuỗi băm mật khẩu.
 * @param hash_buf_size Kích thước buffer out_hash_buf.
 * @return DB_SUCCESS (0), DB_ERR_NOTFOUND nếu không tồn tại, hoặc DB_ERR_GENERIC.
 */
int db_user_get_by_username(sqlite3 *db, const char *username, int64_t *out_user_id, char *out_hash_buf, size_t hash_buf_size);

/**
 * Tạo phiên đăng nhập mới trong bảng `login_sessions`.
 * Trước khi tạo, tự động vô hiệu hóa (is_active = 0) các phiên cũ của user này.
 * 
 * @param db Database handle.
 * @param session_id Chuỗi UUID / token nhận diện phiên.
 * @param user_id ID người dùng.
 * @param duration_seconds Thời hạn phiên tính từ thời điểm hiện tại (giây).
 * @return DB_SUCCESS (0) nếu thành công, mã lỗi âm nếu thất bại.
 */
int db_session_create(sqlite3 *db, const char *session_id, int64_t user_id, int duration_seconds);

/**
 * Xác thực phiên đăng nhập: kiểm tra token có tồn tại, is_active == 1 và chưa hết hạn.
 * 
 * @param db Database handle.
 * @param session_id Mã token phiên.
 * @param out_user_id Con trỏ nhận user_id nếu phiên hợp lệ.
 * @param out_username Con trỏ nhận username (tùy chọn, có thể NULL).
 * @param username_size Kích thước buffer out_username.
 * @return DB_SUCCESS (0), DB_ERR_NOTFOUND nếu không hợp lệ hoặc hết hạn, hoặc DB_ERR_GENERIC.
 */
int db_session_validate(sqlite3 *db, const char *session_id, int64_t *out_user_id, char *out_username, size_t username_size);

/**
 * Vô hiệu hóa một phiên (is_active = 0) khi logout.
 * 
 * @param db Database handle.
 * @param session_id Mã token phiên cần hủy.
 * @return DB_SUCCESS (0) nếu thành công.
 */
int db_session_invalidate(sqlite3 *db, const char *session_id);

/**
 * Vô hiệu hóa toàn bộ phiên của một user_id (khi client disconnect hoặc buộc đăng xuất).
 * 
 * @param db Database handle.
 * @param user_id ID người dùng.
 * @return DB_SUCCESS (0) nếu thành công.
 */
int db_session_invalidate_all_for_user(sqlite3 *db, int64_t user_id);

#define QUESTION_CONTENT_MAX_LEN 512
#define QUESTION_OPTIONS_MAX_LEN 512
#define QUESTION_CHOICE_MAX_LEN 8

/**
 * Cấu trúc thông tin câu hỏi lưu trữ nội bộ tại Server.
 * Chú ý: Trường correct_choice chỉ dùng nội bộ Server để đối chiếu và chấm đáp án!
 */
typedef struct {
    int64_t question_id;
    char content[QUESTION_CONTENT_MAX_LEN];
    char options_json[QUESTION_OPTIONS_MAX_LEN];
    char correct_choice[QUESTION_CHOICE_MAX_LEN]; /* Chỉ Server nắm giữ */
    int is_active;
} DbQuestion;

/**
 * Thêm một câu hỏi mới vào ngân hàng câu hỏi SQLite.
 * 
 * @param db Database handle.
 * @param content Nội dung câu hỏi.
 * @param options_json Các phương án dạng JSON (ví dụ '{"A":"...","B":"...","C":"...","D":"..."}').
 * @param correct_choice Đáp án đúng (ví dụ "A", "B", "C", "D").
 * @param out_question_id (Tùy chọn) Con trỏ nhận question_id vừa sinh.
 * @return DB_SUCCESS (0) nếu thành công, hoặc mã lỗi âm.
 */
int db_question_add(sqlite3 *db, const char *content, const char *options_json,
                    const char *correct_choice, int64_t *out_question_id);

/**
 * Lấy câu hỏi theo question_id (bao gồm đáp án đúng cho Server).
 * 
 * @param db Database handle.
 * @param question_id ID câu hỏi cần tìm.
 * @param out_q Con trỏ struct DbQuestion nhận dữ liệu.
 * @return DB_SUCCESS (0), DB_ERR_NOTFOUND nếu không có, hoặc DB_ERR_GENERIC.
 */
int db_question_get_by_id(sqlite3 *db, int64_t question_id, DbQuestion *out_q);

/**
 * Lấy ngẫu nhiên một câu hỏi đang hoạt động (is_active = 1) từ ngân hàng SQLite.
 * 
 * @param db Database handle.
 * @param out_q Con trỏ struct DbQuestion nhận dữ liệu.
 * @return DB_SUCCESS (0), DB_ERR_NOTFOUND nếu ngân hàng rỗng, hoặc DB_ERR_GENERIC.
 */
int db_question_get_random(sqlite3 *db, DbQuestion *out_q);

/**
 * Đóng gói dữ liệu câu hỏi thành chuỗi JSON an toàn gửi cho Client:
 * BẢO ĐẢM TUYỆT ĐỐI KHÔNG CHỨA `correct_choice`!
 * 
 * Định dạng:
 * {"question_id": 1, "content": "...", "options": {...}}
 * 
 * @param q Thông tin câu hỏi.
 * @param out_buf Buffer nhận chuỗi JSON an toàn.
 * @param out_size Kích thước buffer.
 * @return 0 nếu thành công, -1 nếu buffer quá nhỏ.
 */
int db_question_format_client_json(const DbQuestion *q, char *out_buf, size_t out_size);

/* =========================================================================
 * P9.3 — Ghi lịch sử lượt thi đấu theo giao dịch logic nhất quán (Atomic Transaction)
 * ========================================================================= */

#define DB_MAX_ROUND_ANSWERS 100
#define DB_MAX_SCORE_TRANSFERS 100

/* Dữ liệu một câu trả lời của người chơi để lưu DB */
typedef struct {
    int64_t user_id;
    char choice[8];
    int is_correct;
} DbRoundAnswerRecord;

/* Dữ liệu một biến động/chuyển dịch điểm để lưu DB */
typedef struct {
    int64_t from_user_id;
    int64_t to_user_id;
    int32_t amount_units;
    char reason[64];
} DbScoreTransferRecord;

/* Gói dữ liệu toàn bộ lịch sử cần commit của một lượt thi */
typedef struct {
    int64_t match_id;
    int round_id;
    int64_t question_id;
    int64_t main_user_id;
    const char *round_type;   /* "MAIN", "SELECTION" */
    const char *status;       /* "EVALUATED" */

    /* Danh sách answers trong lượt */
    DbRoundAnswerRecord answers[DB_MAX_ROUND_ANSWERS];
    int answer_count;

    /* Danh sách score_transfers */
    DbScoreTransferRecord transfers[DB_MAX_SCORE_TRANSFERS];
    int transfer_count;

    /* Sự kiện log cho game_logs */
    const char *event_type;
    const char *event_description;
} DbRoundHistoryTx;

/**
 * Ghi nhận toàn bộ thông tin một lượt thi gồm:
 * 1. Đảm bảo bảng `matches` tồn tại record cho match_id
 * 2. Ghi record vào bảng `rounds`
 * 3. Ghi tất cả records vào bảng `answers`
 * 4. Ghi tất cả records vào bảng `score_transfers`
 * 5. Ghi record vào bảng `game_logs`
 *
 * TOÀN BỘ ĐƯỢC THỰC HIỆN TRONG MỘT GIAO DỊCH SQLITE DUY NHẤT:
 * BEGIN IMMEDIATE TRANSACTION ... COMMIT / ROLLBACK.
 * Nếu bất kỳ thao tác nào thất bại hoặc dữ liệu không hợp lệ, lập tức ROLLBACK,
 * đảm bảo không bao giờ có tình trạng điểm đã đổi nhưng lịch sử không được lưu.
 *
 * @param db Database handle
 * @param tx Dữ liệu lịch sử cần ghi
 * @return DB_SUCCESS (0) nếu commit thành công; mã âm nếu lỗi và đã rollback.
 */
int db_round_record_history_tx(sqlite3 *db, const DbRoundHistoryTx *tx);

#endif /* DB_H */
