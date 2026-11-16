#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "match.h"

static void test_join_normal(void) {
  MatchContext ctx;
  match_init(&ctx, 101);

  assert(ctx.state == MATCH_STATE_LOBBY);
  assert(ctx.player_count == 0);

  JoinLobbyResult res = match_join_lobby(&ctx, 1, "alice", "sess_alice", 10);
  assert(res == JOIN_LOBBY_OK);
  assert(ctx.player_count == 1);
  assert(ctx.players[0].user_id == 1);
  assert(strcmp(ctx.players[0].username, "alice") == 0);
  assert(ctx.players[0].score_units == 0); /* Trong lobby chưa có điểm */

  printf("[PASS] test_join_normal\n");
}

static void test_join_duplicate(void) {
  MatchContext ctx;
  match_init(&ctx, 102);

  JoinLobbyResult res1 = match_join_lobby(&ctx, 1, "alice", "sess_alice", 10);
  assert(res1 == JOIN_LOBBY_OK);

  /* Cùng user_id tham gia lại -> phải bị chặn */
  JoinLobbyResult res2 = match_join_lobby(&ctx, 1, "alice", "sess_alice2", 11);
  assert(res2 == JOIN_LOBBY_ERR_ALREADY_JOINED);
  assert(ctx.player_count == 1);

  printf("[PASS] test_join_duplicate\n");
}

static void test_join_limit_100(void) {
  MatchContext ctx;
  match_init(&ctx, 103);

  char name[32];
  char sess[64];

  /* Thêm đủ 100 người chơi */
  for (int i = 1; i <= MAX_MATCH_PLAYERS; i++) {
    snprintf(name, sizeof(name), "player_%d", i);
    snprintf(sess, sizeof(sess), "token_%d", i);
    JoinLobbyResult res = match_join_lobby(&ctx, i, name, sess, 100 + i);
    assert(res == JOIN_LOBBY_OK);
  }
  assert(ctx.player_count == 100);

  /* Thêm người thứ 101 -> phải bị từ chối LOBBY_FULL */
  JoinLobbyResult res_overflow =
      match_join_lobby(&ctx, 101, "player_101", "token_101", 999);
  assert(res_overflow == JOIN_LOBBY_ERR_LOBBY_FULL);
  assert(ctx.player_count == 100);

  printf("[PASS] test_join_limit_100\n");
}

static void test_score_initialization(void) {
  MatchContext ctx;
  match_init(&ctx, 104);

  for (int i = 1; i <= 5; i++) {
    char name[32], sess[64];
    snprintf(name, sizeof(name), "user%d", i);
    snprintf(sess, sizeof(sess), "sess%d", i);
    match_join_lobby(&ctx, i, name, sess, i);
    assert(ctx.players[i - 1].score_units == 0);
  }

  /* Khi bắt đầu trận, khởi tạo 10 điểm = 1000 đơn vị cho tất cả người chơi */
  match_start_initialize_scores(&ctx);

  for (int i = 0; i < 5; i++) {
    assert(ctx.players[i].score_units == 1000); /* 1000 đơn vị = 10 điểm */
    assert(ctx.players[i].status == PLAYER_STATUS_ACTIVE);
  }

  printf("[PASS] test_score_initialization\n");
}

static void test_lobby_updated_json(void) {
  MatchContext ctx;
  match_init(&ctx, 200);

  match_join_lobby(&ctx, 10, "alice", "tok_a", 1);
  match_join_lobby(&ctx, 20, "bob", "tok_b", 2);

  char buf[1024];
  int len = match_build_lobby_updated(&ctx, buf, sizeof(buf));
  assert(len > 0);
  assert(strstr(buf, "\"type\":\"LOBBY_UPDATED\"") != NULL);
  assert(strstr(buf, "\"player_count\":2") != NULL);
  assert(strstr(buf, "\"username\":\"alice\"") != NULL);
  assert(strstr(buf, "\"username\":\"bob\"") != NULL);

  printf("[PASS] test_lobby_updated_json: %s", buf);
}

static void test_player_leave(void) {
  MatchContext ctx;
  match_init(&ctx, 300);

  match_join_lobby(&ctx, 1, "u1", "s1", 1);
  match_join_lobby(&ctx, 2, "u2", "s2", 2);
  match_join_lobby(&ctx, 3, "u3", "s3", 3);
  assert(ctx.player_count == 3);

  bool ok = match_remove_player(&ctx, 2);
  assert(ok == true);
  assert(ctx.player_count == 2);
  assert(ctx.players[0].user_id == 1);
  assert(ctx.players[1].user_id == 3);

  bool not_found = match_remove_player(&ctx, 999);
  assert(not_found == false);

  printf("[PASS] test_player_leave\n");
}

static void test_start_game(void) {
  MatchContext ctx;
  match_init(&ctx, 400);

  /* 1. Chưa đủ 2 người chơi */
  match_join_lobby(&ctx, 10, "host_alice", "tok_a", 1);
  StartGameResult res_not_enough = match_start_game(&ctx, 10);
  assert(res_not_enough == START_GAME_ERR_NOT_ENOUGH_PLAYERS);
  assert(ctx.state == MATCH_STATE_LOBBY);

  /* Thêm người thứ 2 */
  match_join_lobby(&ctx, 20, "bob", "tok_b", 2);

  /* 2. Không phải chủ phòng (bob cố tình start) */
  StartGameResult res_non_host = match_start_game(&ctx, 20);
  assert(res_non_host == START_GAME_ERR_UNAUTHORIZED);
  assert(ctx.state == MATCH_STATE_LOBBY);

  /* 3. Chủ phòng bắt đầu trận thành công */
  StartGameResult res_ok = match_start_game(&ctx, 10);
  assert(res_ok == START_GAME_OK);
  assert(ctx.state == MATCH_STATE_SELECTING_MAIN);
  assert(ctx.players[0].score_units == 1000); /* 10 điểm */
  assert(ctx.players[1].score_units == 1000); /* 10 điểm */
  assert(ctx.players[0].status == PLAYER_STATUS_ACTIVE);
  assert(ctx.players[1].status == PLAYER_STATUS_ACTIVE);

  /* 4. Đã start rồi mà gọi start lại -> Lỗi trạng thái không hợp lệ */
  StartGameResult res_invalid_state = match_start_game(&ctx, 10);
  assert(res_invalid_state == START_GAME_ERR_INVALID_STATE);

  /* 5. Kiểm tra sinh JSON GAME_STARTED */
  char buf[512];
  int len = match_build_game_started(&ctx, buf, sizeof(buf));
  assert(len > 0);
  assert(strstr(buf, "\"type\":\"GAME_STARTED\"") != NULL);
  assert(strstr(buf, "\"state\":\"SELECTING_MAIN\"") != NULL);

  printf("[PASS] test_start_game: %s", buf);
}

