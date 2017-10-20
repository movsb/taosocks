#include <ws2tcpip.h>
#include <ws2def.h>

#include "resolver.h"

namespace taosocks {

void Resolver::Resolve(const std::string& host, const std::string& service)
{
    struct addrinfo hints;
    struct addrinfo* pres = nullptr;
    int res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    res = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &pres);
    if (res != 0){
        OnError();
    }
    else {
        auto in_addr = (sockaddr_in*)pres->ai_addr;
        auto addr = ::ntohl(in_addr->sin_addr.s_addr);
        auto port = ::ntohs(in_addr->sin_port);
        OnSuccess(addr, port);
    }
}

}
