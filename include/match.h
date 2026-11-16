#ifndef MATCH_H
#define MATCH_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define MAX_MATCH_PLAYERS 100 /* Tối đa 100 người chơi */
#define INITIAL_SCORE_UNITS                                                    \
  1000 /* 10 điểm = 1000 đơn vị (100 units = 1 điểm) */
#define USERNAME_MAX_LEN 32
#define SESSION_TOKEN_LEN 64

#define TIMEOUT_SUB_SECONDS 30  /* Thời hạn 30 giây cho người phụ */
#define TIMEOUT_MAIN_SECONDS 60 /* Thời hạn 60 giây cho người chính */

/* Vai trò người chơi trong trận */
typedef enum {
  ROLE_NONE = 0, /* Trong lobby hoặc chưa phân vai */
  ROLE_MAIN,     /* Người chơi chính */
  ROLE_SUB       /* Người chơi phụ */
} PlayerRole;

/* Trạng thái người chơi */
typedef enum {
  PLAYER_STATUS_LOBBY = 0,   /* Đang trong phòng chờ */
  PLAYER_STATUS_ACTIVE,      /* Đang tham gia thi đấu */
  PLAYER_STATUS_ELIMINATED,  /* Bị loại */
  PLAYER_STATUS_DISCONNECTED /* Mất kết nối */
} PlayerStatus;

/* Trạng thái trận đấu */
typedef enum {
  MATCH_STATE_IDLE = 0,
  MATCH_STATE_LOBBY, /* Đang mở phòng chờ */
  MATCH_STATE_SELECTING_MAIN,
  MATCH_STATE_ROUND_ACTIVE,
  MATCH_STATE_ROUND_EVALUATING,
  MATCH_STATE_ENDED
} MatchState;

/* Thông tin người chơi trong RAM */
typedef struct {
  int64_t user_id;
  char username[USERNAME_MAX_LEN + 1];
  char session_token[SESSION_TOKEN_LEN + 1];
  int socket_fd;
  PlayerRole role;
  PlayerStatus status;
  int32_t score_units; /* Khởi tạo 1000 (tức 10 điểm) khi vào trận */
  int skip_count;      /* <= 2 */
  time_t joined_at;
} MatchPlayer;

/* Thông tin lượt trả lời câu hỏi của người chơi */
typedef struct {
  int64_t user_id;
  char choice[8];
  uint64_t server_time_ns; /* Thời điểm nhận đáp án hợp lệ do Server ghi nhận
                              (nanoseconds) */
  bool is_correct; /* Đối chiếu đáp án đúng trên Server */
  bool answered;   /* Đã gửi đáp án trong lượt này chưa */
} SelectionAnswerRecord;

/* Trận đấu trong RAM */
typedef struct {
  int64_t match_id;
  MatchState state;
  MatchPlayer players[MAX_MATCH_PLAYERS];
  int player_count;
  int current_round;
  int64_t main_user_id;
  int64_t winner_user_id;
  time_t round_started_at;
  time_t round_deadline;

  /* Dữ liệu lượt chọn người chơi chính (P7.2) */
  int64_t selection_question_id;
  char selection_correct_choice[8]; /* Chỉ Server nắm giữ */
  SelectionAnswerRecord selection_answers[MAX_MATCH_PLAYERS];
  int selection_answer_count;

  /* Dữ liệu lượt thi đấu chính thức (P7.3, P7.4) */
  int64_t round_question_id;
  char round_correct_choice[8]; /* Chỉ Server nắm giữ */
  time_t sub_deadline;          /* Hạn chót cho người phụ (now + 30s) */
  time_t main_deadline;         /* Hạn chót cho người chính (now + 60s) */
  SelectionAnswerRecord round_answers[MAX_MATCH_PLAYERS];
  int round_answer_count;
  bool round_closed;    /* Đã chốt nhận đáp án lượt thi hay chưa */
  bool round_evaluated; /* Đã chấm lượt này chưa (chặn chấm đúp / chấm 2 lần) */
} MatchContext;

