#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #define _POSIX_C_SOURCE 200809L
    #include <unistd.h>
    #include <errno.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #define CLOSE_SOCKET(s) close(s)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080
#define BUFFER_SIZE 512

static void init_network(void)
{
#ifdef _WIN32
    WSADATA wsa_data;
    int res = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (res != 0) {
        fprintf(stderr, "Lỗi WSAStartup thất bại: %d\n", res);
        exit(EXIT_FAILURE);
    }
#endif
}

static void cleanup_network(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

int main(int argc, char *argv[])
{
    const char *server_ip = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Cổng không hợp lệ: %s (chỉ cho phép 1-65535)\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    init_network();

    int sock = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Lỗi tạo socket client");
        cleanup_network();
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Địa chỉ IP server không hợp lệ: %s\n", server_ip);
        CLOSE_SOCKET(sock);
        cleanup_network();
        return EXIT_FAILURE;
    }

    printf("Đang kết nối tới server %s:%d ...\n", server_ip, port);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Lỗi kết nối tới server");
        CLOSE_SOCKET(sock);
        cleanup_network();
        return EXIT_FAILURE;
    }

    printf("Kết nối tới server %s:%d thành công!\n", server_ip, port);

    /*
     * P2.3: Gửi payload thử nghiệm nhỏ và nhận phản hồi từ server.
     * GIỚI HẠN HIỆN TẠI:
     * - Chỉ là một lần send/recv cơ bản với payload nhỏ để kiểm chứng đường truyền 2 chiều.
     * - Chưa có cơ chế gửi lặp (partial send handling) hay đọc theo newline (framing).
     */
    const char *msg = "PING\n";
    int msg_len = (int)strlen(msg);
    printf("Client gửi dữ liệu thử nghiệm: \"%s\"\n", msg);
    int bytes_sent = send(sock, msg, msg_len, 0);
    if (bytes_sent < 0) {
        perror("Lỗi gửi dữ liệu tới server");
    } else {
        char buf[BUFFER_SIZE];
        int bytes_received = recv(sock, buf, sizeof(buf) - 1, 0);
        if (bytes_received < 0) {
            perror("Lỗi nhận dữ liệu từ server");
        } else if (bytes_received == 0) {
            printf("Server đã đóng kết nối trước khi gửi phản hồi.\n");
        } else {
            buf[bytes_received] = '\0';
            printf("Client đã nhận phản hồi (%d bytes): \"%s\"\n", bytes_received, buf);
        }
    }

    printf("Đóng socket client và kết thúc...\n");
    CLOSE_SOCKET(sock);
    cleanup_network();

    return EXIT_SUCCESS;
}
