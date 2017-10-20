#pragma once

#include <string>

#include "winsock_helper.h"
#include "client_socket.h"
#include "packet_manager.h"
#include "socks_server.h"

namespace taosocks {

class ClientRelayClient
{
public:
    ClientRelayClient(ClientPacketManager* pktmgr, ClientSocket* local, int sid);

private:
    void _OnRemoteDisconnect(DisconnectPacket* pkt);

private:
    ClientSocket* _local;
    ClientPacketManager* _pktmgr;
    int _sid;

    bool OnPacket(BasePacket * packet);
};

class ServerRelayClient : public IPacketHandler
{
public:
    ServerRelayClient(ServerPacketManager* pktmgr, ClientSocket* client, GUID guid);

private:
    ClientSocket* _remote;
    ServerPacketManager* _pktmgr;
    GUID _guid;

private:
    void _OnRemoteClose(CloseReason reason);

    // Inherited via IPacketHandler
    virtual GUID GetId() override { return _guid; }
    virtual void OnPacket(BasePacket * packet) override;
};

}
