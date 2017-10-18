#include <cassert>

#include <string>
#include <cstring>
#include <functional>

#include "winsock_helper.h"
#include "socks_server.h"
#include "iocp_model.h"
#include "thread_dispatcher.h"
#include "server_socket.h"
#include "client_socket.h"
#include "relay_client.h"

#include "log.h"


using namespace taosocks;

#ifdef TAOLOG_ENABLED
// {85957455-D5E0-40B4-9114-D6C0F97824D0}
static const GUID providerGuid = 
{ 0x85957455, 0xD5E0, 0x40B4, { 0x91, 0x14, 0xD6, 0xC0, 0xF9, 0x78, 0x24, 0xD0 } };
TaoLogger g_taoLogger(providerGuid);
#endif

Dispatcher* g_disp;
IOCP* g_ios;


int main()
{
    WSA::Startup();
    IOCP iocp;
    Dispatcher disp;

    g_disp = &disp;
    g_ios = &iocp;

    ServerSocket server;

    server.OnAccept([&](ClientSocket* client) {
        LogLog("新的浏览器连接：id=%d", client->GetId());
        auto ss = new SocksServer(client);
        ss->OnSucceed = [&](SocksServer::ConnectionInfo& info) {
            auto rc = new ClientRelayClient(info.pktmgr, info.client, info.sid);
        };
        ss->OnError = [](const std::string& e) {
            LogErr(e.c_str());
        };
    });

    server.Start(INADDR_ANY, 8080);

    return disp.Run();
}
