package main

import (
    "fmt"
    "net"
    "sync"
    "encoding/gob"
)

type OpenPacket struct {
    Addr    string
}

type OpenAckPacket struct {
    Status bool
}

type RelayPacket struct {
    Data []byte
}

type Server struct {

}

func (s *Server) Run(network, addr string) error {
    l, err := net.Listen(network, addr)

    if err != nil {
        panic(err)
    }

    for {
        conn, err := l.Accept()

        // fmt.Println("accepted...")

        if err != nil {
            panic(err)
        }

        go s.handle(conn)
    }

    return nil
}

func (s *Server) handle(conn net.Conn) error {
    defer conn.Close()

    var enc = gob.NewEncoder(conn)
    var dec = gob.NewDecoder(conn)

    var opkt OpenPacket
    err := dec.Decode(&opkt)
    if err != nil {
        return err
    }

    fmt.Println(opkt.Addr)

    conn2, err := net.Dial("tcp", opkt.Addr)
    if err != nil {
        if conn2 != nil {
            conn2.Close()
        }
        enc.Encode(OpenAckPacket{Status:false})
        return err
    }

    defer conn2.Close()

    enc.Encode(OpenAckPacket{Status:true})

    wg := &sync.WaitGroup{}
    wg.Add(2)

    go relay1(enc, conn2, wg)
    go relay2(conn2, dec, wg)

    wg.Wait()

    fmt.Printf("Finished relay\n")

    return nil
}

func relay1(enc *gob.Encoder, conn net.Conn, wg *sync.WaitGroup) {
    defer wg.Done()

    buf := make([]byte, 1024)

    for {
        var pkt RelayPacket
        n, err := conn.Read(buf)
        if err != nil {
            return
        }

        pkt.Data = buf[:n]

        err = enc.Encode(pkt)
        if err != nil {
            return
        }
    }
}

func relay2(conn net.Conn, dec *gob.Decoder, wg *sync.WaitGroup) {
    defer wg.Done()

    for {
        var pkt RelayPacket
        err := dec.Decode(&pkt)
        if err != nil {
            return
        }

        _, err = conn.Write(pkt.Data)
        if err != nil {
            return
        }
    }
}

func main() {
    s :=  Server{}
    s.Run("tcp", "0.0.0.0:1081")
}

