#pragma once

#include <list>

#include "../relay_client.h"

namespace taosocks {

class ConnectionManager
{
public:

private:
    std::list<ServerRelayClient*> _broken;
};

}