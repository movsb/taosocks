package main

import (
	"crypto/tls"
	"encoding/gob"
	"flag"
	"log"
	"net"
	"sync"

	"taosocks/internal"
)

type Config struct {
	Listen string
	Server string
}

var config Config
var hh HTTP
var auth Auth

type Server struct {
}

func (s *Server) Run(network, addr string) error {
	cer, err := tls.LoadX509KeyPair("config/server.crt", "config/server.key")
	if err != nil {
		log.Println(err)
		return nil
	}

	config := &tls.Config{Certificates: []tls.Certificate{cer}}

	l, err := tls.Listen(network, addr, config)

	if err != nil {
		panic(err)
	}

	for {
		conn, err := l.Accept()

		if err != nil {
			panic(err)
		}

		go s.handleAccept(conn)
	}
}

func (s *Server) handleAccept(conn net.Conn) {
	if !hh.Handle(conn) {
		s.handle(conn)
	}
}

func (s *Server) handle(conn net.Conn) error {
	defer conn.Close()

	var enc = gob.NewEncoder(conn)
	var dec = gob.NewDecoder(conn)

	var opkt internal.OpenPacket
	err := dec.Decode(&opkt)
	if err != nil {
		return err
	}

	log.Printf("> %s\n", opkt.Addr)

	conn2, err := net.Dial("tcp", opkt.Addr)
	if err != nil {
		if conn2 != nil {
			conn2.Close()
		}
		enc.Encode(internal.OpenAckPacket{Status: false})
		return err
	}

	defer conn2.Close()

	enc.Encode(internal.OpenAckPacket{Status: true})

	wg := &sync.WaitGroup{}
	wg.Add(2)

	go func() {
		relay1(enc, conn2)
		wg.Done()
		conn.Close()
		conn2.Close()
	}()

	go func() {
		relay2(conn2, dec)
		wg.Done()
		conn.Close()
		conn2.Close()
	}()

	wg.Wait()

	log.Printf("< %s\n", opkt.Addr)

	return nil
}

func relay1(enc *gob.Encoder, conn net.Conn) {
	buf := make([]byte, 1024)

	for {
		var pkt internal.RelayPacket
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

func relay2(conn net.Conn, dec *gob.Decoder) {
	for {
		var pkt internal.RelayPacket
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

func parseConfig() {
	flag.StringVar(&config.Listen, "listen", "", "listen address(host:port)")
	flag.StringVar(&config.Server, "server", "", "where to proxy unknown requests")
	flag.Parse()
}

func main() {
	parseConfig()

	auth.Init("config/users.txt")

	s := Server{}
	s.Run("tcp4", config.Listen)
}
