#include "zymatic_api.h"
#include "zymatic_history.h"
#include "zymatic_pairing.h"

#include <stddef.h>
#include <string.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#include <util/atomic.h>
/* All response literals live in the second 64 KiB flash bank. The separate
 * website script occupies the packed OEM table's low-flash hole. */
typedef const __flash1 char *FlashAddress;
#define CONSTANT(s) (__extension__({ static const __flash1 char v[] = s; v; }))
#define READ(s) ((uint8_t)*(s))
#define SCRIPT_BYTE(i) pgm_read_byte_near(zymatic_pair_js+(i))
#else
typedef uintptr_t FlashAddress;
#define PROGMEM
#define ADDRESS(s) ((uintptr_t)(s))
#define CONSTANT(s) ADDRESS(s)
#define READ(s) (*(const uint8_t *)(s))
#define SCRIPT_BYTE(i) zymatic_pair_js[(i)]
#endif

#ifdef __AVR__
#define ZYMATIC_CODE_SECTION(name) __attribute__((section(name),noinline))
#define ZYMATIC_SITE_SECTION __attribute__((section(".low_web"),used))
#else
#define ZYMATIC_CODE_SECTION(name)
#define ZYMATIC_SITE_SECTION
#endif
#include "site/zymatic_site.h"
#include "site/zymatic_pair_js.h"
#include "site/zymatic_favicon.h"

enum { REQUEST = 1, HOST = 2, LENGTH = 4, AUTH = 8, VALID_KEY = 16, CR = 32, HTTP11 = 64, BODY = 128 };
enum { UNKNOWN, INDEX, PAIR_SCRIPT, FAVICON, HEALTH, OVERVIEW, DISCOVER, STATUS, DISPLAY, ENCODER, HISTORY, HISTORY_ROWS, PAIR_REQUEST, PAIR_CLAIM, PAIR_GRANT };
enum { BAD = 1, DENIED, MISSING, METHOD, LARGE, UNPAIRED, CHANGED, INVALID_COMMAND, HISTORY_BUSY };
enum { PAGE = 128, FRAME_BYTES = 1024 };
_Static_assert(sizeof(zymatic_site) + 128 <= 2048, "HTML response must fit one W5100 send");
_Static_assert(sizeof(zymatic_pair_js) + 128 <= 2048, "Pairing script must fit one W5100 send");
_Static_assert(sizeof(zymatic_favicon) + 128 <= 2048, "Favicon response must fit one W5100 send");
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
__attribute__((used, externally_visible, section(".low_functions")))
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

static ZYMATIC_CODE_SECTION(".crc_hole") uint8_t prefix(const char **text, FlashAddress literal)
{
    uint8_t expected;
    const char *s = *text;
    while ((expected = READ(literal++)) != 0) if (*s++ != expected) return 0;
    *text = s;
    return 1;
}