/* Mã lỗi khi join lobby */
typedef enum {
  JOIN_LOBBY_OK = 0,
  JOIN_LOBBY_ERR_UNAUTHORIZED,
  JOIN_LOBBY_ERR_ALREADY_JOINED,
  JOIN_LOBBY_ERR_LOBBY_FULL,
  JOIN_LOBBY_ERR_INVALID_STATE,
  JOIN_LOBBY_ERR_INVALID_ARGS
} JoinLobbyResult;

/* Mã kết quả khi bắt đầu trận */
typedef enum {
  START_GAME_OK = 0,
  START_GAME_ERR_UNAUTHORIZED,       /* Không phải chủ phòng / chưa đăng nhập */
  START_GAME_ERR_NOT_IN_LOBBY,       /* Người gửi không ở trong phòng chờ */
  START_GAME_ERR_NOT_ENOUGH_PLAYERS, /* Chưa đủ người chơi tối thiểu (ít nhất 2)
                                      */
  START_GAME_ERR_INVALID_STATE       /* Phòng không ở trạng thái LOBBY */
} StartGameResult;

/* Khởi tạo trận đấu / lobby */
void match_init(MatchContext *ctx, int64_t match_id);

/* Thêm người chơi vào phòng chờ */
JoinLobbyResult match_join_lobby(MatchContext *ctx, int64_t user_id,
                                 const char *username,
                                 const char *session_token, int socket_fd);

/* Bắt đầu trận đấu: kiểm tra quyền host, khởi tạo 10 điểm, chuyển sang
 * SELECTING_MAIN */
StartGameResult match_start_game(MatchContext *ctx, int64_t requester_user_id);

/* Xóa người chơi khỏi lobby khi thoát/ngắt kết nối */
bool match_remove_player(MatchContext *ctx, int64_t user_id);

/* Khởi tạo điểm cho toàn bộ người chơi khi trận bắt đầu (mỗi người 10 điểm =
 * 1000 units) */
void match_start_initialize_scores(MatchContext *ctx);

/* Tạo thông điệp JSON LOBBY_UPDATED broadcast tới mọi client trong phòng */
int match_build_lobby_updated(const MatchContext *ctx, char *out_buf,
                              size_t out_size);

/* Tạo thông điệp phản hồi kết quả JOIN_LOBBY_RESULT */
int match_build_join_result(JoinLobbyResult res, int64_t match_id,
                            int player_count, char *out_buf, size_t out_size);

/* Tạo thông điệp broadcast GAME_STARTED */
int match_build_game_started(const MatchContext *ctx, char *out_buf,
                             size_t out_size);

/* Tạo thông điệp phản hồi lỗi START_GAME_RESULT (khi start thất bại) */
int match_build_start_game_result(StartGameResult res, char *out_buf,
                                  size_t out_size);

/* Mã kết quả khi submit SELECTION_ANSWER */
typedef enum {
  SELECTION_ANS_OK = 0,
  SELECTION_ANS_ERR_INVALID_STATE,
  SELECTION_ANS_ERR_NOT_IN_MATCH,
  SELECTION_ANS_ERR_ALREADY_SUBMITTED,
  SELECTION_ANS_ERR_TIMEOUT,
  SELECTION_ANS_ERR_INVALID_CHOICE
} SelectionAnswerResult;

/**
 * Thiết lập câu hỏi chọn người chơi chính (lấy từ SQLite) và mở lượt.
 * @param correct_choice Đáp án đúng (server giữ).
 */
void match_setup_selection_question(MatchContext *ctx, int64_t question_id,
                                    const char *correct_choice,
                                    int timeout_seconds);

/**
 * Ghi nhận đáp án câu hỏi chọn người chơi chính.
 * Ghi nhận thời điểm server_time_ns độc lập với client.
 */
SelectionAnswerResult match_submit_selection_answer(MatchContext *ctx,
                                                    int64_t user_id,
                                                    const char *choice,
                                                    uint64_t server_time_ns);

