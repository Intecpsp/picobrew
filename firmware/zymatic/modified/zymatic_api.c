#include "zymatic_api.h"

#include <stddef.h>
#include <string.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#include <util/atomic.h>
typedef uint_farptr_t FlashAddress;
#define ADDRESS(s) pgm_get_far_address(s)
#define CONSTANT(s) (__extension__({ static const char v[] PROGMEM = s; ADDRESS(v); }))
#define READ(s) pgm_read_byte_far(s)
#else
typedef uintptr_t FlashAddress;
#define PROGMEM
#define ADDRESS(s) ((uintptr_t)(s))
#define CONSTANT(s) ADDRESS(s)
#define READ(s) (*(const uint8_t *)(s))
#endif

#include "site/zymatic_site.h"

enum { REQUEST = 1, HOST = 2, LENGTH = 4, AUTH = 8, VALID_KEY = 16, CR = 32, HTTP11 = 64 };
enum { UNKNOWN, INDEX, HEALTH, DISCOVER, STATUS, DISPLAY };
enum { BAD = 1, DENIED, MISSING, METHOD, LARGE, UNPAIRED, CHANGED, DISPLAY_BUSY };
enum { PAGE = 128, FRAME_BYTES = 1024 };
_Static_assert(sizeof(zymatic_site) + 128 <= 2048, "HTML response must fit one W5100 send");
_Static_assert(sizeof(ZymaticNetwork) <= 424, "Check AVR static-memory budget");
#ifdef __AVR__
#define FRAME ((const volatile uint8_t *)(uintptr_t)0x266a)
static uint32_t temperature_bits(uint8_t heat)
{
    uint32_t value;
    const volatile uint32_t *source = (const volatile uint32_t *)(uintptr_t)(heat ? 0x23e0 : 0x23dc);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { value = *source; }
    return value;
}
#else
extern uint8_t zymatic_test_frame[FRAME_BYTES];
extern uint32_t zymatic_test_temperature_bits(uint8_t);
#define FRAME zymatic_test_frame
#define temperature_bits zymatic_test_temperature_bits
#endif

static uint8_t lower(uint8_t ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }

static uint8_t equal(const char *s, FlashAddress literal, uint8_t fold)
{
    for (;;) {
        uint8_t actual = (uint8_t)*s++, expected = READ(literal++);
        if ((fold ? lower(actual) : actual) != expected) return 0;
        if (!expected) return 1;
    }
}

/* 1.1.14 reads the physical identity on its existing setup/sync paths and
 * formats 12 hex digits in the caller's 13-byte output. Consume the result
 * while the stack buffer is live. An API poll never starts a hardware search. */
static char stock_serial[13];
#ifdef __AVR__
#define read_oem_serial ((uint8_t (*)(char *))(uintptr_t)(0x4342u / 2))
#else
extern uint8_t zymatic_test_read_oem_serial(char *);
#define read_oem_serial zymatic_test_read_oem_serial
#endif

#ifdef __AVR__
__attribute__((used, externally_visible))
#endif
uint8_t zymatic_read_stock_serial(char *out)
{
    uint8_t ok = read_oem_serial(out);
    if (!ok) return ok;
    if (out[12]) return ok;
    for (uint8_t i = 0; i < 12; ++i) {
        uint8_t c = lower((uint8_t)out[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return ok;
    }
    memcpy(stock_serial, out, sizeof(stock_serial));
    return ok;
}

static uint8_t token(uint8_t ch)
{
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return 1;
    switch (ch) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+':
    case '-': case '.': case '^': case '_': case '`': case '|': case '~': return 1;
    default: return 0;
    }
}

static uint8_t authorized(const char *val, const uint8_t key[32])
{
    if (!key || strlen(val) != 71) return 0;
    FlashAddress scheme = CONSTANT("bearer ");
    for (uint8_t i = 0; i < 7; i++)
        if (lower((uint8_t)val[i]) != READ(scheme+i)) return 0;
    uint8_t mismatch = 0, nonzero = 0, nonerased = 0;
    for (uint8_t i = 0; i < 64; ++i) {
        uint8_t k = key[i / 2];
        uint8_t nibble = i & 1 ? k & 15 : k >> 4;
        uint8_t expected = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        mismatch |= expected ^ (uint8_t)val[i + 7];
        nonzero |= k;
        nonerased |= (uint8_t)~k;
    }
    return nonzero && nonerased && !mismatch;
}

static uint8_t prefix(const char **text, FlashAddress literal)
{
    uint8_t expected;
    const char *s = *text;
    while ((expected = READ(literal++)) != 0) if (*s++ != expected) return 0;
    *text = s;
    return 1;
}

static uint8_t decimal(const char **s, uint32_t *result)
{
    uint32_t value = 0;
    const char *p = *s;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        uint8_t digit = (uint8_t)(*p++ - '0');
        if (value > UINT32_MAX / 10 || (value == UINT32_MAX / 10 && digit > UINT32_MAX % 10)) return 0;
        value = value * 10 + digit;
    }
    *s = p;
    *result = value;
    return 1;
}

