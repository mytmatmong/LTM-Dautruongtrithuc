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

static void test_invalid_parameters(void)
{
    printf("[TEST] Testing net_send_all with invalid parameters...\n");
    assert(net_send_all((socket_t)-1, NULL, 10) == NET_STATUS_ERR_INVALID);
    char dummy[10] = "test";
#ifdef _WIN32
    assert(net_send_all(INVALID_SOCKET, dummy, sizeof(dummy)) == NET_STATUS_ERR_INVALID);
#else
    assert(net_send_all(-1, dummy, sizeof(dummy)) == NET_STATUS_ERR_INVALID);
#endif
    printf("[PASS] Invalid parameter checks passed.\n");
}

static void test_loopback_send_receive(void)
{
    printf("[TEST] Testing loopback TCP send and receive with net_send_all...\n");

    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd != (socket_t)-1 && listen_fd != 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = 0; /* Cho OS cấp phát port bất kỳ */

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

    /* Tạo client socket và kết nối tới server */
    socket_t client_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(client_fd != (socket_t)-1 && client_fd != 0);

    res = connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    assert(res == 0);

    socket_t server_client_fd = accept(listen_fd, NULL, NULL);
    assert(server_client_fd != (socket_t)-1 && server_client_fd != 0);

    /* Chuẩn bị dữ liệu gửi (64 KB) để đảm bảo trải qua nhiều vòng send/recv */
    const size_t test_size = 64 * 1024;
    char *send_buf = (char *)malloc(test_size);
    char *recv_buf = (char *)malloc(test_size);
    assert(send_buf != NULL && recv_buf != NULL);

    for (size_t i = 0; i < test_size; i++) {
        send_buf[i] = (char)((i % 251) + 1); /* Dữ liệu mẫu */
    }

    /* Gửi toàn bộ dữ liệu qua net_send_all */
    NetStatus send_status = net_send_all(client_fd, send_buf, test_size);
    assert(send_status == NET_STATUS_OK);

    /* Nhận toàn bộ dữ liệu ở server */
    size_t total_received = 0;
    while (total_received < test_size) {
        int bytes = recv(server_client_fd, recv_buf + total_received, (int)(test_size - total_received), 0);
        assert(bytes > 0);
        total_received += (size_t)bytes;
    }

    assert(total_received == test_size);
    assert(memcmp(send_buf, recv_buf, test_size) == 0);

    free(send_buf);
    free(recv_buf);

    CLOSE_SOCKET(server_client_fd);
    CLOSE_SOCKET(client_fd);
    CLOSE_SOCKET(listen_fd);

    printf("[PASS] Loopback TCP 64KB net_send_all passed successfully.\n");
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

    test_invalid_parameters();
    test_loopback_send_receive();

#ifdef _WIN32
    WSACleanup();
#endif

    printf("All net_send_all tests passed!\n");
    return 0;
}
