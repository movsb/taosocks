#include "relay_client.h"
#include "packet_manager.h"

#include "log.h"

namespace taosocks {

ClientRelayClient::ClientRelayClient(ClientPacketManager* pktmgr, ClientSocket* local, int sfd)
    : _pktmgr(pktmgr)
    , _local(local)
    , _sfd(sfd)
{
    _pktmgr->AddHandler(this);

    _local->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        // LogLog("读取了 %d 字节", size);
        auto p = RelayPacket::Create(_sfd, _local->GetDescriptor(), data, size);
        _pktmgr->Send(p);
    });

    _local->OnWrite([this](ClientSocket*, size_t size) {
        // LogLog("写入了 %d 字节", size);
    });

    _local->OnClose([this](ClientSocket*, CloseReason::Value reason) {
        LogLog("浏览器断开连接，理由：%d", reason);
        if(reason == CloseReason::Actively) {

        }
        else if(reason == CloseReason::Passively || reason == CloseReason::Reset) {
            _pktmgr->RemoveHandler(this);
            auto pkt = new DisconnectPacket;
            pkt->__size = sizeof(DisconnectPacket);
            pkt->__cmd = PacketCommand::Disconnect;
            pkt->__sfd = _sfd;
            pkt->__cfd = (int)INVALID_SOCKET;
            _pktmgr->Send(pkt);
        }
    });
}

// 网站主动关闭连接
void ClientRelayClient::_OnRemoteDisconnect(DisconnectPacket * pkt)
{
    LogLog("收包：网站断开连接，浏览器fd=%d，浏览器当前状态：%s", _local->GetDescriptor(), _local->IsClosed() ? "已断开" : "未断开");
    if(!_local->IsClosed()) {
        _local->Close();
        _pktmgr->RemoveHandler(this);
    }
    else {
        LogLog("已关闭");
    }
}

int ClientRelayClient::GetDescriptor()
{
    return _local->GetDescriptor();
}

void ClientRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _local->Write(pkt->data, pkt->__size - sizeof(BasePacket), nullptr);
    }
    else if(packet->__cmd == PacketCommand::Disconnect) {
        auto pkt = static_cast<DisconnectPacket*>(packet);
        _OnRemoteDisconnect(pkt);
    }
}

//////////////////////////////////////////////////////////////////////////

ServerRelayClient::ServerRelayClient(ServerPacketManager* pktmgr, ClientSocket* remote, int cfd, GUID guid)
    : _pktmgr(pktmgr)
    , _remote(remote)
    , _cfd(cfd)
    , _guid(guid)
{
    _remote->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        // LogLog("读取了 %d 字节", size);
        auto p = RelayPacket::Create(_remote->GetDescriptor(), _cfd, data, size);
        p->__guid = _guid;
        _pktmgr->Send(p);
    });

    _remote->OnWrite([this](ClientSocket*, size_t size) {
        // LogLog("写入了 %d 字节", size);
    });

    _remote->OnClose([this](ClientSocket*, CloseReason::Value reason) {
        LogLog("网站断开连接");
        _OnRemoteClose(reason);
    });
}

void ServerRelayClient::_OnRemoteClose(CloseReason::Value reason)
{
    if(reason == CloseReason::Actively) {
        LogLog("浏览器请求断开连接");
    }
    else if(reason == CloseReason::Passively || reason == CloseReason::Reset) {
        LogLog("网站关闭/异常断开连接");
        _pktmgr->RemoveHandler(this);
        _pktmgr->CloseLocal(_guid, _cfd);
        _remote->Close();

        auto pkt = new DisconnectPacket;
        pkt->__size = sizeof(DisconnectPacket);
        pkt->__cmd = PacketCommand::Disconnect;
        pkt->__guid = _guid;
        pkt->__cfd = _cfd;
        pkt->__sfd = (int)INVALID_SOCKET;
        _pktmgr->Send(pkt);
   }
}

int ServerRelayClient::GetDescriptor()
{
    return _remote->GetDescriptor();
}

void ServerRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _remote->Write(pkt->data, pkt->__size - sizeof(BasePacket), nullptr);
    }
    else if(packet->__cmd == PacketCommand::Disconnect) {
        LogLog("收包：浏览器断开连接");
        _remote->Close();
        _pktmgr->RemoveHandler(this);
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
    LogLog("解析前");
    rsv.resolve(pkt->host, pkt->service);
    LogLog("解析后");

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

