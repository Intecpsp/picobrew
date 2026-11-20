#include "http.hpp"
extern "C" {
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
}
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using picobrew::standalone::esp8266::HttpListener;
namespace {
unsigned checks=0,created=0,freed=0;
void require(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"ESP8266 HTTP: %s\n",message);std::exit(1);}}
struct Sdk {
    std::vector<tcp_pcb*> connections;
    tcp_pcb* listener=nullptr;
    tcp_accept_fn on_accept=nullptr;
    void* accept_context=nullptr;
    std::string sent;
    unsigned acknowledged=0,aborted=0,outputs=0;
    bool allocation_failure=false,listen_failure=false,copy_failure=false;
    err_t bind_result=ERR_OK,write_result=ERR_OK,output_result=ERR_OK,close_result=ERR_OK;
    tcp_pcb* client(){for(auto* p:connections)if(p!=listener)return p;return nullptr;}
    void erase(tcp_pcb* p){const auto it=std::find(connections.begin(),connections.end(),p);require(it!=connections.end(),"one PCB release");connections.erase(it);delete p;}
    tcp_pcb* connect(){tcp_pcb* p=tcp_new();require(p&&on_accept,"listener accepts test connection");
        const auto result=on_accept(accept_context,p,ERR_OK);return result==ERR_OK?p:nullptr;}
    err_t receive(pbuf* packet,err_t error=ERR_OK){tcp_pcb* p=client();require(p&&p->recv,"receive callback bound");return p->recv(p->callback_arg,p,packet,error);}
    void fail(){tcp_pcb* p=client();require(p&&p->errf,"error callback bound");const auto callback=p->errf;void* context=p->callback_arg;erase(p);callback(context,ERR_RST);}
} sdk;
pbuf* packet(const std::string& text,std::size_t split){
    pbuf* p=new pbuf;std::memset(p,0,sizeof(*p));const auto count=std::min(split,text.size());
    p->ref=1;p->len=static_cast<u16_t>(count);p->tot_len=static_cast<u16_t>(text.size());p->payload=new char[count?count:1];std::memcpy(p->payload,text.data(),count);++created;
    if(count<text.size())p->next=packet(text.substr(count),text.size());
    return p;
}
}
extern "C" {
const ip_addr_t ip_addr_any={0};
tcp_pcb* tcp_new(void){if(sdk.allocation_failure)return nullptr;auto* p=new tcp_pcb;std::memset(p,0,sizeof(*p));p->snd_buf=8192;sdk.connections.push_back(p);return p;}
void tcp_arg(tcp_pcb* p,void* value){p->callback_arg=value;if(p==sdk.listener)sdk.accept_context=value;}
void tcp_recv(tcp_pcb* p,tcp_recv_fn value){p->recv=value;}
void tcp_sent(tcp_pcb* p,tcp_sent_fn value){p->sent=value;}
void tcp_poll(tcp_pcb* p,tcp_poll_fn value,u8_t interval){p->poll=value;p->pollinterval=interval;}
void tcp_err(tcp_pcb* p,tcp_err_fn value){p->errf=value;}
void tcp_accept(tcp_pcb* p,tcp_accept_fn value){require(p==sdk.listener,"accept belongs to listener");sdk.on_accept=value;}
err_t tcp_bind(tcp_pcb*,ip_addr_t*,u16_t port){require(port!=0,"bind valid port");return sdk.bind_result;}
tcp_pcb* tcp_listen_with_backlog(tcp_pcb* p,u8_t backlog){require(backlog==1,"one connection backlog");if(sdk.listen_failure)return nullptr;p->state=LISTEN;sdk.listener=p;return p;}
void tcp_abort(tcp_pcb* p){require(!p->callback_arg&&!p->recv&&!p->poll&&!p->errf,"abort callbacks detached");++sdk.aborted;sdk.erase(p);}
err_t tcp_close(tcp_pcb* p){require(!p->callback_arg,"close context detached");if(p==sdk.listener){require(!sdk.on_accept,"listener callback detached");sdk.listener=nullptr;sdk.erase(p);return ERR_OK;}
    if(sdk.close_result==ERR_OK)sdk.erase(p);
    return sdk.close_result;}
void tcp_recved(tcp_pcb*,u16_t amount){sdk.acknowledged+=amount;}
err_t tcp_write(tcp_pcb* p,const void* data,u16_t size,u8_t flags){require(flags==TCP_WRITE_FLAG_COPY&&size<=p->snd_buf&&size<=PICO_HTTP_IO_BUDGET,"copied bounded sends");
    if(sdk.write_result!=ERR_OK)return sdk.write_result;
    sdk.sent.append(static_cast<const char*>(data),size);p->snd_buf-=size;return ERR_OK;}
err_t tcp_output(tcp_pcb*){++sdk.outputs;return sdk.output_result;}
u8_t pbuf_free(pbuf* p){u8_t count=0;while(p){pbuf* next=p->next;delete[] static_cast<char*>(p->payload);delete p;p=next;++count;++freed;}return count;}
u16_t pbuf_copy_partial(pbuf* p,void* destination,u16_t amount,u16_t offset){if(sdk.copy_failure)return 0;
    auto* out=static_cast<char*>(destination);u16_t count=0;
    while(p&&offset>=p->len){offset-=p->len;p=p->next;}
    while(p&&count<amount){const auto part=std::min<u16_t>(amount-count,p->len-offset);std::memcpy(out+count,static_cast<char*>(p->payload)+offset,part);count+=part;offset=0;p=p->next;}return count;}
}
namespace {
void lifecycle(){
    HttpListener listener;require(!listener.listening()&&listener.accept()==PICO_HTTP_IO_AGAIN&&sdk.connections.empty(),"construction has no network I/O");
    require(!listener.begin(0),"zero port rejected");sdk.allocation_failure=true;require(!listener.begin(),"allocation failure returns");sdk.allocation_failure=false;
    sdk.bind_result=ERR_USE;require(!listener.begin()&&sdk.connections.empty(),"bind failure releases PCB");sdk.bind_result=ERR_OK;
    sdk.listen_failure=true;require(!listener.begin()&&sdk.connections.empty(),"listen allocation failure releases PCB");sdk.listen_failure=false;
    require(listener.begin()&&listener.listening(),"listener starts");require(listener.begin(),"repeat begin does not rebind");
    require(sdk.connect()!=nullptr,"first peer accepted");require(sdk.connect()==nullptr,"second peer rejected without queued owner");
    const int fd=listener.accept();require(fd>=0&&listener.accept()==PICO_HTTP_IO_AGAIN,"descriptor taken once");
    char bytes[16];require(listener.receive(fd,reinterpret_cast<std::uint8_t*>(bytes),sizeof(bytes))==PICO_HTTP_IO_AGAIN,"empty socket nonblocking");
    sdk.receive(packet("abcdefghi",3));require(sdk.acknowledged==0,"TCP window is not acked before consumption");
    pbuf* refused=packet("later",2);require(sdk.receive(refused)==ERR_MEM,"one chain receive backpressure");
    require(listener.receive(fd,reinterpret_cast<std::uint8_t*>(bytes),4)==4&&!std::memcmp(bytes,"abcd",4)&&sdk.acknowledged==4,"partial chained read and exact ack");
    sdk.receive(nullptr);require(listener.receive(fd,reinterpret_cast<std::uint8_t*>(bytes),16)==5&&!std::memcmp(bytes,"efghi",5),"FIN drains remaining bytes");
    require(listener.receive(fd,reinterpret_cast<std::uint8_t*>(bytes),16)==0,"EOF follows bytes");
    require(listener.send(fd,reinterpret_cast<const std::uint8_t*>("response"),8)==8,"half-closed request can still receive response");
    pbuf_free(refused);listener.close(fd);require(!sdk.client(),"close releases connection");
    require(sdk.connect()!=nullptr,"next client accepted");const int next=listener.accept();require(next!=fd,"descriptor generation changes");listener.close(fd);
    require(sdk.client()!=nullptr&&listener.send(fd,reinterpret_cast<const std::uint8_t*>("x"),1)==PICO_HTTP_IO_ERROR,"stale descriptor cannot affect replacement");
    sdk.fail();require(sdk.connect()==nullptr,"failed active descriptor remains reserved until server closes");
    require(listener.receive(next,reinterpret_cast<std::uint8_t*>(bytes),16)==PICO_HTTP_IO_ERROR,"SDK free reported as I/O error");listener.close(next);
    require(sdk.connect()!=nullptr,"next peer after error cleanup");listener.end();require(sdk.connections.empty()&&!listener.listening(),"end frees pending peer and listener");
}
void backpressure_and_errors(){
    HttpListener listener;require(listener.begin(),"backpressure listener");sdk.connect();const int fd=listener.accept();std::vector<std::uint8_t> bytes(2000,0x55);
    sdk.sent.clear();require(listener.send(fd,bytes.data(),bytes.size())==512,"send call budget enforced");
    sdk.client()->snd_buf=7;require(listener.send(fd,bytes.data(),50)==7,"partial TCP capacity");
    require(listener.send(fd,bytes.data(),50)==PICO_HTTP_IO_AGAIN,"full send window does not block");
    sdk.client()->snd_buf=100;sdk.write_result=ERR_MEM;const auto count=sdk.sent.size();require(listener.send(fd,bytes.data(),50)==PICO_HTTP_IO_AGAIN&&sdk.sent.size()==count,"allocation backpressure accepts nothing");
    sdk.write_result=ERR_OK;sdk.output_result=ERR_MEM;require(listener.send(fd,bytes.data(),50)==50,"queued bytes not duplicated after output deferral");
    sdk.output_result=ERR_OK;sdk.receive(packet(std::string(700,'x'),100));std::uint8_t received[900];require(listener.receive(fd,received,sizeof(received))==512,"read call budget enforced");
    sdk.copy_failure=true;require(listener.receive(fd,received,sizeof(received))==PICO_HTTP_IO_ERROR,"short pbuf copy aborts");sdk.copy_failure=false;listener.close(fd);
    sdk.connect();int next=listener.accept();sdk.receive(packet("request",3),ERR_RST);require(listener.receive(next,received,1)==PICO_HTTP_IO_ERROR,"receive error frees pbuf and PCB");listener.close(next);
    sdk.connect();next=listener.accept();sdk.close_result=ERR_MEM;const auto aborted=sdk.aborted;listener.close(next);require(sdk.aborted==aborted+1,"close backpressure aborts promptly");sdk.close_result=ERR_OK;
    sdk.connect();next=listener.accept();require(sdk.receive(packet(std::string(6000,'a'),1000))==ERR_ABRT,"oversized chain rejected");listener.close(next);
    sdk.connect();next=listener.accept();sdk.output_result=ERR_RTE;tcp_pcb* active=sdk.client();require(active->poll(active->callback_arg,active)==ERR_ABRT,"SDK poll abort convention");sdk.output_result=ERR_OK;listener.close(next);listener.end();
}
void real_http_transport(){
    HttpListener listener;require(listener.begin(),"actual HTTP listener");PicoHttpServer server;unsigned commands=0;
    pico_http_server_initialize(&server,&HttpListener::io,&listener,
        [](const PicoHttpRequest* request,PicoHttpResponse* response,char* buffer,std::size_t,void* context){
            if(!request->authenticated)return false;
            ++*static_cast<unsigned*>(context);std::strcpy(buffer,"{\"accepted\":true}");response->status=202;response->content_type="application/json";
            response->body=reinterpret_cast<const std::uint8_t*>(buffer);response->body_length=std::strlen(buffer);return true;
        },&commands,nullptr,0);
    const std::string key(64,'b');require(pico_http_server_set_key(&server,key.c_str(),key.size()),"test key configured");
    auto request=[&](bool authorized){sdk.sent.clear();sdk.connect();std::string wire="POST /api/v1/commands HTTP/1.1\r\nHost: picoferm.local\r\nContent-Type: application/json\r\nContent-Length: 18\r\n";
        if(authorized)wire+="Authorization: Bearer "+key+"\r\n";
        wire+="\r\n{\"command\":\"stop\"}";sdk.receive(packet(wire,19));sdk.receive(nullptr);
        for(unsigned i=0;i<100&&sdk.client();++i)pico_http_server_poll(&server,i*5);
        require(!sdk.client(),"actual transport closes without blocking");};
    request(false);require(commands==0&&sdk.sent.find("401")!=std::string::npos,"unauthenticated stop never reaches handler");
    request(true);require(commands==1&&sdk.sent.find("202")!=std::string::npos&&sdk.sent.find("accepted")!=std::string::npos,"authenticated fragmented half-close reaches handler exactly once");
    pico_http_server_disconnect(&server);listener.end();
}
}
int main(){lifecycle();backpressure_and_errors();real_http_transport();require(sdk.connections.empty()&&created==freed,"all callback-owned memory released");std::printf("{\"checks\":%u,\"pbufs\":%u}\n",checks,created);}