static ZYMATIC_CODE_SECTION(".low_functions") uint8_t decimal(const char **s, uint32_t *result)
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
    if (equal(line, CONSTANT("POST"), 0)) {
        if (equal(path, CONSTANT("/api/v1/commands"), 0)) n->route = ENCODER;
        else {
            const char *stage = path;
            if (prefix(&stage, CONSTANT("/api/v1/pairing/"))) {
                if (equal(stage, CONSTANT("request"), 0)) n->route = PAIR_REQUEST;
                else if (equal(stage, CONSTANT("claim"), 0)) n->route = PAIR_CLAIM;
                else if (equal(stage, CONSTANT("grant"), 0)) n->route = PAIR_GRANT;
            }
            if (!n->route) n->http_error = METHOD;
        }
    } else if (!equal(line, CONSTANT("GET"), 0)) n->http_error = METHOD;
    else if (equal(path, CONSTANT("/"), 0)) n->route = INDEX;
    else if (equal(path, CONSTANT("/favicon.ico"), 0)) n->route = FAVICON;
    else if (equal(path, CONSTANT("/pair.js"), 0)) n->route = PAIR_SCRIPT;
    else if (equal(path, CONSTANT("/api/v1/health"), 0)) n->route = HEALTH;
    else if (equal(path, CONSTANT("/api/v1/overview"), 0)) n->route = OVERVIEW;
    else if (equal(path, CONSTANT("/api/v1"), 0)) n->route = DISCOVER;
    else if (equal(path, CONSTANT("/api/v1/status"), 0)) n->route = STATUS;
    else if (equal(path, CONSTANT("/api/v1/history"), 0)) n->route = HISTORY;
    else if (equal(path, CONSTANT("/api/v1/display"), 0)) n->route = DISPLAY;
    else {
        uint32_t offset, revision;
        const char *p = path;
        if (prefix(&p, CONSTANT("/api/v1/history/samples/"))) {
            if (decimal(&p, &offset) && offset <= ZH_ROWS && prefix(&p, CONSTANT("?id="))
                && decimal(&p, &revision) && revision && !*p) {
                n->route = HISTORY_ROWS;
                n->display_offset = (uint16_t)offset;
                n->display_revision = revision;
            }
        } else if (prefix(&p, CONSTANT("/api/v1/display/")) && decimal(&p, &offset)
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
        const char *size = v;
        uint32_t length;
        if (!decimal(&size, &length) || *size || length > 192 || (length && n->route != ENCODER && n->route < PAIR_REQUEST))
            n->http_error = BAD;
        else n->body_length = (uint8_t)length;
    } else if (equal(name, CONSTANT("transfer-encoding"), 1)) n->http_error = BAD;
    if (n->flags & flag) n->http_error = BAD;
    n->flags |= flag;
}

