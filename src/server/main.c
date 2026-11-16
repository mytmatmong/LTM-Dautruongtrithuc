#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <process.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSE_SOCKET(s) closesocket(s)
typedef HANDLE thread_t;
#define IS_INVALID_SOCKET(s) ((s) == INVALID_SOCKET)
#else
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define CLOSE_SOCKET(s) close(s)
#define IS_INVALID_SOCKET(s) ((s) < 0)
typedef pthread_t thread_t;
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "auth_crypto.h"
#include "auth_msg.h"
#include "db.h"
#include "match.h"
#include "net_framing.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080
#define DEFAULT_DB_PATH "server.db"
#define BACKLOG_QUEUE 128
#define MAX_CONCURRENT_CLIENTS 100
#define SESSION_EXPIRY_SECONDS (24 * 3600) /* 24 giờ */

/* Cấu trúc dữ liệu truyền cho mỗi luồng xử lý client */
typedef struct {
  socket_t client_sock;
  struct sockaddr_in client_addr;
} ClientContext;

/* Trạng thái dùng chung và bộ khóa mutex */
static int g_active_clients = 0;
static sqlite3 *g_db = NULL;
static MatchContext g_active_match;

#ifdef _WIN32
static CRITICAL_SECTION g_clients_lock;
static CRITICAL_SECTION g_db_lock;
static CRITICAL_SECTION g_match_lock;
static void init_sync(void) {
  InitializeCriticalSection(&g_clients_lock);
  InitializeCriticalSection(&g_db_lock);
  InitializeCriticalSection(&g_match_lock);
}
static void cleanup_sync(void) {
  DeleteCriticalSection(&g_clients_lock);
  DeleteCriticalSection(&g_db_lock);
  DeleteCriticalSection(&g_match_lock);
}
static void lock_sync(void) { EnterCriticalSection(&g_clients_lock); }
static void unlock_sync(void) { LeaveCriticalSection(&g_clients_lock); }
static void lock_db(void) { EnterCriticalSection(&g_db_lock); }
static void unlock_db(void) { LeaveCriticalSection(&g_db_lock); }
static void lock_match(void) { EnterCriticalSection(&g_match_lock); }
static void unlock_match(void) { LeaveCriticalSection(&g_match_lock); }
#else
static pthread_mutex_t g_clients_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_db_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_match_lock = PTHREAD_MUTEX_INITIALIZER;
static void init_sync(void) {}
static void cleanup_sync(void) {
  pthread_mutex_destroy(&g_clients_lock);
  pthread_mutex_destroy(&g_db_lock);
  pthread_mutex_destroy(&g_match_lock);
}
static void lock_sync(void) { pthread_mutex_lock(&g_clients_lock); }
static void unlock_sync(void) { pthread_mutex_unlock(&g_clients_lock); }
static void lock_db(void) { pthread_mutex_lock(&g_db_lock); }
static void unlock_db(void) { pthread_mutex_unlock(&g_db_lock); }
static void lock_match(void) { pthread_mutex_lock(&g_match_lock); }
static void unlock_match(void) { pthread_mutex_unlock(&g_match_lock); }
#endif

static void init_network(void) {
#ifdef _WIN32
  WSADATA wsa_data;
  int res = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (res != 0) {
    fprintf(stderr, "Lỗi WSAStartup thất bại: %d\n", res);
    exit(EXIT_FAILURE);
  }
#endif
  init_sync();
}

static void cleanup_network(void) {
#ifdef _WIN32
  WSACleanup();
#endif
  cleanup_sync();
}

static void generate_session_token(char *out_buf, size_t buf_size) {
  static const char hex[] = "0123456789abcdef";
  char rand_part[33];
  for (int i = 0; i < 32; i++) {
    rand_part[i] = hex[rand() % 16];
  }
  rand_part[32] = '\0';
  snprintf(out_buf, buf_size, "sess_%s", rand_part);
}

static void broadcast_lobby_update(void) {
  char update_buf[8192];
  int sock_list[MAX_MATCH_PLAYERS];
  int count = 0;

  lock_match();
  match_build_lobby_updated(&g_active_match, update_buf, sizeof(update_buf));
  for (int i = 0; i < g_active_match.player_count; i++) {
    sock_list[count++] = g_active_match.players[i].socket_fd;
  }
  unlock_match();

  size_t len = strlen(update_buf);
  for (int i = 0; i < count; i++) {
    if (sock_list[i] >= 0) {
      net_send_all(sock_list[i], update_buf, len);
    }
  }
}

