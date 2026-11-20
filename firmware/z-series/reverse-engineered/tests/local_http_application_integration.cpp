// Compose framing, real JSON decoding, program ownership and Core publication.
#include "zseries/local_http_application.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
using namespace zseries::oem;

struct Wire {
    bool connected=false, accept_pending=false;
    unsigned closed=0, writes=0, fail_write=0;
    std::string input,output;
    std::size_t position=0;
    void bind(LocalHttpServer& server) {
        auto& io=server.services.transport;io.context=this;
        io.connected=[](void* p){return static_cast<Wire*>(p)->connected;};
        io.accept=[](void* p){auto& w=*static_cast<Wire*>(p);w.connected=w.accept_pending;w.accept_pending=false;return true;};
        io.available=[](void* p){auto& w=*static_cast<Wire*>(p);return static_cast<int>(w.input.size()-w.position);};
        io.read=[](void* p){auto& w=*static_cast<Wire*>(p);return w.position<w.input.size()?static_cast<unsigned char>(w.input[w.position++]):-1;};
        io.close=[](void* p){auto& w=*static_cast<Wire*>(p);w.connected=false;++w.closed;return true;};
        io.begin_listener=[](void*){return true;};io.stop_listener=[](void*){return true;};
        io.write=[](void* p,const std::uint8_t* bytes,std::size_t size){
            auto& w=*static_cast<Wire*>(p);++w.writes;
            if(w.fail_write&&w.writes==w.fail_write){if(size)w.output.append(reinterpret_cast<const char*>(bytes),size-1);return false;}
            w.output.append(reinterpret_cast<const char*>(bytes),size);return true;
        };
    }
    LocalHttpResult send(LocalHttpServer& server,const std::string& request,std::size_t fragment=1,unsigned fail=0) {
        input.clear();output.clear();position=0;writes=0;fail_write=fail;accept_pending=true;
        assert(server.tick(1000)==LocalHttpResult::Ok);assert(connected);assert(!server.frozen);
        LocalHttpResult result=LocalHttpResult::Ok;
        for(std::size_t offset=0;offset<request.size();offset+=fragment){
            input.append(request,offset,std::min(fragment,request.size()-offset));
            result=server.tick(1001);
            if(result!=LocalHttpResult::Ok)break;
        }
        return result;
    }
};