static void parse_request(ZymaticNetwork *n, char *line)
{
    char *path = strchr(line, ' ');
    if (!path) { n->http_error = BAD; return; }
    *path++ = 0;
    char *version = strchr(path, ' ');
    if (!version) { n->http_error = BAD; return; }
    *version++ = 0;
    if (equal(version, CONSTANT("HTTP/1.1"), 0)) n->flags |= HTTP11;
    else if (!equal(version, CONSTANT("HTTP/1.0"), 0)) { n->http_error = BAD; return; }
    if (!equal(line, CONSTANT("GET"), 0)) { n->http_error = METHOD; return; }
    if (equal(path, CONSTANT("/"), 0)) n->route = INDEX;
    else if (equal(path, CONSTANT("/api/v1/health"), 0)) n->route = HEALTH;
    else if (equal(path, CONSTANT("/api/v1"), 0)) n->route = DISCOVER;
    else if (equal(path, CONSTANT("/api/v1/status"), 0)) n->route = STATUS;
    else if (equal(path, CONSTANT("/api/v1/display"), 0)) n->route = DISPLAY;
    else {
        uint32_t offset, revision;
        const char *p = path;
        if (prefix(&p, CONSTANT("/api/v1/display/")) && decimal(&p, &offset)
            && offset > 0 && offset < FRAME_BYTES && offset % PAGE == 0
            && prefix(&p, CONSTANT("?revision=")) && decimal(&p, &revision) && !*p) {
            n->route = DISPLAY;
            n->display_offset = (uint16_t)offset;
            n->display_revision = revision;
        }
    }
    n->flags |= REQUEST;
}

static void parse_header(ZymaticNetwork *n, char *name, const uint8_t *key)
{
    char *v = name;
    while (token((uint8_t)*v)) ++v;
    if (v == name || *v != ':') { n->http_error = BAD; return; }
    *v++ = 0;
    while (*v == ' ' || *v == '\t') ++v;
    size_t len = strlen(v);
    while (len && (v[len-1] == ' ' || v[len-1] == '\t')) v[--len] = 0;
    uint8_t flag = 0;
    if (equal(name, CONSTANT("host"), 1)) {
        flag = HOST;
        if (!*v) n->http_error = BAD;
    } else if (equal(name, CONSTANT("authorization"), 1)) {
        flag = AUTH;
        if (authorized(v, key)) n->flags |= VALID_KEY;
    } else if (equal(name, CONSTANT("content-length"), 1)) {
        flag = LENGTH;
        if (!equal(v, CONSTANT("0"), 0)) n->http_error = BAD;
    } else if (equal(name, CONSTANT("transfer-encoding"), 1)) n->http_error = BAD;
    if (n->flags & flag) n->http_error = BAD;
    n->flags |= flag;
}

uint8_t zymatic_api_feed(ZymaticNetwork *n, uint8_t byte, const uint8_t key[32])
{
    if (++n->header_bytes > 1536) n->http_error = LARGE;
    else if (n->flags & CR) {
        n->flags &= (uint8_t)~CR;
        if (byte != '\n') n->http_error = BAD;
        else if (!(n->flags & REQUEST)) parse_request(n, n->buffer.line);
        else if (!n->line_used) {
            if ((n->flags & HTTP11) && !(n->flags & HOST)) n->http_error = BAD;
            else if (n->route != INDEX && n->route != HEALTH && !(n->flags & VALID_KEY)) n->http_error = DENIED;
            else if (!n->route) n->http_error = MISSING;
            return 1;
        } else parse_header(n, n->buffer.line, key);
        memset(n->buffer.line, 0, n->line_used);
        n->line_used = 0;
    } else if (byte == '\r') n->flags |= CR;
    else if ((byte < 0x20 && (byte != '\t' || !(n->flags & REQUEST))) || byte > 0x7e) n->http_error = BAD;
    else if (n->line_used + 1u >= sizeof(n->buffer.line)) n->http_error = LARGE;
    else n->buffer.line[n->line_used++] = (char)byte;
    return n->http_error != 0;
}

