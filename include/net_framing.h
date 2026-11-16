#ifndef NET_FRAMING_H
#define NET_FRAMING_H

#include <stddef.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    typedef int socket_t;
#endif

#define NET_MAX_MSG_LEN   8192    /* Kích thước tối đa cho 1 thông điệp JSON (8KB) */
#define NET_BUF_CAPACITY  16384   /* Kích thước buffer tích lũy (16KB) */

typedef enum {
    NET_STATUS_OK           =  0, /* Thao tác thành công, trả về 1 message hoàn chỉnh hoặc gửi đủ */
    NET_STATUS_MORE         =  1, /* Chưa đủ newline '\n', cần nhận thêm dữ liệu */
    NET_STATUS_EOF          = -1, /* Kết nối bị đóng từ phía đối tác (recv trả về 0) */
    NET_STATUS_ERR_SOCKET   = -2, /* Lỗi socket I/O (gặp lỗi khác EINTR) */
    NET_STATUS_ERR_OVERFLOW = -3, /* Thông điệp vượt quá kích thước giới hạn (NET_MAX_MSG_LEN / out_cap) */
    NET_STATUS_ERR_INVALID  = -4  /* Tham số đầu vào không hợp lệ */
} NetStatus;

/**
 * Cấu trúc buffer nhận tích lũy cho mỗi socket connection
 */
typedef struct {
    char buf[NET_BUF_CAPACITY];
    size_t len;
} NetRecvBuffer;

/**
 * Khởi tạo hoặc xóa trắng bộ đệm nhận.
 */
void net_recv_buffer_init(NetRecvBuffer *rb);

/**
 * Gửi toàn bộ buffer qua TCP socket (send loop).
 *
 * @param sockfd Socket kết nối TCP
 * @param buf Vùng nhớ chứa dữ liệu cần gửi
 * @param total_bytes Tổng số byte cần gửi
 * @return NET_STATUS_OK nếu gửi đủ tất cả bytes
 *         NET_STATUS_EOF nếu socket trả về 0 (kết nối đóng)
 *         NET_STATUS_ERR_SOCKET nếu gặp lỗi socket khác
 *         NET_STATUS_ERR_INVALID nếu buf NULL hoặc socket không hợp lệ
 */
NetStatus net_send_all(socket_t sockfd, const void *buf, size_t total_bytes);

/**
 * Đọc thêm dữ liệu từ socket vào NetRecvBuffer (gọi recv).
 * Xử lý ngắt EINTR/WSAEINTR.
 *
 * @param sockfd Socket kết nối TCP
 * @param rb Con trỏ tới NetRecvBuffer
 * @return NET_STATUS_OK nếu nhận được ít nhất 1 byte
 *         NET_STATUS_EOF nếu socket trả về 0 (peer đã đóng kết nối)
 *         NET_STATUS_ERR_SOCKET nếu gặp lỗi socket I/O
 *         NET_STATUS_ERR_OVERFLOW nếu buffer đã đầy mà chưa có newline
 *         NET_STATUS_ERR_INVALID nếu tham số không hợp lệ
 */
NetStatus net_read_to_buffer(socket_t sockfd, NetRecvBuffer *rb);

/**
 * Trích xuất đúng 1 thông điệp hoàn chỉnh kết thúc bằng '\n' từ NetRecvBuffer (không gọi socket I/O).
 * - Nếu tìm thấy '\n': sao chép vào out_msg (loại bỏ '\r' nếu có, kết thúc bằng '\0'),
 *   sau đó memmove dồn phần dữ liệu còn dư lên đầu buffer và cập nhật lại rb->len.
 * - Nếu chưa tìm thấy '\n': trả về NET_STATUS_MORE nếu còn chỗ chứa, hoặc
 *   NET_STATUS_ERR_OVERFLOW nếu kích thước tích lũy đã vượt quá giới hạn.
 *
 * @param rb Con trỏ tới NetRecvBuffer
 * @param out_msg Vùng nhớ lưu chuỗi thông điệp nhận được
 * @param out_cap Dung lượng tối đa của out_msg
 * @return NET_STATUS_OK nếu đã trích xuất được 1 thông điệp
 *         NET_STATUS_MORE nếu chưa có newline trong buffer
 *         NET_STATUS_ERR_OVERFLOW nếu thông điệp vượt quá out_cap hoặc NET_MAX_MSG_LEN
 *         NET_STATUS_ERR_INVALID nếu tham số không hợp lệ
 */
NetStatus net_extract_message(NetRecvBuffer *rb, char *out_msg, size_t out_cap);

/**
 * Nhận một thông điệp hoàn chỉnh theo newline (hàm blocking tiện ích):
 * Lặp việc net_extract_message và net_read_to_buffer cho tới khi có 1 thông điệp
 * hoàn chỉnh, hoặc gặp EOF / lỗi. Giữ lại toàn bộ dữ liệu dư trong rb cho lần gọi sau.
 *
 * @param sockfd Socket kết nối TCP
 * @param rb Con trỏ tới NetRecvBuffer của kết nối
 * @param out_msg Vùng nhớ lưu chuỗi thông điệp
 * @param out_cap Dung lượng tối đa của out_msg
 * @return NET_STATUS_OK: Nhận thành công đúng 1 thông điệp
 *         NET_STATUS_EOF: Kết nối bị đóng
 *         NET_STATUS_ERR_OVERFLOW: Thông điệp quá dài
 *         NET_STATUS_ERR_SOCKET: Lỗi mạng
 */
NetStatus net_recv_message(socket_t sockfd, NetRecvBuffer *rb, char *out_msg, size_t out_cap);

#endif /* NET_FRAMING_H */