/**
 * Quyết định người chơi chính dựa trên danh sách câu trả lời đúng đến sớm nhất
 * (server_time_ns). Nếu thời gian bằng nhau (tie), áp dụng tie-break rule:
 * user_id nhỏ hơn ưu tiên trước. Nếu không ai trả lời đúng, tie-break chọn
 * người trả lời đầu tiên (hoặc host). Sau khi chọn xong, phân vai:
 * - Người được chọn: ROLE_MAIN
 * - Các người chơi còn lại: ROLE_SUB
 * Trạng thái chuyển sang MATCH_STATE_ROUND_ACTIVE.
 * @return user_id của người chơi chính được chọn.
 */
int64_t match_evaluate_selection(MatchContext *ctx);

/* Tạo thông điệp SELECTION_QUESTION gửi tới client (tuyệt đối không chứa
 * correct_choice) */
int match_build_selection_question(const MatchContext *ctx, const char *content,
                                   const char *options_json,
                                   int timeout_seconds, char *out_buf,
                                   size_t out_size);

/* Tạo thông điệp SELECTION_ANSWER_RESULT phản hồi cho người nộp */
int match_build_selection_answer_result(SelectionAnswerResult res,
                                        char *out_buf, size_t out_size);

/* Tạo thông điệp broadcast ROLE_ASSIGNED công bố người chơi chính */
int match_build_role_assigned(const MatchContext *ctx, char *out_buf,
                              size_t out_size);

/* Lấy thời gian đồng hồ máy chủ độ phân giải nano giây */
uint64_t match_get_server_time_ns(void);

/**
 * Thiết lập lượt thi đấu chính thức (P7.3):
 * - Đặt round_question_id và lưu round_correct_choice bí mật trên Server.
 * - sub_deadline = round_started_at + TIMEOUT_SUB_SECONDS (30s)
 * - main_deadline = round_started_at + TIMEOUT_MAIN_SECONDS (60s)
 * - round_deadline = main_deadline
 * - round_closed = false
 */
void match_setup_round_question(MatchContext *ctx, int64_t question_id,
                                const char *correct_choice);

/**
 * Kiểm tra xem người chơi đã hết hạn nộp đáp án hay chưa theo vai trò (P7.3):
 * - ROLE_SUB: hạn chót là sub_deadline (30 giây)
 * - ROLE_MAIN: hạn chót là main_deadline (60 giây)
 * @param current_server_time Thời gian máy chủ hiện tại
 * @return true nếu đã quá hạn nộp, false nếu còn trong hạn
 */
bool match_is_player_timed_out(const MatchContext *ctx, int64_t user_id,
                               time_t current_server_time);

/**
 * Chốt lượt thi đấu (P7.3):
 * Đánh dấu round_closed = true và chuyển trạng thái sang
 * MATCH_STATE_ROUND_EVALUATING. Sau khi chốt lượt, mọi đáp án gửi đến muộn đều
 * bị từ chối.
 */
void match_close_round(MatchContext *ctx);

/* Mã kết quả khi submit ANSWER_SUBMIT (P7.4) */
typedef enum {
  ANSWER_ACK_OK = 0,
  ANSWER_ACK_ERR_UNAUTHORIZED,
  ANSWER_ACK_ERR_INVALID_MATCH,
  ANSWER_ACK_ERR_INVALID_ROUND,
  ANSWER_ACK_ERR_INVALID_ROLE,
  ANSWER_ACK_ERR_INVALID_STATE,
  ANSWER_ACK_ERR_ALREADY_SUBMITTED,
  ANSWER_ACK_ERR_TIMEOUT,
  ANSWER_ACK_ERR_INVALID_CHOICE
} AnswerSubmitResult;