static void broadcast_game_started(void) {
  char started_buf[1024];
  int sock_list[MAX_MATCH_PLAYERS];
  int count = 0;

  lock_match();
  match_build_game_started(&g_active_match, started_buf, sizeof(started_buf));
  for (int i = 0; i < g_active_match.player_count; i++) {
    sock_list[count++] = g_active_match.players[i].socket_fd;
  }
  unlock_match();

  size_t len = strlen(started_buf);
  for (int i = 0; i < count; i++) {
    if (sock_list[i] >= 0) {
      net_send_all(sock_list[i], started_buf, len);
    }
  }
}

static void broadcast_selection_question(void) {
  DbQuestion q;
  memset(&q, 0, sizeof(q));

  lock_db();
  int rc = db_question_get_random(g_db, &q);
  unlock_db();

  if (rc != DB_SUCCESS) {
    /* Nếu DB chưa có câu hỏi, tạo câu hỏi mặc định */
    q.question_id = 1;
    snprintf(q.content, sizeof(q.content), "%s",
             "Thủ đô của Việt Nam là thành phố nào?");
    snprintf(q.options_json, sizeof(q.options_json), "%s",
             "{\"A\":\"Hà Nội\",\"B\":\"TP.HCM\",\"C\":\"Đà Nẵng\",\"D\":\"Cần "
             "Thơ\"}");
    snprintf(q.correct_choice, sizeof(q.correct_choice), "%s", "A");
    q.is_active = 1;

    lock_db();
    db_question_add(g_db, q.content, q.options_json, q.correct_choice, NULL);
    unlock_db();
  }

  char question_buf[2048];
  int sock_list[MAX_MATCH_PLAYERS];
  int count = 0;
  int timeout_seconds = 15; /* 15s cho câu hỏi chọn người chơi chính */

  lock_match();
  match_setup_selection_question(&g_active_match, q.question_id,
                                 q.correct_choice, timeout_seconds);
  match_build_selection_question(&g_active_match, q.content, q.options_json,
                                 timeout_seconds, question_buf,
                                 sizeof(question_buf));
  for (int i = 0; i < g_active_match.player_count; i++) {
    sock_list[count++] = g_active_match.players[i].socket_fd;
  }
  unlock_match();

  size_t len = strlen(question_buf);
  for (int i = 0; i < count; i++) {
    if (sock_list[i] >= 0) {
      net_send_all(sock_list[i], question_buf, len);
    }
  }
  printf("[Game] Đã phát câu hỏi chọn người chơi chính (QID: %ld)\n",
         (long)q.question_id);
}

static void broadcast_game_started(void);
static void broadcast_selection_question(void);
static void broadcast_role_assigned(void);
static void broadcast_round_started(void);
static void broadcast_round_result_and_scores(const RoundEvaluationResult *eval);

