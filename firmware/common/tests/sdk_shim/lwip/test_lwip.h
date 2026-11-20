#pragma once
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
using err_t=int;
constexpr err_t ERR_OK=0,ERR_INPROGRESS=-5;
struct ip_addr_t {std::uint32_t address;};
#define IP_IS_V4(p) ((p)!=nullptr)
#define ip_2_ip4(p) (p)
#define ip4_addr_get_u32(p) ((p)->address)
using tcpip_callback_fn=void(*)(void*);
using dns_found_callback=void(*)(const char*,const ip_addr_t*,void*);
err_t tcpip_callback_with_block(tcpip_callback_fn,void*,unsigned char);
err_t dns_gethostbyname(const char*,ip_addr_t*,dns_found_callback,void*);
#define lwip_socket ::socket
#define lwip_bind ::bind
#define lwip_listen ::listen
#define lwip_accept ::accept
#define lwip_connect ::connect
#define lwip_select ::select
#define lwip_fcntl ::fcntl
#define lwip_setsockopt ::setsockopt
#define lwip_getsockopt ::getsockopt
#define lwip_shutdown ::shutdown
#define lwip_close ::close
#define lwip_recv ::recv
#define lwip_send ::send
