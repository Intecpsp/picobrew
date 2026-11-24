// Native NOR flash with interruptible erase/program operations; never opens USB.
#include "transfer.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

struct Flash {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(0x200000,0xff);
    int writes=0, cut=-1, prefix=0;
    bool powered=true, fail_record=false, cut_after_select=false;
    std::unique_ptr<picoferm::Update> app;
    unsigned running=2;
    static int read(void* p,uint32_t a,void* dest,uint32_t n) {
        auto& f=*static_cast<Flash*>(p);
        if (f.fail_record && (a==0x1000 || a==0x2000)) {f.fail_record=false;return 1;}
        if (!f.powered || a%4 || n%4 || a>f.bytes.size() || n>f.bytes.size()-a) return 1;
        std::memcpy(dest,f.bytes.data()+a,n);return 0;
    }
    uint32_t length(uint32_t size) {
        if (++writes!=cut) return size;
        powered=false;
        return std::min(size,static_cast<uint32_t>(prefix));
    }
    static int write(void* p,uint32_t a,const void* v,uint32_t n) {
        auto& f=*static_cast<Flash*>(p);
        if (!f.powered || a%4 || n%4 || a>f.bytes.size() || n>f.bytes.size()-a) return 1;
        auto* source=static_cast<const uint8_t*>(v);uint32_t until=f.length(n);
        for (uint32_t i=0;i<until;++i) f.bytes[a+i]&=source[i];
        return f.powered?0:1;
    }
    static bool erase(void* p,unsigned sector) {
        auto& f=*static_cast<Flash*>(p);
        if (!f.powered || sector>=f.bytes.size()/FERM_SECTOR) return false;
        auto n=f.length(FERM_SECTOR);
        std::fill_n(f.bytes.begin()+sector*FERM_SECTOR,n,0xff);
        return f.powered;
    }
    static uint32_t random(void*) {return 0x9a85913u;}
    FermBootIO io(){return {this,read,write};}
};
extern "C" {
Flash* start(){return new Flash;}
void destroy(Flash* f){delete f;}
void seed(Flash* f,unsigned address,const uint8_t* src,unsigned n) {
    if (address>f->bytes.size() || n>f->bytes.size()-address) std::abort();
    std::memcpy(f->bytes.data()+address,src,n);
}
void copyout(Flash* f,unsigned address,uint8_t* to,unsigned n) {
    std::memcpy(to,f->bytes.data()+address,n);
}
void fault(Flash* f,int event,int prefix){f->cut=event;f->prefix=prefix;f->writes=0;}
int writes(Flash* f){return f->writes;}
int boot(Flash* f){
    f->powered=true;
    FermBootIO io=f->io(); int bank=ferm_select(&io);
    f->app.reset();
    f->fail_record=f->cut_after_select;
    f->cut_after_select=false;
    if (bank<0) return -1;
    f->running=static_cast<unsigned>(bank);
    f->app.reset(new picoferm::Update({io,Flash::erase,Flash::random}));
    const char* version=reinterpret_cast<char*>(f->bytes.data()+FERM_BANK_ADDRESS(bank)+16+13);
    return f->app->attach(bank,"aabbccddeeff",version,true)?bank:-2;
}
void fail_first_app_record(Flash* f){f->cut_after_select=true;}
int trial(Flash* f){return f->app && f->app->trial();}
int request(Flash* f,int post,const char* path,const char* data,int auth,int idle,char* result){
    PicoHttpRequest req{};req.method=post?PICO_HTTP_POST:PICO_HTTP_GET;req.authenticated=auth;req.auth_configured=true;
    std::snprintf(req.path,sizeof(req.path),"/api/v1/firmware/ota%s",path);
    req.body=data;req.body_length=std::strlen(data);
    PicoHttpResponse res{};
    return f->app && f->app->handle(req,res,result,4096,idle)?res.status:500;
}
}
