#include "relay_client.h"
#include "packet_manager.h"

#include "log.h"

namespace taosocks {

ClientRelayClient::ClientRelayClient(ClientPacketManager* pktmgr, ClientSocket* local, int sid)
    : _pktmgr(pktmgr)
    , _local(local)
    , _sid(sid)
{
    _local->Read();

    _local->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        auto p = RelayPacket::Create(data, size);
        _pktmgr->Send(p);
    });

    _local->OnWrite([this](ClientSocket*, size_t size) {

    });

    _local->OnClose([this](ClientSocket*, CloseReason reason) {
        LogLog("浏览器断开连接，理由：%d", reason);
        if(reason == CloseReason::Actively) {

        }
        else if(reason == CloseReason::Passively || reason == CloseReason::Reset) {
            auto pkt = new DisconnectPacket;
            pkt->__size = sizeof(DisconnectPacket);
            pkt->__cmd = PacketCommand::Disconnect;
            _pktmgr->Send(pkt);
        }
    });

    _pktmgr->OnError = [this]() {
        assert(0);
    };

    _pktmgr->OnPacketSent = [this]() {
        if(!_local->IsClosed()) {
            _local->Read();
        }
    };

    _pktmgr->OnPacketRead = [this](BasePacket* packet) {
        return OnPacket(packet);
    };

    _pktmgr->Read();
}

void ClientRelayClient::_OnRemoteDisconnect(DisconnectPacket * pkt)
{
    LogLog("收包：网站断开连接 浏览器当前状态：%s", _local->IsClosed() ? "已断开" : "未断开");
    if(!_local->IsClosed()) {
        _local->Close(false);
    }
    else {
        LogLog("已关闭");
    }
}

bool ClientRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _local->Write(pkt->data, pkt->__size - sizeof(BasePacket));
    }
    else if(packet->__cmd == PacketCommand::Disconnect) {
        auto pkt = static_cast<DisconnectPacket*>(packet);
        _OnRemoteDisconnect(pkt);
    }
    else {
        assert(0 && "invalid packet");
    }

    return true;
}

//////////////////////////////////////////////////////////////////////////

ServerRelayClient::ServerRelayClient(ServerPacketManager* pktmgr, ClientSocket* remote, GUID guid)
    : _pktmgr(pktmgr)
    , _remote(remote)
    , _guid(guid)
{
    _remote->Read();

    _remote->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        // LogLog("读取了 %d 字节", size);
        auto p = RelayPacket::Create(data, size);
        p->__guid = _guid;
        _pktmgr->Send(p);
        _remote->Read();
    });

    _remote->OnWrite([this](ClientSocket*, size_t size) {
        // LogLog("写入了 %d 字节", size);
    });

    _remote->OnClose([this](ClientSocket*, CloseReason reason) {
        LogLog("网站断开连接");
        _OnRemoteClose(reason);
    });
}

void ServerRelayClient::_OnRemoteClose(CloseReason reason)
{
    if(reason == CloseReason::Actively) {
        LogLog("浏览器请求断开连接");
    }
    else if(reason == CloseReason::Passively || reason == CloseReason::Reset) {
        LogLog("网站关闭/异常断开连接");
        _pktmgr->RemoveHandler(this);
        _remote->Close(true);

        auto pkt = new DisconnectPacket;
        pkt->__size = sizeof(DisconnectPacket);
        pkt->__cmd = PacketCommand::Disconnect;
        pkt->__guid = _guid;
        _pktmgr->Send(pkt);
   }
}

void ServerRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _remote->Write(pkt->data, pkt->__size - sizeof(BasePacket));
    }
    else if(packet->__cmd == PacketCommand::Disconnect) {
        LogLog("收包：浏览器断开连接");
        _remote->Close(true);
        _pktmgr->RemoveHandler(this);
    }
}

}

