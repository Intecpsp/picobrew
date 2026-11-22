/* Model W5100 opening, rings, SEND_OK and peer disconnects. */
#include "zymatic_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t regs[0x8000], selected, drop_write, program_active;
static unsigned ops, writes, sends;
uint8_t zymatic_test_frame[1024];
static float temps[2];
uint32_t zymatic_test_temperature_bits(uint8_t heat)
{
    uint32_t bits;
    _Static_assert(sizeof(float) == sizeof(bits), "native simulator needs binary32");
    memcpy(&bits, &temps[heat], sizeof(bits));
    return bits;
}
static uint8_t serial_result;
static const char *serial_value;
uint8_t zymatic_test_read_oem_serial(char *s)
{
    if (serial_result) memcpy(s, serial_value, 13);
    return serial_result;
}
static uint8_t key[32];
static const char uid[] = "zy-unit-01";
static const char auth[] = "Authorization: Bearer 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\n";
static char sent[2100];

static void put16(unsigned addr, unsigned v) { regs[addr]=(uint8_t)(v>>8); regs[addr+1]=(uint8_t)v; }
static unsigned get16(unsigned addr) { return regs[addr]*256u + regs[addr+1]; }
uint8_t zymatic_test_network_selected(void) { return selected; }
uint8_t zymatic_test_register(uint8_t write, uint16_t addr, uint8_t *v)
{
    ++ops;
    assert(addr < sizeof(regs));
    if (!write) { *v=regs[addr]; return 1; }
    ++writes;
    assert((addr >= 0x700 && addr < 0x730) || (addr >= 0x5800 && addr < 0x6000));
    if (drop_write) { drop_write=0; return 0; }
    if (addr == 0x702) regs[addr] &= (uint8_t)~*v;
    else if (addr != 0x701) regs[addr]=*v;
    else switch (*v) {
    case 1: regs[0x703]=0x13; break;
    case 2: regs[0x703]=0x14; break;
    case 8: case 16: regs[0x703]=0; break;
    case 64: put16(0x726,(uint16_t)(get16(0x72a)-get16(0x728))); break;
    case 32: {
        unsigned p=get16(0x722), end=get16(0x724);
        unsigned length=(uint16_t)(end-p);
        assert(length < sizeof(sent));
        for (unsigned i=0; i<length; ++i) sent[i]=(char)regs[0x5800+((p+i)&2047)];
        sent[length]=0;
        put16(0x722,end);
        regs[0x702] |= 16;
        ++sends;
        break;
    }
    default: assert(!"unknown W5100 command");
    }
    return 1;
}

static void setup(ZymaticNetwork *n)
{
    memset(n,0,sizeof(*n));
    memset(regs,0,sizeof(regs));
    selected=1;
    program_active=0;
    ops=writes=sends=drop_write=0;
    sent[0]=0;
    regs[0x1a]=regs[0x1b]=0x55;
    put16(0x720, 2048);
    /* Exercise both the ring mask and 16-bit counter wrap. */
    put16(0x722, 0xffe8);
    put16(0x724, 0xffe8);
    put16(0x728, 0xfffb);
    put16(0x72a, 0xfffb);
}
static void poll(ZymaticNetwork *n, uint32_t ms)
{
    unsigned before=ops;
    zymatic_network_poll(n,key,uid,ms,program_active);
    assert(ops-before < 44);
}

static void req(ZymaticNetwork *n, const char *input)
{
    poll(n,0);
    poll(n,5);
    assert(regs[0x703]==0x14);
    for (unsigned i=0; i<strlen(input); ++i) regs[0x7800+((0xfffb+i)&2047)]=(uint8_t)input[i];
    put16(0x72a,0xfffb+(unsigned)strlen(input));
    put16(0x726,(unsigned)strlen(input));
    regs[0x703]=0x17;
    for (unsigned i=1; i<360 && sends==0; i++) poll(n,i*10);
    assert(sends==1);
    poll(n,3610);
    assert(regs[0x703]==0);
    assert(!strstr(sent,"0123456789abcdef"));
}

