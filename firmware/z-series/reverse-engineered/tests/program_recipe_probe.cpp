
#include "../adapters/arduinojson/recipe_document.hpp"
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

static void tree(std::ostream& out,ArduinoJson::JsonVariantConst value){
    if(value.isNull()){out<<"null";return;}
    if(value.is<bool>()){out<<"{\"kind\":\"bool\",\"value\":"<<(value.as<bool>()?"true":"false")<<'}';return;}
    if(value.is<const char*>()){out<<"{\"kind\":\"string\",\"value\":";quoted(out,value.as<const char*>());out<<'}';return;}
    if(value.is<std::int32_t>()){out<<"{\"kind\":\"integer\",\"value\":"<<value.as<std::int32_t>()<<'}';return;}
    if(value.is<std::uint32_t>()){out<<"{\"kind\":\"integer\",\"value\":"<<value.as<std::uint32_t>()<<'}';return;}
    if(value.is<double>()){out<<"{\"kind\":\"double\",\"bits\":";bits(out,value.as<double>());out<<'}';return;}
    if(value.is<ArduinoJson::JsonArrayConst>()){
        out<<"{\"kind\":\"array\",\"value\":[";bool first=true;
        for(auto child:value.as<ArduinoJson::JsonArrayConst>()){if(!first)out<<',';first=false;tree(out,child);}
        out<<"]}";return;
    }
    out<<"{\"kind\":\"object\",\"value\":[";bool first=true;
    for(auto child:value.as<ArduinoJson::JsonObjectConst>()){
        if(!first)out<<',';
        first=false;out<<"{\"key\":";quoted(out,child.key().c_str());out<<",\"value\":";tree(out,child.value());out<<'}';
    }out<<"]}";
}
static void one(unsigned action,std::size_t capacity,unsigned nesting,double pressure,double maximum,
                int abv,int ibu,int resumed,unsigned session,std::vector<char>& input){
    ProgramBuffer buffer;unsigned notifications=0,backend=0x5555,error_code=0;std::size_t used=0;
    std::string error="unchanged",json_tree="null";
    if(action==0){
        ArduinoJson::DynamicJsonDocument document(JSON_ARRAY_SIZE(capacity/16));
        const auto status=json_adapter::decode(document,input.data(),input.size()-1,static_cast<std::uint8_t>(nesting));
        error_code=static_cast<unsigned>(status);error=recipe_json_error_name(status);
        used=document.memoryUsage()/JSON_ARRAY_SIZE(1)*16;
        std::ostringstream value;tree(value,document.as<ArduinoJson::JsonVariantConst>());json_tree=value.str();
    }else if(action==1){
        RecipeNotification notification;notification.context=&notifications;notification.ready=[](void* p){++*static_cast<unsigned*>(p);};
        const auto result=parse_local_recipe(buffer,input.data(),input.size()-1,{pressure,maximum},notification);
        if(result.ready)backend=7;
        if(result.json_error==RecipeJsonError::Ok)error="";
        else error="deserializeJson error "+std::to_string(static_cast<unsigned>(result.json_error))+" "+recipe_json_error_name(result.json_error);
    }else{
        ProgramParameters parameters;parameters.id=ZProgramId::Buffered;parameters.abv=abv;parameters.ibu=ibu;
        parameters.time={123,Unit::Second};parameters.water={99,Unit::Gallon};parameters.intensity={7,Unit::Intensity};
        parameters.temperature={60,Unit::Fahrenheit};parameters.resumed_session=resumed!=0;
        const auto result=parse_http_recipe(buffer,input.data(),input.size()-1,
            action==2?HttpRecipeFormat::Zymatic:HttpRecipeFormat::PicoPak,static_cast<SessionType>(session),123,
            parameters,{pressure,maximum});
        error_code=static_cast<unsigned>(result.json_error);
        error=result.json_error==RecipeJsonError::Ok?"":recipe_json_error_name(result.json_error);
    }
    std::cout<<"{\"assertion\":"<<buffer.assertion_line<<",\"error_code\":"<<error_code<<",\"error\":";quoted(std::cout,error.c_str());
    std::cout<<",\"tree\":"<<json_tree<<",\"pool_bytes\":"<<used<<",\"buffer\":";record(std::cout,buffer.record());
    std::cout<<",\"parameters\":";parameters(std::cout,buffer.parameters);std::cout<<",\"stats\":";stats(std::cout,buffer.stats);
    std::cout<<",\"backend_state\":"<<backend<<",\"notifications\":"<<notifications<<"}\n";
}
static int integration(){
    ProgramBuffer buffer;
    unsigned notified=0;
    RecipeNotification notification;notification.context=&notified;notification.ready=[](void* p){++*static_cast<unsigned*>(p);};
    char input[]="{\"Name\":\"Owned recipe\",\"IsBeer\":true,\"Steps\":[{\"Name\":\"Owned pause\",\"StepType\":2}]}";
    const auto parsed=parse_local_recipe(buffer,input,sizeof(input)-1,{101325,100},notification);
    if(!parsed.ready||notified!=1||buffer.record().view.code_size!=4)return 1;
    std::memset(input,'x',sizeof(input));
    if(std::strcmp(buffer.record().view.name,"Owned recipe")||std::strcmp(buffer.record().view.strings[1],"Owned pause"))return 2;
    ProgramRepository repository(buffer);ProgramRuntime runtime;
    runtime.services.context=&repository;runtime.services.find_program=ProgramRepository::runtime_find;
    runtime.services.millis=[](void*)->Word{return 123;};
    bool selected=false;
    if(runtime.select_program(24,selected)!=RuntimeResult::Ok||!selected||runtime.program_load_result!=RuntimeResult::Ok)return 3;
    for(std::size_t i=0;i<4;i++){
        Instruction got{OpCode::Noop,0};const auto& expected=buffer.record().view.code[i];
        if(runtime.stack.pop(got)!=RuntimeResult::Ok||got.opcode!=expected.opcode||got.argument!=expected.argument)return 4;
    }
    char malformed_http[]="{\"Name\":";ProgramParameters parameters;
    const auto http=parse_http_recipe(buffer,malformed_http,sizeof(malformed_http)-1,HttpRecipeFormat::Zymatic,
        SessionType::BrewZRecipe,55,parameters,{101325,100});
    if(http.ready||http.json_error!=RecipeJsonError::IncompleteInput||buffer.record().view.code_size!=4||
       std::strcmp(buffer.record().view.name,"Owned recipe"))return 5;
    char malformed_local[]="{\"Name\":";
    const auto local=parse_local_recipe(buffer,malformed_local,sizeof(malformed_local)-1,{101325,100},notification);
    if(local.ready||local.json_error!=RecipeJsonError::IncompleteInput||!buffer.record().empty()||notified!=1)return 6;
    return 0;
}
int main(int argc,char** argv){
    if(argc==2&&std::strcmp(argv[1],"--integration")==0)return integration();
    if(argc==2&&std::strcmp(argv[1],"--abi")==0){
        std::cout<<"{\"pointer\":"<<sizeof(void*)<<",\"long\":"<<sizeof(long)<<",\"json_integer\":"<<sizeof(ArduinoJson::JsonUInt)
                 <<",\"local_capacity\":"<<json_adapter::local_capacity<<",\"http_capacity\":"<<json_adapter::http_capacity<<"}\n";return 0;
    }
    std::string line;
    while(std::getline(std::cin,line)){
        std::istringstream values(line);unsigned action,nesting,session;int abv,ibu,resumed;std::size_t capacity;double pressure,maximum;std::string hex;
        if(!(values>>action>>capacity>>nesting>>pressure>>maximum>>abv>>ibu>>resumed>>session))return 2;
        values>>hex;
        std::vector<char> input;for(std::size_t i=0;i<hex.size();i+=2)input.push_back(static_cast<char>(std::stoul(hex.substr(i,2),nullptr,16)));input.push_back(0);
        one(action,capacity,nesting,pressure,maximum,abv,ibu,resumed,session,input);
    }
}