static void test_selection_flow_and_tie_break(void) {
  MatchContext ctx;
  match_init(&ctx, 201);

  match_join_lobby(&ctx, 10, "player_10", "token_10", 1);
  match_join_lobby(&ctx, 20, "player_20", "token_20", 2);
  match_join_lobby(&ctx, 30, "player_30", "token_30", 3);

  /* Bắt đầu trận -> trạng thái SELECTING_MAIN */
  StartGameResult s_res = match_start_game(&ctx, 10);
  assert(s_res == START_GAME_OK);
  assert(ctx.state == MATCH_STATE_SELECTING_MAIN);

  /* Thiết lập câu hỏi chọn người chơi chính với đáp án đúng là "B" */
  match_setup_selection_question(&ctx, 1001, "B", 15);

  /* 1. Kiểm tra format JSON câu hỏi: KHÔNG rò rỉ đáp án đúng */
  char q_json[512];
  int q_len = match_build_selection_question(
      &ctx, "Thủ đô của Pháp là gì?",
      "{\"A\":\"Lyon\",\"B\":\"Paris\",\"C\":\"Nice\",\"D\":\"Marseille\"}", 15,
      q_json, sizeof(q_json));
  assert(q_len > 0);
  assert(strstr(q_json, "\"type\":\"SELECTION_QUESTION\"") != NULL);
  assert(strstr(q_json, "\"Paris\"") != NULL);
  assert(strstr(q_json, "correct_choice") == NULL);
  assert(strstr(q_json, "\"correct\"") == NULL);

  /* 2. Người chơi 10 nộp đáp án "A" (SAI) tại mốc thời gian t = 1000 ns */
  SelectionAnswerResult r1 = match_submit_selection_answer(&ctx, 10, "A", 1000);
  assert(r1 == SELECTION_ANS_OK);

  /* 3. Người chơi 10 nộp lại -> Bị chặn ALREADY_SUBMITTED */
  SelectionAnswerResult r1_dup =
      match_submit_selection_answer(&ctx, 10, "B", 1500);
  assert(r1_dup == SELECTION_ANS_ERR_ALREADY_SUBMITTED);

  /* 4. Người chơi 20 nộp đáp án "B" (ĐÚNG) tại mốc thời gian t = 2000 ns */
  SelectionAnswerResult r2 = match_submit_selection_answer(&ctx, 20, "B", 2000);
  assert(r2 == SELECTION_ANS_OK);

  /* 5. Người chơi 30 nộp đáp án "B" (ĐÚNG) tại mốc thời gian t = 2000 ns (BẰNG
   * NHAU THEO ĐỘ PHÂN GIẢI THỜI GIAN -> TIE) */
  SelectionAnswerResult r3 = match_submit_selection_answer(&ctx, 30, "B", 2000);
  assert(r3 == SELECTION_ANS_OK);

  /* 6. Đánh giá lựa chọn và phân vai:
   * - Cả player 20 và player 30 đều trả lời đúng tại t = 2000 ns.
   * - Quy tắc tie-break: ưu tiên user_id nhỏ hơn (20 < 30).
   * -> Player 20 phải được chọn làm người chơi chính!
   */
  int64_t main_id = match_evaluate_selection(&ctx);
  assert(main_id == 20);
  assert(ctx.main_user_id == 20);
  assert(ctx.players[0].role == ROLE_SUB);
  assert(ctx.players[1].role == ROLE_MAIN);
  assert(ctx.players[2].role == ROLE_SUB);
  assert(ctx.state == MATCH_STATE_ROUND_ACTIVE);

  /* 7. Kiểm tra sinh JSON ROLE_ASSIGNED */
  char role_json[512];
  int r_len = match_build_role_assigned(&ctx, role_json, sizeof(role_json));
  assert(r_len > 0);
  assert(strstr(role_json, "\"type\":\"ROLE_ASSIGNED\"") != NULL);
  assert(strstr(role_json, "\"main_user_id\":20") != NULL);
  assert(strstr(role_json, "\"player_20\"") != NULL);

  printf("[PASS] test_selection_flow_and_tie_break: %s", role_json);
}

static void test_round_timer_deadlines(void) {
  MatchContext ctx;
  match_init(&ctx, 301);

  match_join_lobby(&ctx, 1, "alice_main", "token_1", 1);
  match_join_lobby(&ctx, 2, "bob_sub", "token_2", 2);

  match_start_game(&ctx, 1);

  /* Giả lập hoàn thành chọn người chơi chính: Alice là MAIN, Bob là SUB */
  ctx.main_user_id = 1;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;

  /* Bắt đầu lượt thi chính thức với câu hỏi có đáp án đúng là "C" */
  match_setup_round_question(&ctx, 2001, "C");
  assert(ctx.state == MATCH_STATE_ROUND_ACTIVE);
  assert(ctx.round_closed == false);

  time_t start_t = ctx.round_started_at;

  /* 1. Kiểm tra trong hạn 30s: Cả người phụ (Bob) và người chính (Alice) đều
   * nộp đúng hạn */
  time_t t_15s = start_t + 15;
  assert(match_is_player_timed_out(&ctx, 2, t_15s) == false); /* Bob còn hạn */
  assert(match_is_player_timed_out(&ctx, 1, t_15s) ==
         false); /* Alice còn hạn */

  /* Bob nộp đáp án đúng hạn tại t = 15s */
  AnswerSubmitResult res_bob_ok =
      match_submit_round_answer_validated(&ctx, 2, 301, 1, "C", t_15s, 15000);
  assert(res_bob_ok == ANSWER_ACK_OK);
  assert(ctx.round_answers[0].is_correct == true);

  /* 2. Kiểm tra mốc t = 35s (> 30s hạn người phụ, <= 60s hạn người chính): */
  time_t t_35s = start_t + 35;
  assert(match_is_player_timed_out(&ctx, 2, t_35s) ==
         true); /* Người phụ đã hết giờ (30s) */
  assert(match_is_player_timed_out(&ctx, 1, t_35s) ==
         false); /* Người chính còn giờ (60s) */

  /* Thử thêm 1 người phụ khác (id=3) nộp ở mốc t = 35s -> Phải bị từ chối
   * TIMEOUT */
  match_join_lobby(&ctx, 3, "charlie_sub", "token_3", 3);
  ctx.players[2].role = ROLE_SUB;
  ctx.players[2].status = PLAYER_STATUS_ACTIVE;
  AnswerSubmitResult res_sub_late =
      match_submit_round_answer_validated(&ctx, 3, 301, 1, "C", t_35s, 35000);
  assert(res_sub_late == ANSWER_ACK_ERR_TIMEOUT);

  /* Người chính (Alice) nộp đáp án đúng hạn ở mốc t = 45s (trước 60s) -> Thành
   * công */
  time_t t_45s = start_t + 45;
  AnswerSubmitResult res_alice_ok =
      match_submit_round_answer_validated(&ctx, 1, 301, 1, "C", t_45s, 45000);
  assert(res_alice_ok == ANSWER_ACK_OK);

  /* 3. Kiểm tra mốc t = 65s (> 60s hạn người chính): */
  time_t t_65s = start_t + 65;
  assert(match_is_player_timed_out(&ctx, 1, t_65s) ==
         true); /* Người chính cũng đã hết hạn */

  /* Khi hết hạn 60s, server gọi match_close_round chốt lượt */
  match_close_round(&ctx);
  assert(ctx.round_closed == true);
  assert(ctx.state == MATCH_STATE_ROUND_EVALUATING);

  /* 4. Mọi đáp án gửi đến sau khi đã chốt lượt đều bị từ chối */
  AnswerSubmitResult res_late_closed =
      match_submit_round_answer_validated(&ctx, 1, 301, 1, "C", t_65s, 65000);
  assert(res_late_closed == ANSWER_ACK_ERR_INVALID_STATE);

  printf("[PASS] test_round_timer_deadlines: 30s SUB & 60s MAIN & round close "
         "verified!\n");
}

