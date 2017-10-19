#include <cstring>

#include <string>

#include "winsock_helper.h"
#include "iocp_model.h"
#include "thread_dispatcher.h"
#include "server_socket.h"
#include "client_socket.h"
#include "packet_manager.h"
#include "relay_client.h"

using namespace taosocks;

#include "log.h"

#ifdef TAOLOG_ENABLED
// {927E3E09-98DC-46E0-AEFD-B5BFC089C81F}
static const GUID providerGuid =
{0x927E3E09, 0x98DC, 0x46E0, {0xAE, 0xFD, 0xB5, 0xBF, 0xC0, 0x89, 0xC8, 0x1F}};
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

    ServerPacketManager pktmgr;
    ServerSocket server;
    ConnectionHandler newrelay(&pktmgr);

    server.OnAccept([&](ClientSocket* client) {
        pktmgr.AddClient(client);
    });

    pktmgr.AddHandler(&newrelay);

    newrelay.OnCreateClient = [&] {
        auto c = new ClientSocket();
        return c;
    };

    newrelay.OnSucceed = [&](ClientSocket* client, GUID guid) {
        auto rc = new ServerRelayClient(&pktmgr, client, guid);
        pktmgr.AddHandler(rc);
    };

    newrelay.OnError = [&](ClientSocket* client) {
        client->Close(true);
    };

    pktmgr.StartPassive();
    server.Start(INADDR_ANY, 8081);

    return disp.Run();
}