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


int main()
{
    WSA::Startup();

    IOCP iocp;
    Dispatcher disp;

    ServerPacketManager pktmgr(disp);
    ServerSocket server(disp);
    ConnectionHandler newrelay(&pktmgr);

    iocp.Attach(&server);

    server.OnAccept([&](ClientSocket* client) {
        iocp.Attach(client);
        pktmgr.AddClient(client);
    });

    pktmgr.AddHandler(&newrelay);

    newrelay.OnCreateClient = [&] {
        auto c = new ClientSocket(disp);
        iocp.Attach(c);
        return c;
    };

    newrelay.OnSucceeded = [&](ClientSocket* client, int cfd, GUID guid) {
        auto rc = new ServerRelayClient(&pktmgr, client, cfd, guid);
        pktmgr.AddHandler(rc);
    };

    pktmgr.StartPassive();
    server.Start(INADDR_ANY, 8081);

    return disp.Run();
}