uint8_t zymatic_api_feed(ZymaticNetwork *n, uint8_t byte, const uint8_t key[32])
{
    if (n->flags & BODY) {
        if (!byte || byte > 0x7e) { n->http_error = BAD; return 1; }
        n->buffer.line[n->line_used++] = (char)byte;
        return n->line_used == n->body_length;
    }
    if (++n->header_bytes > 1536) n->http_error = LARGE;
    else if (n->flags & CR) {
        n->flags &= (uint8_t)~CR;
        if (byte != '\n') n->http_error = BAD;
        else if (!(n->flags & REQUEST)) parse_request(n, n->buffer.line);
        else if (!n->line_used) {
            if ((n->flags & HTTP11) && !(n->flags & HOST)) n->http_error = BAD;
            else if ((n->route < INDEX || n->route > OVERVIEW) && n->route != PAIR_REQUEST && n->route != PAIR_CLAIM && !(n->flags & VALID_KEY)) n->http_error = DENIED;
            else if (!n->route) n->http_error = MISSING;
            else if (n->route == ENCODER || n->route >= PAIR_REQUEST) {
                if (!n->body_length) n->http_error = BAD;
                else { n->flags |= BODY; return 0; }
            }
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
/* Deferring the Adler reductions is exact for the fixed 1,024-byte frame:
 * the largest second sum is 133,825,024, within an unsigned 32-bit word. */
static uint32_t frame_checksum(void)
{
    uint32_t a = 1, b = 0;
    for (uint16_t i = 0; i < FRAME_BYTES; ++i) {
        a += FRAME[i];
        b += a;
    }
    return ((b % 65521UL) << 16) | (a % 65521UL);
}

/* The page snapshot and command revision are bound to the same physical
 * screen, encoder selection, and polling site. Local input invalidates the
 * issued revision even if that screen's pixels later reappear unchanged. */
static uint32_t issued_revision, issued_checksum, issued_at;
static int16_t issued_position;
static uint8_t issued_mode, remote_press, complete_frame, seen_frame;
uint8_t zymatic_drawn_row;

uint8_t zymatic_panel_take_press(void)
{
    uint8_t pressed = remote_press;
    remote_press = 0;
    return pressed;
}

void zymatic_panel_changed(void)
{
    complete_frame = 0;
    ++issued_revision;
}

#ifdef __AVR__
#define DIAL ((volatile uint8_t *)(uintptr_t)0x2494)
#define FAULT (*(volatile uint8_t *)(uintptr_t)0x2334)
#define PANEL_ATOMIC ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
#else
extern uint8_t zymatic_test_encoder[24], zymatic_test_fault;
#define DIAL zymatic_test_encoder
#define FAULT zymatic_test_fault
#define PANEL_ATOMIC for (uint8_t once = 1; once; once = 0)
#endif

static int16_t position(void)
{
    int16_t value;
    PANEL_ATOMIC { value = *(volatile int16_t *)(DIAL + 18); }
    return value;
}

static uint32_t panel_revision(uint32_t checksum, uint8_t mode, int16_t selected)
{
    if (!seen_frame || checksum != issued_checksum || mode != issued_mode || selected != issued_position) {
        if (!seen_frame) { issued_revision = checksum; seen_frame = 1; }
        else ++issued_revision;
        complete_frame = 0;
        issued_checksum = checksum;
        issued_mode = mode;
        issued_position = selected;
    }
    return issued_revision;
}

static void capture_page(ZymaticNetwork *n)
{
    uint8_t *out = (uint8_t *)n->buffer.reply + sizeof(n->buffer.reply) - PAGE;
    const volatile uint8_t *src = FRAME + n->display_offset;
    /* Capture the OEM row-major slice in one foreground turn. Convert the
     * eight source bits for each output column as that byte is transmitted,
     * amortizing the transpose across bounded W5100 polls. */
    for (uint8_t i = 0; i < PAGE; ++i) out[i] = src[i];
}

static void whitespace(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') ++*p;
}

static uint8_t punctuation(const char **p, char ch)
{
    whitespace(p);
    if (**p != ch) return 0;
    ++*p;
    whitespace(p);
    return 1;
}

/* Decode only the one nonce field used by the Hub. */
static uint8_t pair_operation(const char *p, uint8_t route, uint32_t now)
{
    uint8_t nonce[32];
    if (!punctuation(&p,'{') || !prefix(&p,CONSTANT("\"nonce\"")) ||
        !punctuation(&p,':') || *p++!='"') return ZP_BAD;
    for (uint8_t i=0; i<64; i++) {
        uint8_t digit = (uint8_t)*p++;
        if (digit >= '0' && digit <= '9') digit -= '0';
        else if (digit >= 'a' && digit <= 'f') digit -= 'a'-10;
        else return ZP_BAD;
        if (i & 1) nonce[i/2] |= digit;
        else nonce[i/2] = (uint8_t)(digit<<4);
    }
    if (*p++!='"' || !punctuation(&p,'}') || *p) return ZP_BAD;
    return zymatic_pair_operation(route-PAIR_REQUEST,nonce,now);
}

static uint8_t json_uint(const char **p, uint32_t *value)
{
    return !(**p == '0' && (*p)[1] >= '0' && (*p)[1] <= '9') && decimal(p, value);
}

static uint8_t encoder_command(const char *p, uint8_t mode, uint32_t now_ms)
{
    uint8_t fields = 0, click = 0;
    uint32_t revision = 0;
    int8_t delta = 0;
    if (!punctuation(&p, '{')) return INVALID_COMMAND;
    do {
        uint8_t field;
        if (prefix(&p, CONSTANT("\"command\""))) field = 1;
        else if (prefix(&p, CONSTANT("\"delta\""))) field = 2;
        else if (prefix(&p, CONSTANT("\"click\""))) field = 4;
        else if (prefix(&p, CONSTANT("\"display_revision\""))) field = 8;
        else return INVALID_COMMAND;
        if ((fields & field) || !punctuation(&p, ':')) return INVALID_COMMAND;
        fields |= field;
        if (field == 1) {
            if (!prefix(&p, CONSTANT("\"encoder\""))) return INVALID_COMMAND;
        } else if (field == 2) {
            uint8_t neg = *p == '-';
            uint32_t value;
            if (neg) ++p;
            if (!json_uint(&p, &value) || value > 100) return INVALID_COMMAND;
            delta = neg ? -(int8_t)value : (int8_t)value;
        } else if (field == 4) {
            if (prefix(&p, CONSTANT("true"))) click = 1;
            else if (!prefix(&p, CONSTANT("false"))) return INVALID_COMMAND;
        } else if (!json_uint(&p, &revision)) return INVALID_COMMAND;
    } while (punctuation(&p, ','));
    if (!punctuation(&p, '}') || *p || (fields & 9) != 9 || (!delta && !click) || (delta && click)) return INVALID_COMMAND;
    if (!complete_frame || mode == ZYMATIC_OUTER || mode != issued_mode || revision != issued_revision ||
        (uint32_t)(now_ms - issued_at) > 10000 ||
        (mode == ZYMATIC_PROGRAM && delta) || FAULT || frame_checksum() != issued_checksum)
        return CHANGED;
    uint8_t outcome = CHANGED;
    PANEL_ATOMIC {
        volatile int16_t *current = (volatile int16_t *)(DIAL + 18);
        int16_t low = *(volatile int16_t *)(DIAL + 12);
        int16_t high = *(volatile int16_t *)(DIAL + 14);
        if (DIAL[11] && *current == issued_position && !FAULT && low <= high &&
            (mode == ZYMATIC_PROGRAM || (*current == zymatic_drawn_row && *current >= low && *current <= high))) {
            if (delta) {
                int32_t next = (int32_t)*current + delta;
                *current = next < low ? low : next > high ? high : (int16_t)next;
            }
            if (click) remote_press = 0xff;
            outcome = 0;
        }
    }
    if (!outcome) zymatic_panel_changed();
    return outcome;
}

static void append_temperature(ZymaticNetwork *n, uint8_t heat)
{
    uint32_t bits = temperature_bits(heat);
    /* The OEM stores IEEE-754 binary32 Fahrenheit. Accept 0 < value <= 400.
     * Quantize the significand to tenths without linking a floating-point
     * formatting library into the AVR. Values below 1/32 round to zero. */
    uint16_t tenths = zymatic_temperature_tenths(bits);
    if (tenths == UINT16_MAX) return;
    literal(n, heat ? CONSTANT(",\"thermoblock_f\":") : CONSTANT(",\"wort_f\":"));
    number(n, tenths / 10);
    append(n, '.');
    append(n, (char)('0' + tenths % 10));
}

static void history_value(ZymaticNetwork *n, uint16_t value, uint8_t temperature)
{
    if (value == UINT16_MAX) literal(n, CONSTANT("null"));
    else if (temperature) {
        number(n, value / 10);
        append(n, '.');
        append(n, (char)('0' + value % 10));
    } else number(n, value);
}

static void history_rows(ZymaticNetwork *n, uint8_t count)
{
    uint16_t offset = n->display_offset;
    literal(n, CONSTANT("{\"id\":"));
    number(n, n->display_revision);
    literal(n, CONSTANT(",\"offset\":"));
    number(n, offset);
    literal(n, CONSTANT(",\"samples\":["));
    for (uint8_t i = 0; i < 3 && offset < count; ++i, ++offset) {
        ZymaticHistoryRow row;
        if (!zymatic_history_row(n->display_revision, offset, &row)) {
            n->http_error = HISTORY_BUSY;
            return;
        }
        if (i) append(n, ',');
        append(n, '[');
        number(n, row.seconds * 1000UL);
        append(n, ','); history_value(n, row.wort, 1);
        append(n, ','); history_value(n, row.heat, 1);
        append(n, ','); history_value(n, row.target == UINT8_MAX ? UINT16_MAX : row.target, 0);
        append(n, ','); history_value(n, row.location == UINT8_MAX ? UINT16_MAX : row.location, 0);
        append(n, ','); history_value(n, row.step == UINT8_MAX ? UINT16_MAX : row.step, 0);
        append(n, ']');
    }
    append(n, ']');
    if (offset < count) {
        literal(n, CONSTANT(",\"next_offset\":"));
        number(n, offset);
    }
    append(n, '}');
}

/* Inventory is sent one record at a time. The starting ID freezes the upper
 * bound if another session opens mid-request; committed records are immutable. */
int8_t zymatic_api_more(ZymaticNetwork *n)
{
    if (n->route != HISTORY || n->http_error || n->display_offset > ZH_SLOTS) return 0;
    if (!zymatic_history_ready()) return -1;
    n->response_bytes = n->sent = 0;
    while (n->display_offset < ZH_SLOTS && n->display_offset < n->display_revision) {
        ZymaticHistorySession session;
        uint32_t id = n->display_revision - n->display_offset++;
        if (!zymatic_history_session(id, &session)) {
            if (zymatic_history_ready()) continue;
            --n->display_offset;
            return -1;
        }
        if (n->body_length++) append(n, ',');
        literal(n, CONSTANT("{\"id\":")); number(n, id);
        literal(n, CONSTANT(",\"available\":true,\"command\":\"brew\",\"recipe_name\":\""));
        for (uint8_t i = 0; session.name[i]; ++i) {
            if (session.name[i] == '"' || session.name[i] == '\\') append(n, '\\');
            append(n, session.name[i]);
        }
        literal(n, CONSTANT("\",\"state\":\""));
        if (session.flags & ZH_ACTIVE) literal(n, CONSTANT("recording"));
        else if (session.flags & ZH_COMPLETE) literal(n, CONSTANT("completed"));
        else literal(n, CONSTANT("interrupted"));
        literal(n, CONSTANT("\",\"sample_interval_ms\":60000,\"record_count\":")); number(n, session.count);
        literal(n, CONSTANT(",\"persisted_record_count\":")); number(n, session.count);
        literal(n, CONSTANT(",\"duration_ms\":")); number(n, session.duration * 1000UL);
        if (session.first_elapsed) {
            literal(n, CONSTANT(",\"first_elapsed_ms\":")); number(n, session.first_elapsed * 1000UL);
        }
        if (session.flags & ZH_TRUNCATED) literal(n, CONSTANT(",\"truncated\":true"));
        if (!(session.flags & (ZH_ACTIVE | ZH_COMPLETE | ZH_ABORTED))) literal(n, CONSTANT(",\"recovered_after_restart\":true"));
        append(n, '}');
        return 1;
    }
    literal(n, CONSTANT("]}"));
    n->display_offset = ZH_SLOTS + 1;
    return 1;
}

void zymatic_api_reply(ZymaticNetwork *n, const uint8_t key[32], const char *uid, uint32_t now_ms, uint8_t program_active)
{
    if (n->route == ENCODER && !n->http_error) n->http_error = encoder_command(n->buffer.line, program_active, now_ms);
    uint8_t pairing=0;
    char code[9];
    if (!n->http_error && n->route >= PAIR_REQUEST) {
        uint8_t nonzero=0, nonerased=0;
        if (key) for (uint8_t i=0; i<32; ++i) { nonzero|=key[i]; nonerased|=(uint8_t)~key[i]; }
        if (!nonzero || !nonerased) n->http_error=UNPAIRED;
        else {
            pairing=pair_operation(n->buffer.line,n->route,now_ms);
            if (pairing==ZP_CONFLICT) n->http_error=CHANGED;
            else if (pairing==ZP_MISSING) n->http_error=MISSING;
            else if (pairing==ZP_BAD) n->http_error=BAD;
            else if (pairing==ZP_REQUESTED) {
                uint32_t ticket;
                zymatic_pair_prompt(now_ms,code,&ticket);
            }
        }
    }
    /* Erase incomplete credentials even after a rejected/oversize header. */
    memset(n->buffer.reply, 0, sizeof(n->buffer.reply));
    n->response_bytes = n->sent = 0;
    if (n->route == STATUS && !n->http_error && !valid_uid(uid)) n->http_error = UNPAIRED;
    uint8_t row_count = 0;
    if (!n->http_error && (n->route == HISTORY || n->route == HISTORY_ROWS)) {
        if (!zymatic_history_ready()) n->http_error = HISTORY_BUSY;
        else if (n->route == HISTORY_ROWS) {
            ZymaticHistorySession session;
            if (!zymatic_history_session(n->display_revision, &session))
                n->http_error = zymatic_history_ready() ? MISSING : HISTORY_BUSY;
            else if (n->display_offset > session.count) n->http_error = BAD;
            else row_count = session.count;
        }
    }
    if (n->route == DISPLAY && !n->http_error) {
        uint32_t checksum = frame_checksum();
        int16_t selected = position();
        uint32_t revision = panel_revision(checksum, program_active, selected);
        if (n->display_offset && revision != n->display_revision) n->http_error = CHANGED;
        else {
            capture_page(n);
            if (n->display_offset == FRAME_BYTES - PAGE) {
                complete_frame = 1;
                issued_at = now_ms;

            }
        }
        n->display_revision = revision;
    }
    literal(n, CONSTANT("HTTP/1.0 "));
    switch (n->http_error) {
    case INVALID_COMMAND: case BAD: literal(n, CONSTANT("400 Bad Request")); break;
    case DENIED: literal(n, CONSTANT("401 Unauthorized")); break;
    case MISSING: literal(n, CONSTANT("404 Not Found")); break;
    case METHOD: literal(n, CONSTANT("405 Method Not Allowed")); break;
    case LARGE: literal(n, CONSTANT("431 Request Header Fields Too Large")); break;
    case CHANGED: literal(n, CONSTANT("409 Conflict")); break;
    case HISTORY_BUSY: case UNPAIRED: literal(n, CONSTANT("503 Service Unavailable")); break;
    default: literal(n, (n->route == ENCODER || (pairing && pairing!=ZP_REDEEMED)) ? CONSTANT("202 Accepted") : CONSTANT("200 OK")); break;
    }
    literal(n, CONSTANT("\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: "));
    if (n->route >= INDEX && n->route <= FAVICON && !n->http_error) {
        literal(n, n->route == INDEX ? CONSTANT("text/html; charset=utf-8") :
                   n->route == FAVICON ? CONSTANT("image/x-icon") : CONSTANT("application/javascript"));
        literal(n, CONSTANT("\r\nContent-Encoding: gzip\r\n\r\n"));
        n->line_used = n->response_bytes;
        n->response_bytes += n->route == INDEX ? sizeof(zymatic_site) :
                             n->route == FAVICON ? sizeof(zymatic_favicon) : sizeof(zymatic_pair_js);
        return;
    }
    n->line_used = 0;
    literal(n, CONSTANT("application/json\r\n\r\n"));
    if (!n->http_error && (n->route == DISCOVER || n->route == STATUS))
        literal(n, CONSTANT("{\"model\":\"zymatic\",\"model_name\":\"PicoBrew Zymatic\",\"model_family\":\"zymatic\""));
    if (n->route >= PAIR_REQUEST && !n->http_error) {
        if (pairing==ZP_REDEEMED) {
            literal(n,CONSTANT("{\"api_key\":\""));
            for (uint8_t i=0;i<32;i++){append(n,zymatic_hex_digit(key[i]>>4));append(n,zymatic_hex_digit(key[i]&15));}
            literal(n,CONSTANT("\"}"));
        } else {
            literal(n,CONSTANT("{\"state\":\""));
            literal(n,pairing==ZP_APPROVED?CONSTANT("approved"):CONSTANT("pending"));
            if(pairing==ZP_REQUESTED){literal(n,CONSTANT("\",\"code\":\""));for(uint8_t i=0;i<8;i++)append(n,code[i]);}
            literal(n,CONSTANT("\"}"));
        }
    }
    else if (n->route >= PAIR_REQUEST && n->http_error==CHANGED) literal(n,CONSTANT("{\"error\":\"pairing_busy\"}"));
    else if (n->http_error == CHANGED) literal(n, CONSTANT("{\"error\":\"display_changed\"}"));
    else if (n->http_error == HISTORY_BUSY) literal(n, CONSTANT("{\"error\":\"history_unavailable\"}"));
    else if (n->http_error) literal(n, CONSTANT("{\"error\":\"request_rejected\"}"));
    else if (n->route == ENCODER) literal(n, CONSTANT("{\"accepted\":true,\"command\":\"encoder\"}"));
    else if (n->route == HISTORY) {
        n->display_revision = zymatic_history_latest();
        n->body_length = 0;
        literal(n, CONSTANT("{\"fields\":[\"elapsed_ms\",\"wort_f\",\"thermoblock_f\",\"target_f\",\"compartment\",\"current_step\"],\"sessions\":["));
    }
    else if (n->route == HISTORY_ROWS) {
        history_rows(n, row_count);
        if (n->http_error) {
            n->response_bytes = 0;
            literal(n, CONSTANT("HTTP/1.0 503 Service Unavailable\r\nConnection: close\r\nRetry-After: 2\r\nContent-Type: application/json\r\n\r\n{\"error\":\"history_unavailable\"}"));
        }
    }
    else if (n->route == HEALTH) literal(n, CONSTANT("{\"ok\":true}"));
    else if (n->route == OVERVIEW) {
        literal(n, program_active == ZYMATIC_PROGRAM ? CONSTANT("{\"busy\":true") : CONSTANT("{\"busy\":false"));
        literal(n, CONSTANT(",\"pairing_v1\":true"));
        goto report_care;
    }
    else if (n->route == DISCOVER) literal(n, CONSTANT(",\"pairing_v1\":true,\"api_version\":1,\"mode\":\"standalone\",\"firmware_version\":\"1.0.0\",\"commands\":[\"encoder\"],\"capabilities\":{\"display\":true,\"front_panel\":true,\"panel_revision\":true,\"history_persistent\":true,\"temperature\":true}}"));
    else if (n->route == DISPLAY) {
        literal(n, CONSTANT("{\"width\":128,\"height\":64,\"format\":\"page-lsb-hex\",\"total_bytes\":1024,\"offset\":"));
        number(n, n->display_offset);
        literal(n, CONSTANT(",\"revision\":"));
        number(n, n->display_revision);
        if (!n->display_offset) {
            literal(n, CONSTANT(",\"dial\":\""));
            if (program_active == ZYMATIC_OUTER || FAULT) literal(n, CONSTANT("off"));
            else if (program_active == ZYMATIC_PROGRAM) literal(n, CONSTANT("press"));
            else literal(n, CONSTANT("menu"));
            append(n,'"');
        }
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
        if (program_active == ZYMATIC_PROGRAM) literal(n, CONSTANT("\",\"state\":\"program_running\",\"busy\":true"));
        else literal(n, CONSTANT("\",\"state\":\"monitoring\""));
        literal(n, CONSTANT(",\"ready\":false,\"uptime_ms\":"));
        number(n, now_ms);
        if (stock_serial[0]) {
            literal(n, CONSTANT(",\"stock_serial\":\""));
            for (uint8_t i=0; i<12; ++i) append(n,stock_serial[i]);
            append(n,'"');
        }
    report_care: ;
        uint8_t care = zymatic_maintenance();
        if (care != 0xff) {
            literal(n, CONSTANT(",\"maintenance\":{\"due\":["));
            if (care & 4) literal(n, CONSTANT("\"rinse\",\"clean_filter\""));
            if ((care & 3) == 3) {
                if (care & 4) append(n, ',');
                literal(n, CONSTANT("\"deep_clean\""));
            }
            literal(n, CONSTANT("]}"));
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
        if (n->route == PAIR_SCRIPT) return SCRIPT_BYTE(offset);
        if (n->route == INDEX || n->route == FAVICON) {
#ifdef __AVR__
            uint32_t start = n->route == FAVICON ? pgm_get_far_address(zymatic_favicon) : pgm_get_far_address(zymatic_site);
            return pgm_read_byte_far(start + offset);
#else
            return (n->route == FAVICON ? zymatic_favicon : zymatic_site)[offset];
#endif
        }
        if (offset >= PAGE * 2) return offset == PAGE * 2 ? '"' : '}';
        uint8_t x = (uint8_t)(offset / 2);
        const uint8_t *pixel = (const uint8_t *)n->buffer.reply + sizeof(n->buffer.reply) - PAGE + x/8;
        uint8_t mask = 0x80 >> (x % 8), v = 0;
        for (uint8_t y = 0; y < 8; ++y) {
            v >>= 1;
            if (pixel[16*y] & mask) v |= 0x80;
        }
        v = (offset & 1) ? v & 15 : v >> 4;
        return (uint8_t)(v < 10 ? '0' + v : 'a' + v - 10);
    }
    return (uint8_t)n->buffer.reply[offset];
}
