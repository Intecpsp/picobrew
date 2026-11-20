
#include "zseries/local_http_application.hpp"
#include "core_event_fixture.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>
#include <iostream>
#include <string>
using namespace zseries::oem;
using Word=std::uint32_t;
static void quoted(std::ostream& out,const char* text){
    out << '"';if(!text)text="<null>";
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p){
        switch(*p){case '"':out<<"\\\"";break;case '\\':out<<"\\\\";break;
        case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;
        default:if(*p<32)out<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(*p)<<std::dec;else out<<*p;}
    }out << '"';
}
static void bits(std::ostream& out,double value){
    std::uint64_t b;std::memcpy(&b,&value,8);out << '"' << std::hex << std::setfill('0') << std::setw(16) << b << std::dec << '"';
}
static void record(std::ostream& out,const ProgramRecord& p){
    out<<"{\"type\":"<<static_cast<Word>(p.type)<<",\"id\":"<<p.unique_id<<",\"name\":";quoted(out,p.view.name);
    out<<",\"code\":[";for(std::size_t i=0;i<p.view.code_size;i++){if(i)out<<',';out<<'['<<static_cast<Word>(p.view.code[i].opcode)<<','<<p.view.code[i].argument<<']';}
    out<<"],\"strings\":[";for(std::size_t i=0;i<p.view.string_count;i++){if(i)out<<',';quoted(out,p.view.strings[i]);}out<<"]}";
}
static void parameters(std::ostream& out,const ProgramParameters& p){
    out<<"{\"id\":"<<static_cast<Word>(p.id)<<",\"abv\":"<<static_cast<Word>(p.abv)<<",\"ibu\":"<<static_cast<Word>(p.ibu);
    const char* names[]={"time","water","intensity","temperature"};const Quantity quantities[]={p.time,p.water,p.intensity,p.temperature};
    for(unsigned i=0;i<4;i++){out<<",\""<<names[i]<<"\":[";bits(out,quantities[i].value);out<<','<<static_cast<Word>(quantities[i].unit)<<']';}
    out<<",\"resumed\":"<<p.resumed_session<<'}';
}
static void stats(std::ostream& out,const ProgramStats& p){
    out<<"{\"rate\":";bits(out,p.heating_rate_celsius_per_minute);out<<",\"duration\":"<<p.duration_seconds<<",\"pressure\":";
    bits(out,p.pressure_pascal);out<<",\"maximum\":";bits(out,p.maximum_temperature_celsius);out<<",\"added\":"<<p.maximum_temperature_added_seconds<<'}';
}

static std::vector<std::uint8_t> unhex(const std::string& text) {
    std::vector<std::uint8_t> result;
    for(std::size_t i=0;i<text.size();i+=2)result.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i,2),nullptr,16)));
    return result;
}
static void hex(std::ostream& out,const std::vector<std::uint8_t>& data) {
    out<<'"';for(std::size_t i=4;i<data.size();++i)out<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(data[i]);out<<std::dec<<'"';
}
struct Response {
    unsigned status=0;std::int32_t length=0;const char* type=nullptr;std::string body;
    static bool headers(void* context,std::uint32_t status,std::int32_t length,const char* type) {
        auto& self=*static_cast<Response*>(context);self.status=status;self.length=length;self.type=type;return true;
    }
    static bool write(void* context,const std::uint8_t* bytes,std::size_t size) {
        static_cast<Response*>(context)->body.append(reinterpret_cast<const char*>(bytes),size);return true;
    }
};
static int one(bool pending,double pressure,double maximum,const std::string& path_hex,
               const std::string& json_hex,const std::string& current_hex,const std::string& queued_hex) {
    LocalHttpServer server;ProgramBuffer buffer;CoreEvent event;CoreEventMailbox mailbox;
    auto current=unhex(current_hex),queued=unhex(queued_hex),path=unhex(path_hex),json=unhex(json_hex);
    if(current.size()!=core_event_fixture::bytes||queued.size()!=core_event_fixture::bytes||
       path.size()>64||json.size()>LocalHttpServer::BufferCapacity)return 3;
    core_event_fixture::load(event,current.data());core_event_fixture::load(mailbox.value,queued.data());mailbox.pending=pending;
    LocalRecipePublication publication(event,mailbox);Response response;
    server.services.response.context=&response;server.services.response.headers=Response::headers;server.services.response.write=Response::write;
    std::copy(path.begin(),path.end(),server.parser.request.path.bytes.begin());
    std::copy(json.begin(),json.end(),server.buffer.begin());server.length=static_cast<std::uint32_t>(json.size());
    if(route_local_http_post(server,buffer,{pressure,maximum},publication)!=LocalHttpResult::Ok)return 4;
    std::cout<<"{\"assertion\":"<<buffer.assertion_line<<",\"status\":"<<response.status<<",\"length\":"<<response.length<<",\"mime\":";
    if(response.type)quoted(std::cout,response.type);else std::cout<<"null";
    std::cout<<",\"body\":";quoted(std::cout,response.body.c_str());
    std::cout<<",\"buffer\":";record(std::cout,buffer.record());std::cout<<",\"parameters\":";parameters(std::cout,buffer.parameters);
    std::cout<<",\"stats\":";stats(std::cout,buffer.stats);std::cout<<",\"frozen\":"<<(server.frozen?"true":"false")
             <<",\"pending\":"<<(mailbox.pending?"true":"false")<<",\"current_hex\":";hex(std::cout,core_event_fixture::save(event));
    std::cout<<",\"queued_hex\":";hex(std::cout,core_event_fixture::save(mailbox.value));std::cout<<"}\n";
    return 0;
}
int main(){
    std::string line;
    while(std::getline(std::cin,line)){
        std::istringstream input(line);bool pending;double pressure,maximum;std::string path,json,current,queued;
        if(!(input>>pending>>pressure>>maximum>>path>>json>>current>>queued))return 2;
        const int status=one(pending,pressure,maximum,path=="-"?"":path,json=="-"?"":json,current,queued);if(status)return status;
    }
}