static void test_answer_submit_and_ack_validation(void) {
  MatchContext ctx;
  match_init(&ctx, 401);

  match_join_lobby(&ctx, 10, "player_main", "token_10", 1);
  match_join_lobby(&ctx, 20, "player_sub1", "token_20", 2);
  match_join_lobby(&ctx, 30, "player_sub2", "token_30", 3);

  match_start_game(&ctx, 10);

  /* Giả lập phân vai: 10 là MAIN, 20 và 30 là SUB */
  ctx.main_user_id = 10;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;
  ctx.players[2].role = ROLE_SUB;

  ctx.current_round = 5;
  match_setup_round_question(&ctx, 501, "B");

  time_t now = ctx.round_started_at;

  /* 1. Kiểm tra session/user không hợp lệ (user_id = 0 hoặc người lạ ngoài
   * trận) */
  AnswerSubmitResult r_unauth =
      match_submit_round_answer_validated(&ctx, 0, 401, 5, "B", now + 5, 5000);
  assert(r_unauth == ANSWER_ACK_ERR_UNAUTHORIZED);

  AnswerSubmitResult r_stranger = match_submit_round_answer_validated(
      &ctx, 999, 401, 5, "B", now + 5, 5000);
  assert(r_stranger == ANSWER_ACK_ERR_UNAUTHORIZED);

  /* 2. Kiểm tra match_id sai */
  AnswerSubmitResult r_wrong_match =
      match_submit_round_answer_validated(&ctx, 20, 999, 5, "B", now + 5, 5000);
  assert(r_wrong_match == ANSWER_ACK_ERR_INVALID_MATCH);

  /* 3. Kiểm tra round_id sai */
  AnswerSubmitResult r_wrong_round =
      match_submit_round_answer_validated(&ctx, 20, 401, 6, "B", now + 5, 5000);
  assert(r_wrong_round == ANSWER_ACK_ERR_INVALID_ROUND);

  /* 4. Kiểm tra định dạng lựa chọn sai (không phải A/B/C/D) */
  AnswerSubmitResult r_bad_choice1 =
      match_submit_round_answer_validated(&ctx, 20, 401, 5, "E", now + 5, 5000);
  assert(r_bad_choice1 == ANSWER_ACK_ERR_INVALID_CHOICE);

  AnswerSubmitResult r_bad_choice2 = match_submit_round_answer_validated(
      &ctx, 20, 401, 5, "AB", now + 5, 5000);
  assert(r_bad_choice2 == ANSWER_ACK_ERR_INVALID_CHOICE);

  /* 5. Nộp đáp án hợp lệ lần đầu: player 20 chọn "B" */
  AnswerSubmitResult r_sub1_ok = match_submit_round_answer_validated(
      &ctx, 20, 401, 5, "b", now + 10, 10000);
  assert(r_sub1_ok == ANSWER_ACK_OK);
  assert(ctx.round_answer_count == 1);
  assert(ctx.round_answers[0].is_correct == true);

  /* 6. Kiểm tra chỉ nhận tối đa MỘT đáp án hợp lệ mỗi người mỗi lượt (chặn nộp
   * lại) */
  AnswerSubmitResult r_sub1_dup = match_submit_round_answer_validated(
      &ctx, 20, 401, 5, "A", now + 12, 12000);
  assert(r_sub1_dup == ANSWER_ACK_ERR_ALREADY_SUBMITTED);
  assert(ctx.round_answer_count == 1); /* Số lượng không tăng */

  /* 7. Kiểm tra sinh JSON ANSWER_ACK */
  char ack_ok[256];
  int len_ok = match_build_answer_ack(ANSWER_ACK_OK, 5, ack_ok, sizeof(ack_ok));
  assert(len_ok > 0);
  assert(strstr(ack_ok, "\"type\":\"ANSWER_ACK\"") != NULL);
  assert(strstr(ack_ok, "\"status\":\"ok\"") != NULL);
  assert(strstr(ack_ok, "\"accepted\":true") != NULL);

  char ack_err[256];
  int len_err = match_build_answer_ack(ANSWER_ACK_ERR_ALREADY_SUBMITTED, 5,
                                       ack_err, sizeof(ack_err));
  assert(len_err > 0);
  assert(strstr(ack_err, "\"status\":\"error\"") != NULL);
  assert(strstr(ack_err, "\"ALREADY_SUBMITTED\"") != NULL);

  /* 8. Kiểm tra ROUND_STARTED JSON không rò rỉ đáp án đúng */
  char round_json[512];
  int len_r = match_build_round_started(
      &ctx, 5, "1 byte bằng bao nhiêu bit?",
      "{\"A\":\"4\",\"B\":\"8\",\"C\":\"16\",\"D\":\"32\"}", 30, round_json,
      sizeof(round_json));
  assert(len_r > 0);
  assert(strstr(round_json, "correct_choice") == NULL);
  assert(strstr(round_json, "\"correct\"") == NULL);
  assert(strstr(round_json, "\"round_id\":5") != NULL);

  printf("[PASS] test_answer_submit_and_ack_validation: all checks passed!\n");
}

/* =========================================================================
 * Unit tests cho P8.2: match_evaluate_round_pure & match_apply_evaluation
 * ========================================================================= */

