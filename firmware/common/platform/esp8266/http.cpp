#include "http.hpp"
extern "C" {
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
}
#include <new>

namespace picobrew { namespace standalone { namespace esp8266 {
struct HttpListener::Impl {
    tcp_pcb* listener=nullptr;
    tcp_pcb* client=nullptr;
    pbuf* incoming=nullptr;
    std::uint16_t offset=0;
    int handle=0;
    bool in_use=false,peer_closed=false;
    ~Impl(){end();}
    void release(){if(incoming)pbuf_free(incoming);incoming=nullptr;offset=0;}
    static void detach(tcp_pcb* connection){
        tcp_arg(connection,nullptr);tcp_recv(connection,nullptr);tcp_sent(connection,nullptr);
        tcp_poll(connection,nullptr,0);tcp_err(connection,nullptr);
    }
    void drop(bool abort){
        tcp_pcb* connection=client;client=nullptr;
        if(connection){detach(connection);if(abort||tcp_close(connection)!=ERR_OK)tcp_abort(connection);}
        release();peer_closed=false;
    }
    void end(){
        drop(false);in_use=false;
        if(listener){tcp_arg(listener,nullptr);tcp_accept(listener,nullptr);
            tcp_pcb* previous=listener;listener=nullptr;
            // tcp_close on a LISTEN PCB releases it immediately in lwIP 1.4.
            if(tcp_close(previous)!=ERR_OK)tcp_abort(previous);
        }
    }
    bool begin(std::uint16_t port){
        if(listener)return true;
        if(!port)return false;
        tcp_pcb* connection=tcp_new();if(!connection)return false;
        if(tcp_bind(connection,IP_ADDR_ANY,port)!=ERR_OK){detach(connection);tcp_abort(connection);return false;}
        tcp_pcb* bound=tcp_listen_with_backlog(connection,1);
        if(!bound){detach(connection);tcp_abort(connection);return false;}
        listener=bound;tcp_arg(listener,this);tcp_accept(listener,accepted);return true;
    }
    static err_t accepted(void* context,tcp_pcb* connection,err_t error){
        Impl* self=static_cast<Impl*>(context);
        if(!self||!connection)return ERR_ARG;
        if(error!=ERR_OK||self->client||self->in_use){detach(connection);tcp_abort(connection);return ERR_ABRT;}
        self->client=connection;self->peer_closed=false;self->release();
        self->handle=self->handle==0x7fffffff?1:self->handle+1;
        tcp_arg(connection,self);tcp_recv(connection,received);tcp_err(connection,failed);tcp_poll(connection,polled,2);
        tcp_accepted(self->listener);return ERR_OK;
    }
    static void failed(void* context,err_t){
        Impl* self=static_cast<Impl*>(context);if(!self)return;
        // The SDK frees this PCB before invoking tcp_err. Keep an active
        // descriptor reserved until PicoHttpServer observes the I/O failure.
        self->client=nullptr;self->release();self->peer_closed=false;
    }
    static err_t received(void* context,tcp_pcb* connection,pbuf* packet,err_t error){
        Impl* self=static_cast<Impl*>(context);
        if(!self||self->client!=connection){if(packet)pbuf_free(packet);return ERR_OK;}
        if(error!=ERR_OK){if(packet)pbuf_free(packet);self->drop(true);return ERR_ABRT;}
        if(!packet){self->peer_closed=true;return ERR_OK;}
        if(packet->tot_len>PICO_HTTP_HEADER_LIMIT+PICO_HTTP_ARENA_CAPACITY){pbuf_free(packet);self->drop(true);return ERR_ABRT;}
        // Retain one SDK-owned pbuf chain, applying receive-window backpressure
        // until foreground code consumes it. A refused chain stays SDK-owned.
        if(self->incoming)return ERR_MEM;
        self->incoming=packet;self->offset=0;return ERR_OK;
    }
    bool flush(){
        if(!client)return false;
        const err_t result=tcp_output(client);
        if(result==ERR_OK||result==ERR_MEM||result==ERR_BUF)return true;
        drop(true);return false;
    }
    static err_t polled(void* context,tcp_pcb* connection){
        Impl* self=static_cast<Impl*>(context);
        if(!self||self->client!=connection)return ERR_OK;
        return self->flush()?ERR_OK:ERR_ABRT;
    }
    int accept(){
        if(!client||in_use)return PICO_HTTP_IO_AGAIN;
        in_use=true;return handle;
    }
    int receive(int descriptor,std::uint8_t* bytes,std::size_t capacity){
        if(!in_use||descriptor!=handle||!client||(capacity&&!bytes))return PICO_HTTP_IO_ERROR;
        if(!capacity)return PICO_HTTP_IO_AGAIN;
        if(!flush())return PICO_HTTP_IO_ERROR;
        if(!incoming)return peer_closed?0:PICO_HTTP_IO_AGAIN;
        const std::size_t remaining=incoming->tot_len-offset;
        std::size_t count=capacity<remaining?capacity:remaining;
        if(count>PICO_HTTP_IO_BUDGET)count=PICO_HTTP_IO_BUDGET;
        const auto copied=pbuf_copy_partial(incoming,bytes,static_cast<u16_t>(count),offset);
        if(copied!=count){drop(true);return PICO_HTTP_IO_ERROR;}
        offset=static_cast<std::uint16_t>(offset+copied);tcp_recved(client,copied);
        if(offset==incoming->tot_len)release();
        return copied?static_cast<int>(copied):PICO_HTTP_IO_AGAIN;
    }
    int send(int descriptor,const std::uint8_t* bytes,std::size_t length){
        if(!in_use||descriptor!=handle||!client||(length&&!bytes))return PICO_HTTP_IO_ERROR;
        if(!length)return 0;
        std::size_t count=tcp_sndbuf(client);if(count>length)count=length;
        if(count>PICO_HTTP_IO_BUDGET)count=PICO_HTTP_IO_BUDGET;
        if(!count)return flush()?PICO_HTTP_IO_AGAIN:PICO_HTTP_IO_ERROR;
        const err_t result=tcp_write(client,bytes,static_cast<u16_t>(count),TCP_WRITE_FLAG_COPY);
        if(result==ERR_MEM||result==ERR_BUF)return flush()?PICO_HTTP_IO_AGAIN:PICO_HTTP_IO_ERROR;
        if(result!=ERR_OK){drop(true);return PICO_HTTP_IO_ERROR;}
        // tcp_write accepted a copy. Deferred output must not cause a retry of
        // those bytes. A peer FIN closes only its sending half, so a complete
        // HTTP request may still receive its response here.
        return flush()?static_cast<int>(count):PICO_HTTP_IO_ERROR;
    }
    void close(int descriptor){if(!in_use||descriptor!=handle)return;drop(false);in_use=false;}
};
HttpListener::HttpListener() noexcept:impl_(new(std::nothrow)Impl){}
HttpListener::~HttpListener(){delete impl_;}
bool HttpListener::begin(std::uint16_t port){return impl_&&impl_->begin(port);}
void HttpListener::end()noexcept{if(impl_)impl_->end();}
bool HttpListener::listening()const noexcept{return impl_&&impl_->listener;}
int HttpListener::accept(){return impl_?impl_->accept():PICO_HTTP_IO_AGAIN;}
int HttpListener::receive(int client,std::uint8_t* data,std::size_t size){return impl_?impl_->receive(client,data,size):PICO_HTTP_IO_ERROR;}
int HttpListener::send(int client,const std::uint8_t* data,std::size_t size){return impl_?impl_->send(client,data,size):PICO_HTTP_IO_ERROR;}
void HttpListener::close(int client)noexcept{if(impl_)impl_->close(client);}
const PicoHttpIo HttpListener::io={
    [](void* p){return static_cast<HttpListener*>(p)->accept();},
    [](void* p,int fd,std::uint8_t* data,std::size_t size){return static_cast<HttpListener*>(p)->receive(fd,data,size);},
    [](void* p,int fd,const std::uint8_t* data,std::size_t size){return static_cast<HttpListener*>(p)->send(fd,data,size);},
    [](void* p,int fd){static_cast<HttpListener*>(p)->close(fd);}
};
}}}
