#include "match.h"
#include "net_framing.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

uint64_t match_get_server_time_ns(void) {
#ifdef _WIN32
  LARGE_INTEGER freq, counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  if (freq.QuadPart == 0)
    return (uint64_t)time(NULL) * 1000000000ULL;
  return (uint64_t)((counter.QuadPart * 1000000000ULL) / freq.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

void match_init(MatchContext *ctx, int64_t match_id) {
  if (!ctx)
    return;
  memset(ctx, 0, sizeof(MatchContext));
  ctx->match_id = match_id;
  ctx->state = MATCH_STATE_LOBBY;
  ctx->player_count = 0;
  ctx->current_round = 0;
  ctx->main_user_id = -1;
  ctx->winner_user_id = -1;
}

JoinLobbyResult match_join_lobby(MatchContext *ctx, int64_t user_id,
                                 const char *username,
                                 const char *session_token, int socket_fd) {
  if (!ctx || user_id <= 0 || !username || !session_token) {
    return JOIN_LOBBY_ERR_INVALID_ARGS;
  }

  if (ctx->state != MATCH_STATE_LOBBY) {
    return JOIN_LOBBY_ERR_INVALID_STATE;
  }

  /* Kiểm tra xem người dùng đã tham gia phòng chưa (chặn người tham gia trùng)
   */
  for (int i = 0; i < ctx->player_count; i++) {
    if (ctx->players[i].user_id == user_id) {
      return JOIN_LOBBY_ERR_ALREADY_JOINED;
    }
  }

  /* Kiểm tra giới hạn tối đa 100 người */
  if (ctx->player_count >= MAX_MATCH_PLAYERS) {
    return JOIN_LOBBY_ERR_LOBBY_FULL;
  }

  /* Thêm người chơi vào danh sách phòng chờ */
  MatchPlayer *p = &ctx->players[ctx->player_count];
  memset(p, 0, sizeof(MatchPlayer));
  p->user_id = user_id;
  strncpy(p->username, username, USERNAME_MAX_LEN);
  p->username[USERNAME_MAX_LEN] = '\0';
  strncpy(p->session_token, session_token, SESSION_TOKEN_LEN);
  p->session_token[SESSION_TOKEN_LEN] = '\0';
  p->socket_fd = socket_fd;
  p->role = ROLE_NONE;
  p->status = PLAYER_STATUS_LOBBY;
  p->score_units = 0; /* Điểm sẽ được khởi tạo khi bắt đầu trận */
  p->skip_count = 0;
  p->joined_at = time(NULL);

  ctx->player_count++;
  return JOIN_LOBBY_OK;
}

bool match_remove_player(MatchContext *ctx, int64_t user_id) {
  if (!ctx || user_id <= 0)
    return false;

  for (int i = 0; i < ctx->player_count; i++) {
    if (ctx->players[i].user_id == user_id) {
      /* Dịch chuyển các phần tử phía sau lên */
      for (int j = i; j < ctx->player_count - 1; j++) {
        ctx->players[j] = ctx->players[j + 1];
      }
      ctx->player_count--;
      memset(&ctx->players[ctx->player_count], 0, sizeof(MatchPlayer));
      return true;
    }
  }
  return false;
}

void match_start_initialize_scores(MatchContext *ctx) {
  if (!ctx)
    return;
  for (int i = 0; i < ctx->player_count; i++) {
    ctx->players[i].score_units =
        INITIAL_SCORE_UNITS; /* 1000 units = 10 điểm */
    ctx->players[i].status = PLAYER_STATUS_ACTIVE;
  }
}

int match_build_lobby_updated(const MatchContext *ctx, char *out_buf,
                              size_t out_size) {
  if (!ctx || !out_buf || out_size == 0)
    return -1;

  int written = snprintf(out_buf, out_size,
                         "{\"type\":\"LOBBY_UPDATED\",\"match_id\":%ld,"
                         "\"player_count\":%d,\"players\":[",
                         (long)ctx->match_id, ctx->player_count);

  if (written < 0 || (size_t)written >= out_size)
    return -1;

  for (int i = 0; i < ctx->player_count; i++) {
    int n =
        snprintf(out_buf + written, out_size - written,
                 "%s{\"user_id\":%ld,\"username\":\"%s\"}", (i > 0 ? "," : ""),
                 (long)ctx->players[i].user_id, ctx->players[i].username);

    if (n < 0 || (size_t)(written + n) >= out_size)
      return -1;
    written += n;
  }

  int closing = snprintf(out_buf + written, out_size - written, "]}\n");
  if (closing < 0 || (size_t)(written + closing) >= out_size)
    return -1;
  written += closing;

  return written;
}

int match_build_join_result(JoinLobbyResult res, int64_t match_id,
                            int player_count, char *out_buf, size_t out_size) {
  if (!out_buf || out_size == 0)
    return -1;

  if (res == JOIN_LOBBY_OK) {
    return snprintf(out_buf, out_size,
                    "{\"type\":\"JOIN_LOBBY_RESULT\",\"status\":\"ok\","
                    "\"payload\":{\"match_id\":%ld,\"player_count\":%d,"
                    "\"message\":\"Tham gia phòng chờ thành công\"}}\n",
                    (long)match_id, player_count);
  } else {
    const char *err_code = "UNKNOWN_ERROR";
    const char *err_msg = "Lỗi không xác định";
    switch (res) {
    case JOIN_LOBBY_ERR_UNAUTHORIZED:
      err_code = "UNAUTHORIZED";
      err_msg = "Yêu cầu đăng nhập trước khi tham gia phòng chờ";
      break;
    case JOIN_LOBBY_ERR_ALREADY_JOINED:
      err_code = "ALREADY_JOINED";
      err_msg = "Người dùng đã có mặt trong phòng chờ";
      break;
    case JOIN_LOBBY_ERR_LOBBY_FULL:
      err_code = "LOBBY_FULL";
      err_msg = "Phòng chờ đã đạt tối đa 100 người";
      break;
    case JOIN_LOBBY_ERR_INVALID_STATE:
      err_code = "INVALID_STATE";
      err_msg = "Trận đấu đang diễn ra hoặc đã kết thúc";
      break;
    default:
      break;
    }
    return snprintf(out_buf, out_size,
                    "{\"type\":\"JOIN_LOBBY_RESULT\",\"status\":\"error\","
                    "\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
                    err_code, err_msg);
  }
}

StartGameResult match_start_game(MatchContext *ctx, int64_t requester_user_id) {
  if (!ctx)
    return START_GAME_ERR_INVALID_STATE;

  if (ctx->state != MATCH_STATE_LOBBY) {
    return START_GAME_ERR_INVALID_STATE;
  }

  if (ctx->player_count < 2) {
    return START_GAME_ERR_NOT_ENOUGH_PLAYERS;
  }

  /* Quyền host: Người đầu tiên tham gia phòng chờ (players[0]) */
  if (ctx->players[0].user_id != requester_user_id) {
    return START_GAME_ERR_UNAUTHORIZED;
  }

  /* Khởi tạo 10 điểm (1000 score units) cho tất cả người chơi */
  match_start_initialize_scores(ctx);

  /* Chuyển trạng thái trận sang SELECTING_MAIN (chọn người chơi chính) */
  ctx->state = MATCH_STATE_SELECTING_MAIN;
  ctx->current_round = 1;

  return START_GAME_OK;
}

int match_build_game_started(const MatchContext *ctx, char *out_buf,
                             size_t out_size) {
  if (!ctx || !out_buf || out_size == 0)
    return -1;

  return snprintf(
      out_buf, out_size,
      "{\"type\":\"GAME_STARTED\",\"match_id\":%ld,\"player_count\":%d,"
      "\"state\":\"SELECTING_MAIN\",\"initial_score\":10}\n",
      (long)ctx->match_id, ctx->player_count);
}

int match_build_start_game_result(StartGameResult res, char *out_buf,
                                  size_t out_size) {
  if (!out_buf || out_size == 0)
    return -1;

  if (res == START_GAME_OK) {
    return snprintf(
        out_buf, out_size,
        "{\"type\":\"START_GAME_RESULT\",\"status\":\"ok\",\"payload\":{"
        "\"message\":\"Bắt đầu trận đấu thành công\"}}\n");
  }

  const char *err_code = "UNKNOWN_ERROR";
  const char *err_msg = "Lỗi không xác định";
  switch (res) {
  case START_GAME_ERR_UNAUTHORIZED:
    err_code = "UNAUTHORIZED";
    err_msg = "Chỉ chủ phòng (người tạo/vào phòng đầu tiên) mới có quyền bắt "
              "đầu trận";
    break;
  case START_GAME_ERR_NOT_IN_LOBBY:
    err_code = "NOT_IN_LOBBY";
    err_msg = "Bạn chưa tham gia phòng chờ";
    break;
  case START_GAME_ERR_NOT_ENOUGH_PLAYERS:
    err_code = "NOT_ENOUGH_PLAYERS";
    err_msg = "Cần tối thiểu 2 người chơi để bắt đầu trận đấu";
    break;
  case START_GAME_ERR_INVALID_STATE:
    err_code = "INVALID_STATE";
    err_msg = "Trận đấu không ở trạng thái phòng chờ";
    break;
  default:
    break;
  }

  return snprintf(out_buf, out_size,
                  "{\"type\":\"START_GAME_RESULT\",\"status\":\"error\","
                  "\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
                  err_code, err_msg);
}

void match_setup_selection_question(MatchContext *ctx, int64_t question_id,
                                    const char *correct_choice,
                                    int timeout_seconds) {
  if (!ctx)
    return;
  ctx->selection_question_id = question_id;
  strncpy(ctx->selection_correct_choice, correct_choice ? correct_choice : "",
          sizeof(ctx->selection_correct_choice) - 1);
  ctx->selection_correct_choice[sizeof(ctx->selection_correct_choice) - 1] =
      '\0';
  ctx->selection_answer_count = 0;
  memset(ctx->selection_answers, 0, sizeof(ctx->selection_answers));

  time_t now = time(NULL);
  ctx->round_started_at = now;
  ctx->round_deadline = now + timeout_seconds;
}

SelectionAnswerResult match_submit_selection_answer(MatchContext *ctx,
                                                    int64_t user_id,
                                                    const char *choice,
                                                    uint64_t server_time_ns) {
  if (!ctx || !choice)
    return SELECTION_ANS_ERR_INVALID_CHOICE;

  if (ctx->state != MATCH_STATE_SELECTING_MAIN) {
    return SELECTION_ANS_ERR_INVALID_STATE;
  }

  /* Kiểm tra hạn chót dựa trên đồng hồ server */
  if (time(NULL) > ctx->round_deadline) {
    return SELECTION_ANS_ERR_TIMEOUT;
  }

  /* Kiểm tra định dạng choice (chỉ nhận A, B, C, D) */
  if (strlen(choice) != 1) {
    return SELECTION_ANS_ERR_INVALID_CHOICE;
  }
  char ch = (char)toupper((unsigned char)choice[0]);
  if (ch < 'A' || ch > 'D') {
    return SELECTION_ANS_ERR_INVALID_CHOICE;
  }

  /* Xác minh người chơi có thuộc trận đấu không */
  bool in_match = false;
  for (int i = 0; i < ctx->player_count; i++) {
    if (ctx->players[i].user_id == user_id &&
        ctx->players[i].status == PLAYER_STATUS_ACTIVE) {
      in_match = true;
      break;
    }
  }
  if (!in_match) {
    return SELECTION_ANS_ERR_NOT_IN_MATCH;
  }

  /* Kiểm tra xem người chơi đã nộp đáp án chưa (chỉ nhận 1 lần nộp hợp lệ đầu
   * tiên) */
  for (int i = 0; i < ctx->selection_answer_count; i++) {
    if (ctx->selection_answers[i].user_id == user_id &&
        ctx->selection_answers[i].answered) {
      return SELECTION_ANS_ERR_ALREADY_SUBMITTED;
    }
  }

  if (ctx->selection_answer_count >= MAX_MATCH_PLAYERS) {
    return SELECTION_ANS_ERR_INVALID_STATE;
  }

  SelectionAnswerRecord *rec =
      &ctx->selection_answers[ctx->selection_answer_count++];
  rec->user_id = user_id;
  rec->choice[0] = ch;
  rec->choice[1] = '\0';
  rec->server_time_ns = server_time_ns;
  rec->answered = true;
  rec->is_correct =
      (ch == (char)toupper((unsigned char)ctx->selection_correct_choice[0]));

  return SELECTION_ANS_OK;
}

int64_t match_evaluate_selection(MatchContext *ctx) {
  if (!ctx || ctx->player_count == 0)
    return -1;

  int best_idx = -1;
  uint64_t earliest_ns = 0;
  int64_t best_uid = -1;

  /*
   * Tiêu chí chọn người chơi chính:
   * 1. Phải trả lời ĐÚNG (is_correct == true).
   * 2. Thời điểm nhận câu trả lời server_time_ns nhỏ nhất (sớm nhất).
   * 3. Quy tắc Tie-break nếu server_time_ns bằng nhau:
   *    - Ưu tiên người chơi có user_id nhỏ hơn (hoặc gia nhập phòng trước).
   */
  for (int i = 0; i < ctx->selection_answer_count; i++) {
    SelectionAnswerRecord *rec = &ctx->selection_answers[i];
    if (!rec->answered || !rec->is_correct)
      continue;

    if (best_idx == -1) {
      best_idx = i;
      earliest_ns = rec->server_time_ns;
      best_uid = rec->user_id;
    } else {
      if (rec->server_time_ns < earliest_ns) {
        best_idx = i;
        earliest_ns = rec->server_time_ns;
        best_uid = rec->user_id;
      } else if (rec->server_time_ns == earliest_ns) {
        /* Tie-break: user_id nhỏ hơn ưu tiên hơn */
        if (rec->user_id < best_uid) {
          best_idx = i;
          earliest_ns = rec->server_time_ns;
          best_uid = rec->user_id;
        }
      }
    }
  }

  int64_t chosen_main_id = -1;
  if (best_idx != -1) {
    chosen_main_id = ctx->selection_answers[best_idx].user_id;
  } else {
    /*
     * Trường hợp dự phòng nếu không ai trả lời đúng:
     * Chọn người trả lời hợp lệ đầu tiên, hoặc người tạo phòng players[0].
     */
    if (ctx->selection_answer_count > 0) {
      chosen_main_id = ctx->selection_answers[0].user_id;
    } else {
      chosen_main_id = ctx->players[0].user_id;
    }
  }

  ctx->main_user_id = chosen_main_id;

  /* Phân bổ vai trò trong RAM */
  for (int i = 0; i < ctx->player_count; i++) {
    if (ctx->players[i].user_id == chosen_main_id) {
      ctx->players[i].role = ROLE_MAIN;
    } else {
      ctx->players[i].role = ROLE_SUB;
    }
  }

  ctx->state = MATCH_STATE_ROUND_ACTIVE;
  return chosen_main_id;
}

int match_build_selection_question(const MatchContext *ctx, const char *content,
                                   const char *options_json,
                                   int timeout_seconds, char *out_buf,
                                   size_t out_size) {
  if (!ctx || !content || !out_buf || out_size == 0)
    return -1;

  /* Bảo đảm không gửi correct_choice xuống client */
  return snprintf(out_buf, out_size,
                  "{\"type\":\"SELECTION_QUESTION\",\"match_id\":%ld,"
                  "\"payload\":{\"question_id\":%ld,\"content\":\"%s\","
                  "\"options\":%s,\"timeout_seconds\":%d}}\n",
                  (long)ctx->match_id, (long)ctx->selection_question_id,
                  content, options_json ? options_json : "{}", timeout_seconds);
}

int match_build_selection_answer_result(SelectionAnswerResult res,
                                        char *out_buf, size_t out_size) {
  if (!out_buf || out_size == 0)
    return -1;

  if (res == SELECTION_ANS_OK) {
    return snprintf(
        out_buf, out_size,
        "{\"type\":\"SELECTION_ANSWER_RESULT\",\"status\":\"ok\",\"payload\":{"
        "\"message\":\"Ghi nhận đáp án thành công\"}}\n");
  }

  const char *err_code = "UNKNOWN_ERROR";
  const char *err_msg = "Lỗi nộp đáp án";
  switch (res) {
  case SELECTION_ANS_ERR_INVALID_STATE:
    err_code = "INVALID_STATE";
    err_msg = "Trận đấu không ở trạng thái chọn người chơi chính";
    break;
  case SELECTION_ANS_ERR_NOT_IN_MATCH:
    err_code = "NOT_IN_MATCH";
    err_msg = "Bạn không có trong danh sách thi đấu";
    break;
  case SELECTION_ANS_ERR_ALREADY_SUBMITTED:
    err_code = "ALREADY_SUBMITTED";
    err_msg = "Bạn đã gửi đáp án cho câu hỏi này rồi";
    break;
  case SELECTION_ANS_ERR_TIMEOUT:
    err_code = "TIMEOUT";
    err_msg = "Đã hết thời gian nộp đáp án";
    break;
  case SELECTION_ANS_ERR_INVALID_CHOICE:
    err_code = "INVALID_CHOICE";
    err_msg = "Lựa chọn đáp án không hợp lệ (chỉ nhận A, B, C, D)";
    break;
  default:
    break;
  }

  return snprintf(out_buf, out_size,
                  "{\"type\":\"SELECTION_ANSWER_RESULT\",\"status\":\"error\","
                  "\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
                  err_code, err_msg);
}

int match_build_role_assigned(const MatchContext *ctx, char *out_buf,
                              size_t out_size) {
  if (!ctx || !out_buf || out_size == 0)
    return -1;

  const char *main_username = "unknown";
  for (int i = 0; i < ctx->player_count; i++) {
    if (ctx->players[i].user_id == ctx->main_user_id) {
      main_username = ctx->players[i].username;
      break;
    }
  }

  return snprintf(out_buf, out_size,
                  "{\"type\":\"ROLE_ASSIGNED\",\"match_id\":%ld,\"payload\":{"
                  "\"main_user_id\":%ld,\"main_username\":\"%s\"}}\n",
                  (long)ctx->match_id, (long)ctx->main_user_id, main_username);
}

void match_setup_round_question(MatchContext *ctx, int64_t question_id,
                                const char *correct_choice) {
  if (!ctx)
    return;
  ctx->round_question_id = question_id;
  strncpy(ctx->round_correct_choice, correct_choice ? correct_choice : "",
          sizeof(ctx->round_correct_choice) - 1);
  ctx->round_correct_choice[sizeof(ctx->round_correct_choice) - 1] = '\0';
  ctx->round_answer_count = 0;
  memset(ctx->round_answers, 0, sizeof(ctx->round_answers));
  ctx->round_closed = false;
  ctx->round_evaluated = false;

  time_t now = time(NULL);
  ctx->round_started_at = now;
  ctx->sub_deadline = now + TIMEOUT_SUB_SECONDS;   /* 30s cho người phụ */
  ctx->main_deadline = now + TIMEOUT_MAIN_SECONDS; /* 60s cho người chính */
  ctx->round_deadline = ctx->main_deadline;
  ctx->state = MATCH_STATE_ROUND_ACTIVE;
}

bool match_is_player_timed_out(const MatchContext *ctx, int64_t user_id,
                               time_t current_server_time) {
  if (!ctx)
    return true;

  /* Xác định vai trò của người chơi */
  PlayerRole role = ROLE_NONE;
  for (int i = 0; i < ctx->player_count; i++) {
    if (ctx->players[i].user_id == user_id) {
      role = ctx->players[i].role;
      break;
    }
  }

  if (role == ROLE_MAIN) {
    return current_server_time > ctx->main_deadline;
  } else {
    /* Mặc định người phụ hoặc chưa phân vai: áp dụng hạn chót 30s */
    return current_server_time > ctx->sub_deadline;
  }
}

void match_close_round(MatchContext *ctx) {
  if (!ctx)
    return;
  ctx->round_closed = true;
  ctx->state = MATCH_STATE_ROUND_EVALUATING;
}

AnswerSubmitResult match_submit_round_answer_validated(
    MatchContext *ctx, int64_t user_id, int64_t match_id, int round_id,
    const char *choice, time_t current_server_time, uint64_t server_time_ns) {
  if (!ctx || !choice)
    return ANSWER_ACK_ERR_INVALID_CHOICE;

  /* 1. Kiểm tra session/user_id hợp lệ */
  if (user_id <= 0)
    return ANSWER_ACK_ERR_UNAUTHORIZED;

  /* 2. Kiểm tra match_id khớp với trận hiện tại */
  if (match_id != ctx->match_id)
    return ANSWER_ACK_ERR_INVALID_MATCH;

  /* 3. Kiểm tra round_id khớp với lượt thi hiện tại */
  if (round_id != ctx->current_round)
    return ANSWER_ACK_ERR_INVALID_ROUND;

  /* 4. Kiểm tra trạng thái trận đấu: phải là ROUND_ACTIVE và round chưa bị chốt */
  if (ctx->state != MATCH_STATE_ROUND_ACTIVE || ctx->round_closed) {
    return ANSWER_ACK_ERR_INVALID_STATE;
  }

  /* 5. Kiểm tra người chơi và vai trò (phải là MAIN hoặc SUB, và ACTIVE) */
  bool in_match = false;
  PlayerRole role = ROLE_NONE;
  for (int i = 0; i < ctx->player_count; i++) {
    if (ctx->players[i].user_id == user_id) {
      if (ctx->players[i].status == PLAYER_STATUS_ACTIVE) {
        in_match = true;
        role = ctx->players[i].role;
      }
      break;
    }
  }
  if (!in_match) {
    return ANSWER_ACK_ERR_UNAUTHORIZED;
  }
  if (role != ROLE_MAIN && role != ROLE_SUB) {
    return ANSWER_ACK_ERR_INVALID_ROLE;
  }

  /* 6. Kiểm tra hạn giờ theo vai trò (SUB: 30s, MAIN: 60s) */
  if (match_is_player_timed_out(ctx, user_id, current_server_time)) {
    return ANSWER_ACK_ERR_TIMEOUT;
  }

  /* 7. Kiểm tra định dạng lựa chọn (chỉ nhận A, B, C, D) */
  if (strlen(choice) != 1) {
    return ANSWER_ACK_ERR_INVALID_CHOICE;
  }
  char ch = (char)toupper((unsigned char)choice[0]);
  if (ch < 'A' || ch > 'D') {
    return ANSWER_ACK_ERR_INVALID_CHOICE;
  }

  /* 8. Kiểm tra xem người chơi đã nộp đáp án trong lượt này chưa (chỉ nhận 1 lần nộp hợp lệ) */
  for (int i = 0; i < ctx->round_answer_count; i++) {
    if (ctx->round_answers[i].user_id == user_id &&
        ctx->round_answers[i].answered) {
      return ANSWER_ACK_ERR_ALREADY_SUBMITTED;
    }
  }

  if (ctx->round_answer_count >= MAX_MATCH_PLAYERS) {
    return ANSWER_ACK_ERR_INVALID_STATE;
  }

  /* Ghi nhận đáp án hợp lệ vào RAM */
  SelectionAnswerRecord *rec = &ctx->round_answers[ctx->round_answer_count++];
  rec->user_id = user_id;
  rec->choice[0] = ch;
  rec->choice[1] = '\0';
  rec->server_time_ns = server_time_ns;
  rec->answered = true;
  rec->is_correct =
      (ch == (char)toupper((unsigned char)ctx->round_correct_choice[0]));

  return ANSWER_ACK_OK;
}

int match_build_answer_ack(AnswerSubmitResult res, int round_id, char *out_buf,
                           size_t out_size) {
  if (!out_buf || out_size == 0)
    return -1;

  if (res == ANSWER_ACK_OK) {
    return snprintf(out_buf, out_size,
                    "{\"type\":\"ANSWER_ACK\",\"status\":\"ok\","
                    "\"payload\":{\"round_id\":%d,\"accepted\":true}}\n",
                    round_id);
  }

  const char *err_code = "UNKNOWN_ERROR";
  const char *err_msg = "Lỗi gửi đáp án";
  switch (res) {
  case ANSWER_ACK_ERR_UNAUTHORIZED:
    err_code = "UNAUTHORIZED";
    err_msg = "Người chơi không hợp lệ hoặc không có trong danh sách thi đấu";
    break;
  case ANSWER_ACK_ERR_INVALID_MATCH:
    err_code = "INVALID_MATCH";
    err_msg = "Mã trận đấu không khớp";
    break;
  case ANSWER_ACK_ERR_INVALID_ROUND:
    err_code = "INVALID_ROUND";
    err_msg = "Mã lượt thi không khớp";
    break;
  case ANSWER_ACK_ERR_INVALID_ROLE:
    err_code = "INVALID_ROLE";
    err_msg = "Vai trò người chơi không hợp lệ";
    break;
  case ANSWER_ACK_ERR_INVALID_STATE:
    err_code = "INVALID_STATE";
    err_msg = "Lượt thi đã kết thúc hoặc không ở trạng thái nhận đáp án";
    break;
  case ANSWER_ACK_ERR_ALREADY_SUBMITTED:
    err_code = "ALREADY_SUBMITTED";
    err_msg = "Bạn đã gửi đáp án trong lượt này rồi (chỉ được gửi tối đa 1 lần)";
    break;
  case ANSWER_ACK_ERR_TIMEOUT:
    err_code = "TIMEOUT";
    err_msg = "Đáp án đến muộn (đã quá thời hạn quy định)";
    break;
  case ANSWER_ACK_ERR_INVALID_CHOICE:
    err_code = "INVALID_CHOICE";
    err_msg = "Lựa chọn đáp án không hợp lệ (chỉ chấp nhận A, B, C, D)";
    break;
  default:
    break;
  }

  return snprintf(out_buf, out_size,
                  "{\"type\":\"ANSWER_ACK\",\"status\":\"error\","
                  "\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
                  err_code, err_msg);
}

int match_build_round_started(const MatchContext *ctx, int round_id,
                              const char *content, const char *options_json,
                              int timeout_seconds, char *out_buf,
                              size_t out_size) {
  if (!ctx || !content || !out_buf || out_size == 0)
    return -1;

  /* Tuyệt đối không gửi đáp án đúng round_correct_choice tới client */
  return snprintf(
      out_buf, out_size,
      "{\"type\":\"ROUND_STARTED\",\"match_id\":%ld,\"payload\":{\"round_"
      "id\":%d,\"question_id\":%ld,\"content\":\"%s\",\"options\":%s,"
      "\"timeout_seconds\":%d}}\n",
      (long)ctx->match_id, round_id, (long)ctx->round_question_id, content,
      options_json ? options_json : "{}", timeout_seconds);
}

/* =========================================================================
 * P8.2 — Cài đặt bộ xử lý kết quả lượt thi đấu độc lập với socket
 * ========================================================================= */

int match_evaluate_round_pure(const MatchContext *ctx, RoundEvaluationResult *out_eval) {
  if (!ctx || !out_eval)
    return -1;

  memset(out_eval, 0, sizeof(RoundEvaluationResult));
  out_eval->round_id = ctx->current_round;
  out_eval->old_main_id = ctx->main_user_id;
  out_eval->new_main_id = ctx->main_user_id;
  out_eval->winner_user_id = -1;
  out_eval->new_match_state = MATCH_STATE_ROUND_ACTIVE;

  /* Tìm thông tin người chơi chính */
  int main_idx = -1;
  for (int i = 0; i < ctx->player_count; i++) {
    if (ctx->players[i].user_id == ctx->main_user_id) {
      main_idx = i;
      break;
    }
  }

  if (main_idx < 0 || ctx->players[main_idx].status != PLAYER_STATUS_ACTIVE) {
    out_eval->outcome = ROUND_OUTCOME_INVALID_STATE;
    return -2;
  }

  /* Khởi tạo danh sách delta cho mọi người chơi đang có trong trận */
  out_eval->delta_count = ctx->player_count;
  for (int i = 0; i < ctx->player_count; i++) {
    out_eval->player_deltas[i].user_id = ctx->players[i].user_id;
    out_eval->player_deltas[i].old_score_units = ctx->players[i].score_units;
    out_eval->player_deltas[i].new_score_units = ctx->players[i].score_units;
    out_eval->player_deltas[i].score_delta_units = 0;
    out_eval->player_deltas[i].old_status = ctx->players[i].status;
    out_eval->player_deltas[i].new_status = ctx->players[i].status;
    out_eval->player_deltas[i].old_role = ctx->players[i].role;
    out_eval->player_deltas[i].new_role = ctx->players[i].role;
  }

  /* Xác định kết quả đáp án của người chính và người phụ */
  bool main_correct = false;
  for (int i = 0; i < ctx->round_answer_count; i++) {
    if (ctx->round_answers[i].user_id == ctx->main_user_id &&
        ctx->round_answers[i].answered) {
      main_correct = ctx->round_answers[i].is_correct;
      break;
    }
  }

  /* Phân loại các người phụ đang ACTIVE: ai đúng, ai sai/không trả lời */
  int sub_correct_indices[MAX_MATCH_PLAYERS];
  int sub_correct_count = 0;
  uint64_t sub_correct_times[MAX_MATCH_PLAYERS];

  int sub_wrong_indices[MAX_MATCH_PLAYERS];
  int sub_wrong_count = 0;

  for (int i = 0; i < ctx->player_count; i++) {
    if (i == main_idx)
      continue;
    if (ctx->players[i].status != PLAYER_STATUS_ACTIVE)
      continue;

    /* Tìm đáp án của người phụ này */
    bool answered = false;
    bool is_correct = false;
    uint64_t ans_time = UINT64_MAX;

    for (int j = 0; j < ctx->round_answer_count; j++) {
      if (ctx->round_answers[j].user_id == ctx->players[i].user_id &&
          ctx->round_answers[j].answered) {
        answered = true;
        is_correct = ctx->round_answers[j].is_correct;
        ans_time = ctx->round_answers[j].server_time_ns;
        break;
      }
    }

    if (answered && is_correct) {
      sub_correct_indices[sub_correct_count] = i;
      sub_correct_times[sub_correct_count] = ans_time;
      sub_correct_count++;
    } else {
      sub_wrong_indices[sub_wrong_count] = i;
      sub_wrong_count++;
    }
  }

  /* =========================================================================
   * NHÁNH 1: Người chơi chính trả lời ĐÚNG
   * ========================================================================= */
  if (main_correct) {
    out_eval->outcome = ROUND_OUTCOME_MAIN_CORRECT;

    /* Người chính gom toàn bộ điểm của người phụ trả lời sai/không trả lời */
    int32_t total_won_units = 0;
    for (int k = 0; k < sub_wrong_count; k++) {
      int idx = sub_wrong_indices[k];
      int32_t pts = ctx->players[idx].score_units;
      total_won_units += pts;

      /* Người phụ trả lời sai bị loại */
      out_eval->player_deltas[idx].score_delta_units = -pts;
      out_eval->player_deltas[idx].new_score_units = 0;
      out_eval->player_deltas[idx].new_status = PLAYER_STATUS_ELIMINATED;
    }

    /* Cộng dồn điểm cho người chính */
    out_eval->player_deltas[main_idx].score_delta_units = total_won_units;
    out_eval->player_deltas[main_idx].new_score_units += total_won_units;

    /* Nếu không còn người phụ nào trả lời đúng -> Người chính THẮNG TRẬN */
    if (sub_correct_count == 0) {
      out_eval->winner_user_id = ctx->main_user_id;
      out_eval->new_match_state = MATCH_STATE_ENDED;
    } else {
      out_eval->winner_user_id = -1;
      out_eval->new_match_state = MATCH_STATE_ROUND_ACTIVE;
    }

    return 0;
  }

  /* =========================================================================
   * NHÁNH 2: Người chơi chính trả lời SAI (hoặc hết hạn / không nộp)
   * ========================================================================= */
  out_eval->outcome = ROUND_OUTCOME_MAIN_WRONG;

  /* Người chơi chính bị loại */
  int32_t main_score = ctx->players[main_idx].score_units;
  out_eval->player_deltas[main_idx].score_delta_units = -main_score;
  out_eval->player_deltas[main_idx].new_score_units = 0;
  out_eval->player_deltas[main_idx].new_status = PLAYER_STATUS_ELIMINATED;
  out_eval->player_deltas[main_idx].new_role = ROLE_NONE;

  /* Nếu không có người phụ nào trả lời đúng: Cuộc thi kết thúc, không ai thắng */
  if (sub_correct_count == 0) {
    out_eval->winner_user_id = -1;
    out_eval->new_match_state = MATCH_STATE_ENDED;
    out_eval->new_main_id = -1;
    return 0;
  }

  /* Xác định tân người chơi chính: người phụ đúng gửi đáp án đến server sớm nhất */
  int earliest_sub_idx = sub_correct_indices[0];
  uint64_t earliest_time = sub_correct_times[0];

  for (int k = 1; k < sub_correct_count; k++) {
    int idx = sub_correct_indices[k];
    uint64_t t = sub_correct_times[k];
    if (t < earliest_time) {
      earliest_time = t;
      earliest_sub_idx = idx;
    } else if (t == earliest_time) {
      /* Tie-break: user_id nhỏ hơn ưu tiên */
      if (ctx->players[idx].user_id < ctx->players[earliest_sub_idx].user_id) {
        earliest_time = t;
        earliest_sub_idx = idx;
      }
    }
  }

  out_eval->new_main_id = ctx->players[earliest_sub_idx].user_id;
  out_eval->player_deltas[earliest_sub_idx].new_role = ROLE_MAIN;

  /* Chia đều điểm của người chơi chính cho các người chơi phụ còn lại trong trận */
  /* Tổng số người phụ còn lại = sub_correct_count + sub_wrong_count */
  int total_surviving_subs = sub_correct_count + sub_wrong_count;
  if (total_surviving_subs > 0 && main_score > 0) {
    int32_t base_share = main_score / total_surviving_subs;
    int32_t remainder = main_score % total_surviving_subs;

    /* Cộng điểm cơ bản cho tất cả người phụ còn lại */
    for (int k = 0; k < sub_correct_count; k++) {
      int idx = sub_correct_indices[k];
      out_eval->player_deltas[idx].score_delta_units += base_share;
      out_eval->player_deltas[idx].new_score_units += base_share;
    }
    for (int k = 0; k < sub_wrong_count; k++) {
      int idx = sub_wrong_indices[k];
      out_eval->player_deltas[idx].score_delta_units += base_share;
      out_eval->player_deltas[idx].new_score_units += base_share;
    }

    /* Phân phối remainder (nếu có): ưu tiên người trả lời đúng sớm nhất theo thứ tự */
    int r_distributed = 0;
    /* Ưu tiên 1: người phụ trả lời đúng theo thứ tự thời gian */
    for (int k = 0; k < sub_correct_count && r_distributed < remainder; k++) {
      int idx = sub_correct_indices[k];
      out_eval->player_deltas[idx].score_delta_units += 1;
      out_eval->player_deltas[idx].new_score_units += 1;
      r_distributed++;
    }
    /* Ưu tiên 2: nếu vẫn còn dư, chia tiếp cho người phụ sai theo thứ tự user_id */
    for (int k = 0; k < sub_wrong_count && r_distributed < remainder; k++) {
      int idx = sub_wrong_indices[k];
      out_eval->player_deltas[idx].score_delta_units += 1;
      out_eval->player_deltas[idx].new_score_units += 1;
      r_distributed++;
    }
  }

  out_eval->winner_user_id = -1;
  out_eval->new_match_state = MATCH_STATE_ROUND_ACTIVE;

  return 0;
}

int match_apply_evaluation(MatchContext *ctx, const RoundEvaluationResult *eval) {
  if (!ctx || !eval)
    return -1;

  /* Cập nhật thông tin trận đấu */
  ctx->state = eval->new_match_state;
  ctx->main_user_id = eval->new_main_id;
  ctx->winner_user_id = eval->winner_user_id;

  /* Cập nhật điểm, vai trò và trạng thái cho từng người chơi */
  for (int d = 0; d < eval->delta_count; d++) {
    int64_t uid = eval->player_deltas[d].user_id;
    for (int p = 0; p < ctx->player_count; p++) {
      if (ctx->players[p].user_id == uid) {
        ctx->players[p].score_units = eval->player_deltas[d].new_score_units;
        ctx->players[p].status = eval->player_deltas[d].new_status;
        ctx->players[p].role = eval->player_deltas[d].new_role;
        break;
      }
    }
  }

  return 0;
}

int match_evaluate_and_commit_round(MatchContext *ctx, RoundEvaluationResult *out_eval) {
  if (!ctx)
    return -1;

  /* 1. Kiểm tra điều kiện: chỉ chấm khi lượt đã chốt (round_closed) */
  if (!ctx->round_closed) {
    return -2; /* Lượt chưa chốt, không được chấm */
  }

  /* 2. Bảo đảm mỗi lượt CHỈ ĐƯỢC CHẤM MỘT LẦN DUY NHẤT */
  if (ctx->round_evaluated) {
    return -3; /* Lượt này đã chấm rồi, từ chối chấm đúp */
  }

  /* 3. Tính toán trước (evaluation trên bản copy kết quả) */
  RoundEvaluationResult local_eval;
  int eval_rc = match_evaluate_round_pure(ctx, &local_eval);
  if (eval_rc != 0) {
    return -4; /* Lỗi khi phân tích lượt */
  }

  /* 4. Kiểm tra bảo toàn toàn vẹn dữ liệu điểm (Atomic integrity check):
   * Tổng điểm của toàn bộ người chơi trước và sau đánh giá phải bảo toàn 100%.
   * Không cho phép thất thoát điểm hoặc sinh điểm ảo. */
  int32_t total_before = 0;
  for (int p = 0; p < ctx->player_count; p++) {
    total_before += ctx->players[p].score_units;
  }

  int32_t total_after = 0;
  for (int d = 0; d < local_eval.delta_count; d++) {
    total_after += local_eval.player_deltas[d].new_score_units;
  }

  if (total_before != total_after) {
    /* Phát hiện lỗi toàn vẹn -> Hủy bỏ, KHÔNG CẬP NHẬT MỘT PHẦN */
    return -5;
  }

  /* 5. Áp dụng kết quả nguyên tử vào RAM */
  match_apply_evaluation(ctx, &local_eval);

  /* Đánh dấu đã chấm xong lượt này */
  ctx->round_evaluated = true;

  if (out_eval) {
    *out_eval = local_eval;
  }

  return 0;
}

int match_build_round_result(const RoundEvaluationResult *eval, char *out_buf,
                             size_t out_size) {
  if (!eval || !out_buf || out_size == 0)
    return -1;

  const char *outcome_str = "MAIN_WRONG";
  if (eval->outcome == ROUND_OUTCOME_MAIN_CORRECT)
    outcome_str = "MAIN_CORRECT";
  else if (eval->outcome == ROUND_OUTCOME_MAIN_SKIPPED)
    outcome_str = "MAIN_SKIPPED";

  int written = snprintf(
      out_buf, out_size,
      "{\"type\":\"ROUND_RESULT\",\"round_id\":%d,\"payload\":{\"outcome\":"
      "\"%s\",\"winner_user_id\":%ld,\"new_main_id\":%ld,\"match_state\":%d,"
      "\"deltas\":[",
      eval->round_id, outcome_str, (long)eval->winner_user_id,
      (long)eval->new_main_id, (int)eval->new_match_state);

  if (written < 0 || (size_t)written >= out_size)
    return -1;

  for (int i = 0; i < eval->delta_count; i++) {
    const PlayerRoundDelta *d = &eval->player_deltas[i];
    int n = snprintf(
        out_buf + written, out_size - written,
        "%s{\"user_id\":%ld,\"score_delta\":%d,\"new_score\":%d,\"status\":%d,"
        "\"role\":%d}",
        (i > 0 ? "," : ""), (long)d->user_id, (int)d->score_delta_units,
        (int)d->new_score_units, (int)d->new_status, (int)d->new_role);

    if (n < 0 || (size_t)(written + n) >= out_size)
      return -1;
    written += n;
  }

  int closing = snprintf(out_buf + written, out_size - written, "]}}\n");
  if (closing < 0 || (size_t)(written + closing) >= out_size)
    return -1;
  written += closing;

  return written;
}

/* =========================================================================
 * P8.5 — Cài đặt điều kiện kết thúc trận và GAME_FINISHED
 * ========================================================================= */

bool match_is_game_finished(const MatchContext *ctx, MatchEndReason *out_reason) {
  if (!ctx)
    return false;

  if (ctx->state != MATCH_STATE_ENDED)
    return false;

  if (ctx->winner_user_id > 0) {
    if (out_reason)
      *out_reason = END_REASON_WINNER;
    return true;
  }

  /* Trận kết thúc nhưng không có winner (tất cả đều sai) */
  if (out_reason)
    *out_reason = END_REASON_ALL_WRONG;
  return true;
}

int match_build_game_finished(const MatchContext *ctx, char *out_buf, size_t out_size) {
  if (!ctx || !out_buf || out_size == 0)
    return -1;

  MatchEndReason reason = END_REASON_ALL_WRONG;
  match_is_game_finished(ctx, &reason);

  const char *reason_str = "ALL_WRONG";
  if (reason == END_REASON_WINNER)
    reason_str = "WINNER";
  else if (reason == END_REASON_ABORTED)
    reason_str = "ABORTED";

  /* Tìm tên người chiến thắng nếu có */
  const char *winner_name = "";
  if (ctx->winner_user_id > 0) {
    for (int i = 0; i < ctx->player_count; i++) {
      if (ctx->players[i].user_id == ctx->winner_user_id) {
        winner_name = ctx->players[i].username;
        break;
      }
    }
  }

  int written = snprintf(
      out_buf, out_size,
      "{\"type\":\"GAME_FINISHED\",\"match_id\":%ld,\"payload\":{\"reason\":"
      "\"%s\",\"winner_user_id\":%ld,\"winner_username\":\"%s\",\"final_scores\":[",
      (long)ctx->match_id, reason_str, (long)ctx->winner_user_id, winner_name);

  if (written < 0 || (size_t)written >= out_size)
    return -1;

  for (int i = 0; i < ctx->player_count; i++) {
    const MatchPlayer *p = &ctx->players[i];
    int n = snprintf(
        out_buf + written, out_size - written,
        "%s{\"user_id\":%ld,\"username\":\"%s\",\"score\":%d,\"status\":%d}",
        (i > 0 ? "," : ""), (long)p->user_id, p->username,
        (int)p->score_units, (int)p->status);

    if (n < 0 || (size_t)(written + n) >= out_size)
      return -1;
    written += n;
  }

  int closing = snprintf(out_buf + written, out_size - written, "]}}\n");
  if (closing < 0 || (size_t)(written + closing) >= out_size)
    return -1;
  written += closing;

  return written;
}

/* =========================================================================
 * P9.1 — Cài đặt phát thông báo ROUND_STARTED, ROUND_RESULT và SCORE_UPDATED
 * ========================================================================= */

int match_build_score_updated(const MatchContext *ctx, char *out_buf,
                              size_t out_size) {
  if (!ctx || !out_buf || out_size == 0)
    return -1;

  int written = snprintf(
      out_buf, out_size,
      "{\"type\":\"SCORE_UPDATED\",\"match_id\":%ld,\"payload\":{\"round_id\":"
      "%d,\"scores\":[",
      (long)ctx->match_id, ctx->current_round);

  if (written < 0 || (size_t)written >= out_size)
    return -1;

  int item_count = 0;
  for (int i = 0; i < ctx->player_count; i++) {
    const MatchPlayer *p = &ctx->players[i];
    /* Chỉ gửi thông tin điểm của người chơi trong trận (không bao gồm DISCONNECTED) */
    if (p->status == PLAYER_STATUS_ACTIVE || p->status == PLAYER_STATUS_ELIMINATED) {
      int n = snprintf(
          out_buf + written, out_size - written,
          "%s{\"user_id\":%ld,\"username\":\"%s\",\"score\":%d,\"status\":%d,"
          "\"role\":%d}",
          (item_count > 0 ? "," : ""), (long)p->user_id, p->username,
          (int)p->score_units, (int)p->status, (int)p->role);

      if (n < 0 || (size_t)(written + n) >= out_size)
        return -1;
      written += n;
      item_count++;
    }
  }

  int closing = snprintf(out_buf + written, out_size - written, "]}}\n");
  if (closing < 0 || (size_t)(written + closing) >= out_size)
    return -1;
  written += closing;

  return written;
}

int match_get_active_player_sockets(const MatchContext *ctx, bool allow_eliminated,
                                    int *out_sockets, int max_sockets) {
  if (!ctx || !out_sockets || max_sockets <= 0)
    return 0;

  int count = 0;
  for (int i = 0; i < ctx->player_count && count < max_sockets; i++) {
    const MatchPlayer *p = &ctx->players[i];
    if (p->socket_fd < 0)
      continue;

    bool eligible = false;
    if (p->status == PLAYER_STATUS_ACTIVE) {
      eligible = true;
    } else if (allow_eliminated && p->status == PLAYER_STATUS_ELIMINATED) {
      eligible = true;
    }

    if (eligible) {
      out_sockets[count++] = p->socket_fd;
    }
  }

  return count;
}

int match_broadcast_message_safe(const int *sockets, int socket_count,
                                 const char *message, size_t message_len,
                                 int timeout_ms, int *out_failed_sockets,
                                 int max_failed) {
  if (!sockets || socket_count <= 0 || !message || message_len == 0)
    return 0;

  int success_count = 0;
  int failed_count = 0;

  for (int i = 0; i < socket_count; i++) {
    int s = sockets[i];
    if (s < 0)
      continue;

    /* Cài đặt SO_SNDTIMEO để một client gửi chậm/treo không chặn vô hạn luồng phục vụ */
    if (timeout_ms > 0) {
#ifdef _WIN32
      DWORD tv = (DWORD)timeout_ms;
      setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
      struct timeval tv;
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    }

    NetStatus st = net_send_all((socket_t)s, message, message_len);
    if (st == NET_STATUS_OK) {
      success_count++;
    } else {
      if (out_failed_sockets && failed_count < max_failed) {
        out_failed_sockets[failed_count++] = s;
      }
    }
  }

  return success_count;
}
