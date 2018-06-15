package main

import (
	"bufio"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"

	"taosocks/internal"
)

var logf = log.Printf
var logn = log.Println

type xConfig struct {
	Listen   string
	Server   string
	Insecure bool
	Username string
	Password string
}

var config xConfig
var filter HostFilter
var tslog internal.TSLog
var svrmgr *RemoteRelayerManager

// Server is a tcp server which listens on a single local port
// to accept both incoming socks and http connections
type Server struct {
}

// Run starts to listen on the network and address
func (s *Server) Run(network, addr string) {
	l, err := net.Listen(network, addr)

	if err != nil {
		panic(err)
	}

	for {
		conn, err := l.Accept()

		if err != nil {
			panic(err)
		}

		go s.handle(conn)
	}
}

// Handle handles the accepted connections
// which can be socks and http connections
func (s *Server) handle(conn net.Conn) error {
	defer conn.Close()

	// buffered I/O readers & writers
	var bior = bufio.NewReader(conn)
	var biow = bufio.NewWriter(conn)
	var biorw = bufio.NewReadWriter(bior, biow)

	// peek to see the protocol used
	firsts, err := bior.Peek(1)
	if err != nil {
		return err
	}

	switch firsts[0] {
	case '\x04':
	case '\x05':
		var sp SocksProxy
		sp.Handle(conn, biorw)
	default:
		var hp HTTPProxy
		hp.Handle(conn, biorw)
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

func handleInterrupt() {
	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt)
	go func() {
		<-c
		filter.SaveAuto("config/rules-auto.txt")
		fmt.Println()
		os.Exit(0)
	}()
}

func main() {
	handleInterrupt()

	parseConfig()

	filter.Init("config/rules.txt")
	filter.LoadAuto("config/rules-auto.txt")

	svrmgr = NewRemoteRelayerManager()

	s := Server{}
	s.Run("tcp4", config.Listen)
}