static void append(ZymaticNetwork *n, char ch)
{
    if (n->response_bytes < sizeof(n->buffer.reply))
        n->buffer.reply[n->response_bytes++] = ch;
}

static void literal(ZymaticNetwork *n, FlashAddress address)
{
    char ch;
    while ((ch = (char)READ(address++)) != 0) append(n, ch);
}

static void number(ZymaticNetwork *n, uint32_t value)
{
    char digits[10];
    uint8_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) append(n,digits[--count]);
}

static uint8_t valid_uid(const char *uid)
{
    if (!uid) return 0;
    uint8_t len = 0;
    for (; len < 64 && uid[len]; ++len) {
        uint8_t c = uid[len];
        if (!((c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'z') || c == '-' || c == ':')) return 0;
    }
    return len && len < 64;
}

/* Capture one physical row block and independently fingerprint the complete
 * OEM drawing buffer. All reads occur in the foreground; the original display
 * writer also runs there. The Hub retries on 409 if a later page has changed. */
/* Adler-32's modulus is 65536 - 15. Both inputs are below 65521. */
static uint16_t adler_add(uint16_t a, uint16_t b)
{
    uint16_t sum = a + b;
    return sum < a || sum >= 65521u ? (uint16_t)(sum + 15u) : sum;
}

static uint32_t capture_page(ZymaticNetwork *n)
{
    uint16_t a = 1, b = 0;
    for (uint16_t i = 0; i < FRAME_BYTES; ++i) {
        a = adler_add(a, FRAME[i]);
        b = adler_add(b, a);
    }
    uint8_t *out = (uint8_t *)n->buffer.reply + sizeof(n->buffer.reply) - PAGE;
    const volatile uint8_t *src = FRAME + n->display_offset;
    for (uint8_t row = 0, rowbit = 1; row < 8; ++row, rowbit <<= 1) {
        for (uint8_t x = 0; x < 128; x += 8) {
            uint8_t pixels = *src++;
            for (uint8_t bit = 0; bit < 8; ++bit, pixels <<= 1)
                if (pixels & 0x80) out[x+bit] |= rowbit;
        }
    }
    return ((uint32_t)b << 16) | a;
}

static void append_temperature(ZymaticNetwork *n, uint8_t heat)
{
    uint32_t bits = temperature_bits(heat);
    /* The OEM stores IEEE-754 binary32 Fahrenheit. Accept 0 < value <= 400.
     * Quantize the significand to tenths without linking a floating-point
     * formatting library into the AVR. Values below 1/32 round to zero. */
    if (!bits || bits > UINT32_C(0x43c80000)) return;
    uint16_t tenths = 0;
    if (bits >= UINT32_C(0x3d000000)) {
        uint8_t shift = (uint8_t)(150 - (bits >> 23));
        uint32_t scaled = ((bits & UINT32_C(0x7fffff)) | UINT32_C(0x800000)) * 10;
        tenths = (uint16_t)((scaled + (UINT32_C(1) << (shift-1))) >> shift);
    }
    literal(n, heat ? CONSTANT(",\"thermoblock_f\":") : CONSTANT(",\"wort_f\":"));
    number(n, tenths / 10);
    append(n, '.');
    append(n, (char)('0' + tenths % 10));
}

