#include "telemetry.hpp"
extern "C" {
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
}
#include <cstring>
#include <new>

namespace picobrew { namespace standalone { namespace esp8266 {

struct AsyncTelemetry::Impl {
    enum class State : std::uint8_t { Idle, Resolving, Connecting, Connected, Failed };
    struct DnsTicket { Impl* owner = nullptr; bool used = false; };
    State state = State::Idle;
    tcp_pcb* pcb = nullptr;
    pbuf* incoming = nullptr;
    std::uint16_t incoming_offset = 0, port = 0;
    bool peer_closed = false;
    DnsTicket* dns_ticket = nullptr;
    char host[254] = {};

    ~Impl() { close(); }
    static DnsTicket* reserve_ticket(Impl* owner) {
        // lwIP 1.4 has no DNS cancellation API. A canceled ticket remains alive
        // until the SDK invokes its callback, with owner cleared. This bounded
        // pool prevents callbacks from touching a destroyed/reused connection.
        static DnsTicket tickets[DNS_TABLE_SIZE];
        for (DnsTicket& ticket : tickets) {
            if (!ticket.used) { ticket.used=true; ticket.owner=owner; return &ticket; }
        }
        return nullptr;
    }
    void release_incoming() {
        if (incoming) pbuf_free(incoming);
        incoming=nullptr; incoming_offset=0;
    }
    void detach(tcp_pcb* connection) {
        tcp_arg(connection,nullptr);
        tcp_recv(connection,nullptr);
        tcp_sent(connection,nullptr);
        tcp_poll(connection,nullptr,0);
        tcp_err(connection,nullptr);
    }
    void abort() {
        tcp_pcb* connection=pcb; pcb=nullptr;
        if (connection) { detach(connection); tcp_abort(connection); }
        release_incoming(); state=State::Failed;
    }
    void close() {
        if (dns_ticket) { dns_ticket->owner=nullptr; dns_ticket=nullptr; }
        tcp_pcb* connection=pcb; pcb=nullptr;
        if (connection) {
            detach(connection);
            if (tcp_close(connection)!=ERR_OK) tcp_abort(connection);
        }
        release_incoming(); peer_closed=false; state=State::Idle;
    }
    bool connect(ip_addr_t* address) {
        pcb=tcp_new();
        if (!pcb) { state=State::Failed; return false; }
        state=State::Connecting;
        tcp_arg(pcb,this); tcp_recv(pcb,received); tcp_err(pcb,failed);
        tcp_poll(pcb,polled,2);
        if (tcp_connect(pcb,address,port,connected)!=ERR_OK) { abort(); return false; }
        return true;
    }
    bool begin(const char* hostname, std::uint16_t target_port) {
        close();
        if (!hostname || !*hostname || !target_port) { state=State::Failed; return false; }
        std::size_t n=0;
        while (n<sizeof(host) && hostname[n]) {
            const unsigned char c=static_cast<unsigned char>(hostname[n]);
            if (c<=32 || c>=127 || c=='/' || c=='\\' || c==':' || c=='@') {
                state=State::Failed; return false;
            }
            ++n;
        }
        if (n>=sizeof(host)) { state=State::Failed; return false; }
        std::memcpy(host,hostname,n+1); port=target_port;
        ip_addr_t address;
        if (ipaddr_aton(host,&address)) return connect(&address);
        dns_ticket=reserve_ticket(this);
        if (!dns_ticket) { state=State::Failed; return false; }
        state=State::Resolving;
        const err_t result=dns_gethostbyname(host,&address,resolved,dns_ticket);
        if (result==ERR_INPROGRESS) return state!=State::Failed;
        dns_ticket->owner=nullptr; dns_ticket->used=false; dns_ticket=nullptr;
        if (result==ERR_OK) return connect(&address);
        state=State::Failed; return false;
    }
    static void resolved(const char*, ip_addr_t* address, void* context) {
        DnsTicket* ticket=static_cast<DnsTicket*>(context);
        Impl* owner=ticket->owner;
        ticket->owner=nullptr; ticket->used=false;
        if (!owner || owner->dns_ticket!=ticket) return;
        owner->dns_ticket=nullptr;
        if (owner->state!=State::Resolving) return;
        if (!address) owner->state=State::Failed;
        else owner->connect(address);
    }
    static err_t connected(void* context, tcp_pcb* connection, err_t error) {
        Impl* self=static_cast<Impl*>(context);
        if (!self || self->pcb!=connection) return ERR_OK;
        if (error!=ERR_OK) { self->abort(); return ERR_ABRT; }
        self->state=State::Connected; return ERR_OK;
    }
    static void failed(void* context, err_t) {
        Impl* self=static_cast<Impl*>(context);
        if (!self) return;
        // The SDK already freed the PCB before invoking this callback.
        self->pcb=nullptr; self->release_incoming(); self->state=State::Failed;
    }
    static err_t received(void* context, tcp_pcb* connection, pbuf* packet, err_t error) {
        Impl* self=static_cast<Impl*>(context);
        if (!self || self->pcb!=connection) {
            if (packet) pbuf_free(packet);
            return ERR_OK;
        }
        if (error!=ERR_OK) {
            if (packet) pbuf_free(packet);
            self->abort(); return ERR_ABRT;
        }
        if (!packet) { self->peer_closed=true; return ERR_OK; }
        // Retain one SDK-owned chain; defer its window acknowledgement until
        // receive() consumes bytes. The SDK retains a refused next chain.
        if (self->incoming) return ERR_MEM;
        self->incoming=packet; self->incoming_offset=0; return ERR_OK;
    }
    bool flush() {
        if (!pcb || state!=State::Connected) return false;
        const err_t result=tcp_output(pcb);
        if (result==ERR_OK || result==ERR_MEM || result==ERR_BUF) return true;
        abort(); return false;
    }
    static err_t polled(void* context, tcp_pcb* connection) {
        Impl* self=static_cast<Impl*>(context);
        if (!self || self->pcb!=connection || self->state!=State::Connected) return ERR_OK;
        return self->flush() ? ERR_OK : ERR_ABRT;
    }
    int poll_connect() {
        if (state==State::Connected) return flush() ? 1 : -1;
        return state==State::Resolving || state==State::Connecting ? 0 : -1;
    }
    int send(const std::uint8_t* data, std::size_t length) {
        if (state!=State::Connected || !pcb || peer_closed || (length&&!data))
            return PICO_HTTP_IO_ERROR;
        if (!length) return 0;
        const std::size_t available=tcp_sndbuf(pcb);
        if (!available) return flush() ? PICO_HTTP_IO_AGAIN : PICO_HTTP_IO_ERROR;
        std::size_t count=length<available ? length : available;
        if (count>PICO_HTTP_IO_BUDGET) count=PICO_HTTP_IO_BUDGET;
        const err_t result=tcp_write(pcb,data,static_cast<u16_t>(count),TCP_WRITE_FLAG_COPY);
        if (result==ERR_MEM || result==ERR_BUF)
            return flush() ? PICO_HTTP_IO_AGAIN : PICO_HTTP_IO_ERROR;
        if (result!=ERR_OK) { abort(); return PICO_HTTP_IO_ERROR; }
        // Once tcp_write copies these bytes, an output backpressure result must
        // not turn into AGAIN: that would make the caller duplicate the bytes.
        return flush() ? static_cast<int>(count) : PICO_HTTP_IO_ERROR;
    }
    int receive(std::uint8_t* data, std::size_t capacity) {
        if (state!=State::Connected || !pcb || (capacity&&!data)) return PICO_HTTP_IO_ERROR;
        if (!capacity) return PICO_HTTP_IO_AGAIN;
        if (!flush()) return PICO_HTTP_IO_ERROR;
        if (!incoming) return peer_closed ? 0 : PICO_HTTP_IO_AGAIN;
        const std::size_t remaining=incoming->tot_len-incoming_offset;
        std::size_t count=capacity<remaining ? capacity : remaining;
        if (count>PICO_HTTP_IO_BUDGET) count=PICO_HTTP_IO_BUDGET;
        const u16_t copied=pbuf_copy_partial(incoming,data,static_cast<u16_t>(count),incoming_offset);
        if (copied!=count) { abort(); return PICO_HTTP_IO_ERROR; }
        incoming_offset=static_cast<std::uint16_t>(incoming_offset+copied);
        tcp_recved(pcb,copied);
        if (incoming_offset==incoming->tot_len) release_incoming();
        return copied ? static_cast<int>(copied) : PICO_HTTP_IO_AGAIN;
    }
};

AsyncTelemetry::AsyncTelemetry() noexcept : impl_(new (std::nothrow) Impl) {}
AsyncTelemetry::~AsyncTelemetry() { delete impl_; }
TelemetryIo AsyncTelemetry::binding() noexcept {
    TelemetryIo result; result.context=this;
    result.begin=[](void* c,const char* h,std::uint16_t p){return static_cast<AsyncTelemetry*>(c)->begin(h,p);};
    result.poll_connect=[](void* c){return static_cast<AsyncTelemetry*>(c)->poll_connect();};
    result.send=[](void* c,const std::uint8_t* d,std::size_t n){return static_cast<AsyncTelemetry*>(c)->send(d,n);};
    result.receive=[](void* c,std::uint8_t* d,std::size_t n){return static_cast<AsyncTelemetry*>(c)->receive(d,n);};
    result.close=[](void* c){static_cast<AsyncTelemetry*>(c)->close();};
    return result;
}
bool AsyncTelemetry::begin(const char* host,std::uint16_t port) { return impl_&&impl_->begin(host,port); }
int AsyncTelemetry::poll_connect() { return impl_?impl_->poll_connect():-1; }
int AsyncTelemetry::send(const std::uint8_t* data,std::size_t n) { return impl_?impl_->send(data,n):PICO_HTTP_IO_ERROR; }
int AsyncTelemetry::receive(std::uint8_t* data,std::size_t n) { return impl_?impl_->receive(data,n):PICO_HTTP_IO_ERROR; }
void AsyncTelemetry::close() noexcept { if (impl_) impl_->close(); }

}}}