static void http_matrix(void)
{
    ZymaticNetwork n;
    char input[1700];
    struct case_t { const char *line, *auth, *extra, *expected; } cases[] = {
        {"GET / HTTP/1.1", "", "", "<h1>PicoBrew Zymatic</h1>"},
        {"GET /api/v1/health HTTP/1.1","","","{\"ok\":true}"},
        {"GET /api/v1/status HTTP/1.1",auth,"","\"device_uid\":\"zy-unit-01\""},
        {"GET /api/v1 HTTP/1.1",auth,"","\"api_version\":1"},
        {"GET /api/v1/status HTTP/1.1","","","401 Unauthorized"},
        {"GET /api/v1/status HTTP/1.1",auth,auth,"400 Bad Request"},
        {"GET /missing HTTP/1.1",auth,"","404 Not Found"},
        {"GET /api/v1 HTTP/1.1",auth,"Content-Length: 2\r\n","400 Bad Request"},
        {"GET /api/v1 HTTP/1.1",auth,"Transfer-Encoding: chunked\r\n","400 Bad Request"},
        {"GET /api/v1 HTTP/1.1",auth,"Host: other\r\n","400 Bad Request"},
        {"GET /api/v1 HTTP/1.1",auth,"bad : yes\r\n","400 Bad Request"},
        {"GET /api/v1 HTTP/1.1","Authorization: Basic xyz\r\n","","401 Unauthorized"},
        {"POST /api/v1/control HTTP/1.1",auth,"","405 Method Not Allowed"},
    };
    for (unsigned i=0; i<sizeof(cases)/sizeof(*cases); ++i) {
        setup(&n);
        snprintf(input,sizeof(input),"%s\r\nHost: brewer\r\n%s%s\r\n",cases[i].line,cases[i].auth,cases[i].extra);
        req(&n,input);
        assert(strstr(sent,cases[i].expected));
    }
    setup(&n); req(&n,"GET /api/v1 HTTP/1.1\r\n\r\n"); assert(strstr(sent,"400 Bad Request"));
    setup(&n);
    strcpy(input,"GET /api/v1/status HTTP/1.1\r\nHost: brewer\r\n");
    strcat(input,auth); input[strlen(input)-3]='2'; strcat(input,"\r\n");
    req(&n,input); assert(strstr(sent,"401 Unauthorized"));
    setup(&n);
    memset(input,'a',sizeof(input)-1); input[sizeof(input)-1]=0;
    req(&n,input); assert(strstr(sent,"431 Request Header"));
    setup(&n);
    strcpy(input,"GET / HTTP/1.0\r\n"); while (strlen(input)<1540) strcat(input,"Foo: bar\r\n");
    req(&n,input); assert(strstr(sent,"431 Request Header"));
    uint8_t saved[32]; memcpy(saved,key,sizeof(saved));
    for (unsigned erased=0; erased<=1; erased++) {
        char candidate[65];
        memset(key,erased?255:0,sizeof(saved));
        memset(candidate,erased?'f':'0',64); candidate[64]=0;
        snprintf(input,sizeof(input),"GET /api/v1 HTTP/1.0\r\nAuthorization: Bearer %s\r\n\r\n",candidate);
        setup(&n); req(&n,input); assert(strstr(sent,"401 Unauthorized"));
    }
    memcpy(key,saved,sizeof(saved));
}