#ifdef _WIN32
static unsigned __stdcall timer_watcher_thread(void *arg)
#else
static void *timer_watcher_thread(void *arg)
#endif
{
  (void)arg;
  while (1) {
#ifdef _WIN32
    Sleep(500); /* Chờ 500ms không busy-wait */
#else
    struct timespec req = {0, 500000000L}; /* 500ms */
    nanosleep(&req, NULL);
#endif

    time_t now = time(NULL);
    bool should_close_selection = false;
    bool should_close_round = false;

    lock_match();
    if (g_active_match.state == MATCH_STATE_SELECTING_MAIN) {
      if (now > g_active_match.round_deadline) {
        should_close_selection = true;
        match_evaluate_selection(&g_active_match);
      }
    } else if (g_active_match.state == MATCH_STATE_ROUND_ACTIVE) {
      if (!g_active_match.round_closed && now > g_active_match.main_deadline) {
        should_close_round = true;
        match_close_round(&g_active_match);
      }
    }
    unlock_match();

    if (should_close_selection) {
      printf("[Timer] Hết thời gian câu hỏi chọn người chơi chính -> Chốt và "
             "phân vai!\n");
      broadcast_role_assigned();
      /* Tự động bắt đầu lượt thi đấu chính thức đầu tiên */
      broadcast_round_started();
    }

    if (should_close_round) {
      printf("[Timer] Hết thời hạn lượt thi đấu chính thức (60s) -> Đã chốt "
             "lượt, từ chối đáp án muộn!\n");
      RoundEvaluationResult eval;
      memset(&eval, 0, sizeof(eval));
      DbRoundHistoryTx tx;
      memset(&tx, 0, sizeof(tx));

      lock_match();
      int eval_rc = match_evaluate_and_commit_round(&g_active_match, &eval);
      if (eval_rc == 0) {
        /* Chuẩn bị dữ liệu transaction ghi DB lịch sử */
        tx.match_id = g_active_match.match_id;
        tx.round_id = eval.round_id;
        tx.question_id = g_active_match.round_question_id;
        tx.main_user_id = eval.old_main_id;
        tx.round_type = "MAIN";
        tx.status = "EVALUATED";
        tx.event_type = "ROUND_EVALUATED";
        tx.event_description = (eval.outcome == ROUND_OUTCOME_MAIN_CORRECT)
                                   ? "Main correct"
                                   : "Main wrong / timeout";

        /* Chép danh sách answers */
        for (int a = 0; a < g_active_match.round_answer_count &&
                        a < DB_MAX_ROUND_ANSWERS;
             a++) {
          tx.answers[a].user_id = g_active_match.round_answers[a].user_id;
          snprintf(tx.answers[a].choice, sizeof(tx.answers[a].choice), "%s",
                   g_active_match.round_answers[a].choice);
          tx.answers[a].is_correct =
              g_active_match.round_answers[a].is_correct ? 1 : 0;
          tx.answer_count++;
        }

        /* Chép danh sách score transfers từ deltas */
        for (int d = 0; d < eval.delta_count && d < DB_MAX_SCORE_TRANSFERS;
             d++) {
          if (eval.player_deltas[d].score_delta_units != 0) {
            tx.transfers[tx.transfer_count].from_user_id =
                (eval.player_deltas[d].score_delta_units < 0)
                    ? eval.player_deltas[d].user_id
                    : 0;
            tx.transfers[tx.transfer_count].to_user_id =
                (eval.player_deltas[d].score_delta_units > 0)
                    ? eval.player_deltas[d].user_id
                    : 0;
            tx.transfers[tx.transfer_count].amount_units =
                abs(eval.player_deltas[d].score_delta_units);
            snprintf(tx.transfers[tx.transfer_count].reason,
                     sizeof(tx.transfers[tx.transfer_count].reason), "%s",
                     (eval.outcome == ROUND_OUTCOME_MAIN_CORRECT)
                         ? "MAIN_CORRECT_TRANSFER"
                         : "MAIN_WRONG_TRANSFER");
            tx.transfer_count++;
          }
        }
      }
      unlock_match();

      if (eval_rc == 0) {
        /* Ghi lịch sử SQLite theo atomic transaction */
        lock_db();
        int db_tx_rc = db_round_record_history_tx(g_db, &tx);
        unlock_db();

        if (db_tx_rc == DB_SUCCESS) {
          printf("[DB] Ghi nhận lịch sử lượt %d (Match %ld) vào DB thành công!\n",
                 eval.round_id, (long)tx.match_id);
        } else {
          fprintf(stderr,
                  "[DB ERROR] Ghi nhận lịch sử lượt %d thất bại -> Đã rollback "
                  "DB!\n",
                  eval.round_id);
        }

        /* Phát kết quả tới các client */
        broadcast_round_result_and_scores(&eval);
      }
    }
  }

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

static void broadcast_role_assigned(void) {
  char role_buf[512];
  int sock_list[MAX_MATCH_PLAYERS];
  int count = 0;

  lock_match();
  match_build_role_assigned(&g_active_match, role_buf, sizeof(role_buf));
  count = match_get_active_player_sockets(&g_active_match, false, sock_list,
                                          MAX_MATCH_PLAYERS);
  int64_t main_uid = g_active_match.main_user_id;
  unlock_match();

  size_t len = strlen(role_buf);
  match_broadcast_message_safe(sock_list, count, role_buf, len, 1000, NULL, 0);
  printf("[Game] Đã công bố người chơi chính (User ID: %ld)\n", (long)main_uid);
}

static void broadcast_round_started(void) {
  DbQuestion q;
  memset(&q, 0, sizeof(q));

  lock_db();
  int rc = db_question_get_random(g_db, &q);
  unlock_db();

  if (rc != DB_SUCCESS) {
    q.question_id = 100;
    snprintf(q.content, sizeof(q.content), "%s",
             "Giao thức tầng transport nào hướng kết nối và tin cậy?");
    snprintf(q.options_json, sizeof(q.options_json), "%s",
             "{\"A\":\"UDP\",\"B\":\"TCP\",\"C\":\"IP\",\"D\":\"ICMP\"}");
    snprintf(q.correct_choice, sizeof(q.correct_choice), "%s", "B");
    q.is_active = 1;
  }

  char round_buf[2048];
  int sock_list[MAX_MATCH_PLAYERS];
  int count = 0;

  lock_match();
  g_active_match.current_round++;
  match_setup_round_question(&g_active_match, q.question_id, q.correct_choice);
  int round_id = g_active_match.current_round;
  match_build_round_started(&g_active_match, round_id, q.content,
                            q.options_json, TIMEOUT_SUB_SECONDS, round_buf,
                            sizeof(round_buf));
  count = match_get_active_player_sockets(&g_active_match, false, sock_list,
                                          MAX_MATCH_PLAYERS);
  unlock_match();

  size_t len = strlen(round_buf);
  int sent = match_broadcast_message_safe(sock_list, count, round_buf, len,
                                          1000, NULL, 0);
  printf("[Game] Đã phát ROUND_STARTED lượt %d tới %d/%d client còn trong trận "
         "(QID: %ld)\n",
         round_id, sent, count, (long)q.question_id);
}

static void broadcast_round_result_and_scores(const RoundEvaluationResult *eval) {
  if (!eval)
    return;

  char res_buf[4096];
  char score_buf[4096];
  int sock_list[MAX_MATCH_PLAYERS];
  int count = 0;

  lock_match();
  match_build_round_result(eval, res_buf, sizeof(res_buf));
  match_build_score_updated(&g_active_match, score_buf, sizeof(score_buf));
  /* Gửi ROUND_RESULT và SCORE_UPDATED cho cả người chơi vừa bị ELIMINATED trong lượt */
  count = match_get_active_player_sockets(&g_active_match, true, sock_list,
                                          MAX_MATCH_PLAYERS);
  unlock_match();

  size_t res_len = strlen(res_buf);
  size_t score_len = strlen(score_buf);

  int sent_res = match_broadcast_message_safe(sock_list, count, res_buf,
                                              res_len, 1000, NULL, 0);
  int sent_score = match_broadcast_message_safe(sock_list, count, score_buf,
                                                score_len, 1000, NULL, 0);

  printf("[Game] Đã phát ROUND_RESULT (tới %d/%d) và SCORE_UPDATED (tới %d/%d) "
         "cho lượt %d\n",
         sent_res, count, sent_score, count, eval->round_id);
}

/**
 * Xử lý thông điệp từ client:
 * - REGISTER
 * - LOGIN
 * - LOGOUT
 * - JOIN_LOBBY
 * - Các thông điệp game (yêu cầu session hợp lệ)
 */
static void process_client_message(socket_t sock, const char *raw_msg,
                                   int64_t *thread_user_id,
                                   char *thread_session_id,
                                   const char *client_ip,
                                   uint16_t client_port) {
  char reply_buf[1024];
  reply_buf[0] = '\0';

  AuthMsgType type = parse_message_type(raw_msg);

  if (type == MSG_TYPE_REGISTER) {
    char username[64] = {0};
    char password[128] = {0};

    json_extract_string(raw_msg, "username", username, sizeof(username));
    json_extract_string(raw_msg, "password", password, sizeof(password));

    size_t ulen = strlen(username);
    size_t plen = strlen(password);

    if (ulen < 3 || ulen > 32 || plen < 6) {
      build_register_result(
          false, 0, username, "INVALID_INPUT",
          "Username phai từ 3-32 ký tự, mật khẩu tối thiểu 6 ký tự", reply_buf,
          sizeof(reply_buf));
    } else {
      char hashed_pw[256];
      if (auth_hash_password(password, hashed_pw, sizeof(hashed_pw)) != 0) {
        build_register_result(false, 0, username, "INTERNAL_ERROR",
                              "Lỗi băm mật khẩu máy chủ", reply_buf,
                              sizeof(reply_buf));
      } else {
        int64_t new_uid = 0;
        lock_db();
        int rc = db_user_create(g_db, username, hashed_pw, &new_uid);
        unlock_db();

        if (rc == DB_SUCCESS) {
          printf("[Auth %s:%u] Đăng ký thành công user '%s' (ID: %ld)\n",
                 client_ip, client_port, username, (long)new_uid);
          build_register_result(true, new_uid, username, NULL, NULL, reply_buf,
                                sizeof(reply_buf));
        } else if (rc == DB_ERR_EXISTS) {
          build_register_result(false, 0, username, "USER_EXISTS",
                                "Tên đăng nhập đã tồn tại", reply_buf,
                                sizeof(reply_buf));
        } else {
          build_register_result(false, 0, username, "INTERNAL_ERROR",
                                "Lỗi cơ sở dữ liệu", reply_buf,
                                sizeof(reply_buf));
        }
      }
    }
  } else if (type == MSG_TYPE_LOGIN) {
    char username[64] = {0};
    char password[128] = {0};

    json_extract_string(raw_msg, "username", username, sizeof(username));
    json_extract_string(raw_msg, "password", password, sizeof(password));

    if (strlen(username) == 0 || strlen(password) == 0) {
      build_login_result(false, NULL, 0, username, "INVALID_INPUT",
                         "Vui lòng cung cấp username và password", reply_buf,
                         sizeof(reply_buf));
    } else {
      int64_t uid = 0;
      char stored_hash[256] = {0};

      lock_db();
      int rc = db_user_get_by_username(g_db, username, &uid, stored_hash,
                                       sizeof(stored_hash));
      unlock_db();

      if (rc == DB_SUCCESS && auth_verify_password(password, stored_hash)) {
        char sess_token[64];
        generate_session_token(sess_token, sizeof(sess_token));

        lock_db();
        int s_rc =
            db_session_create(g_db, sess_token, uid, SESSION_EXPIRY_SECONDS);
        unlock_db();

        if (s_rc == DB_SUCCESS) {
          *thread_user_id = uid;
          snprintf(thread_session_id, 64, "%s", sess_token);

          printf("[Auth %s:%u] Đăng nhập thành công: user '%s' (ID: %ld, "
                 "Session: %s)\n",
                 client_ip, client_port, username, (long)uid, sess_token);
          build_login_result(true, sess_token, uid, username, NULL, NULL,
                             reply_buf, sizeof(reply_buf));
        } else {
          build_login_result(false, NULL, 0, username, "INTERNAL_ERROR",
                             "Không thể tạo phiên đăng nhập", reply_buf,
                             sizeof(reply_buf));
        }
      } else {
        build_login_result(false, NULL, 0, username, "AUTH_FAILED",
                           "Tên đăng nhập hoặc mật khẩu không chính xác",
                           reply_buf, sizeof(reply_buf));
      }
    }
  } else if (type == MSG_TYPE_LOGOUT) {
    char token[64] = {0};
    json_extract_string(raw_msg, "session_id", token, sizeof(token));
    if (strlen(token) == 0 && strlen(thread_session_id) > 0) {
      snprintf(token, sizeof(token), "%s", thread_session_id);
    }

    if (strlen(token) > 0) {
      lock_db();
      db_session_invalidate(g_db, token);
      unlock_db();
    }

    if (*thread_user_id > 0) {
      lock_match();
      bool removed = match_remove_player(&g_active_match, *thread_user_id);
      unlock_match();
      if (removed) {
        broadcast_lobby_update();
      }
    }

    *thread_user_id = 0;
    thread_session_id[0] = '\0';

    printf("[Auth %s:%u] Đăng xuất thành công phiên '%s'\n", client_ip,
           client_port, token);
    snprintf(reply_buf, sizeof(reply_buf),
             "{\"type\":\"LOGOUT_RESULT\",\"status\":\"ok\",\"payload\":{"
             "\"message\":\"Đăng xuất thành công\"}}\n");
  } else if (type == MSG_TYPE_JOIN_LOBBY) {
    char token[64] = {0};
    json_extract_string(raw_msg, "session_id", token, sizeof(token));
    if (strlen(token) == 0 && strlen(thread_session_id) > 0) {
      snprintf(token, sizeof(token), "%s", thread_session_id);
    }

    int64_t val_uid = 0;
    char val_user[64] = {0};
    lock_db();
    int s_val = (strlen(token) > 0)
                    ? db_session_validate(g_db, token, &val_uid, val_user,
                                          sizeof(val_user))
                    : DB_ERR_NOTFOUND;
    unlock_db();

    if (s_val != DB_SUCCESS) {
      match_build_join_result(JOIN_LOBBY_ERR_UNAUTHORIZED, 0, 0, reply_buf,
                              sizeof(reply_buf));
    } else {
      *thread_user_id = val_uid;
      snprintf(thread_session_id, 64, "%s", token);

      lock_match();
      JoinLobbyResult res = match_join_lobby(&g_active_match, val_uid, val_user,
                                             token, (int)sock);
      int p_count = g_active_match.player_count;
      int64_t m_id = g_active_match.match_id;
      unlock_match();

      match_build_join_result(res, m_id, p_count, reply_buf, sizeof(reply_buf));

      if (res == JOIN_LOBBY_OK) {
        printf(
            "[Lobby %s:%u] User '%s' (ID: %ld) tham gia phòng chờ (Tổng: %d)\n",
            client_ip, client_port, val_user, (long)val_uid, p_count);
      }
    }
  } else if (type == MSG_TYPE_START_GAME) {
    char token[64] = {0};
    json_extract_string(raw_msg, "session_id", token, sizeof(token));
    if (strlen(token) == 0 && strlen(thread_session_id) > 0) {
      snprintf(token, sizeof(token), "%s", thread_session_id);
    }

    int64_t val_uid = 0;
    char val_user[64] = {0};
    lock_db();
    int s_val = (strlen(token) > 0)
                    ? db_session_validate(g_db, token, &val_uid, val_user,
                                          sizeof(val_user))
                    : DB_ERR_NOTFOUND;
    unlock_db();

    if (s_val != DB_SUCCESS) {
      match_build_start_game_result(START_GAME_ERR_UNAUTHORIZED, reply_buf,
                                    sizeof(reply_buf));
    } else {
      lock_match();
      StartGameResult res = match_start_game(&g_active_match, val_uid);
      int64_t started_mid = g_active_match.match_id;
      unlock_match();

      match_build_start_game_result(res, reply_buf, sizeof(reply_buf));

      if (res == START_GAME_OK) {
        printf("[Game %s:%u] Trận đấu Match %ld đã được bắt đầu bởi Host user "
               "'%s' (ID: %ld)\n",
               client_ip, client_port, (long)started_mid, val_user,
               (long)val_uid);
        broadcast_game_started();
        broadcast_selection_question();
      }
    }
  } else if (type == MSG_TYPE_SELECTION_ANSWER) {
    char token[64] = {0};
    json_extract_string(raw_msg, "session_id", token, sizeof(token));
    if (strlen(token) == 0 && strlen(thread_session_id) > 0) {
      snprintf(token, sizeof(token), "%s", thread_session_id);
    }

    int64_t val_uid = 0;
    char val_user[64] = {0};
    lock_db();
    int s_val = (strlen(token) > 0)
                    ? db_session_validate(g_db, token, &val_uid, val_user,
                                          sizeof(val_user))
                    : DB_ERR_NOTFOUND;
    unlock_db();

    if (s_val != DB_SUCCESS) {
      match_build_selection_answer_result(SELECTION_ANS_ERR_NOT_IN_MATCH,
                                          reply_buf, sizeof(reply_buf));
    } else {
      /* Ghi nhận thời điểm đến Server với độ phân giải cao */
      uint64_t server_ts_ns = match_get_server_time_ns();

      char choice[16] = {0};
      json_extract_string(raw_msg, "choice", choice, sizeof(choice));

      lock_match();
      SelectionAnswerResult res = match_submit_selection_answer(
          &g_active_match, val_uid, choice, server_ts_ns);
      int ans_count = g_active_match.selection_answer_count;
      int active_player_count = g_active_match.player_count;
      unlock_match();

      match_build_selection_answer_result(res, reply_buf, sizeof(reply_buf));

      if (res == SELECTION_ANS_OK) {
        printf("[Game %s:%u] Nhận đáp án '%s' từ user '%s' (ID: %ld, "
               "server_time_ns: %lu, tổng: %d/%d)\n",
               client_ip, client_port, choice, val_user, (long)val_uid,
               (unsigned long)server_ts_ns, ans_count, active_player_count);
      }
    }
  } else if (type == MSG_TYPE_ANSWER_SUBMIT) {
    char token[64] = {0};
    json_extract_string(raw_msg, "session_id", token, sizeof(token));
    if (strlen(token) == 0 && strlen(thread_session_id) > 0) {
      snprintf(token, sizeof(token), "%s", thread_session_id);
    }

    int64_t val_uid = 0;
    char val_user[64] = {0};
    lock_db();
    int s_val = (strlen(token) > 0)
                    ? db_session_validate(g_db, token, &val_uid, val_user,
                                          sizeof(val_user))
                    : DB_ERR_NOTFOUND;
    unlock_db();

    if (s_val != DB_SUCCESS) {
      match_build_answer_ack(ANSWER_ACK_ERR_UNAUTHORIZED, 0, reply_buf,
                             sizeof(reply_buf));
    } else {
      /* Lấy thời điểm hiện tại từ server */
      time_t current_server_time = time(NULL);
      uint64_t server_ts_ns = match_get_server_time_ns();

      /* Trích xuất match_id, round_id, choice từ raw_msg */
      char match_id_str[32] = {0};
      char round_id_str[32] = {0};
      char choice[16] = {0};

      json_extract_string(raw_msg, "match_id", match_id_str,
                          sizeof(match_id_str));
      json_extract_string(raw_msg, "round_id", round_id_str,
                          sizeof(round_id_str));
      json_extract_string(raw_msg, "choice", choice, sizeof(choice));

      int64_t msg_match_id = (strlen(match_id_str) > 0)
                                 ? atoll(match_id_str)
                                 : g_active_match.match_id;
      int msg_round_id = (strlen(round_id_str) > 0)
                             ? atoi(round_id_str)
                             : g_active_match.current_round;

      lock_match();
      AnswerSubmitResult res = match_submit_round_answer_validated(
          &g_active_match, val_uid, msg_match_id, msg_round_id, choice,
          current_server_time, server_ts_ns);
      int round_id_resp = g_active_match.current_round;
      unlock_match();

      match_build_answer_ack(res, round_id_resp, reply_buf, sizeof(reply_buf));

      if (res == ANSWER_ACK_OK) {
        printf("[Game %s:%u] ANSWER_SUBMIT thành công từ user '%s' (ID: %ld, "
               "choice: %s, round: %d)\n",
               client_ip, client_port, val_user, (long)val_uid, choice,
               round_id_resp);
      } else {
        printf("[Game %s:%u] ANSWER_SUBMIT bị từ chối từ user '%s' (ID: %ld, "
               "err: %d)\n",
               client_ip, client_port, val_user, (long)val_uid, res);
      }
    }
  }

  if (strlen(reply_buf) > 0) {
    net_send_all(sock, reply_buf, strlen(reply_buf));
  }
}

#ifdef _WIN32
static unsigned __stdcall handle_client_thread(void *arg)
#else
static void *handle_client_thread(void *arg)
#endif
{
  ClientContext *ctx = (ClientContext *)arg;
  socket_t client_sock = ctx->client_sock;
  struct sockaddr_in client_addr = ctx->client_addr;

  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
  uint16_t client_port = ntohs(client_addr.sin_port);

  int64_t thread_user_id = 0;
  char thread_session_id[64] = {0};

  NetRecvBuffer *rb = (NetRecvBuffer *)malloc(sizeof(NetRecvBuffer));
  char *msg_buf = (char *)malloc(NET_MAX_MSG_LEN + 1);

  if (!rb || !msg_buf) {
    fprintf(stderr, "[Thread %s:%u] Cấp phát bộ nhớ thất bại!\n", client_ip,
            (unsigned)client_port);
    free(rb);
    free(msg_buf);
    CLOSE_SOCKET(client_sock);
    lock_sync();
    g_active_clients--;
    unlock_sync();
    free(ctx);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
  }

  net_recv_buffer_init(rb);

  while (1) {
    NetStatus status =
        net_recv_message(client_sock, rb, msg_buf, NET_MAX_MSG_LEN + 1);

    if (status == NET_STATUS_OK) {
      process_client_message(client_sock, msg_buf, &thread_user_id,
                             thread_session_id, client_ip, client_port);
    } else if (status == NET_STATUS_EOF) {
      printf("[Thread %s:%u] Client đã chủ động ngắt kết nối.\n", client_ip,
             (unsigned)client_port);
      break;
    } else {
      fprintf(stderr, "[Thread %s:%u] Lỗi I/O socket (status=%d).\n", client_ip,
              (unsigned)client_port, status);
      break;
    }
  }

  /* Khi client disconnect, tự động xóa khỏi lobby (nếu đang ở lobby) và vô hiệu
   * hóa session trong DB */
  if (thread_user_id > 0) {
    lock_match();
    bool removed = match_remove_player(&g_active_match, thread_user_id);
    unlock_match();
    if (removed) {
      broadcast_lobby_update();
    }

    lock_db();
    db_session_invalidate_all_for_user(g_db, thread_user_id);
    unlock_db();
    printf(
        "[Thread %s:%u] Đã vô hiệu hóa phiên của user ID %ld khi ngắt kết nối\n",
        client_ip, (unsigned)client_port, (long)thread_user_id);
  }

  free(rb);
  free(msg_buf);
  CLOSE_SOCKET(client_sock);

  lock_sync();
  g_active_clients--;
  int current_count = g_active_clients;
  unlock_sync();

  printf("[Thread] Kết thúc phục vụ %s:%d. Còn lại %d clients đang hoạt động.\n",
         client_ip, client_port, current_count);

  free(ctx);

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

int main(int argc, char *argv[]) {
  const char *bind_ip = DEFAULT_HOST;
  int port = DEFAULT_PORT;
  const char *db_path = DEFAULT_DB_PATH;

  if (argc >= 2) {
    port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
      fprintf(stderr, "Cổng không hợp lệ: %s (chỉ cho phép 1-65535)\n",
              argv[1]);
      return EXIT_FAILURE;
    }
  }
  if (argc >= 3) {
    db_path = argv[2];
  }

  srand((unsigned int)time(NULL));
  init_network();

  /* Khởi tạo cơ sở dữ liệu SQLite */
  if (db_init(db_path, &g_db) != DB_SUCCESS) {
    fprintf(stderr, "Khởi tạo cơ sở dữ liệu %s thất bại!\n", db_path);
    cleanup_network();
    return EXIT_FAILURE;
  }
  printf("Cơ sở dữ liệu SQLite khởi tạo thành công tại: %s\n", db_path);

  /* Khởi tạo phòng chờ trận mặc định (match_id = 1) */
  match_init(&g_active_match, 1);
  printf("Đã khởi tạo phòng chờ cho trận đấu (Match ID: 1)\n");

  /* Khởi chạy luồng timer watcher không busy-wait để tự động chốt lượt khi hết
   * hạn */
#ifdef _WIN32
  uintptr_t timer_handle =
      _beginthreadex(NULL, 0, timer_watcher_thread, NULL, 0, NULL);
  if (timer_handle != 0) {
    CloseHandle((HANDLE)timer_handle);
  }
#else
  pthread_t timer_tid;
  if (pthread_create(&timer_tid, NULL, timer_watcher_thread, NULL) == 0) {
    pthread_detach(timer_tid);
  }
#endif

  socket_t server_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET(server_sock)) {
    perror("Lỗi tạo socket");
    db_close(g_db);
    cleanup_network();
    return EXIT_FAILURE;
  }

  int optval = 1;
#ifdef _WIN32
  setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval,
             sizeof(optval));
