#include <cstring>

#include <string>

#include "winsock_helper.h"
#include "iocp_model.h"
#include "thread_dispatcher.h"
#include "server_socket.h"
#include "client_socket.h"
#include "packet_manager.h"

using namespace taosocks;

int main()
{
    WSA::Startup();

    IOCP iocp;
    Dispatcher disp;

    PacketManager pktmgr(disp);
    ServerSocket server(disp);

    iocp.Attach(&server);

    server.OnAccepted([&](ClientSocket* client) {
        iocp.Attach(client);
    });

    server.Start(INADDR_ANY, 8081);

    return disp.Run();
}