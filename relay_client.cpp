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

    _client->OnClose([this](ClientSocket*) {
        LogLog("浏览器或网站断开连接");
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

    _client->OnClose([this](ClientSocket*) {
        LogLog("浏览器或网站断开连接");
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

int ConnectionHandler::GetDescriptor()
{
    return (int)INVALID_SOCKET;
}

void ConnectionHandler::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Connect) {
        auto pkt = static_cast<ConnectPacket*>(packet);
        resolver rsv;
        rsv.resolve(pkt->host, pkt->service);
        assert(rsv.size() > 0);

        auto c = OnCreateClient();
        auto ad = rsv[0];
        auto pt = std::atoi(pkt->service);

        c->OnConnect([&,c, pkt, ad, pt](ClientSocket*) {
            auto p = new ConnectRespondPacket;
            p->__size = sizeof(ConnectRespondPacket);
            p->__cmd = PacketCommand::Connect;
            p->__sfd = c->GetDescriptor();
            p->__cfd = pkt->__cfd;
            p->__guid = pkt->__guid;
            p->addr = ad;
            p->port = pt;
            p->status = 0;
            _pktmgr->Send(p);
            assert(OnSucceeded);
            OnSucceeded(c, pkt->__cfd, pkt->__guid);
        });

        in_addr addr;
        addr.S_un.S_addr = rsv[0];
        c->Connect(addr, pt);
    }
}

}

