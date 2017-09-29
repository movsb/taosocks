#include <iostream>

#include <process.h>

#include "packet_manager.h"


namespace taosocks {
namespace packet_manager {
PacketManager::PacketManager(Dispatcher & disp)
    : _disp(disp)
    , _client(disp)
    , _connected(false)
{
}

void PacketManager::Start()
{
    ::_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);

    _client.OnConnected([this](ClientSocket*) {
        std::cout << "PacketManager: connected to remote\n";
        _connected = true;
    });

    _client.OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        return OnRead(data, size);
    });

    in_addr addr;
    addr.S_un.S_addr = ::inet_addr("127.0.0.1");
    _client.Connect(addr, 8081);
}

void PacketManager::Send(BasePacket* pkt)
{
    assert(pkt != nullptr);
    _packets.push_back(pkt);
}

void PacketManager::OnRead(unsigned char* data, size_t size)
{
    _recv_data.insert(_recv_data.cend(), data, data + size);
    if(_recv_data.size() >= sizeof(BasePacket)) {
        auto bpkt = (BasePacket*)_recv_data.data();
        if(_recv_data.size() >= bpkt->__size) {
            auto pkt = new (new unsigned char[bpkt->__size]) BasePacket;
            std::memcpy(pkt, _recv_data.data(), bpkt->__size);
            _recv_data.erase(_recv_data.cbegin(), _recv_data.cbegin() + bpkt->__size);
            auto handler = _handlers.find(pkt->__cfd);
            assert(handler != _handlers.cend());
            handler->second->OnPacket(pkt);
        }
    }
}

unsigned int PacketManager::PacketThread()
{
    for(;;) {
        BasePacket* pkt =nullptr;

        if(_connected) {
            _lock.LockExecute([&] {
                if(!_packets.empty()) {
                    pkt = _packets.front();
                    _packets.pop_front();
                }
            });

        }

        if(pkt != nullptr) {
            _client.Write((char*)pkt, pkt->__size, nullptr);
        }
        else {
            ::Sleep(500);
        }
    }

    return 0;
}

unsigned int PacketManager::__ThreadProc(void * that)
{
    return static_cast<PacketManager*>(that)->PacketThread();
}

}
}