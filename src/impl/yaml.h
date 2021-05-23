#ifndef __YAML__H__
#define __YAML__H__
#ifdef YAML_CONFIG
#include <chrono>
#include <string>
#include <sys/socket.h>

#include "../inc/endpoints.h"

auto duration(YAML::Node cfg) -> std::chrono::nanoseconds {
    if (!cfg) return std::chrono::nanoseconds(0);
    std::string period = cfg.as<std::string>();
    std::size_t pos;
    int count = std::stoi(period,&pos);
    auto suffix = period.substr(pos);
    if (suffix.empty()) return std::chrono::seconds(count);
    if (suffix=="s") return std::chrono::seconds(count);
    if (suffix=="ms") return std::chrono::milliseconds(count);
    if (suffix=="us") return std::chrono::microseconds(count);
    if (suffix=="ns") return std::chrono::nanoseconds(count);
    if (suffix=="m") return std::chrono::minutes(count);
    if (suffix=="h") return std::chrono::hours(count);
    return std::chrono::nanoseconds(0);
}

auto address_family(YAML::Node cfg) -> int {
    if (!cfg) return AF_UNSPEC;
    std::string data = cfg.as<std::string>();
    if (data=="v4") return AF_INET;
    if (data=="v6") return AF_INET6;
    return AF_UNSPEC;
}

auto udp_type(YAML::Node cfg) -> UdpServer::Mode {
    if (!cfg) return UdpServer::Mode::UNICAST;
    std::string data = cfg.as<std::string>();
    if (data=="unicast") return UdpServer::Mode::UNICAST;
    if (data=="broadcast") return UdpServer::Mode::BROADCAST;
    if (data=="multicast") return UdpServer::Mode::MULTICAST;
    return UdpServer::Mode::UNICAST;
}

#endif //YAML_CONFIG
#endif  //!__YAML__H__