static void test_evaluate_main_correct(void) {
  MatchContext ctx;
  match_init(&ctx, 501);

  /* Tạo 4 người chơi: 1 MAIN (10), 3 SUB (20, 30, 40) */
  match_join_lobby(&ctx, 10, "alice_main", "tok_10", 1);
  match_join_lobby(&ctx, 20, "bob_sub", "tok_20", 2);
  match_join_lobby(&ctx, 30, "charlie_sub", "tok_30", 3);
  match_join_lobby(&ctx, 40, "david_sub", "tok_40", 4);

  match_start_game(&ctx, 10);
  ctx.main_user_id = 10;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;
  ctx.players[2].role = ROLE_SUB;
  ctx.players[3].role = ROLE_SUB;

  /* Giả lập round 1: đáp án đúng là "A" */
  match_setup_round_question(&ctx, 1, "A");

  /* Alice (MAIN) trả lời A (đúng) */
  match_submit_round_answer_validated(&ctx, 10, 501, 1, "A", ctx.round_started_at + 10, 1000);
  /* Bob (SUB) trả lời A (đúng) */
  match_submit_round_answer_validated(&ctx, 20, 501, 1, "A", ctx.round_started_at + 5, 500);
  /* Charlie (SUB) trả lời B (sai) */
  match_submit_round_answer_validated(&ctx, 30, 501, 1, "B", ctx.round_started_at + 6, 600);
  /* David (SUB) không nộp (coi như sai/hết hạn) */

  match_close_round(&ctx);

  RoundEvaluationResult eval;
  int rc = match_evaluate_round_pure(&ctx, &eval);
  assert(rc == 0);
  assert(eval.outcome == ROUND_OUTCOME_MAIN_CORRECT);
  assert(eval.winner_user_id == -1); /* Vẫn còn Bob đúng nên chưa thắng trận */
  assert(eval.new_match_state == MATCH_STATE_ROUND_ACTIVE);
  assert(eval.new_main_id == 10); /* Alice tiếp tục làm người chính */

  /* Điểm: Charlie (1000) và David (1000) bị loại -> Alice nhận 2000 -> mới = 3000 */
  /* Delta của Alice */
  assert(eval.player_deltas[0].user_id == 10);
  assert(eval.player_deltas[0].score_delta_units == 2000);
  assert(eval.player_deltas[0].new_score_units == 3000);
  assert(eval.player_deltas[0].new_status == PLAYER_STATUS_ACTIVE);

  /* Delta của Bob (đúng): giữ nguyên 1000 */
  assert(eval.player_deltas[1].user_id == 20);
  assert(eval.player_deltas[1].score_delta_units == 0);
  assert(eval.player_deltas[1].new_score_units == 1000);
  assert(eval.player_deltas[1].new_status == PLAYER_STATUS_ACTIVE);

  /* Delta của Charlie (sai): bị trừ 1000, new = 0, ELIMINATED */
  assert(eval.player_deltas[2].user_id == 30);
  assert(eval.player_deltas[2].score_delta_units == -1000);
  assert(eval.player_deltas[2].new_score_units == 0);
  assert(eval.player_deltas[2].new_status == PLAYER_STATUS_ELIMINATED);

  /* Delta của David (không nộp): bị trừ 1000, new = 0, ELIMINATED */
  assert(eval.player_deltas[3].user_id == 40);
  assert(eval.player_deltas[3].score_delta_units == -1000);
  assert(eval.player_deltas[3].new_score_units == 0);
  assert(eval.player_deltas[3].new_status == PLAYER_STATUS_ELIMINATED);

  /* Áp dụng vào ctx */
  match_apply_evaluation(&ctx, &eval);
  assert(ctx.players[0].score_units == 3000);
  assert(ctx.players[1].score_units == 1000);
  assert(ctx.players[2].status == PLAYER_STATUS_ELIMINATED);
  assert(ctx.players[3].status == PLAYER_STATUS_ELIMINATED);

  printf("[PASS] test_evaluate_main_correct: points transferred & eliminated correctly!\n");
}

static void test_evaluate_main_correct_and_wins(void) {
  MatchContext ctx;
  match_init(&ctx, 502);

  match_join_lobby(&ctx, 10, "alice_main", "tok_10", 1);
  match_join_lobby(&ctx, 20, "bob_sub", "tok_20", 2);

  match_start_game(&ctx, 10);
  ctx.main_user_id = 10;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;

  match_setup_round_question(&ctx, 1, "B");

  /* Alice đúng, Bob sai */
  match_submit_round_answer_validated(&ctx, 10, 502, 1, "B", ctx.round_started_at + 10, 1000);
  match_submit_round_answer_validated(&ctx, 20, 502, 1, "C", ctx.round_started_at + 5, 500);

  match_close_round(&ctx);

  RoundEvaluationResult eval;
  int rc = match_evaluate_round_pure(&ctx, &eval);
  assert(rc == 0);
  assert(eval.outcome == ROUND_OUTCOME_MAIN_CORRECT);
  assert(eval.winner_user_id == 10); /* Bob sai -> hết người phụ đúng -> Alice thắng! */
  assert(eval.new_match_state == MATCH_STATE_ENDED);
  assert(eval.player_deltas[0].new_score_units == 2000);

  match_apply_evaluation(&ctx, &eval);
  assert(ctx.winner_user_id == 10);
  assert(ctx.state == MATCH_STATE_ENDED);

  printf("[PASS] test_evaluate_main_correct_and_wins: main wins match verified!\n");
}

static void test_evaluate_main_wrong_and_role_transfer(void) {
  MatchContext ctx;
  match_init(&ctx, 503);

  /* 1 MAIN (Alice=10), 3 SUB (Bob=20, Charlie=30, David=40) */
  match_join_lobby(&ctx, 10, "alice_main", "tok_10", 1);
  match_join_lobby(&ctx, 20, "bob_sub", "tok_20", 2);
  match_join_lobby(&ctx, 30, "charlie_sub", "tok_30", 3);
  match_join_lobby(&ctx, 40, "david_sub", "tok_40", 4);

  match_start_game(&ctx, 10);
  ctx.main_user_id = 10;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;
  ctx.players[2].role = ROLE_SUB;
  ctx.players[3].role = ROLE_SUB;

  match_setup_round_question(&ctx, 1, "D");

  /* Alice sai (nộp A) */
  match_submit_round_answer_validated(&ctx, 10, 503, 1, "A", ctx.round_started_at + 10, 1000);

  /* Charlie đúng tại ns=400 (sớm nhất) */
  match_submit_round_answer_validated(&ctx, 30, 503, 1, "D", ctx.round_started_at + 4, 400);

  /* Bob đúng tại ns=800 (muộn hơn Charlie) */
  match_submit_round_answer_validated(&ctx, 20, 503, 1, "D", ctx.round_started_at + 8, 800);

  /* David sai (nộp B) */
  match_submit_round_answer_validated(&ctx, 40, 503, 1, "B", ctx.round_started_at + 9, 900);

  match_close_round(&ctx);

  RoundEvaluationResult eval;
  int rc = match_evaluate_round_pure(&ctx, &eval);
  assert(rc == 0);
  assert(eval.outcome == ROUND_OUTCOME_MAIN_WRONG);
  assert(eval.winner_user_id == -1);
  assert(eval.new_match_state == MATCH_STATE_ROUND_ACTIVE);

  /* Charlie đúng sớm nhất -> trở thành tân người chính */
  assert(eval.new_main_id == 30);
  assert(eval.player_deltas[2].new_role == ROLE_MAIN);

  /* Alice bị loại */
  assert(eval.player_deltas[0].new_status == PLAYER_STATUS_ELIMINATED);
  assert(eval.player_deltas[0].new_score_units == 0);

  /* Điểm của Alice (1000 units) chia cho 3 người phụ còn sống (Bob, Charlie, David):
   * 1000 / 3 = 333 units mỗi người, dư 1 unit được ưu tiên cho Charlie (đúng sớm nhất) -> Charlie nhận +334, Bob +333, David +333 */
  assert(eval.player_deltas[2].user_id == 30); /* Charlie */
  assert(eval.player_deltas[2].score_delta_units == 334);
  assert(eval.player_deltas[2].new_score_units == 1334);

  assert(eval.player_deltas[1].user_id == 20); /* Bob */
  assert(eval.player_deltas[1].score_delta_units == 333);
  assert(eval.player_deltas[1].new_score_units == 1333);

  assert(eval.player_deltas[3].user_id == 40); /* David (sai nhưng vẫn sống nhận phần chia) */
  assert(eval.player_deltas[3].score_delta_units == 333);
  assert(eval.player_deltas[3].new_score_units == 1333);

  /* Tổng điểm 4 người trước và sau bảo toàn: 1000*4 = 4000 = 0 + 1333 + 1334 + 1333 */
  int32_t total_after = eval.player_deltas[0].new_score_units +
                        eval.player_deltas[1].new_score_units +
                        eval.player_deltas[2].new_score_units +
                        eval.player_deltas[3].new_score_units;
  assert(total_after == 4000);

  match_apply_evaluation(&ctx, &eval);
  assert(ctx.main_user_id == 30);
  assert(ctx.players[0].status == PLAYER_STATUS_ELIMINATED);
  assert(ctx.players[2].role == ROLE_MAIN);

  printf("[PASS] test_evaluate_main_wrong_and_role_transfer: score split & role reassigned successfully!\n");
}

