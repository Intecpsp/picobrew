#include "telemetry.hpp"
extern "C" {
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
}
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using picobrew::standalone::esp8266::AsyncTelemetry;
namespace {
unsigned checks=0, dns_calls=0, tcp_calls=0, copied_writes=0, packets_created=0, packets_freed=0;
void require(bool ok,const char* message) {
    ++checks;if(!ok){std::fprintf(stderr,"async telemetry: %s\n",message);std::exit(1);}
}
struct Dns { dns_found_callback callback;void* context;std::string host; };
struct Mock {
    std::vector<Dns> queries;
    std::vector<tcp_pcb*> connections;
    std::vector<std::uint8_t> written;
    err_t dns_result=ERR_INPROGRESS,connect_result=ERR_OK,write_result=ERR_OK,output_result=ERR_OK,close_result=ERR_OK;
    bool allocation_failure=false,copy_failure=false;
    unsigned output_calls=0,abort_calls=0,close_calls=0,acknowledged=0;
    tcp_pcb* latest() { require(!connections.empty(),"active connection exists");return connections.back(); }
    void erase(tcp_pcb* pcb) {
        const auto it=std::find(connections.begin(),connections.end(),pcb);
        require(it!=connections.end(),"SDK releases each PCB once");connections.erase(it);delete pcb;
    }
    void resolved(std::size_t index,bool ok=true) {
        require(index<queries.size(),"pending DNS query exists");Dns query=queries[index];queries.erase(queries.begin()+index);
        ip_addr_t address;address.addr=0x04030201u;query.callback(query.host.c_str(),ok?&address:nullptr,query.context);
    }
    void connected(err_t error=ERR_OK) {
        tcp_pcb* pcb=latest();tcp_connected_fn callback=pcb->connected;
        require(callback!=nullptr,"connection callback bound");
        const err_t result=callback(pcb->callback_arg,pcb,error);
        require(result==(error==ERR_OK?ERR_OK:ERR_ABRT),"connection callback abort convention");
    }
    void failed() {
        tcp_pcb* pcb=latest();tcp_err_fn callback=pcb->errf;void* context=pcb->callback_arg;
        require(callback!=nullptr,"error callback bound");erase(pcb);callback(context,ERR_RST);
    }
    err_t receive(pbuf* packet,err_t error=ERR_OK) {
        tcp_pcb* pcb=latest();require(pcb->recv!=nullptr,"receive callback bound");
        return pcb->recv(pcb->callback_arg,pcb,packet,error);
    }
    void reset_results() {
        dns_result=ERR_INPROGRESS;connect_result=write_result=output_result=close_result=ERR_OK;
        allocation_failure=copy_failure=false;written.clear();acknowledged=0;
    }
} sdk;
pbuf* packet(const std::string& text,std::size_t split) {
    require(text.size()<=65535,"test packet bounded");
    pbuf* head=new pbuf;std::memset(head,0,sizeof(*head));
    const std::size_t first=std::min(split,text.size());
    head->len=static_cast<u16_t>(first);head->tot_len=static_cast<u16_t>(text.size());head->ref=1;
    head->payload=new std::uint8_t[first?first:1];std::memcpy(head->payload,text.data(),first);++packets_created;
    if(first<text.size())head->next=packet(text.substr(first),text.size());
    return head;
}
void establish(AsyncTelemetry& transport) {
    sdk.reset_results();require(transport.begin("192.0.2.15",8080),"literal begin");
    require(transport.poll_connect()==0,"handshake pending");sdk.connected();
    require(transport.poll_connect()==1,"handshake connected");
}
}

