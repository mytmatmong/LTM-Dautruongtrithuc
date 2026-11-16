#ifndef AUTH_CRYPTO_H
#define AUTH_CRYPTO_H

#include <stddef.h>

/**
 * Băm mật khẩu kết hợp salt ngẫu nhiên:
 * Chuỗi kết quả có định dạng "$5$<salt>$<hash>" (SHA-256 crypt tiêu chuẩn Linux/POSIX)
 * hoặc băm salt tùy biến an toàn.
 *
 * @param password Mật khẩu thuần
 * @param out_hash Buffer lưu chuỗi băm
 * @param out_hash_len Độ lớn buffer
 * @return 0 nếu thành công, -1 nếu thất bại
 */
int auth_hash_password(const char *password, char *out_hash, size_t out_hash_len);

/**
 * So sánh mật khẩu thuần với chuỗi hash đã lưu trong database.
 * 
 * @param password Mật khẩu thuần do client cung cấp
 * @param stored_hash Chuỗi hash đã lưu trong DB
 * @return 1 nếu trùng khớp, 0 nếu không khớp hoặc lỗi
 */
int auth_verify_password(const char *password, const char *stored_hash);

#endif /* AUTH_CRYPTO_H */