static void test_evaluate_all_wrong_no_winner(void) {
  MatchContext ctx;
  match_init(&ctx, 504);

  match_join_lobby(&ctx, 10, "alice_main", "tok_10", 1);
  match_join_lobby(&ctx, 20, "bob_sub", "tok_20", 2);

  match_start_game(&ctx, 10);
  ctx.main_user_id = 10;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;

  match_setup_round_question(&ctx, 1, "C");

  /* Cả Alice và Bob đều trả lời sai */
  match_submit_round_answer_validated(&ctx, 10, 504, 1, "A", ctx.round_started_at + 10, 1000);
  match_submit_round_answer_validated(&ctx, 20, 504, 1, "B", ctx.round_started_at + 5, 500);

  match_close_round(&ctx);

  RoundEvaluationResult eval;
  int rc = match_evaluate_round_pure(&ctx, &eval);
  assert(rc == 0);
  assert(eval.outcome == ROUND_OUTCOME_MAIN_WRONG);
  assert(eval.winner_user_id == -1); /* Không ai thắng */
  assert(eval.new_match_state == MATCH_STATE_ENDED);

  match_apply_evaluation(&ctx, &eval);
  assert(ctx.state == MATCH_STATE_ENDED);
  assert(ctx.winner_user_id == -1);

  printf("[PASS] test_evaluate_all_wrong_no_winner: ended with no winner verified!\n");
}

/* =========================================================================
 * Unit tests cho P8.3: match_evaluate_and_commit_round (atomic, single-eval)
 * ========================================================================= */

static void test_p8_3_atomic_commit_and_single_eval(void) {
  MatchContext ctx;
  match_init(&ctx, 601);

  /* 3 người chơi: Alice (MAIN=1), Bob (SUB=2), Charlie (SUB=3) */
  match_join_lobby(&ctx, 1, "alice", "tok_1", 1);
  match_join_lobby(&ctx, 2, "bob", "tok_2", 2);
  match_join_lobby(&ctx, 3, "charlie", "tok_3", 3);

  match_start_game(&ctx, 1);
  ctx.main_user_id = 1;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;
  ctx.players[2].role = ROLE_SUB;

  match_setup_round_question(&ctx, 1, "A");

  /* Alice nộp A (đúng) */
  match_submit_round_answer_validated(&ctx, 1, 601, 1, "A", ctx.round_started_at + 10, 1000);
  /* Bob nộp A (đúng) */
  match_submit_round_answer_validated(&ctx, 2, 601, 1, "A", ctx.round_started_at + 5, 500);
  /* Charlie nộp C (sai) */
  match_submit_round_answer_validated(&ctx, 3, 601, 1, "C", ctx.round_started_at + 6, 600);

  /* 1. Khi lượt chưa chốt (round_closed == false), gọi commit phải bị từ chối */
  RoundEvaluationResult eval;
  int err_unclosed = match_evaluate_and_commit_round(&ctx, &eval);
  assert(err_unclosed == -2);

  /* Chốt lượt */
  match_close_round(&ctx);
  assert(ctx.round_closed == true);
  assert(ctx.round_evaluated == false);

  /* 2. Lần chấm đầu tiên: thành công, cập nhật điểm nguyên tử */
  int rc = match_evaluate_and_commit_round(&ctx, &eval);
  assert(rc == 0);
  assert(ctx.round_evaluated == true);

  /* Kiểm tra điểm: Alice nhận 1000 của Charlie -> Alice=2000, Bob=1000, Charlie=0 (bị loại) */
  assert(ctx.players[0].score_units == 2000);
  assert(ctx.players[1].score_units == 1000);
  assert(ctx.players[2].score_units == 0);
  assert(ctx.players[2].status == PLAYER_STATUS_ELIMINATED);

  /* 3. Chấm lần thứ hai trên cùng một lượt -> PHẢI BỊ TỪ CHỐI (không chấm đúp) */
  RoundEvaluationResult eval_dup;
  int err_dup = match_evaluate_and_commit_round(&ctx, &eval_dup);
  assert(err_dup == -3);

  /* Điểm tuyệt đối không bị thay đổi thêm một lần nữa */
  assert(ctx.players[0].score_units == 2000);
  assert(ctx.players[1].score_units == 1000);

  /* 4. Kiểm tra JSON ROUND_RESULT sinh ra đúng định dạng */
  char res_json[1024];
  int jlen = match_build_round_result(&eval, res_json, sizeof(res_json));
  assert(jlen > 0);
  assert(strstr(res_json, "\"type\":\"ROUND_RESULT\"") != NULL);
  assert(strstr(res_json, "\"MAIN_CORRECT\"") != NULL);
  assert(strstr(res_json, "\"deltas\"") != NULL);

  printf("[PASS] test_p8_3_atomic_commit_and_single_eval: single commit & JSON verified!\n");
}