extern "C" {
int ipaddr_aton(const char* text,ip_addr_t* address) {
    if(!std::strcmp(text,"192.0.2.15")||!std::strcmp(text,"127.0.0.1")) {address->addr=1;return 1;}
    return 0;
}
err_t dns_gethostbyname(const char* hostname,ip_addr_t* address,dns_found_callback callback,void* context) {
    ++dns_calls;
    if(sdk.dns_result==ERR_OK)address->addr=1;
    if(sdk.dns_result==ERR_INPROGRESS)sdk.queries.push_back(Dns{callback,context,hostname});
    return sdk.dns_result;
}
tcp_pcb* tcp_new(void) {
    ++tcp_calls;if(sdk.allocation_failure)return nullptr;
    tcp_pcb* pcb=new tcp_pcb;std::memset(pcb,0,sizeof(*pcb));pcb->snd_buf=2920;sdk.connections.push_back(pcb);return pcb;
}
void tcp_arg(tcp_pcb* pcb,void* arg) {pcb->callback_arg=arg;}
void tcp_recv(tcp_pcb* pcb,tcp_recv_fn fn) {pcb->recv=fn;}
void tcp_sent(tcp_pcb* pcb,tcp_sent_fn fn) {pcb->sent=fn;}
void tcp_poll(tcp_pcb* pcb,tcp_poll_fn fn,u8_t interval) {pcb->poll=fn;pcb->pollinterval=interval;}
void tcp_err(tcp_pcb* pcb,tcp_err_fn fn) {pcb->errf=fn;}
err_t tcp_connect(tcp_pcb* pcb,ip_addr_t*,u16_t port,tcp_connected_fn callback) {
    ++tcp_calls;require(port!=0,"nonzero TCP port");pcb->connected=callback;return sdk.connect_result;
}
void tcp_abort(tcp_pcb* pcb) {
    ++sdk.abort_calls;require(!pcb->callback_arg&&!pcb->recv&&!pcb->errf&&!pcb->poll,"abort detaches callbacks");sdk.erase(pcb);
}
err_t tcp_close(tcp_pcb* pcb) {
    ++sdk.close_calls;require(!pcb->callback_arg&&!pcb->recv&&!pcb->errf&&!pcb->poll,"close detaches callbacks");
    if(sdk.close_result==ERR_OK)sdk.erase(pcb);
    return sdk.close_result;
}
void tcp_recved(tcp_pcb*,u16_t count) {sdk.acknowledged+=count;}
err_t tcp_write(tcp_pcb* pcb,const void* data,u16_t count,u8_t flags) {
    ++tcp_calls;require(flags==TCP_WRITE_FLAG_COPY,"send copies caller memory");
    require(count<=pcb->snd_buf&&count<=PICO_HTTP_IO_BUDGET,"bounded send count");
    if(sdk.write_result!=ERR_OK)return sdk.write_result;
    const std::uint8_t* bytes=static_cast<const std::uint8_t*>(data);
    sdk.written.insert(sdk.written.end(),bytes,bytes+count);pcb->snd_buf-=count;++copied_writes;return ERR_OK;
}
err_t tcp_output(tcp_pcb*) {++sdk.output_calls;return sdk.output_result;}
u8_t pbuf_free(pbuf* head) {
    u8_t count=0;
    while(head){pbuf* next=head->next;delete[] static_cast<std::uint8_t*>(head->payload);delete head;head=next;++count;++packets_freed;}
    return count;
}
u16_t pbuf_copy_partial(pbuf* head,void* destination,u16_t count,u16_t offset) {
    if(sdk.copy_failure)return 0;
    std::uint8_t* output=static_cast<std::uint8_t*>(destination);u16_t copied=0;
    while(head&&offset>=head->len){offset-=head->len;head=head->next;}
    while(head&&copied<count){const u16_t part=std::min<u16_t>(count-copied,head->len-offset);
        std::memcpy(output+copied,static_cast<const std::uint8_t*>(head->payload)+offset,part);
        copied+=part;offset=0;head=head->next;}
    return copied;
}
}

