#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "net_framing.h"

#ifdef _WIN32
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #define CLOSE_SOCKET(s) close(s)
#endif

/*
 * Test 1: Kiểm thử logic tích lũy bộ đệm, nhiều bản tin trong 1 lần đọc,
 * dữ liệu bị chia nhỏ (partial) và xử lý phần dư bằng net_extract_message.
 */
static void test_extract_buffer_logic(void)
{
    printf("[TEST] Testing buffer accumulation, partial chunks, and multiple messages...\n");

    NetRecvBuffer rb;
    net_recv_buffer_init(&rb);
    char msg[1024];

    /* 1.1 Khi buffer rỗng */
    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_MORE);

    /* 1.2 Dữ liệu bị chia nhỏ (partial read): mới nhận một nửa dòng */
    const char *chunk1 = "{\"action\":\"login\"";
    memcpy(rb.buf, chunk1, strlen(chunk1));
    rb.len = strlen(chunk1);
    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_MORE);

    /* 1.3 Nhận tiếp nửa còn lại kèm '\n' */
    const char *chunk2 = ",\"username\":\"alice\"}\n";
    memcpy(rb.buf + rb.len, chunk2, strlen(chunk2));
    rb.len += strlen(chunk2);

    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_OK);
    assert(strcmp(msg, "{\"action\":\"login\",\"username\":\"alice\"}") == 0);
    assert(rb.len == 0); /* Buffer đã được dọn sạch */

    /* 1.4 Nhiều thông điệp trong cùng một lần đọc (ví dụ 3 message liên tiếp) */
    const char *multi = "{\"msg\":1}\n{\"msg\":2}\r\n{\"msg\":3}\n";
    memcpy(rb.buf, multi, strlen(multi));
    rb.len = strlen(multi);

    /* Trích xuất bản tin 1 */
    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_OK);
    assert(strcmp(msg, "{\"msg\":1}") == 0);

    /* Trích xuất bản tin 2 (có chứa \r\n, kiểm tra tự động lược bỏ \r) */
    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_OK);
    assert(strcmp(msg, "{\"msg\":2}") == 0);

    /* Trích xuất bản tin 3 */
    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_OK);
    assert(strcmp(msg, "{\"msg\":3}") == 0);

    /* Bây giờ buffer rỗng */
    assert(rb.len == 0);
    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_MORE);

    /* 1.5 Nhiều thông điệp + 1 phần dư dở dang */
    const char *multi_with_tail = "{\"msg\":4}\n{\"incomplete\":";
    memcpy(rb.buf, multi_with_tail, strlen(multi_with_tail));
    rb.len = strlen(multi_with_tail);

    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_OK);
    assert(strcmp(msg, "{\"msg\":4}") == 0);

    /* Kiểm tra phần dư vẫn còn trong buffer */
    assert(net_extract_message(&rb, msg, sizeof(msg)) == NET_STATUS_MORE);
    assert(rb.len == strlen("{\"incomplete\":"));
    assert(memcmp(rb.buf, "{\"incomplete\":", rb.len) == 0);

    printf("[PASS] Buffer accumulation & multi-message extraction passed.\n");
}

/*
 * Test 2: Kiểm thử thông điệp quá dài vượt quá giới hạn (overflow)
 */
static void test_message_overflow(void)
{
    printf("[TEST] Testing message overflow protection...\n");

    NetRecvBuffer rb;
    net_recv_buffer_init(&rb);
    char small_out[32];

    /* Dòng dài hơn dung lượng output buffer small_out */
    const char *long_msg = "{\"this_is_a_very_long_message_that_exceeds_output_buffer\":true}\n";
    memcpy(rb.buf, long_msg, strlen(long_msg));
    rb.len = strlen(long_msg);

    NetStatus status = net_extract_message(&rb, small_out, sizeof(small_out));
    assert(status == NET_STATUS_ERR_OVERFLOW);
    /* Sau khi phát hiện dòng tràn, dòng đó bị loại bỏ để buffer không kẹt */
    assert(rb.len == 0);

    /* Test trường hợp buffer tích lũy dữ liệu vượt quá NET_MAX_MSG_LEN mà không có '\n' */
    rb.len = NET_MAX_MSG_LEN + 1;
    memset(rb.buf, 'A', rb.len);
    status = net_extract_message(&rb, small_out, sizeof(small_out));
    assert(status == NET_STATUS_ERR_OVERFLOW);

    printf("[PASS] Message overflow protection passed.\n");
}

/*
 * Test 3: Kết nối TCP thật (loopback), kiểm tra net_recv_message
 * qua mạng với stream nhiều gói, partial write/read và đóng kết nối (EOF).
 */
static void test_loopback_stream_recv(void)
{
    printf("[TEST] Testing end-to-end TCP loopback receiving and EOF handling...\n");

    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd != (socket_t)-1 && listen_fd != 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = 0;

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    int res = bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    assert(res == 0);

    res = listen(listen_fd, 1);
    assert(res == 0);

#ifdef _WIN32
    int addr_len = sizeof(serv_addr);
#else
    socklen_t addr_len = sizeof(serv_addr);
#endif
    res = getsockname(listen_fd, (struct sockaddr *)&serv_addr, &addr_len);
    assert(res == 0);

    socket_t client_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(client_fd != (socket_t)-1 && client_fd != 0);

    res = connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    assert(res == 0);

    socket_t server_client_fd = accept(listen_fd, NULL, NULL);
    assert(server_client_fd != (socket_t)-1 && server_client_fd != 0);

    /* Client gửi 3 thông điệp qua mạng TCP với các kích cỡ và phân cách khác nhau */
    const char *payload = "{\"id\":1}\n{\"id\":2}\r\n{\"id\":3}\n";
    NetStatus send_st = net_send_all(client_fd, payload, strlen(payload));
    assert(send_st == NET_STATUS_OK);

    NetRecvBuffer rb;
    net_recv_buffer_init(&rb);
    char out[256];

    /* Nhận message 1 */
    assert(net_recv_message(server_client_fd, &rb, out, sizeof(out)) == NET_STATUS_OK);
    assert(strcmp(out, "{\"id\":1}") == 0);

    /* Nhận message 2 */
    assert(net_recv_message(server_client_fd, &rb, out, sizeof(out)) == NET_STATUS_OK);
    assert(strcmp(out, "{\"id\":2}") == 0);

    /* Nhận message 3 */
    assert(net_recv_message(server_client_fd, &rb, out, sizeof(out)) == NET_STATUS_OK);
    assert(strcmp(out, "{\"id\":3}") == 0);

    /* Bây giờ client đóng kết nối (EOF) */
    CLOSE_SOCKET(client_fd);

    /* Gọi net_recv_message tiếp tục phải trả về NET_STATUS_EOF */
    NetStatus eof_st = net_recv_message(server_client_fd, &rb, out, sizeof(out));
    assert(eof_st == NET_STATUS_EOF);

    CLOSE_SOCKET(server_client_fd);
    CLOSE_SOCKET(listen_fd);

    printf("[PASS] End-to-end TCP loopback receiving & EOF passed.\n");
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

    test_extract_buffer_logic();
    test_message_overflow();
    test_loopback_stream_recv();

#ifdef _WIN32
    WSACleanup();
#endif

    printf("All net_recv_message tests passed successfully!\n");
    return 0;
}

