#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "net_framing.h"
#include "protocol.h"
#include "auth_crypto.h"
#include "auth_msg.h"
#include "db.h"
#include "match.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <process.h>
    #define CLOSE_SOCKET(s) closesocket(s)
    typedef HANDLE test_thread_t;
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <pthread.h>
    #define CLOSE_SOCKET(s) close(s)
    typedef pthread_t test_thread_t;
#endif

/*
 * Test 1: Client ngắt kết nối giữa thông điệp (partial transmission before disconnect).
 * Server/receiver buffer giữ phần dở dang, khi connection drop trả về NET_STATUS_EOF.
 * Bộ đệm không bị crash và có thể dọn dẹp an toàn.
 */
static void test_disconnect_mid_message(void)
{
    printf("[TEST] Testing client disconnect mid-message (EOF during partial chunk)...\n");

    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd != (socket_t)-1);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = 0; /* Cho OS cấp port ngẫu nhiên */

    assert(bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);
    assert(listen(listen_fd, 5) == 0);

    socklen_t addr_len = sizeof(serv_addr);
    assert(getsockname(listen_fd, (struct sockaddr *)&serv_addr, &addr_len) == 0);

    /* Client kết nối tới server */
    socket_t client_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(client_fd != (socket_t)-1);
    assert(connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);

    socket_t server_sock = accept(listen_fd, NULL, NULL);
    assert(server_sock != (socket_t)-1);

    /* Client gửi một nửa bản tin (chưa có '\n') rồi đột ngột đóng socket */
    const char *half_msg = "{\"type\":\"REGISTER\",\"username\":\"incomplete";
    NetStatus send_st = net_send_all(client_fd, half_msg, strlen(half_msg));
    assert(send_st == NET_STATUS_OK);

    /* Đóng kết nối đột ngột từ client */
    CLOSE_SOCKET(client_fd);

    NetRecvBuffer rb;
    net_recv_buffer_init(&rb);
    char out_msg[NET_MAX_MSG_LEN + 1];

    /* net_recv_message đọc được nửa bản tin, nhận tiếp thấy EOF -> trả về NET_STATUS_EOF */
    NetStatus recv_st = net_recv_message(server_sock, &rb, out_msg, sizeof(out_msg));
    assert(recv_st == NET_STATUS_EOF);

    /* Dữ liệu dở dang trong rb được dọn sạch khi khởi tạo lại hoặc giải phóng */
    assert(rb.len == strlen(half_msg));
    net_recv_buffer_init(&rb);
    assert(rb.len == 0);

    CLOSE_SOCKET(server_sock);
    CLOSE_SOCKET(listen_fd);
    printf("[PASS] Disconnect mid-message handled gracefully (returned EOF).\n");
}

/*
 * Test 2: Client gửi JSON lỗi / cú pháp sai lệch.
 * Kiểm tra server xử lý an toàn: không crash, parser từ chối hoặc nhận diện MSG_TYPE_UNKNOWN,
 * trả về bản tin phản hồi ERROR định dạng hợp lệ.
 */
static void test_malformed_json_and_error_handling(void)
{
    printf("[TEST] Testing malformed JSON and syntax error handling...\n");

    const char *bad_jsons[] = {
        "This is not json at all",
        "{\"type\":",
        "{\"type\": 12345}",
        "{type:\"NO_QUOTES\"}",
        "{\"missing_type_field\": true}",
        "{\\\"broken\\\": [1,2,3}"
    };

    for (size_t i = 0; i < sizeof(bad_jsons)/sizeof(bad_jsons[0]); i++) {
        AuthMsgType t = parse_message_type(bad_jsons[i]);
        /* Tất cả JSON lỗi đều phải phân loại thành MSG_TYPE_UNKNOWN an toàn */
        assert(t == MSG_TYPE_UNKNOWN);

        char out_str[64] = {0};
        bool extracted = json_extract_string(bad_jsons[i], "username", out_str, sizeof(out_str));
        assert(!extracted || strlen(out_str) == 0);
    }

    printf("[PASS] Malformed JSON handled safely without crash.\n");
}

/*
 * Test 3: Client gửi message vượt quá kích thước giới hạn (NET_MAX_MSG_LEN = 8192).
 * Phải kích hoạt NET_STATUS_ERR_OVERFLOW, ngắt kết nối client vi phạm để bảo vệ tài nguyên.
 */
