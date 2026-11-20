#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <climits>
namespace picobrew { namespace standalone { namespace detail {
struct Json {
    const char* p;const char* end;bool ok=true;
    Json(const char* bytes,std::size_t length):p(bytes?bytes:""),end(p+length){}
    void space(){while(p<end&&(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'))++p;}
    bool token(char c){space();if(!ok||p==end||*p!=c)return ok=false;++p;return true;}
    bool peek(char c){space();return ok&&p<end&&*p==c;}
    bool next(char end_token){if(peek(end_token))return false;return token(',');}
    bool done(){space();return ok&&p==end;}
    static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
    bool string(char* out,std::size_t capacity){
        if(!out||!capacity||!token('"'))return ok=false;
        std::size_t count=0;
        while(p<end){unsigned c=static_cast<unsigned char>(*p++);if(c=='"'){out[count]=0;return true;}
            if(c=='\\'){if(p==end)break;c=static_cast<unsigned char>(*p++);if(c=='u'){if(end-p<4)break;unsigned value=0;for(unsigned k=0;k<4;++k){int digit=hex(*p++);if(digit<0)return ok=false;value=value*16+digit;}c=value;}else if(c!='"'&&c!='\\'&&c!='/')break;}
            if(c<32||c>126||count+1>=capacity)break;
            out[count++]=static_cast<char>(c);
        }return ok=false;
    }
    bool key(char (&name)[40]){return string(name,sizeof(name))&&token(':');}
    bool boolean(bool& value){space();if(end-p>=4&&!std::memcmp(p,"true",4)){p+=4;value=true;return true;}if(end-p>=5&&!std::memcmp(p,"false",5)){p+=5;value=false;return true;}return ok=false;}
    bool integer(std::int64_t minimum,std::int64_t maximum,std::int64_t& value){
        space();if(!ok||p==end)return ok=false;bool negative=*p=='-';if(negative)++p;if(p==end||*p<'0'||*p>'9')return ok=false;
        bool zero=*p=='0';std::uint64_t n=0;unsigned digits=0;
        while(p<end&&*p>='0'&&*p<='9'){unsigned digit=*p++-'0';if(++digits>10||(zero&&digits>1)||n>(UINT32_MAX-digit)/10u)return ok=false;n=n*10+digit;}
        value=negative?-static_cast<std::int64_t>(n):static_cast<std::int64_t>(n);if(value<minimum||value>maximum)return ok=false;return true;
    }
    bool number(double& value){
        space();const char* first=p;if(p<end&&*p=='-')++p;if(p==end||*p<'0'||*p>'9')return ok=false;
        if(*p=='0')++p;else while(p<end&&*p>='0'&&*p<='9')++p;
        if(p<end&&*p=='.'){++p;const char* digits=p;while(p<end&&*p>='0'&&*p<='9')++p;if(p==digits)return ok=false;}
        if(p<end&&(*p=='e'||*p=='E')){++p;if(p<end&&(*p=='+'||*p=='-'))++p;const char* digits=p;while(p<end&&*p>='0'&&*p<='9')++p;if(p==digits)return ok=false;}
        const std::size_t length=p-first;if(length>=64)return ok=false;char text[64];std::memcpy(text,first,length);text[length]=0;char* finish=nullptr;value=std::strtod(text,&finish);return ok=ok&&finish==text+length&&std::isfinite(value);
    }
};
struct Writer {
    char* data;std::size_t length=0,capacity;bool ok=true;bool counting=false;
    Writer():data(nullptr),capacity(SIZE_MAX),counting(true){}
    Writer(char* bytes,std::size_t size):data(bytes),capacity(size){ok=bytes&&size;}
    void append(const char* text,std::size_t size){if(!ok||size>=capacity-length){ok=false;return;}if(!counting)std::memcpy(data+length,text,size);length+=size;if(!counting)data[length]=0;}
    void text(const char* value){append(value,std::strlen(value));}
    void format(const char* pattern,...){if(!ok)return;va_list args;va_start(args,pattern);int size=::vsnprintf(counting?nullptr:data+length,counting?0:capacity-length,pattern,args);va_end(args);if(size<0||static_cast<std::size_t>(size)>=capacity-length)ok=false;else length+=size;}
    void quote(const char* value){static const char hex[]="0123456789abcdef";text("\"");for(;value&&*value;++value){unsigned c=static_cast<unsigned char>(*value);if(c=='"'||c=='\\'){char b[2]={'\\',static_cast<char>(c)};append(b,2);}else if(c<32||c>126){char b[6]={'\\','u','0','0',hex[c>>4],hex[c&15]};append(b,6);}else{char b=static_cast<char>(c);append(&b,1);}}text("\"");}
    void number(double value){if(!std::isfinite(value)||value<=-1e12||value>=1e12){text("null");return;}const bool negative=value<0;if(negative)value=-value;const std::uint64_t n=static_cast<std::uint64_t>(value*1000.0+0.5);format("%s%llu.%03u",negative?"-":"",static_cast<unsigned long long>(n/1000),static_cast<unsigned>(n%1000));}
    void boolean(bool value){text(value?"true":"false");}
    void reset(){length=0;ok=counting||(data&&capacity);if(ok&&!counting)data[0]=0;}
};
struct Bytes {std::uint8_t* data;std::size_t size;explicit Bytes(std::size_t n):data(static_cast<std::uint8_t*>(std::calloc(n,1))),size(n){}~Bytes(){std::free(data);}Bytes(const Bytes&)=delete;Bytes&operator=(const Bytes&)=delete;};
inline void copy(char* output,std::size_t capacity,const char* input){if(!capacity)return;::snprintf(output,capacity,"%s",input?input:"");}
inline void put32(std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(8*i));}
inline std::uint32_t get32(const std::uint8_t* p){return std::uint32_t(p[0])|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;}
inline void put64(std::uint8_t* p,std::uint64_t v){put32(p,v);put32(p+4,v>>32);}
inline std::uint64_t get64(const std::uint8_t* p){return get32(p)|(std::uint64_t(get32(p+4))<<32);}
inline bool decimal_path(const char*& p,std::uint32_t& out){if(*p<'0'||*p>'9'||(*p=='0'&&p[1]>='0'&&p[1]<='9'))return false;out=0;do{unsigned d=*p++-'0';if(out>(UINT32_MAX-d)/10u)return false;out=out*10+d;}while(*p>='0'&&*p<='9');return true;}
}}}
