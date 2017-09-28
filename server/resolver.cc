#include <cstdlib>
#include <cstring>

#include <string>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void resolve(const std::string& host, const std::string& service)
{
    addrinfo hints;
    addrinfo* res;

    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if(::getaddrinfo(host.c_str(), service.c_str(), &hints, &res) == 0) {
        while(res) {
            auto addr = (sockaddr_in*)res->ai_addr;
            auto ip = (std::string)::inet_ntoa(addr->sin_addr);
            auto port = ::ntohs(addr->sin_port);

            std::cout << "ip: " << ip << ", port: " << port << std::endl;

            res = res->ai_next;
        }

        ::freeaddrinfo(res);
    }
}

int main(int argc, char** argv)
{
    if(argc == 3) {
        resolve(argv[1], argv[2]);
    }

    return 0;
}

