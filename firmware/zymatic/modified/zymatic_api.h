#ifndef ZYMATIC_API_H
#define ZYMATIC_API_H

#include <stdint.h>

/* The caller owns and zero-initializes this state. Reuse the line storage for
 * the response only after parsing a complete request or rejecting the client. */
typedef struct {
    union { char line[392]; char reply[392]; } buffer;
    uint16_t line_used, header_bytes, response_bytes, sent;
    uint32_t started, display_revision;
    uint16_t display_offset;
    uint8_t flags, route, phase, http_error;
} ZymaticNetwork;

uint8_t zymatic_read_stock_serial(char *output);
void zymatic_network_poll(ZymaticNetwork *net, const uint8_t key[32], const char *uid, uint32_t now_ms, uint8_t program_active);

/* Hardware-independent HTTP processing. feed returns 1 when the response is
 * ready. reply_byte streams the completed response, including the optional
 * device website from program flash. A new request starts with a zeroed state. */
uint8_t zymatic_api_feed(ZymaticNetwork *net, uint8_t byte, const uint8_t key[32]);
void zymatic_api_reply(ZymaticNetwork *net, const char *uid, uint32_t now_ms, uint8_t program_active);
uint8_t zymatic_api_reply_byte(const ZymaticNetwork *net, uint16_t offset);

#endif
