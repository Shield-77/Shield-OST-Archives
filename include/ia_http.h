#ifndef SHIELD_ONLINE_IA_HTTP_H
#define SHIELD_ONLINE_IA_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { HTTP_IDLE, HTTP_BUSY, HTTP_DONE, HTTP_ERROR } HttpState;
typedef struct {
    HttpState state;
    uint64_t received;
    uint64_t resumed_from;
    uint64_t expected;
    uint64_t elapsed_ms;
    uint64_t dns_us, connect_us, tls_us, first_byte_us, total_us;
    uint64_t sd_write_us;
    unsigned write_calls;
    long new_connections;
    long status;
    char error[120];
} HttpSnapshot;

bool ia_http_init(void);
bool ia_http_start(const char *url, const char *file_path, size_t max_bytes);
bool ia_http_start_range(const char *url, const char *file_path, size_t max_bytes);
bool ia_http_start_resume(const char *url, const char *file_path, size_t max_bytes,
                          uint64_t offset, const char *if_range);
const char *ia_http_validator(void);
HttpSnapshot ia_http_snapshot(void);
char *ia_http_take_body(size_t *length);
void ia_http_reset(void);
void ia_http_cancel(void);
void ia_http_exit(void);
const char *ia_http_init_error(void);

#endif
