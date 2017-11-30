#pragma once

#include <string>
#include <functional>

namespace taosocks {

class Resolver
{
public:
    Resolver()
    { }

public:
    std::function<void(unsigned int addr, unsigned short port)> OnSuccess;
    std::function<void()> OnError;


public:
    void Resolve(const std::string& host, const std::string& service);
};

}
