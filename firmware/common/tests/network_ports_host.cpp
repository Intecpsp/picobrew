#include "network_ports.hpp"
#include <lwip/test_lwip.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>

using namespace picobrew::standalone::esp32;
namespace {
tcpip_callback_fn work=nullptr;void* work_context=nullptr;
dns_found_callback answer=nullptr;void* answer_context=nullptr;
unsigned dns_calls=0;
void dispatch(){assert(work);auto callback=work;auto* context=work_context;work=nullptr;callback(context);}
void resolve(bool ok){assert(answer);ip_addr_t address{htonl(INADDR_LOOPBACK)};auto callback=answer;answer=nullptr;callback("hub.local",ok?&address:nullptr,answer_context);}
int listener(std::uint16_t& port){int fd=::socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(::bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);socklen_t length=sizeof address;assert(getsockname(fd,reinterpret_cast<sockaddr*>(&address),&length)==0);port=ntohs(address.sin_port);assert(listen(fd,2)==0);assert(fcntl(fd,F_SETFL,O_NONBLOCK)==0);return fd;}
}
err_t tcpip_callback_with_block(tcpip_callback_fn callback,void* context,unsigned char block){assert(!block&&!work);work=callback;work_context=context;return ERR_OK;}
err_t dns_gethostbyname(const char* name,ip_addr_t*,dns_found_callback callback,void* context){assert(!std::strcmp(name,"hub.local"));++dns_calls;answer=callback;answer_context=context;return ERR_INPROGRESS;}
int main(){
    std::uint16_t port;int server=listener(port);
    TelemetrySocket telemetry;
    assert(telemetry.begin("hub.local",port));assert(dns_calls==0&&telemetry.poll_connect()==0);
    telemetry.close();assert(!telemetry.begin("hub.local",port));dispatch();assert(dns_calls==0);
    assert(telemetry.begin("hub.local",port));dispatch();assert(dns_calls==1&&telemetry.poll_connect()==0);
    telemetry.close();assert(!telemetry.begin("hub.local",port));resolve(true);assert(telemetry.poll_connect()==-1);
    assert(telemetry.begin("hub.local",port));dispatch();resolve(true);
    int connected=0;for(unsigned i=0;i<1000&&!connected;++i){connected=telemetry.poll_connect();if(!connected)std::this_thread::yield();}assert(connected==1);
    int peer=accept(server,nullptr,nullptr);assert(peer>=0);
    const std::uint8_t request[]={'h','e','l','l','o'};assert(telemetry.send(request,sizeof request)==5);
    char received[8];assert(recv(peer,received,5,0)==5&&!std::memcmp(received,request,5));
    std::uint8_t bytes[8];const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<1000;++i)assert(telemetry.receive(bytes,sizeof bytes)==PICO_HTTP_IO_AGAIN);
    assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
    assert(send(peer,"OK",2,0)==2);int n=-1;for(unsigned i=0;i<1000&&n<0;++i){n=telemetry.receive(bytes,sizeof bytes);if(n<0)std::this_thread::yield();}assert(n==2&&bytes[0]=='O');
    telemetry.close();close(peer);close(server);assert(telemetry.send(bytes,1)==PICO_HTTP_IO_ERROR);
    assert(telemetry.begin("hub.local",port));dispatch();resolve(false);assert(telemetry.poll_connect()==-1);telemetry.close();

    // Real loopback traffic exercises the incoming nonblocking callbacks.
    server=listener(port);close(server);HttpSocket http;assert(http.begin(port));const auto io=http.io();
    assert(io.accept(&http)==PICO_HTTP_IO_AGAIN);
    int client=socket(AF_INET,SOCK_STREAM,0);sockaddr_in target{};target.sin_family=AF_INET;target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);target.sin_port=htons(port);assert(connect(client,reinterpret_cast<sockaddr*>(&target),sizeof target)==0);
    int accepted=io.accept(&http);assert(accepted>=0);assert(io.receive(&http,accepted,bytes,sizeof bytes)==PICO_HTTP_IO_AGAIN);
    assert(send(client,"GET",3,0)==3);n=-1;for(unsigned i=0;i<1000&&n<0;++i){n=io.receive(&http,accepted,bytes,sizeof bytes);if(n<0)std::this_thread::yield();}assert(n==3&&!std::memcmp(bytes,"GET",3));
    assert(io.send(&http,accepted,request,5)==5);assert(recv(client,received,5,0)==5);
    io.close(&http,accepted);close(client);http.end();assert(io.accept(&http)==PICO_HTTP_IO_AGAIN);
}