static void test_p8_3_atomic_rollback_on_integrity_error(void) {
  MatchContext ctx;
  match_init(&ctx, 602);

  match_join_lobby(&ctx, 1, "alice", "tok_1", 1);
  match_join_lobby(&ctx, 2, "bob", "tok_2", 2);

  match_start_game(&ctx, 1);
  ctx.main_user_id = 1;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;

  match_setup_round_question(&ctx, 1, "A");
  match_close_round(&ctx);

  /* Cố ý làm sai lệch trạng thái bộ đệm để giả lập lỗi tính toán không toàn vẹn */
  /* Lưu điểm snapshot trước đó */
  int32_t alice_old = ctx.players[0].score_units;
  int32_t bob_old = ctx.players[1].score_units;

  /* Giả sử gọi evaluate thuần túy lỗi */
  ctx.players[0].status = PLAYER_STATUS_ELIMINATED; /* Làm hỏng trạng thái main */
  RoundEvaluationResult eval;
  int rc = match_evaluate_and_commit_round(&ctx, &eval);
  assert(rc < 0); /* Phải trả về lỗi âm */
  assert(ctx.round_evaluated == false); /* Không được đánh dấu đã chấm */

  /* Khôi phục lại */
  ctx.players[0].status = PLAYER_STATUS_ACTIVE;
  ctx.players[0].score_units = alice_old;
  ctx.players[1].score_units = bob_old;

  printf("[PASS] test_p8_3_atomic_rollback_on_integrity_error: integrity protection verified!\n");
}

/* =========================================================================
 * Unit tests cho P8.5: match_is_game_finished & match_build_game_finished
 * ========================================================================= */

static void test_p8_5_game_finished_with_winner(void) {
  MatchContext ctx;
  match_init(&ctx, 701);

  /* 2 người chơi: Alice (MAIN=10), Bob (SUB=20) */
  match_join_lobby(&ctx, 10, "alice", "tok_10", 1);
  match_join_lobby(&ctx, 20, "bob", "tok_20", 2);

  match_start_game(&ctx, 10);
  ctx.main_user_id = 10;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;

  /* Giả lập câu hỏi và lượt thi đấu: Alice trả lời đúng, Bob trả lời sai */
  match_setup_round_question(&ctx, 1, "C");
  match_submit_round_answer_validated(&ctx, 10, 701, 1, "C", ctx.round_started_at + 10, 1000);
  match_submit_round_answer_validated(&ctx, 20, 701, 1, "D", ctx.round_started_at + 5, 500);
  match_close_round(&ctx);

  /* Chấm lượt */
  RoundEvaluationResult eval;
  int rc = match_evaluate_and_commit_round(&ctx, &eval);
  assert(rc == 0);

  /* Kiểm tra điều kiện kết thúc trận */
  MatchEndReason reason;
  bool is_finished = match_is_game_finished(&ctx, &reason);
  assert(is_finished == true);
  assert(reason == END_REASON_WINNER);
  assert(ctx.winner_user_id == 10);

  /* Kiểm tra format bản tin GAME_FINISHED broadcast */
  char buf[1024];
  int len = match_build_game_finished(&ctx, buf, sizeof(buf));
  assert(len > 0);
  assert(strstr(buf, "\"type\":\"GAME_FINISHED\"") != NULL);
  assert(strstr(buf, "\"reason\":\"WINNER\"") != NULL);
  assert(strstr(buf, "\"winner_user_id\":10") != NULL);
  assert(strstr(buf, "\"winner_username\":\"alice\"") != NULL);
  assert(strstr(buf, "\"final_scores\"") != NULL);
  assert(strstr(buf, "\"score\":2000") != NULL); /* Alice gom trọn 2000 units */

  printf("[PASS] test_p8_5_game_finished_with_winner: winner broadcast & reason verified!\n");
}

static void test_p8_5_game_finished_no_winner(void) {
  MatchContext ctx;
  match_init(&ctx, 702);

  /* 2 người chơi: Alice (MAIN=10), Bob (SUB=20) */
  match_join_lobby(&ctx, 10, "alice", "tok_10", 1);
  match_join_lobby(&ctx, 20, "bob", "tok_20", 2);

  match_start_game(&ctx, 10);
  ctx.main_user_id = 10;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;

  /* Giả lập câu hỏi: Cả Alice và Bob đều trả lời sai */
  match_setup_round_question(&ctx, 1, "A");
  match_submit_round_answer_validated(&ctx, 10, 702, 1, "B", ctx.round_started_at + 10, 1000);
  match_submit_round_answer_validated(&ctx, 20, 702, 1, "C", ctx.round_started_at + 5, 500);
  match_close_round(&ctx);

  /* Chấm lượt */
  RoundEvaluationResult eval;
  int rc = match_evaluate_and_commit_round(&ctx, &eval);
  assert(rc == 0);

  /* Kiểm tra điều kiện kết thúc trận */
  MatchEndReason reason;
  bool is_finished = match_is_game_finished(&ctx, &reason);
  assert(is_finished == true);
  assert(reason == END_REASON_ALL_WRONG);
  assert(ctx.winner_user_id == -1);

  /* Kiểm tra format bản tin GAME_FINISHED broadcast không ai thắng */
  char buf[1024];
  int len = match_build_game_finished(&ctx, buf, sizeof(buf));
  assert(len > 0);
  assert(strstr(buf, "\"type\":\"GAME_FINISHED\"") != NULL);
  assert(strstr(buf, "\"reason\":\"ALL_WRONG\"") != NULL);
  assert(strstr(buf, "\"winner_user_id\":-1") != NULL);

  printf("[PASS] test_p8_5_game_finished_no_winner: all wrong / no winner verified!\n");
}

/* =========================================================================
 * Unit tests cho P9.1: Phát thông báo ROUND_STARTED, ROUND_RESULT và SCORE_UPDATED
 * ========================================================================= */

static void test_p9_1_broadcast_score_updated_json(void) {
  MatchContext ctx;
  match_init(&ctx, 901);

  match_join_lobby(&ctx, 101, "player_1", "tok_1", 10);
  match_join_lobby(&ctx, 102, "player_2", "tok_2", 11);
  match_join_lobby(&ctx, 103, "player_3", "tok_3", -1); /* Client disconnected: socket_fd < 0 */
  ctx.players[2].status = PLAYER_STATUS_DISCONNECTED;

  match_start_game(&ctx, 101);
  ctx.current_round = 3;
  ctx.players[0].score_units = 1500;
  ctx.players[1].score_units = 500;

  char score_json[1024];
  int len = match_build_score_updated(&ctx, score_json, sizeof(score_json));
  assert(len > 0);
  assert(strstr(score_json, "\"type\":\"SCORE_UPDATED\"") != NULL);
  assert(strstr(score_json, "\"match_id\":901") != NULL);
  assert(strstr(score_json, "\"round_id\":3") != NULL);
  assert(strstr(score_json, "\"player_1\"") != NULL);
  assert(strstr(score_json, "\"score\":1500") != NULL);
  assert(strstr(score_json, "\"player_2\"") != NULL);
  assert(strstr(score_json, "\"score\":500") != NULL);
  /* Người đã DISCONNECTED không xuất hiện trong danh sách điểm của trận */
  assert(strstr(score_json, "\"player_3\"") == NULL);

  printf("[PASS] test_p9_1_broadcast_score_updated_json: SCORE_UPDATED json verified!\n");
}

