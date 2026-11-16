#include "net_framing.h"

#include <string.h>
#include <limits.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <errno.h>
    #include <sys/types.h>
    #include <sys/socket.h>
#endif

void net_recv_buffer_init(NetRecvBuffer *rb)
{
    if (rb != NULL) {
        rb->len = 0;
        rb->buf[0] = '\0';
    }
}

NetStatus net_send_all(socket_t sockfd, const void *buf, size_t total_bytes)
{
    if (buf == NULL) {
        return NET_STATUS_ERR_INVALID;
    }

#ifdef _WIN32
    if (sockfd == INVALID_SOCKET) {
        return NET_STATUS_ERR_INVALID;
    }
#else
    if (sockfd < 0) {
        return NET_STATUS_ERR_INVALID;
    }
#endif

    const char *ptr = (const char *)buf;
    size_t total_sent = 0;

    while (total_sent < total_bytes) {
        size_t bytes_left = total_bytes - total_sent;

#ifdef _WIN32
        int send_len = (bytes_left > (size_t)INT_MAX) ? INT_MAX : (int)bytes_left;
        int bytes = send(sockfd, ptr + total_sent, send_len, 0);

        if (bytes == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
            return NET_STATUS_ERR_SOCKET;
        }
#else
        #ifndef MSG_NOSIGNAL
            #define MSG_NOSIGNAL 0
        #endif
        ssize_t bytes = send(sockfd, ptr + total_sent, bytes_left, MSG_NOSIGNAL);

        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return NET_STATUS_ERR_SOCKET;
        }
#endif

        if (bytes == 0) {
            return NET_STATUS_EOF;
        }

        total_sent += (size_t)bytes;
    }

    return NET_STATUS_OK;
}

NetStatus net_read_to_buffer(socket_t sockfd, NetRecvBuffer *rb)
{
    if (rb == NULL) {
        return NET_STATUS_ERR_INVALID;
    }

#ifdef _WIN32
    if (sockfd == INVALID_SOCKET) {
        return NET_STATUS_ERR_INVALID;
    }
#else
    if (sockfd < 0) {
        return NET_STATUS_ERR_INVALID;
    }
#endif

    if (rb->len >= NET_BUF_CAPACITY) {
        return NET_STATUS_ERR_OVERFLOW;
    }

    size_t space_left = NET_BUF_CAPACITY - rb->len;

#ifdef _WIN32
    int recv_len = (space_left > (size_t)INT_MAX) ? INT_MAX : (int)space_left;
    int bytes = recv(sockfd, rb->buf + rb->len, recv_len, 0);

    if (bytes == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEINTR) {
            return NET_STATUS_MORE; /* Ngắt tín hiệu, lần sau gọi lại */
        }
        return NET_STATUS_ERR_SOCKET;
    }
#else
    ssize_t bytes = recv(sockfd, rb->buf + rb->len, space_left, 0);

    if (bytes < 0) {
        if (errno == EINTR) {
            return NET_STATUS_MORE;
        }
        return NET_STATUS_ERR_SOCKET;
    }
#endif

    if (bytes == 0) {
        return NET_STATUS_EOF;
    }

    rb->len += (size_t)bytes;
    return NET_STATUS_OK;
}

NetStatus net_extract_message(NetRecvBuffer *rb, char *out_msg, size_t out_cap)
{
    if (rb == NULL || out_msg == NULL || out_cap == 0) {
        return NET_STATUS_ERR_INVALID;
    }

    /* Tìm ký tự '\n' trong dữ liệu hiện có */
    const char *newline_pos = (const char *)memchr(rb->buf, '\n', rb->len);

    if (newline_pos == NULL) {
        /* Chưa có '\n' */
        if (rb->len >= NET_MAX_MSG_LEN || rb->len >= NET_BUF_CAPACITY) {
            /* Dữ liệu đã vượt quá độ dài tối đa cho phép mà vẫn không có kết thúc dòng */
            return NET_STATUS_ERR_OVERFLOW;
        }
        return NET_STATUS_MORE;
    }

    size_t line_len = (size_t)(newline_pos - rb->buf); /* Không bao gồm '\n' */

    /* Kiểm tra giới hạn kích thước */
    if (line_len >= out_cap || line_len >= NET_MAX_MSG_LEN) {
        /* Bỏ qua dòng bị lỗi tràn này để tránh kẹt buffer */
        size_t total_consumed = line_len + 1;
        size_t remaining = rb->len - total_consumed;
        if (remaining > 0) {
            memmove(rb->buf, newline_pos + 1, remaining);
        }
        rb->len = remaining;
        return NET_STATUS_ERR_OVERFLOW;
    }

    /* Sao chép chuỗi ra out_msg */
    memcpy(out_msg, rb->buf, line_len);

    /* Loại bỏ '\r' nếu có (hỗ trợ cả kết thúc dòng Windows "\r\n") */
    size_t actual_len = line_len;
    if (actual_len > 0 && out_msg[actual_len - 1] == '\r') {
        actual_len--;
    }
    out_msg[actual_len] = '\0';

    /* Dồn phần dữ liệu còn lại trong buffer lên đầu */
    size_t total_consumed = line_len + 1;
    size_t remaining = rb->len - total_consumed;
    if (remaining > 0) {
        memmove(rb->buf, newline_pos + 1, remaining);
    }
    rb->len = remaining;

    return NET_STATUS_OK;
}

NetStatus net_recv_message(socket_t sockfd, NetRecvBuffer *rb, char *out_msg, size_t out_cap)
{
    if (rb == NULL || out_msg == NULL || out_cap == 0) {
        return NET_STATUS_ERR_INVALID;
    }

    while (1) {
        /* Thử trích xuất nếu trong buffer đã có sẵn ít nhất 1 dòng hoàn chỉnh */
        NetStatus extract_status = net_extract_message(rb, out_msg, out_cap);
        if (extract_status == NET_STATUS_OK) {
            return NET_STATUS_OK;
        }
        if (extract_status != NET_STATUS_MORE) {
            return extract_status; /* Gặp lỗi (ví dụ NET_STATUS_ERR_OVERFLOW) */
        }

        /* Chưa đủ dòng, gọi read_to_buffer từ socket */
        NetStatus read_status = net_read_to_buffer(sockfd, rb);
        if (read_status == NET_STATUS_EOF) {
            /* Peer đóng kết nối. Nếu trong buffer còn dữ liệu dở dang mà không có '\n',
               không tạo thành JSON-line hợp lệ */
            return NET_STATUS_EOF;
        }
        if (read_status == NET_STATUS_ERR_SOCKET || read_status == NET_STATUS_ERR_OVERFLOW) {
            return read_status;
        }
        /* Nếu NET_STATUS_OK hoặc NET_STATUS_MORE (EINTR), lặp tiếp */
    }
}