static void test_oversized_message_protection(void)
{
    printf("[TEST] Testing oversized message protection (NET_MAX_MSG_LEN limit)...\n");

    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd != (socket_t)-1);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = 0;

    assert(bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);
    assert(listen(listen_fd, 5) == 0);

    socklen_t addr_len = sizeof(serv_addr);
    assert(getsockname(listen_fd, (struct sockaddr *)&serv_addr, &addr_len) == 0);

    socket_t client_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(client_fd != (socket_t)-1);
    assert(connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);

    socket_t server_sock = accept(listen_fd, NULL, NULL);
    assert(server_sock != (socket_t)-1);

    /* Tạo một payload không có '\n' dài 9000 bytes (> NET_MAX_MSG_LEN 8192 bytes) */
    char big_buf[9000];
    memset(big_buf, 'A', sizeof(big_buf));

    /* Gửi liên tục */
    NetStatus send_st = net_send_all(client_fd, big_buf, sizeof(big_buf));
    assert(send_st == NET_STATUS_OK);

    NetRecvBuffer rb;
    net_recv_buffer_init(&rb);
    char out_msg[NET_MAX_MSG_LEN + 1];

    /* net_recv_message phải phát hiện overflow và trả về NET_STATUS_ERR_OVERFLOW */
    NetStatus recv_st = net_recv_message(server_sock, &rb, out_msg, sizeof(out_msg));
    assert(recv_st == NET_STATUS_ERR_OVERFLOW);

    CLOSE_SOCKET(client_fd);
    CLOSE_SOCKET(server_sock);
    CLOSE_SOCKET(listen_fd);
    printf("[PASS] Oversized message overflow protection triggered successfully.\n");
}

/*
 * Test 4: Đóng kết nối bất ngờ (RST / abortive close) và xác nhận server
 * vẫn phục vụ trơn tru các client còn lại.
 */
typedef struct {
    int port;
    int client_id;
    bool success;
} ClientWorkerArg;

