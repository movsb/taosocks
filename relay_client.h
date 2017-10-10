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
    int _cid;
    GUID _guid;

private:
    void _Respond(int code, int sid, unsigned int addr, unsigned short port);
    void _OnConnectPacket(ConnectPacket* pkt);
    void _OnResolve(unsigned int addr, unsigned short port);

public:
    // Inherited via IPacketHandler
    virtual int GetId() override;
    virtual void OnPacket(BasePacket * packet) override;

    std::function<ClientSocket*()> OnCreateClient;
    std::function<void(ClientSocket*, int cid, GUID guid)> OnSucceed;
    std::function<void(ClientSocket*)> OnError;
};

class ClientRelayClient : public IPacketHandler
{
public:
    ClientRelayClient(ClientPacketManager* pktmgr, ClientSocket* local, int sid);

private:
    void _OnRemoteDisconnect(DisconnectPacket* pkt);

private:
    ClientSocket* _local;
    ClientPacketManager* _pktmgr;
    int _sid;

    // Inherited via IPacketHandler
    virtual int GetId() override;
    virtual void OnPacket(BasePacket * packet) override;
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
    void _OnRemoteClose(CloseReason::Value reason);

    // Inherited via IPacketHandler
    virtual int GetId() override;
    virtual void OnPacket(BasePacket * packet) override;
};

}
