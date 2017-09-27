
#include <cassert>

#include <iostream>
#include <string>
#include <cstring>
#include <functional>

#include "winsock_helper.h"
#include "socks_server.h"
#include "iocp_model.h"
#include "thread_dispatcher.h"
#include "server_socket.h"
#include "client_socket.h"

using namespace taosocks;

#ifdef _DEBUG
#define TAOLOG_ENABLED
#define TAOLOG_METHOD_COPYDATA
#endif

#include "taolog.h"

#ifdef TAOLOG_ENABLED
// {DA72210D-0C17-4223-AA34-7EB7EDADB64F}
static const GUID providerGuid = 
{ 0xDA72210D, 0x0C17, 0x4223, { 0xAA, 0x34, 0x7E, 0xB7, 0xED, 0xAD, 0xB6, 0x4F } };
TaoLogger g_taoLogger(providerGuid);
#endif

int main()
{
#ifdef _DEBUG
#ifdef TAOLOG_ENABLED 
    g_taoLogger.Init();
#endif // DEBUG
#endif

    winsock::WSA wsa;

    wsa.Startup();

    iocp::IOCP iocp;

    threading::Dispatcher disp;

    ServerSocket server(disp);

    iocp.Attach(server.GetSocket(), &server);

    server.OnAccepted([&](ClientSocket* client) {
        iocp.Attach(client->GetSocket(), client);
        auto ss = new SocksServer(disp, client);
        ss->OnCreateRelayer([&](ClientSocket* relay) {
            iocp.Attach(relay->GetSocket(), relay);
        });
    });

    server.Start(INADDR_ANY, 8080);

    return disp.Run();
}
