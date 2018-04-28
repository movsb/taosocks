package main

import (
	"bufio"
	"flag"
	"log"
	"net"
)

var logf = log.Printf
var logn = log.Println

type Config struct {
	Listen   string
	Server   string
	Insecure bool
	Username string
	Password string
}

var config Config
var filter HostFilter

type Server struct {
}

func (s *Server) Run(network, addr string) error {
	l, err := net.Listen(network, addr)

	if err != nil {
		panic(err)
	}

	for {
		conn, err := l.Accept()

		if err != nil {
			break
		}

		go s.handle(conn)
	}

	return nil
}

func (s *Server) handle(conn net.Conn) error {
	defer conn.Close()

	var bior = bufio.NewReader(conn)
	var biow = bufio.NewWriter(conn)
	var bio = bufio.NewReadWriter(bior, biow)

	var first byte
	if firsts, err := bior.Peek(1); err != nil {
		// logf("empty connection")
		return err
	} else {
		first = firsts[0]
	}

	switch first {
	case '\x05':
		var sp SocksProxy
		sp.Handle(conn, bio)
	default:
		var hp HTTPProxy
		hp.Handle(conn, bio)
	}

	return nil
}

func parseConfig() {
	flag.StringVar(&config.Listen, "listen", "127.0.0.1:1080", "listen address(host:port)")
	flag.StringVar(&config.Server, "server", "127.0.0.1:1081", "server address(host:port)")
	flag.BoolVar(&config.Insecure, "insecure", false, "don't verify server certificate")
	flag.StringVar(&config.Username, "username", "", "login username")
	flag.StringVar(&config.Password, "password", "", "login password")
	flag.Parse()
}

func main() {
	parseConfig()

	filter.Init("")

	s := Server{}
	s.Run("tcp", config.Listen)
}