static void test_p9_1_active_player_sockets_filtering(void) {
  MatchContext ctx;
  match_init(&ctx, 902);

  /* Tạo 4 người chơi:
   * p1: ACTIVE (socket 10)
   * p2: ACTIVE (socket 20)
   * p3: ELIMINATED (socket 30)
   * p4: DISCONNECTED (socket -1)
   */
  match_join_lobby(&ctx, 1, "p1", "t1", 10);
  match_join_lobby(&ctx, 2, "p2", "t2", 20);
  match_join_lobby(&ctx, 3, "p3", "t3", 30);
  match_join_lobby(&ctx, 4, "p4", "t4", 40);

  match_start_game(&ctx, 1);
  ctx.players[2].status = PLAYER_STATUS_ELIMINATED;
  ctx.players[3].status = PLAYER_STATUS_DISCONNECTED;
  ctx.players[3].socket_fd = -1;

  int socks[10];
  /* Khi phát ROUND_STARTED: chỉ gửi người ACTIVE */
  int count_active = match_get_active_player_sockets(&ctx, false, socks, 10);
  assert(count_active == 2);
  assert(socks[0] == 10);
  assert(socks[1] == 20);

  /* Khi phát ROUND_RESULT / SCORE_UPDATED: gửi cả người vừa bị ELIMINATED */
  int count_with_elim = match_get_active_player_sockets(&ctx, true, socks, 10);
  assert(count_with_elim == 3);
  assert(socks[0] == 10);
  assert(socks[1] == 20);
  assert(socks[2] == 30);

  printf("[PASS] test_p9_1_active_player_sockets_filtering: filtering active/eliminated verified!\n");
}

static void test_p9_1_safe_broadcast_non_blocking_error(void) {
  /* Kiểm tra match_broadcast_message_safe với socket giả/lỗi để đảm bảo:
   * 1 socket lỗi/đóng không chặn vô hạn và không làm chết quá trình phát cho socket khác */
  int fake_socks[3] = {-1, -2, -1};
  const char *msg = "{\"type\":\"TEST\"}\n";
  int failed_socks[3];
  int sent = match_broadcast_message_safe(fake_socks, 3, msg, strlen(msg), 100, failed_socks, 3);
  assert(sent == 0); /* Các socket đều < 0 nên an toàn bỏ qua */

  printf("[PASS] test_p9_1_safe_broadcast_non_blocking_error: safe broadcast verified!\n");
}

/* =========================================================================
 * Unit tests cho P9.2: Bảo vệ trạng thái và kiểm thử đồng thời (Concurrency)
 * ========================================================================= */

#ifdef _WIN32
#include <process.h>
#include <windows.h>
typedef HANDLE test_thread_t;
typedef CRITICAL_SECTION test_mutex_t;
#define TEST_MUTEX_INIT(m) InitializeCriticalSection(m)
#define TEST_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#define TEST_MUTEX_LOCK(m) EnterCriticalSection(m)
#define TEST_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#else
#include <pthread.h>
typedef pthread_t test_thread_t;
typedef pthread_mutex_t test_mutex_t;
#define TEST_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define TEST_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#define TEST_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define TEST_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#endif

typedef struct {
  MatchContext *ctx;
  test_mutex_t *lock;
  int thread_id;
  int eval_success_count;
  int eval_already_done_count;
} ConcurrencyEvalArg;

#ifdef _WIN32
static unsigned __stdcall thread_eval_worker(void *arg)
#else
static void *thread_eval_worker(void *arg)
#endif
{
  ConcurrencyEvalArg *ca = (ConcurrencyEvalArg *)arg;
  RoundEvaluationResult eval;

  TEST_MUTEX_LOCK(ca->lock);
  int rc = match_evaluate_and_commit_round(ca->ctx, &eval);
  TEST_MUTEX_UNLOCK(ca->lock);

  if (rc == 0) {
    ca->eval_success_count++;
  } else if (rc == -3) {
    ca->eval_already_done_count++;
  }

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

static void test_p9_2_concurrent_evaluation_single_commit(void) {
  /* Kiểm tra 10 luồng cùng cố gắng evaluate_and_commit_round trên 1 lượt thi:
   * CHỈ CÓ ĐÚNG 1 LUỒNG THÀNH CÔNG (0), CÁC LUỒNG KHÁC BỊ TỪ CHỐI (-3).
   * Điểm số và trạng thái không bị cập nhật sai hay nhân đôi. */
  MatchContext ctx;
  match_init(&ctx, 920);

  match_join_lobby(&ctx, 1, "alice_main", "tok_1", 1);
  match_join_lobby(&ctx, 2, "bob_sub", "tok_2", 2);
  match_join_lobby(&ctx, 3, "carol_sub", "tok_3", 3);

  match_start_game(&ctx, 1);
  ctx.main_user_id = 1;
  ctx.players[0].role = ROLE_MAIN;
  ctx.players[1].role = ROLE_SUB;
  ctx.players[2].role = ROLE_SUB;

  match_setup_round_question(&ctx, 1, "A");
  /* Main đúng "A", Sub 1 đúng "A", Sub 2 sai "B" */
  match_submit_round_answer_validated(&ctx, 1, 920, 1, "A", time(NULL), 100);
  match_submit_round_answer_validated(&ctx, 2, 920, 1, "A", time(NULL), 200);
  match_submit_round_answer_validated(&ctx, 3, 920, 1, "B", time(NULL), 300);

  match_close_round(&ctx);

  test_mutex_t lock;
  TEST_MUTEX_INIT(&lock);

  const int NUM_THREADS = 10;
  test_thread_t threads[10];
  ConcurrencyEvalArg args[10];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].ctx = &ctx;
    args[i].lock = &lock;
    args[i].thread_id = i;
    args[i].eval_success_count = 0;
    args[i].eval_already_done_count = 0;

#ifdef _WIN32
    threads[i] = (HANDLE)_beginthreadex(NULL, 0, thread_eval_worker, &args[i], 0, NULL);
#else
    pthread_create(&threads[i], NULL, thread_eval_worker, &args[i]);
#endif
  }

  /* Đợi tất cả hoàn thành */
  for (int i = 0; i < NUM_THREADS; i++) {
#ifdef _WIN32
    WaitForSingleObject(threads[i], INFINITE);
    CloseHandle(threads[i]);
#else
    pthread_join(threads[i], NULL);
#endif
  }

  TEST_MUTEX_DESTROY(&lock);

  int total_success = 0;
  int total_already = 0;
  for (int i = 0; i < NUM_THREADS; i++) {
    total_success += args[i].eval_success_count;
    total_already += args[i].eval_already_done_count;
  }

  /* Khẳng định: chỉ đúng 1 luồng chấm thành công, 9 luồng còn lại bị từ chối */
  assert(total_success == 1);
  assert(total_already == NUM_THREADS - 1);

  /* Tổng điểm bảo toàn: 1000 + 1000 + 1000 = 3000 */
  int32_t total_score = 0;
  for (int p = 0; p < ctx.player_count; p++) {
    total_score += ctx.players[p].score_units;
  }
  assert(total_score == 3000);

  /* Alice nhận 1000 từ Carol: 2000 units. Bob giữ 1000 units. Carol: 0 units */
  assert(ctx.players[0].score_units == 2000);
  assert(ctx.players[1].score_units == 1000);
  assert(ctx.players[2].score_units == 0);
  assert(ctx.players[2].status == PLAYER_STATUS_ELIMINATED);

  printf("[PASS] test_p9_2_concurrent_evaluation_single_commit: 10 concurrent threads exactly 1 commit!\n");
}

