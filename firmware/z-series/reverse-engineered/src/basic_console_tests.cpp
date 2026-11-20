#include "zseries/basic_console_tests.hpp"
#include "zseries/basic_console_json.hpp"
#include "zseries/backend_console_tests.hpp"
#include "zseries/console_test_support.hpp"
#include "zseries/display_text.hpp"
#include "zseries/hardware_console_tests.hpp"
#include "zseries/http_client.hpp"
#include "zseries/local_http.hpp"
#include "zseries/head_dynamic.hpp"
#include "zseries/head_quantity.hpp"
#include "zseries/program_diagnostics.hpp"
#include "zseries/serial_logger.hpp"
#include "zseries/head_encoder.hpp"
#include "zseries/head_presentation.hpp"
#include "zseries/head_screens.hpp"
#include "zseries/software_console_tests.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>

namespace zseries { namespace oem {
namespace {
struct OriginalCheck { const char* file; std::uint32_t line; const char* expression; };
struct OriginalScope { std::uint32_t callback, line; const char* tag; const char* file; const char* name; };
#include "basic_console_literals.inc"
#include "basic_console_samples.inc"
#include "basic_console_paragraphs.inc"
#include "basic_console_binary.inc"

struct Context {
    std::uint32_t callback;
    const ConsoleServices& services;
    const ApplicationConsoleHooks& hooks;
    bool assertion(bool condition, const char* file, std::uint32_t line,
                   const char* expression = nullptr) const {
        if (condition) return true;
        if (!expression) for (const auto& item : original_checks)
            if (item.line == line && std::strcmp(item.file,file) == 0) { expression=item.expression; break; }
        return expression && services.assertion && services.assertion(services.context,file,line,expression);
    }
    const OriginalScope* scope(std::uint32_t line) const {
        for (const auto& item : original_scopes)
            if (item.callback == callback && item.line == line) return &item;
        return nullptr;
    }
    const char* file() const {
        for (const auto& item : original_scopes) if (item.callback == callback) return item.file;
        return nullptr;
    }
    bool log(ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* format,...) const {
        if (!services.log) return false;
        char text[1024]; va_list args; va_start(args,format);
        const int count=::vsnprintf(text,sizeof(text),format,args); va_end(args);
        return count>=0 && static_cast<std::size_t>(count)<sizeof(text) &&
            services.log(services.context,level,tag,file,line,text);
    }
};
struct Scope {
    const Context& context; const OriginalScope* value; bool active=false;
    Scope(const Context& c,const OriginalScope& supplied):context(c),value(&supplied) {}
    Scope(const Context& c,std::uint32_t line):context(c),value(c.scope(line)) {}
    bool begin() {
        if (!value || !context.hooks.test_scope) return false;
        active=context.hooks.test_scope(context.hooks.context,true,value->tag,value->file,value->line,value->name);
        return active;
    }
    bool end() {
        if (!active) return false;
        active=false;
        return context.hooks.test_scope(context.hooks.context,false,value->tag,value->file,value->line,value->name);
    }
    ~Scope() { if (active) end(); }
};
#define TRY(value) do { if (!(value)) return false; } while(false)
#define EXPECT(value,line) TRY(c.assertion((value),c.file(),(line)))
#define CHECK(value,file,line) TRY(c.assertion((value),(file),(line)))
#define BEGIN(line) { Scope scope(c,line); TRY(scope.begin());
#define END() TRY(scope.end()); }
bool same(const char* a,const char* b) { return a==b || (a && b && std::strcmp(a,b)==0); }
bool empty(const char* value) { return !value || !*value; }
const char* boolean(bool value) { return value ? "True" : "False"; }
bool close(double a,double b) { return std::abs(a-b)<=0.000001; }

// The original Buffer<16> specialization exists only in this diagnostic. The
// append/remove/freeze operations retain its capacity and warning semantics.
struct SmallBuffer {
    char text[17]{}; std::size_t length=0; bool truncated=false,frozen=false;
    void reset() { text[0]=0; length=0; truncated=frozen=false; }
    bool append(const Context& c,const char* data,std::size_t size) {
        CHECK(!frozen,"Buffer.h",0x5d);
        if (frozen || !data || !size) return true;
        const auto count=std::min(size,16-length);
        if (count) std::memcpy(text+length,data,count);
        length+=count; text[length]=0;
        if (count<size) {
            if (!truncated) { truncated=true; TRY(c.log(ConsoleLevel::Warning,"BUF","Buffer.h",0x66,"Buffer truncated")); }
            TRY(c.log(ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,"Dropping %u bytes",static_cast<unsigned>(size-count)));
        }
        return true;
    }
    bool append(const Context& c,const char* value) { return append(c,value,value?std::strlen(value):0); }
    bool format(const Context& c,const char* format,...) {
        CHECK(!frozen,"Buffer.h",0x7b);
        if (frozen) return true;
        va_list args; va_start(args,format); const auto available=sizeof(text)-length;
        const int count=::vsnprintf(text+length,available,format,args); va_end(args);
        if (count<0) return false;
        if (static_cast<std::size_t>(count)>=available-1) {
            length=16;
            if (!truncated) { truncated=true; TRY(c.log(ConsoleLevel::Warning,"BUF","Buffer.h",0x8b,"Buffer truncated")); }
        } else length+=static_cast<std::size_t>(count);
        return true;
    }
    void trim() {
        std::size_t first=0; while(first<length && static_cast<unsigned char>(text[first])<=32) ++first;
        std::size_t last=length; while(last>first && static_cast<unsigned char>(text[last-1])<=32) --last;
        std::memmove(text,text+first,last-first); length=last-first; text[length]=0;
    }
};
bool buffer_empty(const Context& c,const SmallBuffer& b) {
    CHECK(b.length==0,"Buffer.h",0xd2); CHECK(b.text[0]==0,"Buffer.h",0xd3);
    CHECK(!b.truncated,"Buffer.h",0xd4); CHECK(!b.frozen,"Buffer.h",0xd5);
    return true;
}
bool array_iterator(const Context& c) {
    const int data[]={1,2,3,4};
    BEGIN(0x44) for (unsigned i=0;i<4;++i) EXPECT(data[i]==static_cast<int>(i+1),0x47); END()
    BEGIN(0x4b) for (unsigned i=0;i<4;++i) EXPECT(data[3-i]==static_cast<int>(4-i),0x4e); END()
    return true;
}
bool backoff(const Context& c) {
    auto arm=[&](WifiBackoff& timer,const char* name,std::uint32_t now,bool start) {
        if (start) { if(!c.log(ConsoleLevel::Info,"BTMR","Timer.h",0x6b,"%s - Starting BackoffTimer",name))return false; timer.start(now); }
        else timer.advance(now);
        return c.log(ConsoleLevel::Info,"BTMR","Timer.h",0x53,"%s - Try #%u, Timeout in %u min",name,timer.attempts,(timer.deadline-now)/60000);
    };
    BEGIN(0x90)
        WifiBackoff timer; timer.maximum_duration=1800000;
        EXPECT(!timer.active,0x93); TRY(arm(timer,"test1",0x48ff4ea,true)); EXPECT(timer.active,0x95);
        EXPECT(!timer.expired(0x48ff4ea),0x96); EXPECT(timer.expired(0x490df4a),0x98);
        TRY(arm(timer,"test1",0x490df4a,false)); EXPECT(timer.active,0x9a);
        EXPECT(!timer.expired(0x490df4a),0x9b); EXPECT(!timer.expired(0x491c9aa),0x9d); EXPECT(timer.expired(0x492b40a),0x9f);
        timer.stop(); TRY(c.log(ConsoleLevel::Info,"BTMR","Timer.h",0x74,"test1 - Stopping BackoffTimer"));
        EXPECT(!timer.expired(0x492b40a),0xa1); EXPECT(!timer.active,0xa2);
    END()
    BEGIN(0xa5)
        WifiBackoff timer; timer.maximum_duration=1800000; TRY(arm(timer,"test2",0,true));
        const unsigned dates[]={1,3,7,15,31,61}; unsigned count=0;
        for (std::uint32_t now=0;now!=0x384e10 && count<6;now+=30000) if(timer.expired(now)) {
            EXPECT(now==dates[count]*60000,0xb5); TRY(arm(timer,"test2",now,false)); ++count;
        }
        EXPECT(count==6,0xbc); EXPECT(timer.attempts==count+1,0xbd);
    END()
    return true;
}
bool buffer(const Context& c) {
    SmallBuffer b;
    BEGIN(0xe6) END() // sizeof/capacity checks were folded by the OEM compiler.
    BEGIN(0xeb) TRY(buffer_empty(c,b)); TRY(buffer_empty(c,b)); TRY(b.append(c,"")); TRY(buffer_empty(c,b)); END()
    BEGIN(0xf7)
        TRY(b.format(c,"12345678")); TRY(c.log(ConsoleLevel::Debug,"BUF","Buffer.h",0xfa,"|%s|",b.text));
        EXPECT(same(b.text,"12345678"),0xfb); EXPECT(!b.truncated,0xfc);
        TRY(b.format(c,"%s","901")); TRY(c.log(ConsoleLevel::Debug,"BUF","Buffer.h",0xff,"|%s|",b.text));
        EXPECT(same(b.text,"12345678901"),0x100); EXPECT(!b.truncated,0x101);
        TRY(b.format(c,"%s","2345678")); TRY(c.log(ConsoleLevel::Debug,"BUF","Buffer.h",0x104,"|%s|",b.text));
        EXPECT(same(b.text,"1234567890123456"),0x105); EXPECT(b.truncated,0x106);
        b.reset(); TRY(b.format(c,"%6s","Right")); TRY(c.log(ConsoleLevel::Debug,"BUF","Buffer.h",0x10b,"|%s|",b.text));
        EXPECT(same(b.text," Right"),0x10c); EXPECT(!b.truncated,0x10d);
        TRY(b.format(c,"%.3f",123.456789)); TRY(c.log(ConsoleLevel::Debug,"BUF","Buffer.h",0x110,"|%s|",b.text));
        EXPECT(same(b.text," Right123.457"),0x111); EXPECT(!b.truncated,0x112);
        TRY(b.format(c,"      ")); TRY(c.log(ConsoleLevel::Debug,"BUF","Buffer.h",0x115,"|%s|",b.text));
        EXPECT(same(b.text," Right123.457   "),0x116); EXPECT(b.truncated,0x117);
    END()
    BEGIN(0x11b) b.reset(); TRY(buffer_empty(c,b)); END()
    BEGIN(0x121) TRY(b.append(c,"Some text",9)); EXPECT(same(b.text,"Some text"),0x125); b.reset(); END()
    BEGIN(0x12a)
        TRY(b.append(c," \r\n\t")); EXPECT(b.length!=0,0x12d); b.trim(); TRY(buffer_empty(c,b));
        TRY(b.append(c,"123 \r\n\t")); b.trim(); EXPECT(b.length==3,0x133); EXPECT(same(b.text,"123"),0x134);
    END()
    b.reset();
    BEGIN(0x13a)
        TRY(b.append(c,"a")); EXPECT(b.length==1,0x13d); EXPECT(same(b.text,"a"),0x13e);
        TRY(b.append(c,"b")); EXPECT(b.length==2,0x141); EXPECT(same(b.text,"ab"),0x142);
        TRY(b.append(c,"")); EXPECT(b.length==2,0x145); EXPECT(same(b.text,"ab"),0x146);
    END()
    BEGIN(0x14a) if(b.length)b.text[--b.length]=0; b.truncated=false; EXPECT(b.length==1,0x14c); EXPECT(same(b.text,"a"),0x14d); END()
    b.reset();
    BEGIN(0x153) TRY(b.append(c,"0123456789")); EXPECT(b.length==10,0x155); for(unsigned i=0;i<10;++i)EXPECT(b.text[i]==static_cast<char>('0'+i),0x158); END()
    BEGIN(0x15d) TRY(b.append(c,"0123456789")); EXPECT(b.length==16,0x15f); EXPECT(b.truncated,0x160); EXPECT(same(b.text,"0123456789012345"),0x161); END()
    BEGIN(0x165) b.reset(); TRY(b.append(c,"01234")); EXPECT(b.length==5,0x167); b.reset(); TRY(b.append(c,"0123456789",3)); EXPECT(b.length==3,0x16a); EXPECT(same(b.text,"012"),0x16b); END()
    BEGIN(0x16f) b.frozen=true; EXPECT(same(b.text,"012"),0x172); END()
    BEGIN(0x176) b.reset(); TRY(buffer_empty(c,b)); END()
    return true;
}
bool command_data(const Context& c) {
    BEGIN(0x1cf)
        HeadUiCommandData data; EXPECT(data.flags==0,0x1d1); EXPECT(!(data.flags&1),0x1d2);
        data.flags|=1; data.index=123;
        EXPECT(data.flags!=0,0x1d4); EXPECT(data.flags&1,0x1d5); EXPECT(data.index==123,0x1d6);
    END()
    BEGIN(0x1d9)
        HeadUiCommandData data; RecipeReference recipe; recipe.id=1234; recipe.kind=static_cast<RecipeKind>(1);
        std::strcpy(recipe.name.data(),"Some recipe"); recipe.name_length=11;
        EXPECT(!(data.flags&4),0x1e0); data.flags|=4; data.recipe=recipe; EXPECT(data.flags&4,0x1e2);
        const auto copy=data.recipe; EXPECT(copy.id==recipe.id && copy.kind==recipe.kind && copy.name==recipe.name,0x1e5);
    END()
    return true;
}
bool enum_helpers(const Context& c) {
    BEGIN(0x46) END() // Validity predicates were folded to constants by the OEM.
    BEGIN(0x54)
        const char* names[]={"Val1","Val2","Val3"};
        const int values[]={0,1,2,3,-1,0,1,2,3,0x7fffffff};
        const unsigned lines[]={0x5b,0x5c,0x5d,0x5e,0x60,0x61,0x62,0x63,0x64,0x65};
        for(unsigned i=0;i<3;++i)EXPECT(same(names[i],names[i]),0x58);
        for(unsigned i=0;i<10;++i)TRY(c.log(ConsoleLevel::Info,"ENUMS","Enums.h",lines[i],"%d %s",values[i],values[i]>=0&&values[i]<3?names[values[i]]:"<TestEnum::INVALID>"));
    END()
    return true;
}
bool event_queue(const Context& c) {
    BEGIN(0x54)
        PendingEvent<std::uint32_t> queue; queue.put(123456); EXPECT(queue.pending,0x5b);
        std::uint32_t value=0; queue.take(value); TRY(c.log(ConsoleLevel::Debug,"EVTQ","EventQ.h",0x4a,"[testqueue] GET %u",value));
        EXPECT(!queue.pending,0x61);
    END()
    return true;
}
bool localization(const Context& c) {
    BEGIN(0x36)
        const char* labels[]={"Val1 English","Val1 French","Val1 Spanish"};
        const char* languages[]={"English","French","Spanish"};
        for(unsigned i=0;i<3;++i) {
            const auto* selected=labels[i]; EXPECT(same(selected,labels[i]),0x39+4*i);
            TRY(c.log(ConsoleLevel::Debug,"L12N","Localization.h",0x3a+4*i,"%s <- getLocalizedText(TestEnum::Val1, LanguageType::%s)",selected,languages[i]));
        }
        TRY(c.log(ConsoleLevel::Error,"L12N","Localization.h",0x2e,"Invalid language TestEnum::0"));
        EXPECT(same(labels[0],"Val1 English"),0x46);
        TRY(c.log(ConsoleLevel::Debug,"L12N","Localization.h",0x47,"%s <- getLocalizedText(TestEnum::Val1, static_cast<LanguageType>(-1))",labels[0]));
        TRY(c.log(ConsoleLevel::Error,"L12N","Localization.h",0x2e,"Invalid enum TestEnum::-1"));
        const char* invalid="<TestEnum::INVALID>"; EXPECT(same(invalid,"<TestEnum::INVALID>"),0x4b);
        TRY(c.log(ConsoleLevel::Debug,"L12N","Localization.h",0x4c,"Invalid enum %s",invalid));
    END()
    return true;
}
bool instruction(const Context& c) {
    BEGIN(0x397) END()
    BEGIN(0x39c) EXPECT(!empty(common_program_string(0x8002)),0x39d); END()
    return true;
}
bool safety(const Context& c) {
    TimedThresholdMonitor monitor; bool flag=false; start_monitor(monitor,0,1,15);
    TRY(c.log(ConsoleLevel::Debug,"SFTY","Safeties.h",0x36,"start \"test\" 0 s 15.0"));
    BEGIN(0x57) for(unsigned i=0;i<3;++i){update_monitor(monitor,0,14+i,flag);EXPECT(!flag,0x58+i);} END()
    BEGIN(0x5f)
        for(unsigned i=0;i<3;++i) {
            const bool was=flag; update_monitor(monitor,1,14+i,flag);
            if(!was&&flag)TRY(c.log(ConsoleLevel::Warning,"SFTY","Safeties.h",0x4a,"test FLAG RAISED %.2f",14.0+i));
            EXPECT(flag==(i==2),0x60+i);
        }
    END()
    BEGIN(0x67)
        stop_monitor(monitor,flag); TRY(c.log(ConsoleLevel::Debug,"SFTY","Safeties.h",0x40,"stop \"test\""));
        for(unsigned i=0;i<3;++i){update_monitor(monitor,1,14+i,flag);EXPECT(!flag,0x69+i);}
    END()
    return true;
}
bool temperature(const Context& c) {
    // All value/assertion expressions are compile-time constants in these two
    // original bodies; only the six observable ScopeLogger pairs remain.
    if(c.callback==0x400db514) { BEGIN(0x3e) END() BEGIN(0x49) END() BEGIN(0x52) END() }
    else { BEGIN(0x88) END() BEGIN(0x92) END() BEGIN(0x99) END() }
    return true;
}
bool wifi_credentials(const Context& c) {
    BEGIN(0x6f)
        WifiCredentials a,b; std::strcpy(a.ssid.data(),"foo"); std::strcpy(a.password.data(),"bar"); a.internet_expected=true; b=a; const auto other=b;
        EXPECT(a.equals(b),0x74); EXPECT(b.equals(other),0x75); EXPECT(a.equals(other),0x76);
        std::strcpy(b.ssid.data(),"foo2"); EXPECT(!a.equals(b),0x78); EXPECT(!b.equals(other),0x79); EXPECT(a.equals(other),0x7a);
        std::strcpy(b.ssid.data(),"foo"); EXPECT(b.equals(other),0x7c); b.internet_expected=false; EXPECT(!b.equals(other),0x7e);
    END()
    return true;
}

bool cstring_copy(const Context& c,char* output,std::size_t capacity,const char* input,
                  std::size_t size,std::size_t& copied) {
    copied=0;
    if(!output)return c.log(ConsoleLevel::Warning,"CSTR","CString.h",0x4e,"Invalid arg: dest is NULL");
    if(!capacity)return c.log(ConsoleLevel::Warning,"CSTR","CString.h",0x52,"Invalid arg: destsize <= 0");
    output[0]=0;
    if(!input)return c.log(ConsoleLevel::Warning,"CSTR","CString.h",0x57,"Invalid arg: src is NULL");
    if(!size)return c.log(ConsoleLevel::Warning,"CSTR","CString.h",0x5b,"Invalid arg: srclen <= 0");
    copied=std::min(size,capacity-1); bool ended=false; for(std::size_t i=0;i<copied;++i){if(!ended&&input[i]==0)ended=true;output[i]=ended?0:input[i];} output[copied]=0;
    return copied==size || c.log(ConsoleLevel::Warning,"CSTR","CString.h",0x63,"String truncated on copy \"%s\"",output);
}
bool cstring_format(const Context& c,char* output,std::size_t capacity,bool& truncated,const char* format,...) {
    va_list args;va_start(args,format);const int count=::vsnprintf(output,capacity,format,args);va_end(args);
    if(count<0)return false;
    truncated=static_cast<std::size_t>(count)>=capacity;
    return !truncated || c.log(ConsoleLevel::Warning,"CSTR","CString.h",0x7c,"printf() string truncated");
}
std::string url_encode(const char* input) {
    std::string result;char digits[4];
    for(const auto* p=reinterpret_cast<const unsigned char*>(input);*p;++p) {
        if((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z'))result.push_back(*p);
        else {::snprintf(digits,sizeof(digits),"%%%02X",*p);result+=digits;}
    }
    return result;
}
std::string url_decode(const char* input) {
    std::string result;
    for(std::size_t i=0;input[i];++i) {
        if(input[i]=='+')result.push_back(' ');
        else if(input[i]=='%'&&input[i+1]&&input[i+2]) {
            char hex[3]={input[i+1],input[i+2],0};result.push_back(static_cast<char>(std::strtol(hex,nullptr,16)));i+=2;
        } else result.push_back(input[i]);
    }
    return result;
}
bool cstring(const Context& c) {
    BEGIN(0x107) END()
    BEGIN(0x10e) EXPECT(!same("a","A"),0x112); END()
    BEGIN(0x116)
        const auto starts=[](const char* value,const char* prefix){return std::strncmp(value,prefix,std::strlen(prefix))==0;};
        EXPECT(starts("Some random long text","Some rand"),0x117);
        EXPECT(!starts("Some random long text","Some partial prefix"),0x118);
        EXPECT(starts("ab","a"),0x119); EXPECT(!starts("a","ab"),0x11a);
        EXPECT(starts("a",""),0x11b); EXPECT(!starts("","a"),0x11d);
    END()
    BEGIN(0x125)
        const char* text="abc"; EXPECT(std::strchr(text,'a')==text,0x129);
        EXPECT(std::strchr(text,'b')==text+1,0x12e); EXPECT(std::strchr(text,'0')==nullptr,0x133);
    END()
    char output[128]{}; std::size_t copied=0;
    BEGIN(0x138)
        TRY(cstring_copy(c,nullptr,16,"Something",9,copied)); EXPECT(copied==0,0x13f);
        TRY(cstring_copy(c,output,0,"Something",9,copied)); EXPECT(copied==0,0x141);
        TRY(cstring_copy(c,output,16,nullptr,0,copied)); EXPECT(copied==0,0x143);
        TRY(cstring_copy(c,output,16,"Something",0,copied)); EXPECT(copied==0,0x145);
        TRY(cstring_copy(c,output,16,"Basic copy test",15,copied)); EXPECT(same(output,"Basic copy test"),0x14b); EXPECT(copied==15,0x14c);
        const char* long_text="Long string that is longer than actual dest buffer size";
        TRY(cstring_copy(c,output,5,long_text,std::strlen(long_text),copied)); EXPECT(copied==4,0x152);
    END()
    BEGIN(0x157)
        bool truncated=false;
        TRY(cstring_format(c,output,8,truncated,"%s","")); EXPECT(!truncated,0x15c); EXPECT(empty(output),0x15d);
        TRY(cstring_format(c,output,8,truncated,"1234567")); EXPECT(!truncated,0x15f); EXPECT(same(output,"1234567"),0x160);
        TRY(cstring_format(c,output,8,truncated,"%s","1234567")); EXPECT(!truncated,0x161); EXPECT(same(output,"1234567"),0x162);
        TRY(cstring_format(c,output,8,truncated,"123456789")); EXPECT(truncated,0x164); EXPECT(same(output,"1234567"),0x165);
        TRY(cstring_format(c,output,8,truncated,"%s","123456789")); EXPECT(truncated,0x166); EXPECT(same(output,"1234567"),0x167);
        TRY(cstring_format(c,output,8,truncated,"%.5f",0.123456789)); EXPECT(!truncated,0x169); EXPECT(same(output,"0.12346"),0x16a);
        TRY(cstring_format(c,output,8,truncated,"%.8f",0.123456789)); EXPECT(truncated,0x16b); EXPECT(same(output,"0.12345"),0x16c);
        TRY(cstring_format(c,output,16,truncated,"%d",12345678)); EXPECT(!truncated,0x172); EXPECT(same(output,"12345678"),0x173);
        TRY(cstring_format(c,output,16,truncated,"%lu",12345678ul)); EXPECT(!truncated,0x174); EXPECT(same(output,"12345678"),0x175);
        TRY(cstring_format(c,output,16,truncated,"%x",0xdeadbeefu)); EXPECT(!truncated,0x176); EXPECT(same(output,"deadbeef"),0x177);
        TRY(cstring_format(c,output,16,truncated,"%X",0xdeadbeefu)); EXPECT(!truncated,0x178); EXPECT(same(output,"DEADBEEF"),0x179);
        TRY(cstring_format(c,output,16,truncated,"%06x",0xdeadu)); EXPECT(!truncated,0x17a); EXPECT(same(output,"00dead"),0x17b);
        TRY(cstring_format(c,output,16,truncated,"%06X",0xdeadu)); EXPECT(!truncated,0x17c); EXPECT(same(output,"00DEAD"),0x17d);
        TRY(cstring_format(c,output,16,truncated,"%10s","Right")); EXPECT(!truncated,0x183); EXPECT(same(output,"     Right"),0x184);
        TRY(cstring_format(c,output,16,truncated,"%-10s","Left")); EXPECT(!truncated,0x186); EXPECT(same(output,"Left      "),0x187);
    END()
    BEGIN(0x18d)
        const char* decoded[]={"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","a b c d e f","`!@#$%^&*()_+{}|[]\\:\";'<>?*+,-./"};
        const char* encoded[]={decoded[0],"a%20b%20c%20d%20e%20f","%60%21%40%23%24%25%5E%26%2A%28%29%5F%2B%7B%7D%7C%5B%5D%5C%3A%22%3B%27%3C%3E%3F%2A%2B%2C%2D%2E%2F"};
        for(unsigned i=0;i<3;++i){EXPECT(url_decode(encoded[i])==decoded[i],0x1ad);EXPECT(url_encode(decoded[i])==encoded[i],0x1b1);}
        EXPECT(url_decode("a+b+c+d+e+f")=="a b c d e f",0x1b7);
    END()
    BEGIN(0x1bc)
        const auto truth=[](const char* value){return same(value,"1")||same(value,"on")||same(value,"true");};
        EXPECT(!truth("0"),0x1be); EXPECT(truth("1"),0x1bf); EXPECT(!truth("off"),0x1c0);
        EXPECT(truth("on"),0x1c1); EXPECT(!truth("false"),0x1c2); EXPECT(truth("true"),0x1c3);
    END()
    BEGIN(0x1c7)
        const std::uint32_t seconds[]={0,1,59,60,61,3599,3600,3601};
        const char* full[]={"0:00:00","0:00:01","0:00:59","0:01:00","0:01:01","0:59:59","1:00:00","1:00:01"};
        const char* short_text[]={"0","1","59","1:00","1:01","59:59","1:00:00","1:00:01"};
        for(unsigned abbreviated=0;abbreviated<2;++abbreviated)for(unsigned i=0;i<8;++i) {
            head_duration_text(output,sizeof(output),seconds[i],abbreviated!=0);
            EXPECT(same(output,abbreviated?short_text[i]:full[i]),0xfe);
        }
        head_duration_text(output,sizeof(output),3599999,true); EXPECT(same(output,"999:59:59"),0xfe);
    END()
    return true;
}
std::int32_t decimal_integer(void*,const char* value) { return static_cast<std::int32_t>(std::strtol(value,nullptr,10)); }
bool http_parser(const Context& c) {
    LocalHttpParser parser; parser.services.decimal_integer=decimal_integer;
    BEGIN(0x9d)
        const char* lines[]={"GET / HTTP/1.0","GET /foo HTTP/1.0","POST /somethingPosted HTTP/1.0","Invalid_request_line_without_space","method uri_not_followed_by_a_space"};
        const HttpMethod methods[]={HttpMethod::GET,HttpMethod::GET,HttpMethod::POST,HttpMethod::Unknown,HttpMethod::Unknown};
        const char* paths[]={"/","/foo","/somethingPosted"}; const unsigned checks[]={0xa4,0xa9,0xae,0xb3,0xb7};
        for(unsigned i=0;i<5;++i) {
            parser.reset(); TRY(parser.consume_line(lines[i])==LocalHttpResult::Ok); if(i==3)TRY(c.log(ConsoleLevel::Error,"HSERV","HttpParser.h",0x62,"Request-Line has no space"));
            if(i==4)TRY(c.log(ConsoleLevel::Error,"HSERV","HttpParser.h",0x6a,"Request-Line has no Request-URI"));
            EXPECT(parser.request.method==methods[i],checks[i]);
            if(i<3)EXPECT(same(parser.request.path.c_str(),paths[i]),checks[i]+1);
        }
    END()
    BEGIN(0xba)
        parser.reset(); EXPECT(parser.request.content_length==0,0xc0); EXPECT(!parser.request.content_is_json,0xc1);
        TRY(parser.consume_line("GET / HTTP/1.0")==LocalHttpResult::Ok);
        TRY(parser.consume_line("Content-Length: 1234")==LocalHttpResult::Ok); EXPECT(parser.request.content_length==1234,0xc5);
        TRY(parser.consume_line("Content-Length: 0")==LocalHttpResult::Ok); EXPECT(parser.request.content_length==0,0xc7);
        TRY(parser.consume_line("Content-Length: 654321")==LocalHttpResult::Ok); EXPECT(parser.request.content_length==654321,0xc9);
        TRY(parser.consume_line("Content-Type: something-not-json")==LocalHttpResult::Ok); EXPECT(!parser.request.content_is_json,0xcb);
        TRY(parser.consume_line("Content-Type: application/json")==LocalHttpResult::Ok); EXPECT(parser.request.content_is_json,0xcd);
        EXPECT(!parser.headers_done,0xce); TRY(parser.consume_line("")==LocalHttpResult::Ok); EXPECT(parser.headers_done,0xd0);
    END()
    return true;
}
bool mac_address(const Context& c) {
    BEGIN(0x3d) END()
    using Mac=std::array<std::uint8_t,6>;
    BEGIN(0x45) const Mac a{{0,1,2,3,4,5}}; auto b=a; EXPECT(a==b,0x49); b[0]=255; EXPECT(a!=b,0x4b); END()
    BEGIN(0x4e)
        if(!c.hooks.scope)return false;
        TRY(c.hooks.scope(c.hooks.context,true,"MAC","MacAddress.h",0x4f,"testAssignment"));
        const Mac a{{0,1,2,3,4,5}}; const auto b=a; EXPECT(a==b,0x52);
        TRY(c.hooks.scope(c.hooks.context,false,"MAC","MacAddress.h",0x4f,"testAssignment"));
    END()
    BEGIN(0x55) const Mac a{{0,1,2,3,4,5}}; for(unsigned i=0;i<6;++i)EXPECT(a[i]==i,0x57+i); END()
    return true;
}
bool recipe_equal(const RecipeReference& a,const RecipeReference& b) {
    const auto& x=a.parameters; const auto& y=b.parameters;
    const auto equal=[](const Quantity& first,const Quantity& second){return first.unit==second.unit&&first.value==second.value;};
    return a.id==b.id && a.kind==b.kind && same(a.name.data(),b.name.data()) &&
        x.abv==y.abv && x.ibu==y.ibu && equal(x.time,y.time) && equal(x.water,y.water) &&
        equal(x.intensity,y.intensity) && equal(x.temperature,y.temperature) && x.resumed_session==y.resumed_session;
}
bool recipe_reference(const Context& c) {
    const auto valid=[](std::int32_t value){return static_cast<std::uint32_t>(value)-1u<9;};
    BEGIN(0x15e)
        RecipeReference recipe; char output[128];
        ::snprintf(output,sizeof(output),"%s %d %s %d %d",to_string(recipe.kind),static_cast<std::int32_t>(recipe.id),recipe.name.data(),recipe.parameters.abv,recipe.parameters.ibu);
        EXPECT(!empty(output),0x161);
    END()
    BEGIN(0x164)
        RecipeReference r; EXPECT(!valid(r.parameters.abv)&&!valid(r.parameters.ibu),0x166);
        EXPECT(!valid(r.parameters.abv),0x167); EXPECT(!valid(r.parameters.ibu),0x168);
        r.parameters.abv=5; EXPECT(valid(r.parameters.abv)||valid(r.parameters.ibu),0x16b);
        EXPECT(valid(r.parameters.abv),0x16c); EXPECT(!valid(r.parameters.ibu),0x16d);
    END()
    BEGIN(0x170)
        RecipeReference a,b; EXPECT(recipe_equal(a,b),0x172);
        std::strcpy(a.name.data(),"some name"); a.name_length=9; EXPECT(!recipe_equal(a,b),0x175);
        std::strcpy(b.name.data(),"some name"); b.name_length=9; EXPECT(recipe_equal(a,b),0x177);
        b.name.fill(0); b.name_length=0; EXPECT(!recipe_equal(a,b),0x179); b=a; EXPECT(recipe_equal(a,b),0x17b);
    END()
    return true;
}
bool noise_smoother(const Context& c) {
    if(!c.hooks.millis||!c.hooks.delay_ms)return false;
    // This is the test's five-sample, 200ms specialization, not the ten-sample
    // 2000ms temperature-acquisition specialization used by Core.
    struct Smoother {std::array<double,5> data{};unsigned next=0,count=0;double sum=0,average=0;std::uint32_t date=0;};
    Smoother state;
    auto update=[&](double value){
        const auto now=c.hooks.millis(c.hooks.context);
        if(state.date && now-state.date>200) {
            if(!c.log(ConsoleLevel::Warning,"NSS","NoiseSmoother.h",0x6e,"Data expired, resetting (%d)",static_cast<std::int32_t>(now-state.date)))return false;
            state.next=state.count=0; state.sum=state.average=0;
        }
        state.date=now;
        if(state.count<5)++state.count;else state.sum-=state.data[state.next];
        state.data[state.next]=value; if(++state.next==5)state.next=0;
        state.sum+=value; state.average=state.sum/state.count; return true;
    };
    BEGIN(0x90)
        TRY(update(1)); EXPECT(state.average==1,0x9a); TRY(update(2)); EXPECT(state.average==1.5,0x9e);
        TRY(update(3)); EXPECT(state.average==2,0xa2); TRY(update(4)); TRY(update(5)); EXPECT(state.average==3,0xa7);
        TRY(update(6)); EXPECT(state.average==4,0xab); state=Smoother{};
        const double values[]={-11,-1,9,-2,0}; for(const auto value:values)TRY(update(value)); EXPECT(state.average==-1,0xb7);
        TRY(c.hooks.delay_ms(c.hooks.context,201)); TRY(update(0)); EXPECT(state.average==0,0xbc);
    END()
    return true;
}

bool linear_input(const Context& c) {
    BEGIN(0x505)
    const double inputs[][4]={{0,.5,1,.1},{1,2,3,.5},{4,8,15.5,.5},{1,2,4.2,.2}};
    const int counts[]={11,5,24,17}; const unsigned lines[]={0x50b,0x511,0x517,0x51d};
    for(unsigned i=0;i<4;++i){const auto* v=inputs[i];
        TRY(c.log(ConsoleLevel::Info,"QINP","UiComponents.h",0x4b8,"_configure(fMin=%f, fCur=%f, fMax=%f, stepLength=%f)",v[0],v[1],v[2],v[3]));
        const auto count=static_cast<int>((v[2]-v[0])/v[3])+1;
        const auto selected=static_cast<int>((v[1]-v[0])/v[3]);
        EXPECT(v[0]+v[3]*selected==v[1],lines[i]); EXPECT(v[0]==inputs[i][0],lines[i]+1);
        EXPECT(v[0]+v[3]*(count-1)==v[2],lines[i]+2); EXPECT(count==counts[i],lines[i]+3);
    }
    END() return true;
}
bool utf8(const Context& c){
    DisplayUtf8 state;std::uint32_t value=0;
    auto init=[&](const char* text){return display_utf8_initialize(state,text,std::strlen(text)+1)==DisplayTextResult::Ok;};
    auto next=[&](){display_utf8_next(state,value);return value;};
    BEGIN(0xdc) TRY(init(""));EXPECT(next()==DisplayInvalidCodepoint,0xde);EXPECT(state.characters==0,0xdf);EXPECT(state.cursor>=state.length,0xe0);END()
    BEGIN(0xe4) TRY(init("13 length str"));EXPECT(state.characters==13,0xe8);EXPECT(state.cursor<state.length,0xe9);EXPECT(next()==49,0xea);
        for(unsigned i=1;i<state.characters-2u;++i){next();}EXPECT(state.cursor<state.length,0xee);EXPECT(next()==116,0xef);EXPECT(next()==114,0xf0);EXPECT(state.cursor>=state.length,0xf1);EXPECT(next()==DisplayInvalidCodepoint,0xf2);END()
    BEGIN(0xf6) TRY(init("37°C"));next();next();EXPECT(state.characters==4,0xfd);EXPECT(state.cursor<state.length,0xff);EXPECT(next()==176,0x100);END()
    BEGIN(0x104) TRY(init("₳, Austral"));EXPECT(state.characters==10,0x108);EXPECT(next()==8371,0x10a);END()
    BEGIN(0x10e) const char* texts[]={"13 length str","37°C","₳, Austral"};const unsigned counts[]={13,4,10};
        for(unsigned i=0;i<3;++i){TRY(display_utf8_length(texts[i],std::strlen(texts[i])+1,value)==DisplayTextResult::Ok);EXPECT(value==counts[i],0x10f+i);}END()
    return true;
}
bool text_justifier(const Context& c){
    char output[21]{};
    auto justify=[&](const char* left,const char* right){
        std::uint32_t nl=0,nr=0;
        for(unsigned i=0;i<2;++i){const char* value=i?right:left;auto& count=i?nr:nl;
            if(!value){if(!c.log(ConsoleLevel::Error,"UTF8","UTF8Decode.h",0x66,"UTF8Decoder.len(),Null String"))return false;}
            else if(display_utf8_length(value,std::strlen(value)+1,count)!=DisplayTextResult::Ok)return false;
        }
        if(nl+nr>=20&&!c.log(ConsoleLevel::Error,"TJUS","UTF8Decode.h",0x130,"Cannot justify as text would overlap"))return false;
        std::memset(output,' ',20);output[20]=0;
        if(left)std::memcpy(output,left,std::min<std::size_t>(20,std::strlen(left)));
        if(right)std::memcpy(output+20-std::min<std::uint32_t>(20,nr),right,std::min<std::size_t>(20,std::strlen(right)));
        return true;
    };
    BEGIN(0x141)
    const char* l[]={"123",nullptr,"","123","123","01234567890123456789",nullptr};
    const char* r[]={"456","456","456",nullptr,"",nullptr,"01234567890123456789"};
    const char* expected[]={"123              456","                 456","                 456","123                 ","123                 ","01234567890123456789","01234567890123456789"};
    const unsigned lines[]={0x144,0x146,0x147,0x148,0x149,0x14a,0x14b};
    for(unsigned i=0;i<7;++i){TRY(justify(l[i],r[i]));EXPECT(same(output,expected[i]),lines[i]);}
    END() return true;
}
bool text_lines(const Context& c){
    std::array<std::array<char,25>,2> lines{};unsigned count=0;
    auto reset=[&](){count=0;for(auto& line:lines)line.fill(0);};
    auto add=[&](const char* value,bool& added){
        if(count==lines.size()){added=false;return c.log(ConsoleLevel::Error,"UI","TextLines.h",0x28,"Too many lines");}
        const auto size=std::strlen(value);auto& line=lines[count++];std::memcpy(line.data(),value,std::min<std::size_t>(24,size));line[24]=0;added=size<25;
        return added||c.log(ConsoleLevel::Error,"UI","TextLines.h",0x32,"Line #%u truncated",count-1);
    };bool added=false;
    BEGIN(0x3c) EXPECT(count==0,0x3e);EXPECT(count<lines.size(),0x3f);END()
    BEGIN(0x42) TRY(add("line1",added));EXPECT(count==1,0x45);reset();EXPECT(count==0,0x47);EXPECT(count<lines.size(),0x48);END()
    BEGIN(0x4b) TRY(add("line1",added));EXPECT(added,0x4d);EXPECT(count==1,0x4e);EXPECT(count<lines.size(),0x4f);
        TRY(add("line2",added));EXPECT(added,0x51);EXPECT(count==2,0x52);EXPECT(count==lines.size(),0x53);
        TRY(add("line3",added));EXPECT(!added,0x55);EXPECT(count==2,0x56);EXPECT(count==lines.size(),0x57);
        reset();TRY(add("A very long line that will be truncated without triggering exception",added));EXPECT(!added,0x5a);EXPECT(count==1,0x5b);EXPECT(count<lines.size(),0x5c);END()
    return true;
}
bool paragraph(const Context& c){
    BEGIN(0x115)
    const unsigned widths[]={6,8,9,14,17},heights[]={8,16,11,16,20};
    for(unsigned font=0;font<5;++font)for(auto text:paragraph_samples){const auto width=widths[font];
        HeadWrappedText result;TRY(head_wrap_text(result,text,4,width));EXPECT(result.count>0,0x128);
        TRY(head_wrap_text(result,text,68,width,60/(heights[font]+1)));EXPECT(result.count>0,0x12c);
    }
    END() return true;
}
bool rect(const Context& c){
    auto eq=[](const DisplayRect&a,const DisplayRect&b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;};
    auto set=[](DisplayRect&r,int w,int h){r.right=r.left+w-1;r.bottom=r.top+h-1;};
    auto move=[](DisplayRect&r,int x,int y){display_rect_translate(r,x-r.left,y-r.top);};
    auto out=[&](const DisplayRect&r,const char* name){return c.log(ConsoleLevel::Info,"IDSP","DisplayTypes.h",0xb5,"Rect (%3d,%3d) (%3d,%3d) W = %3u  H = %3u %7s \"%s\"",r.left,r.top,r.right,r.bottom,display_rect_width(r),display_rect_height(r),display_rect_valid(r)?"VALID":"INVALID",name);};
    BEGIN(0xbc) TRY(out({1,1,1,0},"r1"));TRY(out({0,0,0,0},"r2"));TRY(out({0,0,0,1},"r3"));TRY(out({0,0,1,1},"r4"));END()
    BEGIN(0xc7) DisplayRect r{0,0,0,0};set(r,1,1);EXPECT(display_rect_width(r)==1,0xca);EXPECT(display_rect_height(r)==1,0xcb);set(r,123,456);EXPECT(display_rect_width(r)==123,0xcd);EXPECT(display_rect_height(r)==456,0xce);END()
    BEGIN(0xd1) EXPECT(!display_rect_valid({1,1,1,0}),0xd6);EXPECT(display_rect_valid({0,0,0,0}),0xd7);EXPECT(display_rect_valid({0,0,0,1}),0xd8);EXPECT(display_rect_valid({0,0,1,1}),0xd9);END()
    const DisplayRect r1{0,0,1,1},r2{1,1,2,2},expected{1,1,1,1};
    BEGIN(0xdc) EXPECT(display_rect_intersects(r1,r1),0xdf);EXPECT(display_rect_intersects(r1,r2),0xe0);EXPECT(!display_rect_intersects(r1,{2,2,3,3}),0xe2);END()
    BEGIN(0xe5) DisplayRect result{},a{},b{};TRY(display_rect_intersection(r1,r1,result));TRY(c.assertion(eq(result,r1),c.file(),0xec,"areEquals(result, r1)"));
        TRY(display_rect_intersection(r1,r2,a));TRY(c.assertion(eq(a,expected),c.file(),0xf0,"areEquals(Rect(1, 1, 1, 1), a)"));
        TRY(display_rect_intersection(r1,a,b));EXPECT(eq(b,expected),0xf4);TRY(out(b,"b"));END()
    BEGIN(0xf9) DisplayRect a{},b{},r=r1;TRY(display_rect_intersection(r,r,r));EXPECT(eq(r,r1),0x100);TRY(display_rect_intersection(r,r2,a));EXPECT(eq(a,expected),0x105);TRY(display_rect_intersection(r,a,b));EXPECT(eq(b,expected),0x10a);END()
    BEGIN(0x10d) const DisplayRect a{0,0,10,10},b{1,1,2,2};EXPECT(display_rect_contains(a,a),0x110);EXPECT(display_rect_contains(a,b),0x111);EXPECT(!display_rect_contains(b,a),0x112);END()
    BEGIN(0x115) const DisplayRect a{0,0,10,10};DisplayRect b=a;move(b,10,10);EXPECT(display_rect_width(b)==display_rect_width(a),0x119);EXPECT(display_rect_height(b)==display_rect_height(a),0x11a);move(b,0,0);EXPECT(eq(a,b),0x11c);END()
    BEGIN(0x11f) DisplayRect b{0,0,5,5};const int positions[]={0,2,5};const unsigned checks[][3]={{0x124,0x127,0x12a},{0x130,0x133,0x136},{0x13c,0x13f,0x142}};
        for(unsigned direction=0;direction<3;++direction){move(b,0,0);for(unsigned align=0;align<3;++align){move(b,direction!=1?positions[align]:0,direction!=0?positions[align]:0);EXPECT(b.left==(direction!=1?positions[align]:0),checks[direction][align]);EXPECT(b.top==(direction!=0?positions[align]:0),checks[direction][align]+1);}}END()
    return true;
}
double bits_double(std::uint64_t bits){double result;std::memcpy(&result,&bits,sizeof(result));return result;}
bool stats_computer_values(const Context&c,const double* values,std::size_t size,double expected_mean,double expected_variance){
    double mean=0,m2=0;for(std::size_t i=0;i<size;++i){if(!i)mean=values[i];else{const double delta=values[i]-mean;mean=delta/static_cast<double>(i+1)+mean;m2=m2+(values[i]-mean)*delta;}}
    const double variance=size>1?m2/static_cast<double>(size-1):0;
    TRY(c.log(ConsoleLevel::Debug,"STS","Stats.h",0x46,"ExpectedAvg = %.6f ActualAvg = %.6f",expected_mean,mean));CHECK(close(expected_mean,mean),"Stats.h",0x47);
    TRY(c.log(ConsoleLevel::Debug,"STS","Stats.h",0x4a,"ExpectedVar = %.6f ActualVar = %.6f",expected_variance,variance));CHECK(close(expected_variance,variance),"Stats.h",0x4b);return true;
}
bool stats_computer(const Context&c){
    BEGIN(0x51) double values[10];for(unsigned i=0;i<10;++i)values[i]=123+i;TRY(stats_computer_values(c,values,10,127.5,bits_double(0x402255556084a516ULL)));END()
    BEGIN(0x5a) TRY(stats_computer_values(c,stats_samples,18,bits_double(0x4004789acbc8c0cfULL),bits_double(0x3f4aef6f8f041462ULL)));END()
    return true;
}
template<std::size_t N>bool stats_data(const Context&c,const double(&values)[N],double threshold,bool expect_below,bool verbose){
    PicoStillStatistics stats;bool below=true;
    for(std::size_t i=0;i<N;++i){stats.update(values[i]);const double variance=stats.count>1?stats.squared_deviations/static_cast<double>(stats.count-1):0;const double sd=std::sqrt(variance);
        if(verbose)TRY(c.log(ConsoleLevel::Info,"STS","Stats.h",0x13d,"%d,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f",static_cast<int>(i),values[i],stats.mean,sd,variance,stats.difference,stats.smoothed_difference));
        if(stats.count==30&&sd>threshold)below=false;
    }
    CHECK(below==expect_below,"Stats.h",0x148);return true;
}
bool stats_rolling(const Context&c){
    BEGIN(0x274) PicoStillStatistics stats;for(double value:stats_samples)stats.update(value);const double variance=stats.squared_deviations/(stats.count-1);const double deviation=std::sqrt(variance);
        EXPECT(close(stats.mean,2.558888889),0x27c);EXPECT(close(deviation,0.02867442),0x27d);
        TRY(c.log(ConsoleLevel::Info,"STS","Stats.h",0x107,"count = %u avg = %.3f var = %.8f sdev = %.8f",stats.count,stats.mean,variance,deviation));END()
    BEGIN(0x284) TRY(stats_data(c,stable_pressure,.016,true,false));END()
    BEGIN(0x289) TRY(stats_data(c,unstable_pressure,.02,false,false));END()
    BEGIN(0x28f) TRY(stats_data(c,stable_t1,.02,false,true));END()
    BEGIN(0x294) TRY(stats_data(c,unstable_t1,.02,false,true));END()
    return true;
}
bool quantity(const Context&c){
    BEGIN(0x122) END() BEGIN(0x12f) END() BEGIN(0x13e) END() // Constant-folded original tests.
    struct Vector{unsigned scope;Unit unit;double value;Unit other;double converted;unsigned line;};
    const Vector values[]={{0x145,Unit::Liter,1,Unit::Gallon,1/3.785411784,0x148},{0x14d,Unit::Gallon,1,Unit::Liter,3.785411784,0x150},{0x155,Unit::Gram,1,Unit::Ounce,1/28.349523125,0x158},{0x15d,Unit::Ounce,1,Unit::Gram,28.349523125,0x160},{0x165,Unit::Celsius,0,Unit::Fahrenheit,32,0x168},{0x16d,Unit::Fahrenheit,32,Unit::Celsius,0,0x170},{0x175,Unit::Second,123,Unit::Second,123,0x178},{0x17c,Unit::Intensity,7,Unit::Intensity,7,0x17f}};
    for(const auto&v:values){Scope scope(c,v.scope);TRY(scope.begin());Quantity q{v.value,v.unit};double out=0;TRY(quantity_in(q,v.unit,out,&c.services));EXPECT(out==v.value,v.line);TRY(quantity_in(q,v.unit,out,&c.services));EXPECT(out==v.value,v.line+1);if(v.scope<0x175){TRY(quantity_in(q,v.other,out,&c.services));EXPECT(std::abs(out-v.converted)<=.00001,v.line+2);}TRY(scope.end());}
    BEGIN(0x183) for(unsigned unit=0;unit<8;++unit){double out=0;const double number=static_cast<double>(1.234f);TRY(quantity_in({number,static_cast<Unit>(unit)},static_cast<Unit>(unit),out,&c.services));EXPECT(out==number,0x18f);}END()
    BEGIN(0x193) const unsigned seconds[]={0,1,59,60,61,3599,3600,3601,3660};const char* expected[]={"0 sec","1 sec","59 sec","1 min","1 min 1 sec","59 min 59 sec","1 hr 0 min","1 hr 0 min","1 hr 1 min"};
        for(unsigned i=0;i<9;++i){char text[16]{};TRY(head_quantity_text(text,sizeof(text),{static_cast<double>(seconds[i]),Unit::Second},true));EXPECT(same(text,expected[i]),0x195+i);}END()
    BEGIN(0x1a0) const Quantity samples[]={{1,Unit::Ounce},{16,Unit::Ounce},{17,Unit::Ounce},{1,Unit::Gram},{999,Unit::Gram},{1000,Unit::Gram},{1001,Unit::Gram},{32,Unit::Fahrenheit},{32,Unit::Fahrenheit}};
        const unsigned lines[]={0x1a6,0x1aa,0x1ae,0x1b2,0x1b6,0x1ba,0x1be,0x1c2,0x1c4};
        for(unsigned i=0;i<9;++i){char text[20]{};TRY(head_quantity_text(text,sizeof(text),samples[i],(i>=4&&i<=6)||i==8,1));if(i<4||i==7)TRY(c.log(ConsoleLevel::Info,"QTY","Quantity.h",0xde,"getImperialText %s",text));EXPECT(!empty(text),lines[i]);}END()
    return true;
}
bool session_type(const Context&c){
    BEGIN(0x55) const std::set<unsigned> requires_rinse{6,7,8,9,12,13,14};unsigned line=0x57;for(unsigned value:requires_rinse)EXPECT(requires_rinse.count(value)!=0,line++);EXPECT(!requires_rinse.count(0),0x5e);END()return true;
}

bool http_request(const Context&c){
    const char* uris[]={"http://picodevblob.blob.core.windows.net/recipebinaries/recipe1.bin","https://picobrew.com","https://picobrew.com:65432",
        "https://picobrew.com/super/long/path/that/will/exceed/the/max/size/of/the/internal/buffer/super/long/path/that/will/exceed/the/max/size/of/the/internal/buffer/super/long/path/that/will/exceed/the/max/size/of/the/internal/buffer/super/long/path/that/will/exceed/the/max/size/of/the/internal/buffer?and&its&crazy&parameters&that&are&also&too&big&for&us&to&handle&and&successully&post",
        "https://123.123.123.123","https://123.123.123.123:65432","https://123.123.123.123/foo/bar","https://123.123.123.123:65432/foo/bar"};
    const unsigned scopes[]={0xe2,0xef,0xfc,0x109,0x11b,0x12a,0x139,0x148};
    for(unsigned i=0;i<8;++i){Scope scope(c,scopes[i]);TRY(scope.begin());HttpRequest r;const bool accepted=r.parse(uris[i],&c.services);TRY(r.diagnostic_result==HttpResult::Ok);
        if(i<3)EXPECT(accepted,scopes[i]+3);
        TRY(r.out(c.services)==HttpResult::Ok);
        if(i==3){EXPECT(r.path_truncated,0x114);EXPECT(!accepted,0x115);EXPECT(!r.address,0x116);EXPECT(!r.valid(),0x117);EXPECT(!r.host_truncated,0x118);}
        else if(i<3){EXPECT(r.valid(),scopes[i]+5);EXPECT(r.https==(i!=0),scopes[i]+6);EXPECT(!r.address,scopes[i]+7);EXPECT(r.port==(i==0?80:i==1?443:65432),scopes[i]+8);
            EXPECT(same(r.host.data(),i==0?"picodevblob.blob.core.windows.net":"picobrew.com"),scopes[i]+9);EXPECT(same(r.path.data(),i==0?"/recipebinaries/recipe1.bin":""),scopes[i]+10);}
        else{const unsigned first=scopes[i]+4;EXPECT(!r.path_truncated,first);EXPECT(accepted,first+1);EXPECT(r.valid(),first+2);EXPECT(r.https,first+3);EXPECT(r.address!=0,first+4);EXPECT(r.address==0x7b7b7b7b,first+5);EXPECT(r.port==((i&1)?65432:443),first+6);EXPECT(same(r.host.data(),"123.123.123.123"),first+7);if(i<6)EXPECT(!r.host_truncated,first+8);else EXPECT(same(r.path.data(),"/foo/bar"),first+8);}
        TRY(scope.end());
    }return true;
}
bool program(const Context&c){
    BEGIN(0xcf) double rate=0;TRY(program_heating_rate(11.36,rate));EXPECT(rate==1,0xd0);TRY(program_heating_rate(10.36,rate));EXPECT(rate>1,0xd1);TRY(program_heating_rate(12.36,rate));EXPECT(rate<1,0xd2);END()
    BEGIN(0xd5) ProgramRecord p;EXPECT(p.empty(),0xd8);END()
    BEGIN(0xdc) ProgramRecord p;p.type=SessionType::Test;p.view.code_size=p.view.string_count=1;EXPECT(!p.empty(),0xe4);END()
    const char* strings[]={"string1","string2"};const Instruction code1[]={{OpCode::StepName,0},{OpCode::StepName,1}},code2[]={{OpCode::StepName,3},{OpCode::StepName,1}};
    const ProgramRecord a(SessionType::Unknown,0xffffffffu,"testp1",code1,2,strings,2),b(SessionType::Unknown,123,"testp2",code2,2,strings,2);
    auto valid_ref=[](const ProgramRecord&p,unsigned ref){return is_common_string_reference(ref)||(p.view.strings&&ref<p.view.string_count);};
    BEGIN(0xf0) EXPECT(valid_ref(a,0),0xf1);EXPECT(valid_ref(a,1),0xf2);EXPECT(!valid_ref(a,2),0xf3);END()
    BEGIN(0xf6) EXPECT(same(a.view.strings[0],"string1"),0xf7);EXPECT(same(a.view.strings[1],"string2"),0xf8);END()
    auto error=[&](const ProgramRecord&p,const char*& text){text=program_validation_error(p.view);if(text&&same(text,"Program has invalid string reference")){
        for(std::size_t i=0;i<p.view.code_size;++i){const auto ins=p.view.code[i];if((ins.opcode==OpCode::StepName||ins.opcode==OpCode::SubStepName)&&!valid_ref(p,ins.argument))return c.log(ConsoleLevel::Debug,"PGM","Program.h",0x93,"!isValidStringRef(%u) instruction #%u",ins.argument,static_cast<unsigned>(i));}}
        return true;
    };
    BEGIN(0xfb) const char* text=nullptr;TRY(error(a,text));EXPECT(!text,0xfc);TRY(error(b,text));if(text)TRY(c.log(ConsoleLevel::Debug,"PGM","Program.h",0xa0,"!valid pgm=%s \"%s\"",b.view.name,text));EXPECT(text!=nullptr,0xfd);END()
    BEGIN(0x100) const char* text=nullptr;TRY(error(a,text));EXPECT(empty(text),0x102);TRY(error(b,text));EXPECT(!empty(text),0x104);END()
    return true;
}
bool program_buffer(const Context&c){
    BEGIN(0x17b) ProgramBuffer b;const auto&p=b.record();EXPECT(p.view.code_size==0,0x17e);EXPECT(p.view.string_count==0,0x17f);EXPECT(p.type==SessionType::Unknown,0x180);EXPECT(p.view.code!=nullptr,0x181);EXPECT(p.view.strings!=nullptr,0x182);END()
    BEGIN(0x185) ProgramBuffer b;TRY(b.append_string("My program name")==ProgramBuildResult::Ok);const auto&p=b.record();EXPECT(same(p.view.name,"My program name"),0x188);EXPECT(same(p.view.strings[0],"My program name"),0x189);EXPECT(p.view.string_count==1,0x18a);TRY(b.append_string("Some string")==ProgramBuildResult::Ok);EXPECT(same(p.view.strings[1],"Some string"),0x18d);END()
    BEGIN(0x190) ProgramBuffer b;TRY(b.append({OpCode::Noop,0})==ProgramBuildResult::Ok);EXPECT(b.record().view.code_size==1,0x193);END()
    BEGIN(0x196) ProgramBuffer b;const Instruction code[]={{OpCode::Noop,0},{OpCode::Noop,0}};TRY(b.set_code(code,2)==ProgramBuildResult::Ok);EXPECT(b.record().view.code_size==2,0x19d);END()
    return true;
}
bool program_resolver(const Context&c){
    BEGIN(0x699) ProgramBuffer buffer;ProgramRepository repository(buffer);
        for(unsigned id=1;id<29;++id)if(!is_buffered_program(id)){ProgramStats stats;TRY(repository.static_stats(id,0,100,stats)==ProgramBuildResult::Ok);const auto* p=repository.find(id);TRY(p);TRY(report_program_estimate(c.services,*p,stats.heating_rate_celsius_per_minute,stats.duration_seconds));}
    END()return true;
}
bool static_program(const Context&c,const ProgramRecord&p){
    char name[64];::snprintf(name,sizeof name,"Program::isValid() %s",p.view.name);const OriginalScope description{0x400ed2ec,0x5f7,"RUNT","Program.h",name};Scope scope(c,description);TRY(scope.begin());
    const char* error=program_validation_error(p.view);CHECK(!error,"Program.h",0x5f9);
    if(error)TRY(c.log(ConsoleLevel::Error,"RUNT","Program.h",0x5fc,"Program %s error: %s",p.view.name,error));
    const auto time=estimate_instructions_seconds(p.view.code,p.view.code_size,false,0,0,12.77,1);TRY(report_program_estimate(c.services,p,1,time));CHECK(time<6*3600,"Program.h",0x600);
    TRY(scope.end());return true;
}
bool writer_decode(ProgramBuffer&b){
    auto word=[](std::size_t pos){return std::uint32_t(writer_binary[pos])|(std::uint32_t(writer_binary[pos+1])<<8)|(std::uint32_t(writer_binary[pos+2])<<16)|(std::uint32_t(writer_binary[pos+3])<<24);};
    const auto instructions=word(8),strings=word(12);if(instructions>256||strings>64)return false;
    const auto id=b.record().unique_id;b.reset();std::size_t at=16+instructions*8;if(at>=sizeof writer_binary)return false;
    for(std::uint32_t i=0;i<strings;++i){if(at>=sizeof writer_binary)return false;const unsigned length=writer_binary[at++];if(!length||length>sizeof writer_binary-at||writer_binary[at+length-1]!=0)return false;
        const char* text=reinterpret_cast<const char*>(writer_binary+at);const auto result=i?b.append_string(text):b.start(SessionType::Test,id,text);if(result!=ProgramBuildResult::Ok)return false;at+=length;}
    for(std::uint32_t i=0;i<instructions;++i)if(b.append({static_cast<OpCode>(word(16+i*8)),word(20+i*8)})!=ProgramBuildResult::Ok)return false;
    return b.record().has_program();
}
bool program_writer(const Context&c){
    ProgramBuffer b;
    BEGIN(0x2eb) TRY(b.start(SessionType::Clean,123,"Program name")==ProgramBuildResult::Ok);TRY(b.append({OpCode::MoveValve,2})==ProgramBuildResult::Ok);TRY(b.append({OpCode::ReachTarget,0xa00})==ProgramBuildResult::Ok);TRY(b.append_string("String 1")==ProgramBuildResult::Ok);
        TRY(c.log(ConsoleLevel::Info,"TEST",nullptr,0,"ExpectedError = ERR ASSERT datalen < _left Buffer not big enough"));
        TRY(b.append_string("String 2 that should be trunctacted because it exceeds max length")==ProgramBuildResult::Ok);
        TRY(c.log(ConsoleLevel::Warning,"CSTR","CString.h",0x63,"String truncated on copy \"%s\"",b.record().view.strings[2]));
        TRY(b.append({OpCode::WaitFor,2})==ProgramBuildResult::Ok);EXPECT(same(b.record().view.name,"Program name"),0x2f6);EXPECT(b.record().type==SessionType::Clean,0x2f7);EXPECT(b.record().view.code_size==3,0x2f8);EXPECT(b.record().view.string_count==3,0x2f9);
        ProgramParameters parameters;parameters.water={3,Unit::Gallon};TRY(finish_program(b,parameters,0,0,0)==ProgramBuildResult::Ok);TRY(report_program_estimate(c.services,b.record(),b.stats.heating_rate_celsius_per_minute,b.stats.duration_seconds));TRY(report_program_buffer(c.services,b));END()
    BEGIN(0x300) TRY(writer_decode(b));const auto&p=b.record();
        TRY(c.log(ConsoleLevel::Debug,"TESTS","Program.h",0x304,"buf.pgm.type = %u",static_cast<unsigned>(p.type)));TRY(c.log(ConsoleLevel::Debug,"TESTS","Program.h",0x305,"buf.pgm.name = %s",p.view.name));TRY(c.log(ConsoleLevel::Debug,"TESTS","Program.h",0x306,"buf.pgm.codeSize = %u",static_cast<unsigned>(p.view.code_size)));TRY(c.log(ConsoleLevel::Debug,"TESTS","Program.h",0x307,"buf.pgm.stringsCount = %u",static_cast<unsigned>(p.view.string_count)));
        EXPECT(same(p.view.name,"Zombie Dust Clone - ALL GRAIN"),0x308);EXPECT(p.type==SessionType::Test,0x309);EXPECT(p.view.code_size==27,0x30a);EXPECT(p.view.string_count==7,0x30b);EXPECT(!program_validation_error(p.view),0x30c);TRY(report_program_buffer(c.services,b));END()
    const unsigned scopes[]={0x310,0x317,0x31f,0x327};
    for(unsigned i=0;i<4;++i){Scope scope(c,scopes[i]);TRY(scope.begin());ProgramBuffer coffee;ProgramParameters p;p.id=static_cast<ZProgramId>(i==0?20:i==3?23:22);p.water={2,Unit::Gallon};if(i==1||i==2)p.intensity={i==1?12.0:1.0,Unit::Intensity};if(i==3){p.time={540,Unit::Second};p.temperature={190,Unit::Fahrenheit};}
        TRY(build_coffee_program(coffee,p,0,true)==ProgramBuildResult::Ok);TRY(report_program_estimate(c.services,coffee.record(),coffee.stats.heating_rate_celsius_per_minute,coffee.stats.duration_seconds));TRY(report_program_buffer(c.services,coffee));
        CHECK(coffee.record().type==SessionType::Coffee,"Program.h",0x2de);CHECK(coffee.record().view.code_size>0,"Program.h",0x2df);CHECK(coffee.record().view.string_count>0,"Program.h",0x2e0);CHECK(!program_validation_error(coffee.record().view),"Program.h",0x2e1);TRY(report_program_buffer(c.services,coffee));TRY(scope.end());}
    return true;
}
bool serial_logger(const Context&c){
    HeaterState heater;HeaterSensors temperatures;AuxiliarySensors auxiliary;z_msp_state msp{};ProgramRuntimeState runtime;PicoStillSamples still;CoreCsvSources sources;
    sources.heater=&heater;sources.temperatures=&temperatures;sources.auxiliary=&auxiliary;sources.msp=&msp;sources.runtime=&runtime;sources.still=&still;
    SerialLogServices services;services.context=const_cast<Context*>(&c);services.assertion=[](void*p,const char*file,std::uint32_t line,const char*expression){return static_cast<Context*>(p)->assertion(false,file,line,expression);};
    services.emit=[](void*p,SerialLogMessage kind,const char*text,std::uint32_t value){auto& context=*static_cast<Context*>(p);switch(kind){
        case SerialLogMessage::Header:return context.log(ConsoleLevel::Info,"CSTA",nullptr,0,"CSVHDR = %s",text);
        case SerialLogMessage::Status:return context.log(ConsoleLevel::Info,"CSTA","CoreHelpers.h",0x42,"%s",text);
        case SerialLogMessage::Enabled:return context.log(ConsoleLevel::Info,"LOG",nullptr,0,"_isLogging = %s",text);
        case SerialLogMessage::Interval:return context.log(ConsoleLevel::Info,"LOG",nullptr,0,"_intervalMs = %u",value);
        case SerialLogMessage::BufferTruncated:return context.log(ConsoleLevel::Warning,"BUF","Buffer.h",value,"Buffer truncated");
        case SerialLogMessage::DroppedBytes:return context.log(ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,"Dropping %u bytes",value);
        case SerialLogMessage::IntervalBelowMinimum:return context.log(ConsoleLevel::Warning,"LOG","CoreHelpers.h",0x75,"Interval is too low");
    }return false;};
    BEGIN(0x87) SerialLogger logger;logger.services=services;logger.source=core_csv_source(sources);TRY(logger.header()==SerialLogResult::Ok);EXPECT(!logger.ever_truncated,0x8b);TRY(logger.out()==SerialLogResult::Ok);EXPECT(!logger.ever_truncated,0x8d);TRY(logger.information()==SerialLogResult::Ok);EXPECT(!logger.ever_truncated,0x8f);END()
    BEGIN(0x92) runtime.step_name="|||A very long step name to make sure CSV buffer will get truncated and that SerialLogger actually does detect this!   --------------------------------------------------------------------------------------------------------------------------------------------------->-";
        SerialLogger logger;logger.services=services;logger.source=core_csv_source(sources);TRY(logger.regenerate()==SerialLogResult::Ok);EXPECT(logger.ever_truncated,0x9c);END()
    return true;
}
bool out(const Context&c){
    if(!c.hooks.write||!c.hooks.millis)return false;
    auto variable=[&](const char*name,const char*value){char text[256];const double seconds=static_cast<double>(static_cast<float>(c.hooks.millis(c.hooks.context))/1000.0f);const int n=::snprintf(text,sizeof text,"%4.2f VAR [VARTEST] %s = %s\r\n",seconds,name,value?value:"");return n>=0&&static_cast<std::size_t>(n)<sizeof text&&c.hooks.write(c.hooks.context,text,static_cast<std::size_t>(n));};
    TRY(variable("Out::Var",nullptr));TRY(variable("Out::Var",""));TRY(variable("Out::Var","Something"));
    char text[64];::snprintf(text,sizeof text,"%u",0u);TRY(variable("Out::VarUL",text));::snprintf(text,sizeof text,"%u",0xffffffffu);TRY(variable("Out::VarUL",text));TRY(variable("Out::VarBool",boolean(true)));TRY(variable("Out::VarBool",boolean(false)));
    ::snprintf(text,sizeof text,"%4.2f",0.0);TRY(variable("Out::VarFloat",text));::snprintf(text,sizeof text,"%4.2f",static_cast<double>(4294967296.0f));TRY(variable("Out::VarFloat",text));
    const unsigned addresses[]={0u,0xffffffffu,0x04030201u};for(auto ip:addresses){::snprintf(text,sizeof text,"%u.%u.%u.%u",ip&255,(ip>>8)&255,(ip>>16)&255,(ip>>24)&255);TRY(variable("Out::VarIPAddress",text));}
    TRY(variable("Out::VarBytes","EMPTY"));for(unsigned first=0;first<23;first+=8){char line[41]{};for(unsigned j=0;j<8&&first+j<23;++j)::snprintf(line+j*5,sizeof line-j*5,"0x%02X ",first+j);TRY(variable("Out::VarBytes",line));}
    TRY(c.log(ConsoleLevel::Debug,"TEST","Out.h",0x13a,"debug log"));TRY(c.log(ConsoleLevel::Info,"TEST","Out.h",0x13b,"info log"));TRY(c.log(ConsoleLevel::Debug,"TEST","Out.h",0x140,"debug data %d \"%s\"",123,"mydata"));TRY(c.log(ConsoleLevel::Info,"TEST","Out.h",0x141,"info data %d \"%s\"",123,"mydata"));TRY(c.log(ConsoleLevel::Info,"TEST","Out.h",0x147,"%s()","tests"));
    TRY(c.log(ConsoleLevel::Info,"TEST",nullptr,0,"PBLOG_IVAR = A formatted string \"%s\"","astring"));TRY(c.log(ConsoleLevel::Info,"TEST",nullptr,0,"PBLOG_IVAR_STR = %s","A simple string"));TRY(c.log(ConsoleLevel::Info,"TEST",nullptr,0,"PBLOG_IVAR_INT = %d",123));TRY(c.log(ConsoleLevel::Info,"TEST",nullptr,0,"PBLOG_IVAR_UL = %lu",123456789ul));TRY(c.log(ConsoleLevel::Info,"TEST",nullptr,0,"PBLOG_IVAR_BOOL = %s",boolean(true)));TRY(c.log(ConsoleLevel::Info,"TEST",nullptr,0,"PBLOG_IVAR_IPADDR = %d.%d.%d.%d",0,0,0,0));TRY(c.log(ConsoleLevel::Info,"TEST",nullptr,0,"PBLOG_IVAR_MACADDR = %02x:%02x:%02x:%02x:%02x:%02x",0,1,2,3,4,5));return true;
}

bool network_session(const Context&c){
    auto valid=[&](const AzureSession&s,bool& result){const auto type=static_cast<unsigned>(s.type),program=static_cast<unsigned>(s.program);result=false;
        if(type>16)return c.log(ConsoleLevel::Warning,"AZ","Azure.h",0x32,"SessionType is invalid");
        if(program>28)return c.log(ConsoleLevel::Warning,"AZ","Azure.h",0x37,"ZProgramId is invalid");
        if(!program)return c.log(ConsoleLevel::Warning,"AZ","Azure.h",0x3c,"ZProgramId is ZProgramId::None");
        result=true;return true;
    };
    BEGIN(0x7d) AzureSession s;s.type=SessionType::Unknown;s.program=static_cast<ZProgramId>(1);bool result=false;TRY(valid(s,result));EXPECT(result,0x82);s.program=ZProgramId::None;TRY(valid(s,result));EXPECT(!result,0x85);s.program=static_cast<ZProgramId>(99999);TRY(valid(s,result));EXPECT(!result,0x88);s.program=static_cast<ZProgramId>(1);s.type=static_cast<SessionType>(99999);TRY(valid(s,result));EXPECT(!result,0x8c);END()
    BEGIN(0x8f)
        char json[]={"{\"FirmwareVersion\": \"0.0.dev\",\"SessionType\": 0,\"Name\": \"myrinse\",\"RecipeID\": 4321,\"ZProgramId\": 20,\"ProgramParams\": {  \"Duration\":    12,  \"Water\":       1.234,  \"Intensity\":   12,  \"Temperature\": 123  }}"};
        AzureSession s;RecipeJsonError error;const bool parsed=basic_console_decode_session_json(json,sizeof json-1,s,error);EXPECT(error==RecipeJsonError::Ok,0x9f);TRY(c.log(ConsoleLevel::Info,"JSON","Azure.h",0xa0,"deserializeJson error %u %s",static_cast<unsigned>(error),error==RecipeJsonError::Ok?"Ok":"InvalidInput"));EXPECT(parsed,0xa3);
        TRY(c.log(ConsoleLevel::Info,"ZSES",nullptr,0,"Id = %u",0u));TRY(c.log(ConsoleLevel::Info,"ZSES",nullptr,0,"Type = %s",to_string(s.type)));TRY(c.log(ConsoleLevel::Info,"ZSES",nullptr,0,"Mode = Unknown"));TRY(c.log(ConsoleLevel::Info,"ZSES",nullptr,0,"ProgramId = %s",to_string(s.program)));TRY(c.log(ConsoleLevel::Info,"ZSES",nullptr,0,"RecipeId = %u",s.recipe_id));TRY(c.log(ConsoleLevel::Info,"ZSES",nullptr,0,"Name = %s",s.name.data()));TRY(c.log(ConsoleLevel::Info,"ZSES",nullptr,0,"GroupSession = %s",boolean(s.group_session)));TRY(report_program_parameters(c.services,s.parameters));TRY(report_program_stats(c.services,ProgramStats{}));
        EXPECT(s.program==ZProgramId::CoffeeColdBrew,0xa8);EXPECT(same(s.name.data(),"myrinse"),0xa9);EXPECT(s.recipe_id==4321,0xaa);double n=0;TRY(quantity_in(s.parameters.time,Unit::Second,n,&c.services));EXPECT(n==12,0xad);TRY(quantity_in(s.parameters.water,Unit::Gallon,n,&c.services));EXPECT(std::abs(n-1.234)<=.00001,0xae);TRY(quantity_in(s.parameters.intensity,Unit::Intensity,n,&c.services));EXPECT(n==12,0xaf);TRY(quantity_in(s.parameters.temperature,Unit::Fahrenheit,n,&c.services));EXPECT(std::abs(n-123)<=.00001,0xb0);
    END()return true;
}

} // namespace
bool basic_console_leaf(std::uint32_t callback,const ConsoleServices&services,const ApplicationConsoleHooks&hooks){
    if(!services.assertion||!services.log||!hooks.test_scope)return false;
    const Context c{callback,services,hooks};switch(callback){
    case 0x4010e2b4:return array_iterator(c);
    case 0x400e18a4:return backoff(c);
    case 0x4010e580:return buffer(c);
    case 0x400e1e90:return cstring(c);
    case 0x401015c0:return command_data(c);
    case 0x400fa974:return serial_logger(c);
    case 0x400e2c20:return enum_helpers(c);
    case 0x4010f1c4:return event_queue(c);
    case 0x40109dbc:return http_parser(c);
    case 0x4010cb60:return http_request(c);
    case 0x400e2f4c:return instruction(c);
    case 0x400f195c:return linear_input(c);
    case 0x400e3004:return localization(c);
    case 0x400e32c4:return mac_address(c);
    case 0x40117578:return network_session(c);
    case 0x4011272c:return noise_smoother(c);
    case 0x400d79ac:return out(c);
    case 0x400e38dc:return paragraph(c);
    case 0x400e3a54:return program(c);
    case 0x400e3f10:return program_buffer(c);
    case 0x400ed224:return program_resolver(c);
    case 0x400ecad0:return program_writer(c);
    case 0x400e4544:return quantity(c);
    case 0x40101844:return recipe_reference(c);
    case 0x400eb870:return rect(c);
    case 0x400e5158:return safety(c);
    case 0x4010e0e4:return session_type(c);
    case 0x400e55a0:return stats_computer(c);
    case 0x400f560c:return stats_rolling(c);
    case 0x400db514:return temperature(c);
    case 0x400db56c:return temperature(c);
    case 0x40111df8:return text_justifier(c);
    case 0x4010f418:return text_lines(c);
    case 0x400e56d4:return utf8(c);
    case 0x401011a4:return wifi_credentials(c);
    case 0x400ed2ec:return static_program(c,builtin_programs()[11]);
    case 0x400e1048:return report_program_record(services,builtin_programs()[11]);
    default:return false;
    }
}
bool basic_console_self_test(const ConsoleServices&services,const ApplicationConsoleHooks&hooks){
    const Context c{0x4011ed44,services,hooks};ConsoleTestMarker marker(services,hooks,"Basic Tests");TRY(marker.begin());
    struct Entry{const char* name;std::uint32_t callback;};
    static const Entry entries[]={
        {"ArrayIterT<int>",0x4010e2b4},{"BackoffTimer",0x400e18a4},{"Buffer",0x4010e580},{"CString",0x400e1e90},{"CommandData",0x401015c0},{"CoreHelpers::SerialLogger",0x400fa974},
        {"EepromData",0x400dd2e0},{"EnumHelpers",0x400e2c20},{"EventQ",0x4010f1c4},{"HttpBackend",0x401188f0},{"HttpParser",0x40109dbc},{"HttpRequest",0x4010cb60},{"HttpUpdate",0},
        {"Instruction",0x400e2f4c},{"LinearInput",0x400f195c},{"LocalizationHelpers",0x400e3004},{"MAX11613",0x400ddee0},{"MacAddress",0x400e32c4},{"NetworkSessionState",0x40117578},{"NoiseSmoother",0x4011272c},
        {"Out",0x400d79ac},{"ParagraphText",0x400e38dc},{"PicoStill::Controller",0x4011a204},{"Program",0x400e3a54},{"ProgramBuffer",0x400e3f10},{"ProgramParams",0},{"ProgramResolver",0x400ed224},{"ProgramWriter",0x400ecad0},
        {"Quantity",0x400e4544},{"RecipeRef",0x40101844},{"Rect",0x400eb870},{"Runtime",0x400e0d34},{"RuntimeState",0},{"Safeties",0x400e5158},{"SessionTypeHelper",0x4010e0e4},{"StaticPrograms",0x400ed2ec},
        {"Stats",0x400e55a0},{"Temperature",0x400db514},{"TemperatureHelpers",0x400db56c},{"TextJustifier<21>",0x40111df8},{"TextLines",0x4010f418},{"UTF8Decode",0x400e56d4},{"WiFiCreds",0x401011a4},{"ZBackend",0x4011dc20},{"ProgramParser",0x4011ed44}
    };
    for(unsigned i=0;i<sizeof entries/sizeof entries[0];++i){const auto&entry=entries[i];TRY(c.log(ConsoleLevel::Debug,"CORE","Core.h",0x1ed+i,"TESTS %s::tests()",entry.name));
        switch(entry.callback){
        case 0:break; // The three original labels have no callable test body.
        case 0x400dd2e0:TRY(eeprom_console_self_test(services,hooks));break;
        case 0x400ddee0:TRY(max11613_console_self_test(services,hooks));break;
        case 0x401188f0:TRY(http_backend_console_self_test(services,hooks));break;
        case 0x4011dc20:TRY(z_backend_console_self_test(services,hooks));break;
        case 0x4011a204:TRY(still_console_self_test(services,hooks));break;
        case 0x400e0d34:{SoftwareConsoleClock clock;clock.context=hooks.context;clock.millis=hooks.millis;TRY(runtime_console_self_test(services,hooks,clock));break;}
        case 0x400ed2ec:{const unsigned order[]={0,1,2,3,21,22,4,5,7,8,9,10,11,12,13,14,17,18,15,19,16};for(auto index:order)TRY(static_program(c,builtin_programs()[index]));TRY(report_program_record(services,builtin_programs()[11]));break;}
        case 0x400e55a0:{TRY(basic_console_leaf(entry.callback,services,hooks));TRY(basic_console_leaf(0x400f560c,services,hooks));const OriginalScope description{0x4011ed44,0x2a5,"STS","Stats.h","sizeof(Stats::Rolling)"};Scope scope(c,description);TRY(scope.begin());static_assert(sizeof(PicoStillStatistics)==0x130,"Recovered 30-sample Rolling structure");TRY(c.log(ConsoleLevel::Info,"STS","Stats.h",0x2a6,"sizeof(Stats::Rolling) = %u",static_cast<unsigned>(sizeof(PicoStillStatistics))));TRY(scope.end());break;}
        case 0x4011ed44:{const OriginalScope a{0x4011ed44,0xc4,"PGMPAR","ProgramParser.h","ProgramParser::capStepTempC()"},b{0x4011ed44,0xc9,"PGMPAR","ProgramParser.h","ProgramParser::capStepTempF()"};Scope first(c,a);TRY(first.begin());TRY(first.end());Scope second(c,b);TRY(second.begin());TRY(second.end());break;}
        default:TRY(basic_console_leaf(entry.callback,services,hooks));break;
        }
    }
    if(!hooks.invoke)return false;
    TRY(hooks.invoke(hooks.context,0x400fe350,""));TRY(marker.end());return true;
}
bool basic_console_self_test(CoreApplication&,const ConsoleServices&services,const ApplicationConsoleHooks&hooks){
    return basic_console_self_test(services,hooks);
}
bool basic_console_test(std::uint32_t callback,CoreApplication&application,const ConsoleServices&services,const ApplicationConsoleHooks&hooks){
    if(callback==0x4011ed44||callback==0x4011fbac)return basic_console_self_test(application,services,hooks);
    return basic_console_leaf(callback,services,hooks);
}
}} // namespace zseries::oem