/**
 * Xử lý nộp đáp án lượt chơi chính thức ANSWER_SUBMIT (P7.4).
 * Kiểm tra toàn diện:
 * - Session & user hợp lệ
 * - match_id khớp với trận hiện tại
 * - round_id khớp với lượt thi hiện tại
 * - Vai trò người chơi (ROLE_MAIN hoặc ROLE_SUB)
 * - Trạng thái trận (ROUND_ACTIVE) và round_closed == false
 * - Hạn giờ (timeout theo vai trò: SUB 30s, MAIN 60s)
 * - Định dạng lựa chọn (A, B, C, D)
 * - Đảm bảo chỉ nhận tối đa MỘT đáp án hợp lệ mỗi người mỗi lượt (chặn nộp
 * lại).
 */
AnswerSubmitResult match_submit_round_answer_validated(
    MatchContext *ctx, int64_t user_id, int64_t match_id, int round_id,
    const char *choice, time_t current_server_time, uint64_t server_time_ns);

/* Tạo thông điệp phản hồi ANSWER_ACK gửi lại cho client */
int match_build_answer_ack(AnswerSubmitResult res, int round_id, char *out_buf,
                           size_t out_size);

/* Tạo thông điệp broadcast ROUND_STARTED tới các client khi bắt đầu lượt mới */
int match_build_round_started(const MatchContext *ctx, int round_id,
                              const char *content, const char *options_json,
                              int timeout_seconds, char *out_buf,
                              size_t out_size);

/* =========================================================================
 * P8.2 — Bộ xử lý kết quả lượt thi đấu độc lập với socket
 * ========================================================================= */

/* Lý do / kết quả chi tiết của lượt thi */
typedef enum {
  ROUND_OUTCOME_MAIN_CORRECT = 0,    /* Người chính đúng */
  ROUND_OUTCOME_MAIN_WRONG,          /* Người chính sai */
  ROUND_OUTCOME_MAIN_SKIPPED,        /* Người chính bỏ qua (chờ chốt luật P8.4) */
  ROUND_OUTCOME_INVALID_STATE        /* Trạng thái không hợp lệ để đánh giá */
} RoundOutcomeType;

/* Kết quả cập nhật của từng người chơi sau một lượt */
typedef struct {
  int64_t user_id;
  int32_t old_score_units;
  int32_t score_delta_units; /* Thay đổi điểm (+ / -) */
  int32_t new_score_units;   /* Điểm sau khi áp dụng */
  PlayerStatus old_status;
  PlayerStatus new_status;   /* Có bị ELIMINATED không */
  PlayerRole old_role;
  PlayerRole new_role;       /* Vai trò mới (MAIN hay SUB) */
} PlayerRoundDelta;

/* Kết quả toàn bộ lượt thi trả về từ bộ xử lý thuần túy */
typedef struct {
  RoundOutcomeType outcome;
  int round_id;
  int64_t old_main_id;
  int64_t new_main_id;       /* Người chính cho lượt tiếp theo (nếu chuyển vai) */
  int64_t winner_user_id;    /* Người thắng trận (-1 nếu chưa có hoặc không có) */
  MatchState new_match_state;/* ROUND_ACTIVE nếu tiếp tục, ENDED nếu kết thúc */

  /* Danh sách biến động điểm và trạng thái của từng người chơi */
  PlayerRoundDelta player_deltas[MAX_MATCH_PLAYERS];
  int delta_count;
} RoundEvaluationResult;

