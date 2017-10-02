#include "relay_client.h"
#include "packet_manager.h"

#include "log.h"

namespace taosocks {

ClientRelayClient::ClientRelayClient(IBasePacketManager* pktmgr, ClientSocket* client, int sfd)
    : _pktmgr(pktmgr)
    , _client(client)
    , _sfd(sfd)
{
    _pktmgr->AddHandler(this);

    _client->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        LogLog("读取了 %d 字节", size);
        auto p = RelayPacket::Create(_sfd, _client->GetDescriptor(), data, size);
        _pktmgr->Send(p);
    });

    _client->OnWrite([this](ClientSocket*, size_t size) {
        LogLog("写入了 %d 字节", size);
    });

    _client->OnClose([this](ClientSocket*, CloseReason::Value reason) {
        LogLog("浏览器断开连接");
    });
}

int ClientRelayClient::GetDescriptor()
{
    return _client->GetDescriptor();
}

void ClientRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _client->Write(pkt->data, pkt->__size - sizeof(BasePacket), nullptr);
    }
}

//////////////////////////////////////////////////////////////////////////

ServerRelayClient::ServerRelayClient(IBasePacketManager* pktmgr, ClientSocket* client, int cfd, GUID guid)
    : _pktmgr(pktmgr)
    , _client(client)
    , _cfd(cfd)
    , _guid(guid)
{
    _client->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        LogLog("读取了 %d 字节", size);
        auto p = RelayPacket::Create(_client->GetDescriptor(), _cfd, data, size);
        p->__guid = _guid;
        _pktmgr->Send(p);
    });

    _client->OnWrite([this](ClientSocket*, size_t size) {
        LogLog("写入了 %d 字节", size);
    });

    _client->OnClose([this](ClientSocket*, CloseReason::Value reason) {
        LogLog("网站断开连接");
    });
}

int ServerRelayClient::GetDescriptor()
{
    return _client->GetDescriptor();
}

void ServerRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _client->Write(pkt->data, pkt->__size - sizeof(BasePacket), nullptr);
    }
}

void ConnectionHandler::_Respond(int code, int sfd, unsigned int addr, unsigned short port)
{
    auto p = new ConnectRespondPacket;

    p->__size = sizeof(ConnectRespondPacket);
    p->__cmd = PacketCommand::Connect;
    p->__sfd = sfd;
    p->__cfd = _cfd;
    p->__guid = _guid;
    p->addr = addr;
    p->port = port;
    p->code = code;

    _pktmgr->Send(p);
}

void ConnectionHandler::_OnConnectPacket(ConnectPacket* pkt)
{
    assert(pkt->__sfd == (int)INVALID_SOCKET);

    _cfd = pkt->__cfd;
    _guid = pkt->__guid;

    resolver rsv;
    rsv.resolve(pkt->host, pkt->service);

    if(rsv.size() > 0) {
        unsigned int addr;
        unsigned short port;
        rsv.get(0, &addr, &port);
        _OnResolve(addr, port);
    }
    else {
        _Respond(1, GetDescriptor(), 0, 0);
    }
}

void ConnectionHandler::_OnResolve(unsigned int addr, unsigned short port)
{
    auto c = OnCreateClient();

    c->OnConnect([this, c, addr, port](ClientSocket*, bool connected) {
        if(connected) {
            _Respond(0, c->GetDescriptor(), addr, port);
            OnSucceed(c, _cfd, _guid);
        }
        else {
            _Respond(1, GetDescriptor(), 0, 0);
            OnError(c);
        }
    });

    in_addr a;
    a.s_addr = addr;
    c->Connect(a, port);
}

int ConnectionHandler::GetDescriptor()
{
    return (int)INVALID_SOCKET;
}

void ConnectionHandler::OnPacket(BasePacket* packet)
{
    if(packet->__cmd == PacketCommand::Connect) {
        _OnConnectPacket(static_cast<ConnectPacket*>(packet));
    }
    else {
        assert(0 && "invalid packet");
    }
}

}

