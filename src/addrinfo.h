#ifndef __ADDRINFO_H__
#define __ADDRINFO_H__

#include <memory>
#include <unistd.h>
#include "err.h"
#include "epoll.h"
#include <iostream>

class SockAddr {
public:
  SockAddr();
  SockAddr(SockAddr&& addr);
  SockAddr(sockaddr *addr, socklen_t len);
  SockAddr(addrinfo *ai);
  SockAddr(in_addr_t address, uint16_t port);
  SockAddr(const std::string& address, uint16_t port);
  ~SockAddr();

  void init(sockaddr *addr, socklen_t len);
  void init(addrinfo *ai);
  void init(in_addr_t address, uint16_t port);
  
  auto sock_addr() -> struct sockaddr *;
  auto len() -> socklen_t;
  auto size() -> socklen_t&;
  
  auto is_ip4() -> bool;
  auto is_any() -> bool;
  
  auto ip4_addr_t() -> in_addr_t;
  auto port() -> uint16_t;
  
  auto operator=(const SockAddr &other) -> SockAddr &;
  auto operator=(SockAddr &&other) noexcept -> SockAddr &;
  friend auto operator<<(std::ostream &os, const SockAddr &addr) -> std::ostream&;
private:
    class SockAddrImpl;
    std::unique_ptr<SockAddrImpl> _impl;
};

auto operator<<(std::ostream &os, const SockAddr &addr) -> std::ostream&;

class AddressResolver : public error_handler {
public:
    using callback_t = std::function<void(addrinfo*)>;
    virtual void on_resolve(callback_t func) = 0;

    virtual void remote(const std::string& host, uint16_t port, IOLoop* loop)=0;
    virtual void local(uint16_t port, IOLoop* loop, const std::string& host="")=0;
    virtual void local_interface(const std::string& interface, uint16_t port, IOLoop* loop)=0;
    virtual void broadcast(uint16_t port, IOLoop* loop, const std::string& interface="")=0;
    enum Interface {
        ADDRESS,
        BROADCAST
    };
    virtual auto resolve_interface_ip4(const std::string& interface, uint16_t port, Interface type) -> error_c = 0;
    static auto create() -> std::unique_ptr<AddressResolver>;
};

//TODO: when address resolved on interface or host name the only IPv4 addresses returned. So
//      need to implement IPv6 support

#endif //__ADDRINFO_H__