static void mirror_and_sensors(void)
{
    ZymaticNetwork n;
    unsigned long a = 1, b = 0, revision = 0;
    char path[64], input[256];
    for (unsigned i=0; i<sizeof(zymatic_test_frame); ++i)
        zymatic_test_frame[i]=(uint8_t)(i*117u+(i>>6));
    for (unsigned i=0; i<1024; ++i) {
        a=(a+zymatic_test_frame[i])%65521u;
        b=(b+a)%65521u;
    }
    for (unsigned offset=0; offset<1024; offset+=128) {
        setup(&n);
        if (offset) snprintf(path, sizeof(path), "/api/v1/display/%u?revision=%lu", offset, revision);
        else strcpy(path, "/api/v1/display");
        snprintf(input, sizeof(input), "GET %s HTTP/1.1\r\nHost: brew\r\n%s\r\n", path, auth);
        req(&n,input);
        assert(strstr(sent,"200 OK"));
        const char *rp=strstr(sent,"\"revision\":");
        assert(rp);
        unsigned long current=strtoul(rp+11,NULL,10);
        if (!offset) revision=current;
        assert(current==revision && revision==((b<<16)|a));
        assert(n.line_used < sizeof(n.buffer.reply)-128);
        assert((strstr(sent, "\"next_offset\":") != NULL) == (offset != 896));
        const char *data=strstr(sent, "\"data\":\"");
        assert(data);
        data+=8;
        for (unsigned x=0; x<128; ++x) {
            unsigned expected=0;
            for (unsigned y=0; y<8; ++y)
                if (zymatic_test_frame[offset+y*16+x/8] & (0x80>>(x%8))) expected|=1u<<y;
            char two[3]={data[x*2],data[x*2+1],0};
            assert(strtoul(two,NULL,16) == expected);
        }
        assert(!strcmp(data+256,"\"}"));
    }
    setup(&n); program_active=1; req(&n,input);
    assert(strstr(sent,"503 Service Unavailable") && strstr(sent,"Retry-After: 2"));
    assert(strstr(sent,"\"error\":\"display_busy\"") && !strstr(sent,"\"data\":"));
    zymatic_test_frame[42]^=1;
    setup(&n); req(&n,input);
    assert(strstr(sent, "409 Conflict") && strstr(sent,"\"error\":\"display_changed\""));
    setup(&n);
    req(&n,"GET /api/v1/display HTTP/1.0\r\n\r\n");
    assert(strstr(sent, "401 Unauthorized"));
    snprintf(input,sizeof(input),"GET /api/v1/status HTTP/1.1\r\nHost: brew\r\n%s\r\n",auth);
    char serial[13];
    assert(!zymatic_read_stock_serial(serial));
    serial_result=1; serial_value="bad!bad!bad!";
    assert(zymatic_read_stock_serial(serial));
    setup(&n); req(&n,input); assert(!strstr(sent,"stock_serial"));
    serial_value="012345aBCDeF";
    assert(zymatic_read_stock_serial(serial) && !strcmp(serial,serial_value));
    setup(&n); req(&n,input); assert(strstr(sent,"\"stock_serial\":\"012345aBCDeF\""));
    setup(&n); program_active=1; req(&n,input);
    assert(strstr(sent,"\"state\":\"program_running\"") && strstr(sent,"\"busy\":true"));
    temps[0]=72.25f; temps[1]=181.5f;
    setup(&n); req(&n,input);
    assert(strstr(sent,"\"wort_f\":72.3") && strstr(sent,"\"thermoblock_f\":181.5"));
    temps[0]=0; temps[1]=500;
    setup(&n); req(&n,input); assert(!strstr(sent,"wort_f") && !strstr(sent,"thermoblock_f"));
}

static void network_ownership(void)
{
    ZymaticNetwork n;
    setup(&n);
    selected=0;
    poll(&n,0); assert(ops==0);
    selected=1; regs[0x1b]=0xaa;
    poll(&n,20); assert(writes==0);
    regs[0x1b]=0x55; regs[0x703]=0x17;
    poll(&n,40); assert(writes==0);
    regs[0x703]=0;
    poll(&n,50); poll(&n,60); assert(regs[0x703]==0x14);
    unsigned saved=writes;
    /* If an OEM peer owns this socket, leave its connection alone. */
    put16(0x704,1234); regs[0x703]=0x17;
    poll(&n,70); assert(writes==saved);
    setup(&n); poll(&n,0); poll(&n,1); regs[0x703]=0x17;
    poll(&n,20); poll(&n,5030); assert(regs[0x703]==0);
    setup(&n); poll(&n,0); poll(&n,1);
    selected=0; saved=writes; poll(&n,40); assert(writes==saved);
    selected=1; put16(0x704,888); poll(&n,60); assert(writes==saved);
}

int main(void)
{
    const uint8_t pattern[] = {1,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
    for (unsigned i=0; i<sizeof(key); i++) key[i]=pattern[i%sizeof(pattern)];
    http_matrix();
    network_ownership();
    mirror_and_sensors();
    return 0;
}
