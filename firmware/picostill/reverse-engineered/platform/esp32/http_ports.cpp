#include "http_ports.hpp"
namespace picostill { namespace oem {
bool ArduinoHttpStream::connected(){return client_.connected()!=0;}
int ArduinoHttpStream::available(){return client_.available();}
int ArduinoHttpStream::read(){return client_.read();}
std::size_t ArduinoHttpStream::write(const char* bytes,std::size_t size){return client_.write(reinterpret_cast<const std::uint8_t*>(bytes),size);}
void ArduinoHttpStream::flush(){client_.flush();}
void ArduinoHttpStream::stop(){client_.stop();}
void ArduinoHttpStream::decimal(double value,unsigned digits){client_.print(value,digits);}
void ArduinoHttpStream::unsigned_number(std::uint32_t value){client_.print(static_cast<unsigned long>(value),10);}
void ArduinoHttpListener::listen(unsigned port){server_.begin(port);}
HttpStream* ArduinoHttpListener::accept(){client_=server_.available();return client_?&stream_:nullptr;}
void ArduinoHttpListener::release(HttpStream&){client_=WiFiClient();}
std::uint32_t ArduinoHttpRuntime::millis(){return ports_.millis();}
void ArduinoHttpRuntime::cooperate(){::yield();}
void ArduinoHttpRuntime::variable(const char* scope,const char* name,const char* value){ports_.variable(scope,name,value);}
void ArduinoHttpRuntime::integer(const char* scope,const char* name,std::uint32_t value){serial_.printf("VAR [%s] %s=%u\r\n",scope,name,value);}
void ArduinoHttpRuntime::message(const char* value){ports_.message(value);}
}} // namespace picostill::oem