void zymatic_api_reply(ZymaticNetwork *n, const char *uid, uint32_t now_ms, uint8_t program_active)
{
    /* Erase incomplete credentials even after a rejected/oversize header. */
    memset(n->buffer.reply, 0, sizeof(n->buffer.reply));
    n->response_bytes = n->sent = 0;
    if (n->route == STATUS && !n->http_error && !valid_uid(uid)) n->http_error = UNPAIRED;
    if (n->route == DISPLAY && !n->http_error && program_active) n->http_error = DISPLAY_BUSY;
    if (n->route == DISPLAY && !n->http_error) {
        uint32_t revision = capture_page(n);
        if (n->display_offset && revision != n->display_revision) n->http_error = CHANGED;
        n->display_revision = revision;
    }
    literal(n, CONSTANT("HTTP/1.0 "));
    switch (n->http_error) {
    case BAD: literal(n, CONSTANT("400 Bad Request")); break;
    case DENIED: literal(n, CONSTANT("401 Unauthorized")); break;
    case MISSING: literal(n, CONSTANT("404 Not Found")); break;
    case METHOD: literal(n, CONSTANT("405 Method Not Allowed")); break;
    case LARGE: literal(n, CONSTANT("431 Request Header Fields Too Large")); break;
    case CHANGED: literal(n, CONSTANT("409 Conflict")); break;
    case DISPLAY_BUSY: case UNPAIRED: literal(n, CONSTANT("503 Service Unavailable")); break;
    default: literal(n, CONSTANT("200 OK")); break;
    }
    if (n->http_error == DISPLAY_BUSY) literal(n, CONSTANT("\r\nRetry-After: 2"));
    literal(n, CONSTANT("\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: "));
    if (n->route == INDEX && !n->http_error) {
        literal(n, CONSTANT("text/html; charset=utf-8\r\n\r\n"));
        n->line_used = n->response_bytes;
        n->response_bytes += sizeof(zymatic_site)-1;
        return;
    }
    n->line_used = 0;
    literal(n, CONSTANT("application/json\r\n\r\n"));
    if (!n->http_error && (n->route == DISCOVER || n->route == STATUS))
        literal(n, CONSTANT("{\"model\":\"zymatic\",\"model_name\":\"PicoBrew Zymatic\",\"model_family\":\"zymatic\""));
    if (n->http_error == CHANGED) literal(n, CONSTANT("{\"error\":\"display_changed\"}"));
    else if (n->http_error == DISPLAY_BUSY) literal(n, CONSTANT("{\"error\":\"display_busy\",\"message\":\"Display resumes when the program exits.\"}"));
    else if (n->http_error) literal(n, CONSTANT("{\"error\":\"request_rejected\"}"));
    else if (n->route == HEALTH) literal(n, CONSTANT("{\"ok\":true}"));
    else if (n->route == DISCOVER) literal(n, CONSTANT(",\"api_version\":1,\"mode\":\"standalone\",\"firmware_version\":\"1.2.0\",\"commands\":[],\"capabilities\":{\"display\":true,\"front_panel\":false,\"temperature\":true}}"));
    else if (n->route == DISPLAY) {
        literal(n, CONSTANT("{\"width\":128,\"height\":64,\"format\":\"page-lsb-hex\",\"total_bytes\":1024,\"offset\":"));
        number(n, n->display_offset);
        literal(n, CONSTANT(",\"revision\":"));
        number(n, n->display_revision);
        if (n->display_offset + PAGE < FRAME_BYTES) {
            literal(n, CONSTANT(",\"next_offset\":"));
            number(n, n->display_offset + PAGE);
        }
        literal(n, CONSTANT(",\"data\":\""));
        n->line_used = n->response_bytes;
        n->response_bytes += PAGE * 2 + 2;
    } else {
        literal(n, CONSTANT(",\"device_uid\":\""));
        for (; *uid; ++uid) append(n, *uid);
        if (program_active) literal(n, CONSTANT("\",\"state\":\"program_running\",\"busy\":true"));
        else literal(n, CONSTANT("\",\"state\":\"monitoring\""));
        literal(n, CONSTANT(",\"ready\":false,\"uptime_ms\":"));
        number(n, now_ms);
        if (stock_serial[0]) {
            literal(n, CONSTANT(",\"stock_serial\":\""));
            for (uint8_t i=0; i<12; ++i) append(n,stock_serial[i]);
            append(n,'"');
        }
        append_temperature(n, 0);
        append_temperature(n, 1);
        append(n,'}');
    }
}

uint8_t zymatic_api_reply_byte(const ZymaticNetwork *n, uint16_t offset)
{
    if (offset >= n->response_bytes) return 0;
    if (n->line_used && offset >= n->line_used) {
        offset -= n->line_used;
        if (n->route == INDEX) return READ(ADDRESS(zymatic_site) + offset);
        if (offset >= PAGE * 2) return offset == PAGE * 2 ? '"' : '}';
        uint8_t v = (uint8_t)n->buffer.reply[sizeof(n->buffer.reply) - PAGE + offset/2];
        v = (offset & 1) ? v & 15 : v >> 4;
        return (uint8_t)(v < 10 ? '0' + v : 'a' + v - 10);
    }
    return (uint8_t)n->buffer.reply[offset];
}
