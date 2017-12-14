package main

import (
    "log"
    "net"
    "io"
    "sync"
    "encoding/gob"
    "taosocks/internal"
)

type Relayer interface {
    Begin(addr string, src net.Conn) bool
    Relay()
    End()
    ToRemote(b []byte) error
    ToLocal(b []byte) error
}

type LocalRelayer struct {
    src     net.Conn
    dst     net.Conn
    addr    string
}

func (r *LocalRelayer) Begin(addr string, src net.Conn) bool {
    r.src = src
    r.addr = addr

    dst, err := net.Dial("tcp", addr)
    if err != nil {
        log.Printf("Dial host:%s -  %s\n", addr, err)
        return false
    }

    r.dst = dst
    return true
}

func (r *LocalRelayer) ToLocal(b []byte) error {
    r.src.Write(b)
    return nil
}

func (r *LocalRelayer) ToRemote(b []byte) error {
    r.dst.Write(b)
    return nil
}

func (r *LocalRelayer) End() {
    if r.src != nil {
        r.src.Close()
    }
    if r.dst != nil {
        r.dst.Close()
    }
}

func (r *LocalRelayer) Relay() {
    log.Printf("> [Direct] %s\n", r.addr)

    wg := &sync.WaitGroup{}
    wg.Add(2)

    var tx int64
    var rx int64

    go func() {
        tx, _ = io.Copy(r.dst, r.src)
        wg.Done()
        r.End()
    }()

    go func() {
        rx, _ = io.Copy(r.src, r.dst)
        wg.Done()
        r.End()
    }()

    wg.Wait()

    log.Printf("< [Direct] %s [TX:%d, RX:%d]\n", r.addr, tx, rx)

}


type RemoteRelayer struct {
    Server  string
    Insecure  bool

    src     net.Conn
    dst     net.Conn
    addr    string
}

func (r *RemoteRelayer) Begin(addr string, src net.Conn) bool {
    r.src = src
    r.addr = addr

    serverDialer := ServerDialer{}
    dst, err := serverDialer.Dial(r.Server, r.Insecure)
    if err != nil {
        log.Println(err)
        return false
    }

    r.dst = dst

    enc := gob.NewEncoder(r.dst)
    dec := gob.NewDecoder(r.dst)

    err = enc.Encode(internal.OpenPacket{Addr: r.addr})
    if err != nil {
        return false
    }

    var oapkt internal.OpenAckPacket
    err = dec.Decode(&oapkt)
    if err != nil {
        return false
    }

    if !oapkt.Status {
        return false
    }

    return true
}

func (r *RemoteRelayer) ToLocal(b []byte) error {
    r.src.Write(b)

    return nil
}

func (r *RemoteRelayer) ToRemote(b []byte) error {
    var pkt internal.RelayPacket
    pkt.Data = b

    enc := gob.NewEncoder(r.dst)
    enc.Encode(pkt)

    return nil
}

func (r *RemoteRelayer) End() {
    if r.src != nil {
        r.src.Close()
    }
    if r.dst != nil {
        r.dst.Close()
    }
}

func (r *RemoteRelayer) Relay() {
    log.Printf("> [Proxy ] %s\n", r.addr)

    wg := &sync.WaitGroup{}
    wg.Add(2)

    var tx int64
    var rx int64

    go func() {
        tx, _ = r.src2dst()
        wg.Done()
        r.End()
    }()

    go func() {
        rx, _ = r.dst2src()
        wg.Done()
        r.End()
    }()

    wg.Wait()

    log.Printf("< [Proxy ] %s [TX:%d, RX:%d]\n", r.addr, tx, rx)
}

func (r *RemoteRelayer) src2dst() (int64, error) {
    enc := gob.NewEncoder(r.dst)

    buf := make([]byte, 4096)

    var all int64
    var err error

    for {
        n, err := r.src.Read(buf)
        if err != nil {
            break
        }

        var pkt internal.RelayPacket
        pkt.Data = buf[:n]

        err = enc.Encode(pkt)
        if err != nil {
            log.Printf("server reset: %s\n", err)
            break
        }

        all += int64(n)
    }

    return all, err
}

func (r *RemoteRelayer) dst2src() (int64, error) {
    dec := gob.NewDecoder(r.dst)

    var all int64
    var err error

    for {
        var pkt internal.RelayPacket
        err = dec.Decode(&pkt)
        if err != nil {
            log.Printf("server reset: %s\n", err)
            break
        }

        _, err = r.src.Write(pkt.Data)
        if err != nil {
            break
        }

        all += int64(len(pkt.Data))
    }

    return all, err
}