/**
 * Hàm đánh giá kết quả một lượt thi đấu độc lập hoàn toàn với socket (P8.2).
 * Chỉ nhận trạng thái người chơi và danh sách đáp án đã chốt trong ctx.
 * Không thực hiện I/O mạng hay ghi DB trực tiếp.
 *
 * Nhánh luật đã được xác nhận (AGENTS.md mục 4):
 * 1. Người chính ĐÚNG:
 *    - Nhận tổng điểm của các người phụ trả lời sai (hoặc không trả lời/hết hạn).
 *    - Toàn bộ người phụ sai bị loại (ELIMINATED).
 *    - Người phụ đúng giữ nguyên điểm và tiếp tục.
 *    - Nếu không còn người phụ nào đúng (toàn bộ phụ đều sai) -> Người chính THẮNG (winner = main_user_id, MATCH_STATE_ENDED).
 * 2. Người chính SAI (hoặc không trả lời/hết hạn):
 *    - Người chính bị loại (ELIMINATED).
 *    - Điểm của người chính được chia đều cho các người phụ còn sống trong trận.
 *    - Người phụ có đáp án đúng gửi đến server sớm nhất (server_time_ns) được thăng hạng làm tân người chính (ROLE_MAIN).
 *    - Nếu toàn bộ người phụ đều sai -> Trận kết thúc không có người thắng (winner = -1, MATCH_STATE_ENDED).
 *
 * @param ctx Con trỏ MatchContext chứa danh sách players và round_answers đã chốt.
 * @param out_eval Con trỏ nhận kết quả đánh giá chi tiết.
 * @return 0 nếu thành công, mã âm nếu lỗi tham số hoặc trạng thái không hợp lệ.
 */
int match_evaluate_round_pure(const MatchContext *ctx, RoundEvaluationResult *out_eval);

/**
 * Áp dụng kết quả đánh giá (RoundEvaluationResult) vào MatchContext trong RAM.
 * Đảm bảo tính toán toàn vẹn trước khi cập nhật; không làm hỏng trạng thái nếu có lỗi.
 */
int match_apply_evaluation(MatchContext *ctx, const RoundEvaluationResult *eval);

/**
 * Đánh giá và cập nhật kết quả lượt thi có bảo vệ nguyên tử (P8.3):
 * - Kiểm tra round_closed == true và round_evaluated == false.
 * - Bảo đảm mỗi lượt CHỈ ĐƯỢC CHẤM MỘT LẦN (tránh race condition / double evaluation).
 * - Kiểm tra toàn vẹn điểm trước và sau tính toán: bảo đảm không thất thoát điểm
 *   (tổng điểm sau đánh giá luôn bằng tổng điểm trước đánh giá).
 * - Nếu phát hiện bất kỳ lỗi hoặc vi phạm toàn vẹn, hoàn tác và giữ nguyên trạng thái cũ,
 *   tuyệt đối không cập nhật điểm một phần.
 * - Đánh dấu round_evaluated = true khi hoàn tất thành công.
 *
 * @param ctx Con trỏ MatchContext.
 * @param out_eval (Tùy chọn) Con trỏ nhận kết quả đánh giá (có thể NULL nếu không cần).
 * @return 0 nếu thành công; mã âm nếu trạng thái không hợp lệ, đã chấm rồi, hoặc lỗi dữ liệu.
 */
int match_evaluate_and_commit_round(MatchContext *ctx, RoundEvaluationResult *out_eval);

/* Định dạng thông điệp broadcast ROUND_RESULT gửi tới client */
int match_build_round_result(const RoundEvaluationResult *eval, char *out_buf,
                             size_t out_size);

/* =========================================================================
 * P9.1 — Phát thông báo ROUND_STARTED, ROUND_RESULT và SCORE_UPDATED
 * ========================================================================= */

/**
 * Định dạng thông điệp broadcast SCORE_UPDATED gửi tới client (P9.1).
 * Công bố bảng điểm hiện tại của các người chơi còn trong trận đấu.
 *
 * @param ctx Con trỏ MatchContext
 * @param out_buf Buffer nhận chuỗi JSON
 * @param out_size Kích thước buffer
 * @return Số byte đã ghi, hoặc âm nếu lỗi
 */
int match_build_score_updated(const MatchContext *ctx, char *out_buf,
                              size_t out_size);

/**
 * Lấy danh sách socket fd của các client còn trong trận (P9.1).
 * Chỉ lấy những người chơi có socket_fd hợp lệ (>= 0) và trạng thái còn trong trận:
 * bao gồm PLAYER_STATUS_ACTIVE, hoặc tùy chọn allow_eliminated = true để lấy cả người
 * vừa bị loại trong lượt hiện tại (nhằm nhận thông báo ROUND_RESULT/SCORE_UPDATED cuối cùng).
 *
 * @param ctx Con trỏ MatchContext
 * @param allow_eliminated Nếu true, bao gồm cả PLAYER_STATUS_ELIMINATED
 * @param out_sockets Mảng nhận danh sách socket
 * @param max_sockets Dung lượng tối đa của mảng out_sockets
 * @return Số lượng socket thu thập được
 */
