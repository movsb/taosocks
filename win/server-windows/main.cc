#include <cstring>

#include <string>

#include "winsock_helper.h"
#include "iocp_model.h"
#include "thread_dispatcher.h"
#include "server_socket.h"
#include "client_socket.h"
#include "packet_manager.h"
#include "relay_client.h"
#include "common/resolver.h"

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

    server.OnAccept([&](ClientSocket* client) {
        pktmgr.AddClient(client);
    });

    pktmgr.OnNew = [&](ClientSocket* client, ConnectPacket* pkt) {
        auto resolver = new Resolver();
        resolver->OnError = [=, &pktmgr] {
            auto cp = new ConnectRespondPacket();
            cp->__cmd = PacketCommand::Connect;
            cp->__guid = pkt->__guid;
            cp->__seq = 0;
            cp->__size = sizeof(ConnectRespondPacket);
            cp->code = 1;
            cp->addr = 0;
            cp->port = 0;
            pktmgr.Send(cp);

            delete cp;
            delete pkt;
            delete resolver;
        };

        resolver->OnSuccess = [=,&pktmgr](unsigned int addr, unsigned short port) {
            auto remote = new ClientSocket();
            remote->OnConnect([=, &pktmgr](ClientSocket*, bool connected) {
                auto cp = new ConnectRespondPacket();
                cp->__cmd = PacketCommand::Connect;
                cp->__guid = pkt->__guid;
                cp->__seq = 0;
                cp->__size = sizeof(ConnectRespondPacket);
                cp->code = connected ? 0 : 1;
                cp->addr = addr;
                cp->port = port;
                pktmgr.Send(cp);

                if(connected) {
                    auto rc = new ServerRelayClient(&pktmgr, remote, pkt->__guid);
                    pktmgr.AddHandler(rc);
                }

                delete cp;
                delete pkt;
                delete resolver;
            });

            in_addr a;
            a.s_addr = ::htonl(addr);
            remote->Connect(a, port);
        };

        pkt->revert();

        resolver->Resolve(pkt->host, pkt->service);
    };

    server.Start(INADDR_ANY, 8081);

    return disp.Run();
}