static std::string post(const std::string& body,const char* path="/recipe") {
    return std::string("POST ")+path+" HTTP/1.1\r\nContent-Length: "+std::to_string(body.size())+
        "\r\nContent-Type: application/json\r\n\r\n"+body;
}
static const char recipe[]=R"({"Name":"Owned recipe","IsBeer":true,"Steps":[{"Name":"Pause here","StepType":2}]})";
int main() {
    LocalHttpServer server;ProgramBuffer program;CoreEvent event;CoreEventMailbox mailbox;
    LocalRecipePublication publication(event,mailbox);HeaterSensors sensors;HeaterController heater;
    AuxiliarySensors auxiliary;auxiliary.pressure_pa=101325;z_msp_state msp{};ProgramRuntime runtime;std::int32_t rssi=-30;
    LocalHttpApplication app(server,program,publication,sensors,heater,auxiliary,msp,runtime,rssi);Wire wire;
    app.bind();wire.bind(server);assert(server.enable(true)==LocalHttpResult::Ok);

    event.argument=123;event.second_argument=456;event.count=20;event.offset=40;event.total=80;
    event.recipes[5].id=222;std::strcpy(event.recipes[5].name.data(),"Existing recipe");event.recipes[5].name_length=15;
    assert(wire.send(server,post(recipe))==LocalHttpResult::Ok);
    assert(wire.output=="HTTP/1.0 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    assert(server.frozen&&server.request_complete&&!wire.connected);
    assert(program.record().view.code_size==4&&std::strcmp(program.record().view.name,"Owned recipe")==0);
    assert(std::strcmp(program.record().view.strings[1],"Pause here")==0);
    assert(mailbox.pending&&mailbox.value.type==CoreEventType::ProgramLoaded);
    assert(mailbox.value.argument==123&&mailbox.value.second_argument==456&&mailbox.value.count==0&&mailbox.value.offset==0&&mailbox.value.total==0);
    assert(mailbox.value.recipes[5].id==222&&std::strcmp(mailbox.value.recipes[5].name.data(),"Existing recipe")==0);
    assert(mailbox.value.source==CoreCommandId::None);
    // Body parsing borrows mutable JSON only during the call. Owned code/names
    // remain available after the transport reuses its entire input buffer.
    std::fill(server.buffer.begin(),server.buffer.end(),'x');
    assert(std::strcmp(program.record().view.name,"Owned recipe")==0);
    ProgramRepository repository(program);runtime.services.context=&repository;runtime.services.find_program=ProgramRepository::runtime_find;
    runtime.services.millis=[](void*)->std::uint32_t{return 7;};bool selected=false;
    assert(runtime.select_program(24,selected)==RuntimeResult::Ok&&selected);

    mailbox.value.type=CoreEventType::StateTransition;mailbox.value.argument=987;
    assert(wire.send(server,post(recipe),7)==LocalHttpResult::Ok);
    assert(mailbox.pending&&mailbox.value.type==CoreEventType::StateTransition&&mailbox.value.argument==987);
    assert(event.type==CoreEventType::ProgramLoaded);

    const std::string before=program.record().view.name;
    assert(wire.send(server,post(recipe,"/other"),11)==LocalHttpResult::Ok);
    assert(wire.output=="HTTP/1.0 400 BAD REQUEST\r\nContent-Length: 11\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nBAD REQUEST");
    assert(!server.frozen&&program.record().view.name==before&&mailbox.value.argument==987);
    assert(wire.send(server,post("{\"Name\":"),3)==LocalHttpResult::Ok);
    assert(wire.output.find("deserializeJson error 1 IncompleteInput")!=std::string::npos);
    assert(program.record().empty()&&server.frozen&&mailbox.value.argument==987);

    // The selected bounded builder rejects missing/empty names. The original
    // diagnostic assertions return and may publish an unnamed program; this
    // deliberate validation deviation must reject without a ready event.
    mailbox.pending=false;
    assert(wire.send(server,post("{\"Name\":\"\"}"))==LocalHttpResult::Ok);
    assert(wire.output.find("400 BAD REQUEST")!=std::string::npos&&wire.output.find("program build error 3")!=std::string::npos);
    assert(!mailbox.pending);

    // Actual state references are sampled when a request arrives, after bind.
    for(unsigned i=0;i<4;++i)sensors.temperatures[i].celsius=20+i;
    heater.state.target_celsius=64.5;msp.valve_position=6;msp.pumps=0x10;
    runtime.state.pause_reason=2;runtime.state.step_name="Live mash";runtime.state.elapsed_seconds=11;
    runtime.state.total_left_seconds=22;runtime.state.flags=1;rssi=-73;
    assert(wire.send(server,"GET /state HTTP/1.1\r\n\r\n",2)==LocalHttpResult::Ok);
    assert(wire.output.find("HTTP/1.0 200 OK\r\n")==0&&wire.output.find("Content-Type: application/json\r\n")!=std::string::npos);
    assert(wire.output.find("\"ThermoBlockTemp\":20,\"WortTemp\":21,\"AmbientTemp\":23,\"DrainTemp\":22")!=std::string::npos);
    assert(wire.output.find("\"rssi\":-73,\"TargetTemp\":64.5,\"StepName\":\"Live mash\"")!=std::string::npos);
    assert(wire.output.find("\"SecondsEllapsed\":11,\"SecondsRemaining\":22,\"UsePicoPakMode\":1,\"Location\":3")!=std::string::npos);

    // Recipe processing precedes the response; a failed socket write must be
    // reported and closed without undoing a program already queued by the OEM.
    mailbox.pending=false;
    const unsigned closes=wire.closed;
    assert(wire.send(server,post(recipe),4096,1)==LocalHttpResult::ServiceFailure);
    assert(wire.closed==closes+1&&!wire.connected&&mailbox.pending&&program.record().has_program());
    assert(wire.send(server,post("{"),4096,13)==LocalHttpResult::ServiceFailure);
    assert(!wire.connected&&program.record().empty());

    assert(wire.send(server,post(recipe),4096)==LocalHttpResult::Ok);
    const std::string large(2048,'x');
    assert(wire.send(server,post(large),4096)==LocalHttpResult::Ok);
    assert(wire.output=="HTTP/1.0 400 BAD REQUEST\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    assert(std::strcmp(program.record().view.name,"Owned recipe")==0&&!server.frozen);

    LocalHttpServer unbound;
    assert(route_local_http_post(unbound,program,{101325,100},publication)==LocalHttpResult::MissingService);
    assert(server.enable(false)==LocalHttpResult::Ok);
}