int match_get_active_player_sockets(const MatchContext *ctx, bool allow_eliminated,
                                    int *out_sockets, int max_sockets);

/**
 * Phát (broadcast) thông điệp an toàn tới danh sách socket (P9.1).
 * Thiết lập timeout socket gửi (SO_SNDTIMEO) ngắn (ví dụ 1000ms) để một client bị nghẽn mạng,
 * gửi chậm hoặc lỗi I/O KHÔNG chặn vô hạn luồng của server hay các client khác.
 * Bỏ qua hoặc đóng socket lỗi một cách an toàn mà không làm gián đoạn việc gửi cho các client khác.
 *
 * @param sockets Mảng danh sách socket cần gửi
 * @param socket_count Số lượng socket trong mảng
 * @param message Chuỗi thông điệp JSON kết thúc bằng '\n'
 * @param message_len Độ dài thông điệp
 * @param timeout_ms Thời gian timeout gửi tối đa (mili-giây) cho mỗi socket
 * @param out_failed_sockets (Tùy chọn) Mảng nhận danh sách socket bị lỗi/timeout
 * @param max_failed Dung lượng mảng out_failed_sockets
 * @return Số lượng client đã gửi thành công
 */
int match_broadcast_message_safe(const int *sockets, int socket_count,
                                 const char *message, size_t message_len,
                                 int timeout_ms, int *out_failed_sockets,
                                 int max_failed);

/* =========================================================================
 * P8.5 — Điều kiện kết thúc trận và thông điệp GAME_FINISHED
 * ========================================================================= */

/* Lý do kết thúc trận đấu */
typedef enum {
  END_REASON_WINNER = 0,         /* Có người chiến thắng (người chính loại toàn bộ người phụ) */
  END_REASON_ALL_WRONG,          /* Tất cả người chơi phụ đều sai khi người chính sai -> không ai thắng */
  END_REASON_ABORTED             /* Trận đấu bị hủy hoặc không đủ người tiếp tục */
} MatchEndReason;

/**
 * Kiểm tra xem trận đấu đã đủ điều kiện kết thúc chưa theo luật đã duyệt (AGENTS.md):
 * 1. Có người chiến thắng: người chính trả lời đúng và không còn người phụ nào đúng (winner_user_id > 0).
 * 2. Không có người thắng: người chính sai và toàn bộ người phụ đều sai (winner_user_id == -1 và không còn người phụ đúng).
 * 3. Trạng thái trận đấu ctx->state == MATCH_STATE_ENDED.
 *
 * @param ctx Con trỏ MatchContext.
 * @param out_reason (Tùy chọn) Con trỏ nhận lý do kết thúc.
 * @return true nếu trận đấu đã kết thúc, false nếu trận vẫn tiếp tục lượt kế tiếp.
 */
bool match_is_game_finished(const MatchContext *ctx, MatchEndReason *out_reason);

/**
 * Đóng gói thông điệp broadcast GAME_FINISHED gửi tới toàn bộ client khi kết thúc trận.
 * Bao gồm: match_id, winner_user_id (-1 nếu không ai thắng), winner_username,
 * reason ("WINNER", "ALL_WRONG"), và bảng tổng sắp điểm số cuối cùng của mọi người chơi.
 *
 * @param ctx Con trỏ MatchContext.
 * @param out_buf Buffer nhận chuỗi JSON kết quả.
 * @param out_size Kích thước buffer.
 * @return Số byte đã ghi, hoặc âm nếu lỗi buffer.
 */
int match_build_game_finished(const MatchContext *ctx, char *out_buf, size_t out_size);

#endif /* MATCH_H */
