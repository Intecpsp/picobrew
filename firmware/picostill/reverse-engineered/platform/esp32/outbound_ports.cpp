#include "outbound_ports.hpp"
namespace picostill { namespace oem {
bool ArduinoOutboundClient::connect_ip(std::uint32_t address,unsigned port){return client_.connect(IPAddress(address),port)!=0;}
bool ArduinoOutboundClient::connect_host(const char* host,unsigned port){return client_.connect(host,port)!=0;}
int ArduinoOutboundClient::available(){return client_.available();}
int ArduinoOutboundClient::read(){return client_.read();}
bool ArduinoOutboundClient::connected(){return client_.connected()!=0;}
void ArduinoOutboundClient::write(const char* bytes,std::size_t size){client_.write(reinterpret_cast<const std::uint8_t*>(bytes),size);}
std::string ArduinoOutboundClient::read_line(){const String line=client_.readStringUntil('\n');return std::string(line.c_str(),line.length());}
std::size_t ArduinoOutboundClient::read_bytes(std::uint8_t* bytes,std::size_t size){return client_.readBytes(bytes,size);}
void ArduinoOutboundClient::stop(){client_.stop();}
void ArduinoOutboundClient::flush(){client_.flush();}
bool ArduinoUpdatePorts::begin(std::uint32_t size){return update_.begin(size,U_FLASH,-1);}
std::size_t ArduinoUpdatePorts::write(std::uint8_t* bytes,std::size_t size){return update_.write(bytes,size);}
bool ArduinoUpdatePorts::end(){return update_.end(false);}
bool ArduinoUpdatePorts::finished(){return update_.isFinished();}
unsigned ArduinoUpdatePorts::error(){return update_.getError();}
std::string ArduinoUpdatePorts::md5(){const String value=update_.md5String();return std::string(value.c_str(),value.length());}
void ArduinoUpdatePorts::console(const char* message,bool line){if(line)serial_.println(message);else serial_.print(message);}
}} // namespace picostill::oem