typedef struct {
  MatchContext *ctx;
  test_mutex_t *lock;
  int64_t user_id;
  int round_id;
  int accepted_count;
  int rejected_count;
} ConcurrencyAnswerArg;

#ifdef _WIN32
static unsigned __stdcall thread_answer_worker(void *arg)
#else
static void *thread_answer_worker(void *arg)
#endif
{
  ConcurrencyAnswerArg *ca = (ConcurrencyAnswerArg *)arg;

  TEST_MUTEX_LOCK(ca->lock);
  AnswerSubmitResult res = match_submit_round_answer_validated(
      ca->ctx, ca->user_id, ca->ctx->match_id, ca->round_id, "A", time(NULL),
      match_get_server_time_ns());
  TEST_MUTEX_UNLOCK(ca->lock);

  if (res == ANSWER_ACK_OK) {
    ca->accepted_count++;
  } else if (res == ANSWER_ACK_ERR_ALREADY_SUBMITTED) {
    ca->rejected_count++;
  }

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

static void test_p9_2_concurrent_duplicate_answer_submit(void) {
  /* Kiểm tra 8 luồng cùng nộp đáp án cho CÙNG 1 NGƯỜI CHƠI trong cùng 1 lượt:
   * CHỈ CÓ ĐÚNG 1 LẦN ĐƯỢC CHẤP NHẬN (OK), 7 LẦN CÒN LẠI BỊ TỪ CHỐI (ALREADY_SUBMITTED). */
  MatchContext ctx;
  match_init(&ctx, 921);

  match_join_lobby(&ctx, 50, "same_player", "tok_50", 1);
  match_join_lobby(&ctx, 51, "other_player", "tok_51", 2);

  match_start_game(&ctx, 50);
  match_setup_round_question(&ctx, 1, "A");

  test_mutex_t lock;
  TEST_MUTEX_INIT(&lock);

  const int NUM_THREADS = 8;
  test_thread_t threads[8];
  ConcurrencyAnswerArg args[8];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].ctx = &ctx;
    args[i].lock = &lock;
    args[i].user_id = 50; /* Cùng 1 user_id */
    args[i].round_id = 1;
    args[i].accepted_count = 0;
    args[i].rejected_count = 0;

#ifdef _WIN32
    threads[i] = (HANDLE)_beginthreadex(NULL, 0, thread_answer_worker, &args[i], 0, NULL);
#else
    pthread_create(&threads[i], NULL, thread_answer_worker, &args[i]);
#endif
  }

  for (int i = 0; i < NUM_THREADS; i++) {
#ifdef _WIN32
    WaitForSingleObject(threads[i], INFINITE);
    CloseHandle(threads[i]);
#else
    pthread_join(threads[i], NULL);
#endif
  }

  TEST_MUTEX_DESTROY(&lock);

  int total_accepted = 0;
  int total_rejected = 0;
  for (int i = 0; i < NUM_THREADS; i++) {
    total_accepted += args[i].accepted_count;
    total_rejected += args[i].rejected_count;
  }

  assert(total_accepted == 1);
  assert(total_rejected == NUM_THREADS - 1);
  assert(ctx.round_answer_count == 1);

  printf("[PASS] test_p9_2_concurrent_duplicate_answer_submit: 8 concurrent submits exactly 1 accepted!\n");
}

int main(void) {
  printf("=== Bắt đầu chạy test phòng chờ và trạng thái trận (P6.2, P6.3, "
         "P7.2, P7.3) ===\n");
  printf("=== Bắt đầu chạy test phòng chờ, câu hỏi, timer và nộp đáp án (P6.2 "
         "-> P7.4) ===\n");
  test_join_normal();
  test_join_duplicate();
  test_join_limit_100();
  test_score_initialization();
  test_lobby_updated_json();
  test_player_leave();
  test_start_game();
  test_selection_flow_and_tie_break();
  test_round_timer_deadlines();
  printf("=== TẤT CẢ TEST P6, P7.2 & P7.3 ĐÃ VƯỢT QUA THÀNH CÔNG! ===\n");
  test_answer_submit_and_ack_validation();
  printf("=== TẤT CẢ TEST P6 & P7 ĐÃ VƯỢT QUA THÀNH CÔNG! ===\n");

  printf("=== Bắt đầu chạy test bộ xử lý kết quả lượt thi P8.2 ===\n");
  test_evaluate_main_correct();
  test_evaluate_main_correct_and_wins();
  test_evaluate_main_wrong_and_role_transfer();
  test_evaluate_all_wrong_no_winner();
  printf("=== TẤT CẢ TEST P8.2 ĐÃ VƯỢT QUA THÀNH CÔNG! ===\n");

  printf("=== Bắt đầu chạy test chấm điểm nguyên tử P8.3 ===\n");
  test_p8_3_atomic_commit_and_single_eval();
  test_p8_3_atomic_rollback_on_integrity_error();
  printf("=== TẤT CẢ TEST P8.3 ĐÃ VƯỢT QUA THÀNH CÔNG! ===\n");

  printf("=== Bắt đầu chạy test kết thúc trận P8.5 ===\n");
  test_p8_5_game_finished_with_winner();
  test_p8_5_game_finished_no_winner();
  printf("=== TẤT CẢ TEST P8.5 ĐÃ VƯỢT QUA THÀNH CÔNG! ===\n");

  printf("=== Bắt đầu chạy test phát thông báo P9.1 ===\n");
  test_p9_1_broadcast_score_updated_json();
  test_p9_1_active_player_sockets_filtering();
  test_p9_1_safe_broadcast_non_blocking_error();
  printf("=== TẤT CẢ TEST P9.1 ĐÃ VƯỢT QUA THÀNH CÔNG! ===\n");

  printf("=== Bắt đầu chạy test bảo vệ trạng thái đồng thời P9.2 ===\n");
  test_p9_2_concurrent_evaluation_single_commit();
  test_p9_2_concurrent_duplicate_answer_submit();
  printf("=== TẤT CẢ TEST P9.2 ĐÃ VƯỢT QUA THÀNH CÔNG! ===\n");
  return 0;
}