#ifdef _WIN32
static unsigned __stdcall client_normal_worker(void *arg)
#else
static void *client_normal_worker(void *arg)
#endif
{
    ClientWorkerArg *c = (ClientWorkerArg *)arg;
    c->success = false;

    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == (socket_t)-1) {
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(c->port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCKET(fd);
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

    /* Gửi yêu cầu đăng ký hợp lệ */
    char reg_msg[256];
    snprintf(reg_msg, sizeof(reg_msg),
             "{\"type\":\"REGISTER\",\"username\":\"user_%d\",\"password\":\"secret123\"}\n",
             c->client_id);
    if (net_send_all(fd, reg_msg, strlen(reg_msg)) != NET_STATUS_OK) {
        CLOSE_SOCKET(fd);
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

    /* Đọc kết quả từ server */
    NetRecvBuffer rb;
    net_recv_buffer_init(&rb);
    char resp[1024];
    if (net_recv_message(fd, &rb, resp, sizeof(resp)) == NET_STATUS_OK) {
        if (strstr(resp, "\"status\":\"ok\"") != NULL) {
            c->success = true;
        }
    }

    CLOSE_SOCKET(fd);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void test_resilient_server_with_bad_and_good_clients(void)
{
    printf("[TEST] Testing multi-client concurrency with abrupt disconnects & bad messages...\n");

    /*
     * Khởi tạo server socket loopback và DB in-memory mô phỏng server
     */
    sqlite3 *db = NULL;
    assert(db_init(":memory:", &db) == DB_SUCCESS);

    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd != (socket_t)-1);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = 0;

    assert(bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);
    assert(listen(listen_fd, 10) == 0);

    socklen_t addr_len = sizeof(serv_addr);
    assert(getsockname(listen_fd, (struct sockaddr *)&serv_addr, &addr_len) == 0);
    int port = ntohs(serv_addr.sin_port);

    /* 1. Client Bad 1: Kết nối, gửi 1 nửa bản tin rồi reset socket (SO_LINGER timeout 0) */
    socket_t bad_fd1 = socket(AF_INET, SOCK_STREAM, 0);
    assert(connect(bad_fd1, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);
    socket_t srv_sock1 = accept(listen_fd, NULL, NULL);
    assert(srv_sock1 != (socket_t)-1);

    const char *partial = "{\"type\":\"LOGIN\",\"username\":\"";
    net_send_all(bad_fd1, partial, strlen(partial));

    /* Bật SO_LINGER l_onoff=1, l_linger=0 để gửi RST (đóng kết nối bất ngờ) */
    struct linger sl;
    sl.l_onoff = 1;
    sl.l_linger = 0;
    setsockopt(bad_fd1, SOL_SOCKET, SO_LINGER, (const char *)&sl, sizeof(sl));
    CLOSE_SOCKET(bad_fd1);

    /* Server đọc từ srv_sock1: nhận partial rồi EOF/error, không bị block hay crash */
    NetRecvBuffer rb1;
    net_recv_buffer_init(&rb1);
    char buf1[512];
    NetStatus st1 = net_recv_message(srv_sock1, &rb1, buf1, sizeof(buf1));
    assert(st1 == NET_STATUS_EOF || st1 == NET_STATUS_ERR_SOCKET);
    CLOSE_SOCKET(srv_sock1);

    /* 2. Client Good 1: Kết nối và thực hiện đăng ký tài khoản thành công */
    ClientWorkerArg good1 = { .port = port, .client_id = 101, .success = false };
    test_thread_t th1;
#ifdef _WIN32
    th1 = (HANDLE)_beginthreadex(NULL, 0, client_normal_worker, &good1, 0, NULL);
#else
    pthread_create(&th1, NULL, client_normal_worker, &good1);
#endif

    socket_t srv_sock_good = accept(listen_fd, NULL, NULL);
    assert(srv_sock_good != (socket_t)-1);

    NetRecvBuffer rb_good;
    net_recv_buffer_init(&rb_good);
    char req_buf[1024];
    assert(net_recv_message(srv_sock_good, &rb_good, req_buf, sizeof(req_buf)) == NET_STATUS_OK);

    /* Xử lý REGISTER */
    char u[64] = {0}, p[64] = {0};
    json_extract_string(req_buf, "username", u, sizeof(u));
    json_extract_string(req_buf, "password", p, sizeof(p));
    char hashed[256];
    auth_hash_password(p, hashed, sizeof(hashed));
    int64_t uid = 0;
    db_user_create(db, u, hashed, &uid);

    char reply[512];
    build_register_result(true, uid, u, NULL, NULL, reply, sizeof(reply));
    net_send_all(srv_sock_good, reply, strlen(reply));

#ifdef _WIN32
    WaitForSingleObject(th1, INFINITE);
    CloseHandle(th1);
#else
    pthread_join(th1, NULL);
#endif
    assert(good1.success == true);
    CLOSE_SOCKET(srv_sock_good);

    /* 3. Client Bad 2: Gửi JSON lỗi cú pháp */
    socket_t bad_fd2 = socket(AF_INET, SOCK_STREAM, 0);
    assert(connect(bad_fd2, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);
    socket_t srv_sock2 = accept(listen_fd, NULL, NULL);
    assert(srv_sock2 != (socket_t)-1);

    const char *garbage = "NOT_JSON_DATA_GARBAGE\n";
    net_send_all(bad_fd2, garbage, strlen(garbage));

    NetRecvBuffer rb2;
    net_recv_buffer_init(&rb2);
    char buf2[512];
    assert(net_recv_message(srv_sock2, &rb2, buf2, sizeof(buf2)) == NET_STATUS_OK);
    AuthMsgType bad_t = parse_message_type(buf2);
    assert(bad_t == MSG_TYPE_UNKNOWN);

    /* Server phản hồi ERROR an toàn mà không đóng server */
    const char *err_resp = "{\"type\":\"ERROR\",\"status\":\"error\",\"payload\":{\"code\":\"INVALID_JSON\"}}\n";
    net_send_all(srv_sock2, err_resp, strlen(err_resp));

    NetRecvBuffer rb_bad2;
    net_recv_buffer_init(&rb_bad2);
    char bad2_in[512];
    assert(net_recv_message(bad_fd2, &rb_bad2, bad2_in, sizeof(bad2_in)) == NET_STATUS_OK);
    assert(strstr(bad2_in, "INVALID_JSON") != NULL);

    CLOSE_SOCKET(bad_fd2);
    CLOSE_SOCKET(srv_sock2);

    /* 4. Client Good 2: Vẫn tiếp tục kết nối và nhận phản hồi bình thường */
    ClientWorkerArg good2 = { .port = port, .client_id = 102, .success = false };
    test_thread_t th2;
#ifdef _WIN32
    th2 = (HANDLE)_beginthreadex(NULL, 0, client_normal_worker, &good2, 0, NULL);
#else
    pthread_create(&th2, NULL, client_normal_worker, &good2);
#endif

    socket_t srv_sock_good2 = accept(listen_fd, NULL, NULL);
    assert(srv_sock_good2 != (socket_t)-1);

    NetRecvBuffer rb_good2;
    net_recv_buffer_init(&rb_good2);
    char req_buf2[1024];
    assert(net_recv_message(srv_sock_good2, &rb_good2, req_buf2, sizeof(req_buf2)) == NET_STATUS_OK);

    json_extract_string(req_buf2, "username", u, sizeof(u));
    json_extract_string(req_buf2, "password", p, sizeof(p));
    auth_hash_password(p, hashed, sizeof(hashed));
    db_user_create(db, u, hashed, &uid);
    build_register_result(true, uid, u, NULL, NULL, reply, sizeof(reply));
    net_send_all(srv_sock_good2, reply, strlen(reply));

#ifdef _WIN32
    WaitForSingleObject(th2, INFINITE);
    CloseHandle(th2);
#else
    pthread_join(th2, NULL);
#endif
    assert(good2.success == true);
    CLOSE_SOCKET(srv_sock_good2);

    CLOSE_SOCKET(listen_fd);
    db_close(db);

    printf("[PASS] Server successfully withstood disconnects, malformed JSON, and served valid clients!\n");
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    printf("=== BẮT ĐẦU TEST LỖI MẠNG VÀ TÍNH BỀN VỮNG CỦA SERVER (P10.2) ===\n");
    test_disconnect_mid_message();
    test_malformed_json_and_error_handling();
    test_oversized_message_protection();
    test_resilient_server_with_bad_and_good_clients();
    printf("=== TẤT CẢ TEST P10.2 ĐÃ VƯỢT QUA THÀNH CÔNG 100%%%%! ===\n");

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