namespace {
void dns_and_lifetime() {
    AsyncTelemetry transport;require(transport.poll_connect()==-1,"idle is not connected");
    for(const char* host:{"","bad host","bad/host","bad\\host","user@host","a:b"})
        require(!transport.begin(host,80),"invalid hostname");
    require(!transport.begin(nullptr,80)&&!transport.begin("host",0),"null host or zero port");
    const std::string long_host(254,'x');require(!transport.begin(long_host.c_str(),80),"bounded host copy");
    const unsigned before=dns_calls;require(transport.begin("192.0.2.15",80),"numeric connect");
    require(dns_calls==before,"numeric address bypasses DNS pool");transport.close();
    sdk.dns_result=ERR_OK;require(transport.begin("cached.local",80),"immediate resolution");
    require(transport.poll_connect()==0,"cached DNS still waits for handshake");sdk.connected();transport.close();
    sdk.dns_result=ERR_ARG;require(!transport.begin("failed.local",80)&&transport.poll_connect()==-1,"DNS begin failure");
    sdk.dns_result=ERR_INPROGRESS;require(transport.begin("pending.local",80),"async DNS begin");
    require(transport.poll_connect()==0&&sdk.connections.empty(),"DNS pending does not connect early");
    sdk.resolved(0);require(transport.poll_connect()==0,"resolved connects asynchronously");sdk.connected();transport.close();
    require(transport.begin("timeout.local",80),"DNS timeout begin");sdk.resolved(0,false);
    require(transport.poll_connect()==-1,"DNS failure callback");
    require(transport.begin("old.local",80),"cancel old DNS");transport.close();
    require(transport.begin("new.local",80),"begin new DNS before old callback");sdk.resolved(0);
    require(transport.poll_connect()==0&&sdk.connections.empty(),"stale DNS cannot attach to new connection");
    sdk.resolved(0);sdk.connected();transport.close();
    AsyncTelemetry* destroyed=new AsyncTelemetry;require(destroyed->begin("destroyed.local",80),"destroyed DNS begin");
    delete destroyed;sdk.resolved(0);require(sdk.connections.empty(),"late DNS after owner destruction");
    for(unsigned n=0;n<DNS_TABLE_SIZE;++n){require(transport.begin("canceled.local",80),"reserve bounded DNS ticket");transport.close();}
    require(!transport.begin("exhausted.local",80),"exhausted DNS slots fail promptly");
    require(transport.begin("192.0.2.15",80),"numeric address works while DNS pool full");transport.close();
    while(!sdk.queries.empty())sdk.resolved(0,false);
    require(transport.begin("recovered.local",80),"completed canceled queries release tickets");sdk.resolved(0);transport.close();
    sdk.reset_results();sdk.allocation_failure=true;require(!transport.begin("192.0.2.15",80),"PCB allocation failure");
    sdk.reset_results();sdk.connect_result=ERR_RTE;require(!transport.begin("192.0.2.15",80),"immediate connect failure");
    sdk.reset_results();require(transport.begin("192.0.2.15",80),"handshake failure begins");sdk.failed();require(transport.poll_connect()==-1,"handshake error callback");
}
void send_and_backpressure() {
    AsyncTelemetry transport;establish(transport);std::vector<std::uint8_t> bytes(1300,0x5a);
    require(transport.send(bytes.data(),bytes.size())==512,"per-call write budget");bytes[0]=0;
    require(sdk.written.front()==0x5a,"caller buffer may change immediately after send");
    sdk.latest()->snd_buf=7;require(transport.send(bytes.data(),50)==7,"partial send capacity");
    require(transport.send(bytes.data(),50)==PICO_HTTP_IO_AGAIN,"full send buffer is pending");
    sdk.latest()->snd_buf=200;sdk.write_result=ERR_MEM;
    const std::size_t copied=sdk.written.size();require(transport.send(bytes.data(),100)==PICO_HTTP_IO_AGAIN,"allocation backpressure");
    require(sdk.written.size()==copied,"backpressure accepts no caller bytes");
    sdk.write_result=ERR_OK;sdk.output_result=ERR_MEM;
    require(transport.send(bytes.data(),100)==100,"queued bytes remain accepted if output is deferred");
    std::uint8_t output[32];const unsigned flushes=sdk.output_calls;
    require(transport.receive(output,sizeof(output))==PICO_HTTP_IO_AGAIN&&sdk.output_calls==flushes+1,"receive tick retries deferred output");
    sdk.output_result=ERR_OK;require(transport.poll_connect()==1,"transient output failure recovers");
    require(transport.send(nullptr,0)==0&&transport.send(nullptr,1)==PICO_HTTP_IO_ERROR,"send pointer boundary");
    sdk.write_result=ERR_CONN;require(transport.send(bytes.data(),1)==PICO_HTTP_IO_ERROR,"fatal write fails");
    require(transport.poll_connect()==-1&&sdk.connections.empty(),"fatal write releases PCB");
    establish(transport);sdk.output_result=ERR_RTE;require(transport.send(bytes.data(),1)==PICO_HTTP_IO_ERROR,"fatal output fails");
    require(sdk.connections.empty(),"fatal output aborts");
    establish(transport);sdk.output_result=ERR_RTE;
    tcp_pcb* pcb=sdk.latest();require(pcb->poll(pcb->callback_arg,pcb)==ERR_ABRT,"poll abort return convention");
    establish(transport);sdk.close_result=ERR_MEM;const unsigned aborts=sdk.abort_calls;transport.close();
    require(sdk.abort_calls==aborts+1&&sdk.connections.empty(),"close backpressure aborts without waiting");
    sdk.reset_results();
}
void receive_ownership() {
    AsyncTelemetry transport;establish(transport);std::uint8_t output[1024];
    require(transport.receive(output,sizeof(output))==PICO_HTTP_IO_AGAIN,"empty socket is pending");
    const std::string text="HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\ndone";
    require(sdk.receive(packet(text,13))==ERR_OK,"retain pbuf chain");
    require(sdk.acknowledged==0,"receive callback defers window acknowledgement");
    pbuf* second=packet("next",2);require(sdk.receive(second)==ERR_MEM,"occupied receive buffer exerts backpressure");
    require(transport.receive(output,7)==7&&!std::memcmp(output,text.data(),7),"partial receive first fragment");
    require(sdk.acknowledged==7,"acknowledge consumed bytes only");
    require(sdk.receive(nullptr)==ERR_OK,"peer FIN recorded");
    require(transport.receive(output,sizeof(output))==static_cast<int>(text.size()-7),"pending data drains before EOF");
    require(!std::memcmp(output,text.data()+7,text.size()-7),"chain bytes exact");
    require(transport.receive(output,sizeof(output))==0,"EOF after all pending data");
    require(transport.send(output,1)==PICO_HTTP_IO_ERROR,"send after peer FIN rejected");
    pbuf_free(second);transport.close();
    establish(transport);const std::string large(1300,'z');
    require(sdk.receive(packet(large,777))==ERR_OK,"large chain");
    require(transport.receive(output,sizeof(output))==512,"receive budget");
    require(transport.receive(output,sizeof(output))==512,"second receive budget");
    require(transport.receive(output,sizeof(output))==276,"receive tail");
    second=packet("redelivered",4);require(sdk.receive(second)==ERR_OK,"SDK may redeliver after consumption");
    require(transport.receive(output,sizeof(output))==11&&!std::memcmp(output,"redelivered",11),"redelivered bytes");
    require(transport.receive(nullptr,1)==PICO_HTTP_IO_ERROR&&transport.receive(nullptr,0)==PICO_HTTP_IO_AGAIN,"receive pointer boundary");
    require(sdk.receive(packet("retained",3))==ERR_OK,"retained packet before SDK error");sdk.failed();
    require(transport.receive(output,sizeof(output))==PICO_HTTP_IO_ERROR,"reset frees owned input");
    establish(transport);require(sdk.receive(packet("bad",1),ERR_BUF)==ERR_ABRT,"receive error abort convention");
    establish(transport);require(sdk.receive(packet("short",2))==ERR_OK,"copy fault packet");sdk.copy_failure=true;
    require(transport.receive(output,sizeof(output))==PICO_HTTP_IO_ERROR,"short pbuf copy rejected");sdk.copy_failure=false;
    establish(transport);require(sdk.receive(packet("close",2))==ERR_OK,"pending close packet");transport.close();
    require(packets_created==packets_freed,"all accepted/refused packet chains have one owner");
    auto binding=transport.binding();require(binding.begin(binding.context,"192.0.2.15",80),"public binding begin");sdk.connected();
    require(binding.poll_connect(binding.context)==1,"public binding poll");binding.close(binding.context);
}
}

int main() {
    dns_and_lifetime();send_and_backpressure();receive_ownership();
    require(sdk.connections.empty()&&sdk.queries.empty(),"no retained SDK state");
    std::printf("{\"checks\":%u,\"dns_calls\":%u,\"tcp_calls\":%u,\"copied_writes\":%u,\"pbuf_nodes_created\":%u,\"pbuf_nodes_freed\":%u,\"pointer_bytes\":%zu,\"long_bytes\":%zu,\"passed\":true}\n",checks,dns_calls,tcp_calls,copied_writes,packets_created,packets_freed,sizeof(void*),sizeof(long));
}
