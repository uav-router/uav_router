#ifndef __SOCKADDR_H__
#define __SOCKADDR_H__

#include <memory>
#include <unistd.h>
#include <iostream>
#include <forward_list>
#include <avahi-common/address.h>
#include "err.h"

class SockAddr {
public:
  SockAddr();
  
  SockAddr(SockAddr&& addr);
  void init(SockAddr&& addr);
  
  SockAddr(const SockAddr& addr);
  
  SockAddr(sockaddr *addr, socklen_t len);
  void init(sockaddr *addr, socklen_t len);
  
  SockAddr(addrinfo *ai);
  void init(addrinfo *ai);
  
  SockAddr(in_addr_t address, uint16_t port);
  void init(in_addr_t address, uint16_t port);
  void init(in6_addr address, uint16_t port);
  
  auto init(const std::string& address, uint16_t port = 0) -> bool;
  
  SockAddr(int fd);
  void init(int fd);
  
  SockAddr(const AvahiAddress * addr, uint16_t port);
  void init(const AvahiAddress * addr, uint16_t port);
  
  ~SockAddr();

  auto sock_addr() -> struct sockaddr *;
  auto len() -> socklen_t;
  auto size() -> socklen_t&;
  
  auto is_ip4() -> bool;
  auto is_any() -> bool;
  auto family() -> int;
  
  auto ip4_addr_t() -> in_addr_t;
  
  auto port() -> uint16_t;
  void set_port(uint16_t port);
  
  auto bind(int fd) ->error_c;
  auto connect(int fd) ->error_c;
  auto accept(int fd) ->int;
  auto to_avahi(AvahiAddress& addr) -> bool;

  auto operator=(const SockAddr &other) -> SockAddr &;
  auto operator=(SockAddr &&other) noexcept -> SockAddr &;
  enum Format {
    REG_SERVICE,
    IPADDR_ONLY
  };
  auto format(Format f) -> std::string;

  static auto any(int family, uint16_t port = 0) -> SockAddr;
  friend auto operator<<(std::ostream &os, const SockAddr &addr) -> std::ostream&;
  friend auto operator<(const SockAddr& addr1, const SockAddr& addr2) -> bool;

private:
    class SockAddrImpl;
    std::unique_ptr<SockAddrImpl> _impl;
};

auto operator<(const SockAddr& addr1, const SockAddr& addr2) -> bool;
auto operator<<(std::ostream &os, const SockAddr &addr) -> std::ostream&;

class SockAddrList : public std::forward_list<SockAddr> {
public:
    SockAddrList() = default;
    SockAddrList(addrinfo *ai);
    SockAddrList(const SockAddr& addr);
    void add(const SockAddr& addr);
    void add(SockAddr&& addr);
    auto interface(const std::string& name, uint16_t port, int family = AF_UNSPEC) -> error_c;
    auto broadcast(const std::string& name, uint16_t port) -> error_c;
};

#endif  // __SOCKADDR_H__