#include "http_ports.hpp"
namespace picoferm { namespace oem {
bool ArduinoHttpStream::connected(){return client_.connected()!=0;}
int ArduinoHttpStream::available(){return client_.available();}
int ArduinoHttpStream::read(){return client_.read();}
std::size_t ArduinoHttpStream::write(const char* bytes,std::size_t length){return client_.write(reinterpret_cast<const std::uint8_t*>(bytes),length);}
std::size_t ArduinoHttpStream::write_flash(const char* bytes,std::size_t length){return client_.write_P(bytes,length);}
void ArduinoHttpStream::flush(){client_.flush();}
void ArduinoHttpListener::listen(unsigned port){server_.begin(static_cast<std::uint16_t>(port));}
void ArduinoHttpListener::process_dns(){dns_.processNextRequest();}
HttpStream* ArduinoHttpListener::accept(){client_=server_.available();return client_?&stream_:nullptr;}
void ArduinoHttpListener::release(HttpStream&){client_=WiFiClient();}
void ArduinoHttpListener::set_dns_ttl(unsigned value){dns_.setTTL(value);}
void ArduinoHttpListener::set_dns_reply_code(unsigned value){dns_.setErrorReplyCode(static_cast<DNSReplyCode>(value));}
bool ArduinoHttpListener::start_dns(unsigned port,const char* domain,std::uint32_t address){return dns_.start(static_cast<std::uint16_t>(port),String(domain),IPAddress(address));}
void ArduinoHttpListener::stop_dns(){dns_.stop();}
}}
