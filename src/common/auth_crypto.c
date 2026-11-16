#define _GNU_SOURCE
#include "auth_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__) || defined(__unix__)
#include <crypt.h>

static void generate_salt_string(char *salt_buf, size_t buf_len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)(time(NULL) ^ 0x5a5a));
        seeded = 1;
    }

    /* Định dạng SHA-256 crypt của Linux là: $5$salt$ */
    strcpy(salt_buf, "$5$");
    size_t pos = 3;
    for (int i = 0; i < 16 && pos + 2 < buf_len; i++) {
        salt_buf[pos++] = charset[rand() % (sizeof(charset) - 1)];
    }
    salt_buf[pos++] = '$';
    salt_buf[pos] = '\0';
}

int auth_hash_password(const char *password, char *out_hash, size_t out_hash_len) {
    if (!password || !out_hash || out_hash_len == 0) return -1;

    char setting[32];
    generate_salt_string(setting, sizeof(setting));

    struct crypt_data data;
    memset(&data, 0, sizeof(data));

    char *res = crypt_r(password, setting, &data);
    if (!res || strlen(res) == 0) return -1;

    if (strlen(res) >= out_hash_len) return -1;
    strncpy(out_hash, res, out_hash_len);
    out_hash[out_hash_len - 1] = '\0';
    return 0;
}

int auth_verify_password(const char *password, const char *stored_hash) {
    if (!password || !stored_hash) return 0;

    struct crypt_data data;
    memset(&data, 0, sizeof(data));

    char *res = crypt_r(password, stored_hash, &data);
    if (!res) return 0;

    return (strcmp(res, stored_hash) == 0) ? 1 : 0;
}

#else
/* Fallback cho Windows MinGW (khi biên dịch thử nghiệm cục bộ) */
int auth_hash_password(const char *password, char *out_hash, size_t out_hash_len) {
    if (!password || !out_hash || out_hash_len < 64) return -1;
    /* Giả lập hash dạng $dummy$<pw> khi không có libcrypt trên MinGW */
    snprintf(out_hash, out_hash_len, "$dummy$%s", password);
    return 0;
}

int auth_verify_password(const char *password, const char *stored_hash) {
    if (!password || !stored_hash) return 0;
    char expected[256];
    snprintf(expected, sizeof(expected), "$dummy$%s", password);
    return (strcmp(expected, stored_hash) == 0) ? 1 : 0;
}
#endif

