#include "core_flow_fixture.hpp"
#include "zseries/core_commands.hpp"
using namespace core_flow_fixture;
namespace {
using Word=std::uint32_t;
void number(std::vector<Word>& out,double value){std::uint64_t bits;std::memcpy(&bits,&value,8);out.push_back(bits);out.push_back(bits>>32);}
void quantity(std::vector<Word>& out,const Quantity& q){number(out,q.value);out.push_back(static_cast<Word>(q.unit));}
void parameters(std::vector<Word>& out,const ProgramParameters& p){out.insert(out.end(),{static_cast<Word>(p.id),static_cast<Word>(p.abv),static_cast<Word>(p.ibu)});quantity(out,p.time);quantity(out,p.water);quantity(out,p.intensity);quantity(out,p.temperature);out.push_back(p.resumed_session);}
void stats(std::vector<Word>& out,const ProgramStats& p){number(out,p.heating_rate_celsius_per_minute);out.push_back(p.duration_seconds);number(out,p.pressure_pascal);number(out,p.maximum_temperature_celsius);out.push_back(p.maximum_temperature_added_seconds);}
struct CommandPort:Port{std::vector<Word> commands;};
bool command_perform(void* p,const CoreAction& a,Word& result){
    auto& port=*static_cast<CommandPort*>(p);
    if(a.runtime_command){const auto& c=*a.runtime_command;port.event(static_cast<Word>(a.effect),static_cast<Word>(c.id));
        auto& out=port.commands;out.push_back(static_cast<Word>(c.id));out.push_back(c.program_id);number(out,c.temperature);out.push_back(c.duration);out.push_back(c.location);
        out.push_back(c.manual.byte_00);out.push_back(c.manual.location);quantity(out,c.manual.temperature);quantity(out,c.manual.hold);quantity(out,c.manual.drain);
        out.push_back(c.manual.bytes_38[0]|(Word(c.manual.bytes_38[1])<<8)|(Word(c.manual.bytes_38[2])<<16)|(Word(c.manual.bytes_38[3])<<24));result=0;return true;}
    return perform(static_cast<Port*>(&port),a,result);
}
void seed_parameters(ProgramParameters& p,Word id){p.id=static_cast<ZProgramId>(id);p.abv=7;p.ibu=11;p.time={600,Unit::Second};p.water={9,Unit::Liter};p.intensity={4,Unit::Intensity};p.temperature={70,Unit::Celsius};p.resumed_session=true;}
}
extern "C" int core_commands_action(unsigned kind,Word* w,Word* x,const double* values,Word* events,std::size_t* event_size,char* credentials,
    Word* session,char* name,Word* posted,std::size_t* posted_size,int missing){
    EepromData config;CoreFlow core(config);load(core,w);ProgramBuffer buffer;ProgramRepository repository(buffer);
    if(x[17])buffer.start(static_cast<SessionType>(x[15]),x[16],"Buffered fixture");
    seed_parameters(buffer.parameters,x[18]);buffer.stats.heating_rate_celsius_per_minute=2;buffer.stats.duration_seconds=321;buffer.stats.pressure_pascal=1234;
    buffer.stats.maximum_temperature_celsius=88;buffer.stats.maximum_temperature_added_seconds=456;
    TemperatureAcquisition acquisition;HeaterController heater;HeaterSensors temperatures;AuxiliarySensors auxiliary;
    config.boiler=static_cast<BoilerType>(x[7]);config.use_imperial=x[20];heater.pid.sample_time_ms=x[25];heater.pid.reverse=x[26];
    acquisition.adc.sensor_types[0]=static_cast<TempSensorType>(x[14]);for(unsigned i=0;i<4;i++)acquisition.adc.raw[i]=x[10+i];
    auxiliary.pressure_pa=values[0];temperatures.estimated_boil_celsius=values[1];
    CoreCommands commands(core,repository,acquisition,heater,temperatures,auxiliary);commands.program_source_17b0=x[8];commands.resumed_session=x[9];
    CoreCommand command;command.id=static_cast<CoreCommandId>(w[55]);command.program_id=x[0];std::memcpy(&command.temperature,&x[1],4);
    command.duration=x[2];command.location=x[3];command.still_id=x[4];text(command.credentials.ssid,x[5]?"command-network":"");text(command.credentials.password,x[6]?"command-password":"");
    command.manual.byte_00=x[23];command.manual.location=x[22];command.manual.temperature={70,Unit::Celsius};command.manual.hold={150,Unit::Second};command.manual.drain={210,Unit::Second};
    for(unsigned i=0;i<4;i++)command.manual.bytes_38[i]=x[24]>>(i*8);
    seed_parameters(command.program_parameters,x[19]);core.commands.value.program_id=x[21];
    CommandPort port;port.now=w[44];port.address=w[42];port.network=w[43];core.services.context=&port;core.services.millis=clock;
    core.services.tick=missing?nullptr:tick;core.services.perform=missing?nullptr:command_perform;
    CoreFlowResult result=kind==0?commands.dispatch(command):kind==1?commands.start_program(x[0]):kind==2?commands.load_coffee(command):commands.set_boiler(command.id);
    save(core,w,credentials);x[7]=static_cast<Word>(config.boiler);x[9]=commands.resumed_session;x[14]=static_cast<Word>(acquisition.adc.sensor_types[0]);x[21]=core.commands.value.program_id;
    std::vector<Word> out;out.push_back(commands.session.session_id);out.push_back(commands.session.unique_program_id);out.push_back(commands.session.use_azure);
    parameters(out,commands.session.parameters);stats(out,commands.session.stats);std::copy(out.begin(),out.end(),session);
    std::strncpy(name,commands.session.name.data(),64);name[64]=0;out.clear();
    number(out,heater.pid.display_kp);number(out,heater.pid.display_ki);number(out,heater.pid.display_kd);number(out,heater.pid.kp);number(out,heater.pid.ki);number(out,heater.pid.kd);
    std::copy(out.begin(),out.end(),x+28);
    *event_size=port.events.size();std::copy(port.events.begin(),port.events.end(),events);*posted_size=port.commands.size();std::copy(port.commands.begin(),port.commands.end(),posted);
    return static_cast<int>(result);
}
