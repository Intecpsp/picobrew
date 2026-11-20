#include "network_ports.hpp"
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <cerrno>
#include <cstring>

namespace picobrew { namespace standalone { namespace esp32 {
namespace {
bool again(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS;}
bool nonblocking(int fd){return lwip_fcntl(fd,F_SETFL,O_NONBLOCK)==0;}
void disconnect(int fd){
    if(fd<0)return;
    // Telemetry is best effort; do not wait in foreground for unsent TCP data.
    linger option{};option.l_onoff=1;option.l_linger=0;
    lwip_setsockopt(fd,SOL_SOCKET,SO_LINGER,&option,sizeof option);
    lwip_shutdown(fd,SHUT_RDWR);lwip_close(fd);
}
int receive_bytes(int fd,std::uint8_t* data,std::size_t size){
    const int count=lwip_recv(fd,data,size,0);
    return count<0?(again()?PICO_HTTP_IO_AGAIN:PICO_HTTP_IO_ERROR):count;
}
int send_bytes(int fd,const std::uint8_t* data,std::size_t size){
    const int count=lwip_send(fd,data,size,0);
    return count<0?(again()?PICO_HTTP_IO_AGAIN:PICO_HTTP_IO_ERROR):count;
}
}
bool HttpSocket::begin(std::uint16_t port){
    if(listener_>=0)return true;
    const int fd=lwip_socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(fd<0)return false;
    int reuse=1;lwip_setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=INADDR_ANY;
    if(!nonblocking(fd)||lwip_bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof address)<0||lwip_listen(fd,2)<0){disconnect(fd);return false;}
    listener_=fd;return true;
}
PicoHttpIo HttpSocket::io() const noexcept {
    PicoHttpIo result{};
    result.accept=[](void* p)->int{auto& self=*static_cast<HttpSocket*>(p);
        if(self.listener_<0)return PICO_HTTP_IO_AGAIN;
        const int fd=lwip_accept(self.listener_,nullptr,nullptr);
        if(fd<0)return again()?PICO_HTTP_IO_AGAIN:PICO_HTTP_IO_ERROR;
        if(!nonblocking(fd)){disconnect(fd);return PICO_HTTP_IO_ERROR;}return fd;
    };
    result.receive=[](void*,int fd,std::uint8_t* bytes,std::size_t size){return receive_bytes(fd,bytes,size);};
    result.send=[](void*,int fd,const std::uint8_t* bytes,std::size_t size){return send_bytes(fd,bytes,size);};
    result.close=[](void*,int fd){disconnect(fd);};return result;
}
void HttpSocket::end(){disconnect(listener_);listener_=-1;}
picobrew::standalone::TelemetryIo TelemetrySocket::services() noexcept {
    picobrew::standalone::TelemetryIo result;result.context=this;
    result.begin=[](void* p,const char* host,std::uint16_t port){return static_cast<TelemetrySocket*>(p)->begin(host,port);};
    result.poll_connect=[](void* p){return static_cast<TelemetrySocket*>(p)->poll_connect();};
    result.send=[](void* p,const std::uint8_t* bytes,std::size_t size){return static_cast<TelemetrySocket*>(p)->send(bytes,size);};
    result.receive=[](void* p,std::uint8_t* bytes,std::size_t size){return static_cast<TelemetrySocket*>(p)->receive(bytes,size);};
    result.close=[](void* p){static_cast<TelemetrySocket*>(p)->close();};return result;
}
bool TelemetrySocket::begin(const char* host,std::uint16_t port){
    if(!host||!host[0]||std::strlen(host)>=sizeof host_||!port)return false;
    close();
    portENTER_CRITICAL(&lock_);
    // A cancelled DNS request retains this storage until lwIP calls back.
    // Reusing it earlier could turn a stale result into a new connection.
    if(dns_pending_){portEXIT_CRITICAL(&lock_);return false;}
    std::strcpy(host_,host);port_=port;dns_pending_=true;dns_done_=false;dns_valid_=false;abandoned_=false;
    portEXIT_CRITICAL(&lock_);
    if(tcpip_callback_with_block(resolve_start,this,0)!=ERR_OK){
        portENTER_CRITICAL(&lock_);dns_pending_=false;dns_done_=true;dns_valid_=false;portEXIT_CRITICAL(&lock_);return false;
    }
    return true;
}
void TelemetrySocket::resolve_start(void* context){
    auto& self=*static_cast<TelemetrySocket*>(context);
    portENTER_CRITICAL(&self.lock_);const bool cancelled=self.abandoned_;portEXIT_CRITICAL(&self.lock_);
    if(cancelled){resolved(nullptr,nullptr,context);return;}
    ip_addr_t address;
    const err_t result=dns_gethostbyname(self.host_,&address,resolved,context);
    if(result==ERR_OK)resolved(self.host_,&address,context);
    else if(result!=ERR_INPROGRESS)resolved(self.host_,nullptr,context);
}
void TelemetrySocket::resolved(const char*,const ip_addr_t* address,void* context){
    auto& self=*static_cast<TelemetrySocket*>(context);
    portENTER_CRITICAL(&self.lock_);
    self.dns_valid_=address&&IP_IS_V4(address)&&!self.abandoned_;
    if(self.dns_valid_)self.address_=ip4_addr_get_u32(ip_2_ip4(address));
    self.dns_pending_=false;self.dns_done_=true;
    portEXIT_CRITICAL(&self.lock_);
}
int TelemetrySocket::poll_connect(){
    portENTER_CRITICAL(&lock_);const bool done=dns_done_,valid=dns_valid_,cancelled=abandoned_;const std::uint32_t address=address_;portEXIT_CRITICAL(&lock_);
    if(cancelled)return -1;
    if(!done)return 0;
    if(!valid)return -1;
    if(socket_<0){
        socket_=lwip_socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(socket_<0||!nonblocking(socket_)){close();return -1;}
        sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(port_);target.sin_addr.s_addr=address;
        const int result=lwip_connect(socket_,reinterpret_cast<sockaddr*>(&target),sizeof target);
        if(result==0){connecting_=false;return 1;}
        if(!again()){close();return -1;}connecting_=true;
    }
    if(!connecting_)return 1;
    fd_set writable,failed;FD_ZERO(&writable);FD_ZERO(&failed);FD_SET(socket_,&writable);FD_SET(socket_,&failed);timeval no_wait{};
    const int ready=lwip_select(socket_+1,nullptr,&writable,&failed,&no_wait);
    if(ready<0){close();return -1;}if(!ready)return 0;
    int error=0;socklen_t length=sizeof error;
    if(lwip_getsockopt(socket_,SOL_SOCKET,SO_ERROR,&error,&length)!=0||error){close();return -1;}
    connecting_=false;return 1;
}
int TelemetrySocket::send(const std::uint8_t* data,std::size_t size){return socket_<0?PICO_HTTP_IO_ERROR:send_bytes(socket_,data,size);}
int TelemetrySocket::receive(std::uint8_t* data,std::size_t size){return socket_<0?PICO_HTTP_IO_ERROR:receive_bytes(socket_,data,size);}
void TelemetrySocket::close(){
    portENTER_CRITICAL(&lock_);abandoned_=true;portEXIT_CRITICAL(&lock_);
    disconnect(socket_);socket_=-1;connecting_=false;
}
}}}
