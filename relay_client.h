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
    ConnectionHandler(IBasePacketManager* pktmgr)
        : _pktmgr(pktmgr)
    {

    }

    IBasePacketManager* _pktmgr;

    // Inherited via IPacketHandler
    virtual int GetDescriptor() override;
    virtual void OnPacket(BasePacket * packet) override;

    std::function<ClientSocket*()> OnCreateClient;
    std::function<void(ClientSocket*, int cfd, GUID guid)> OnSucceeded;
};

class ClientRelayClient : public IPacketHandler
{
public:
    ClientRelayClient(IBasePacketManager* pktmgr, ClientSocket* client, int sfd);

private:
    ClientSocket* _client;
    IBasePacketManager* _pktmgr;
    int _sfd;

    // Inherited via IPacketHandler
    virtual int GetDescriptor() override;
    virtual void OnPacket(BasePacket * packet) override;
};

class ServerRelayClient : public IPacketHandler
{
public:
    ServerRelayClient(IBasePacketManager* pktmgr, ClientSocket* client, int cfd, GUID guid);

private:
    ClientSocket* _client;
    IBasePacketManager* _pktmgr;
    int _cfd;
    GUID _guid;

    // Inherited via IPacketHandler
    virtual int GetDescriptor() override;
    virtual void OnPacket(BasePacket * packet) override;
};

}
