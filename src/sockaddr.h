#ifndef __SOCKADDR_H__
#define __SOCKADDR_H__

#include <memory>
#include <unistd.h>
#include <iostream>
#include <avahi-common/address.h>
#include "err.h"

class SockAddr {
public:
  SockAddr();
  SockAddr(SockAddr&& addr);
  SockAddr(const SockAddr& addr);
  SockAddr(sockaddr *addr, socklen_t len);
  SockAddr(addrinfo *ai);
  SockAddr(in_addr_t address, uint16_t port);
  SockAddr(const std::string& address, uint16_t port);
  SockAddr(int fd);
  SockAddr(const AvahiAddress * addr, uint16_t port);
  ~SockAddr();

  void init(sockaddr *addr, socklen_t len);
  void init(SockAddr&& addr);
  void init(addrinfo *ai);
  void init(in_addr_t address, uint16_t port);
  void init(int fd);
  void init(const AvahiAddress * addr, uint16_t port);
  auto sock_addr() -> struct sockaddr *;
  auto len() -> socklen_t;
  auto size() -> socklen_t&;
  
  auto is_ip4() -> bool;
  auto is_any() -> bool;
  
  auto ip4_addr_t() -> in_addr_t;
  auto port() -> uint16_t;
  
  auto bind(int fd) ->error_c;
  auto connect(int fd) ->error_c;

  auto operator=(const SockAddr &other) -> SockAddr &;
  auto operator=(SockAddr &&other) noexcept -> SockAddr &;
  friend auto operator<<(std::ostream &os, const SockAddr &addr) -> std::ostream&;
  friend auto operator<(const SockAddr& addr1, const SockAddr& addr2) -> bool;

private:
    class SockAddrImpl;
    std::unique_ptr<SockAddrImpl> _impl;
};

auto operator<(const SockAddr& addr1, const SockAddr& addr2) -> bool;
auto operator<<(std::ostream &os, const SockAddr &addr) -> std::ostream&;

#endif  // __SOCKADDR_H__