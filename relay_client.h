#pragma once

#include <string>

#include "winsock_helper.h"
#include "client_socket.h"
#include "packet_manager.h"
#include "socks_server.h"

namespace taosocks {

class ConnectionHandler : public IPacketHandler
{
public:
    ConnectionHandler(ServerPacketManager* pktmgr)
        : _pktmgr(pktmgr)
    {

    }

private:
    ServerPacketManager* _pktmgr;

private:
    void _Respond(int code, int sid, int cid, GUID guid, unsigned int addr, unsigned short port);
    void _OnConnectPacket(ConnectPacket* pkt);
    void _OnResolve(int cid, GUID guid, unsigned int addr, unsigned short port);

public:
    // Inherited via IPacketHandler
    virtual int GetId() override;
    virtual void OnPacket(BasePacket * packet) override;

    std::function<ClientSocket*()> OnCreateClient;
    std::function<void(ClientSocket*, int cid, GUID guid)> OnSucceed;
    std::function<void(ClientSocket*)> OnError;

    struct Context
    {
        int cid;
        GUID guid;
    };

    std::map<int, Context> _contexts;
};

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
    ServerRelayClient(ServerPacketManager* pktmgr, ClientSocket* client, int cid, GUID guid);

private:
    ClientSocket* _remote;
    ServerPacketManager* _pktmgr;
    int _cid;
    GUID _guid;

private:
    void _OnRemoteClose(CloseReason reason);

    // Inherited via IPacketHandler
    virtual int GetId() override;
    virtual void OnPacket(BasePacket * packet) override;
};

}