#else
  setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, bind_ip, &server_addr.sin_addr) <= 0) {
    fprintf(stderr, "Địa chỉ IP bind không hợp lệ: %s\n", bind_ip);
    CLOSE_SOCKET(server_sock);
    db_close(g_db);
    cleanup_network();
    return EXIT_FAILURE;
  }

  if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("Lỗi bind socket");
    CLOSE_SOCKET(server_sock);
    db_close(g_db);
    cleanup_network();
    return EXIT_FAILURE;
  }

  if (listen(server_sock, BACKLOG_QUEUE) < 0) {
    perror("Lỗi listen");
    CLOSE_SOCKET(server_sock);
    db_close(g_db);
    cleanup_network();
    return EXIT_FAILURE;
  }

  printf("Server đang lắng nghe trên %s:%d ...\n", bind_ip, port);
  printf("Mô hình đồng thời: Multi-threading (tối đa %d clients đồng thời)\n",
         MAX_CONCURRENT_CLIENTS);
  printf("Đang chờ kết nối từ client (nhấn Ctrl+C để dừng)...\n");

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    socket_t client_sock =
        accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
    if (IS_INVALID_SOCKET(client_sock)) {
#ifdef _WIN32
      int err = WSAGetLastError();
      if (err == WSAEINTR)
        continue;
#else
      if (errno == EINTR)
        continue;
#endif
      perror("Lỗi accept kết nối");
      continue;
    }

    lock_sync();
    if (g_active_clients >= MAX_CONCURRENT_CLIENTS) {
      unlock_sync();
      fprintf(
          stderr,
          "Cảnh báo: Đã đạt giới hạn tối đa %d clients! Từ chối kết nối mới.\n",
          MAX_CONCURRENT_CLIENTS);
      CLOSE_SOCKET(client_sock);
      continue;
    }
    g_active_clients++;
    int total = g_active_clients;
    unlock_sync();

    ClientContext *ctx = (ClientContext *)malloc(sizeof(ClientContext));
    if (ctx == NULL) {
      fprintf(stderr, "Lỗi cấp phát bộ nhớ ClientContext!\n");
      CLOSE_SOCKET(client_sock);
      lock_sync();
      g_active_clients--;
      unlock_sync();
      continue;
    }
    ctx->client_sock = client_sock;
    ctx->client_addr = client_addr;

#ifdef _WIN32
    uintptr_t handle =
        _beginthreadex(NULL, 0, handle_client_thread, ctx, 0, NULL);
    if (handle == 0) {
      fprintf(stderr, "Lỗi tạo luồng Windows cho client mới!\n");
      CLOSE_SOCKET(client_sock);
      free(ctx);
      lock_sync();
      g_active_clients--;
      unlock_sync();
      continue;
    }
    CloseHandle((HANDLE)handle);
#else
    pthread_t tid;
    int err = pthread_create(&tid, NULL, handle_client_thread, ctx);
    if (err != 0) {
      fprintf(stderr, "Lỗi pthread_create: %s\n", strerror(err));
      CLOSE_SOCKET(client_sock);
      free(ctx);
      lock_sync();
      g_active_clients--;
      unlock_sync();
      continue;
    }
    pthread_detach(tid);
#endif

    printf("[Server] Đã chấp nhận kết nối mới. Đang phục vụ: %d clients.\n",
           total);
  }

  CLOSE_SOCKET(server_sock);
  db_close(g_db);
  cleanup_network();
  return EXIT_SUCCESS;
}
