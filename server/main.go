package main

import (
	"bufio"
	"encoding/gob"
	"flag"
	"io"
	"net"
	"net/http"
	"sync"

	"github.com/movsb/taosocks/internal"
)

var gVersion = "taosocks/20180728"
var gForward = "https://example.com"
var gListen string
var gKey string
var tslog = &internal.TSLog{}

func doRelay(conn net.Conn, bio *bufio.ReadWriter) error {
	var err error

	defer conn.Close()

	enc := gob.NewEncoder(bio)
	dec := gob.NewDecoder(bio)

	var openPkt internal.OpenPacket
	if err = dec.Decode(&openPkt); err != nil {
		return err
	}

	tslog.Log("> %s", openPkt.Addr)

	outConn, err := net.Dial("tcp", openPkt.Addr)
	if err != nil {
		enc.Encode(internal.OpenAckPacket{
			Status: false,
		})
		bio.Flush()
		return err
	}

	defer outConn.Close()

	enc.Encode(internal.OpenAckPacket{
		Status: true,
	})

	bio.Flush()

	wg := &sync.WaitGroup{}
	wg.Add(2)

	go func() {
		defer wg.Done()

		buf := make([]byte, 4096)
		for {
			n, err := outConn.Read(buf)
			if err != nil {
				if err != io.EOF {
					tslog.Red("%s", err.Error())
				}
				return
			}

			var pkt internal.RelayPacket
			pkt.Data = buf[:n]

			if err := enc.Encode(&pkt); err != nil {
				tslog.Red("%s", err.Error())
				return
			}

			bio.Flush()
		}
	}()

	go func() {
		defer wg.Done()

		for {
			var pkt internal.RelayPacket
			if err := dec.Decode(&pkt); err != nil {
				if err != io.EOF {
					tslog.Red("%s", err.Error())
				}
				return
			}

			_, err := outConn.Write(pkt.Data)
			if err != nil {
				tslog.Red("%s", err.Error())
				return
			}
		}
	}()

	wg.Wait()

	tslog.Gray("< %s", openPkt.Addr)

	return nil
}

func doForward(w http.ResponseWriter, req *http.Request) {
	resp, err := http.Get(gForward + req.RequestURI)
	if err == nil {
		w.WriteHeader(resp.StatusCode)
		io.Copy(w, resp.Body)
	}
}

func handleRequest(w http.ResponseWriter, req *http.Request) {
	ver := req.Header.Get("Upgrade")
	auth := req.Header.Get("Authorization")

	if ver == gVersion && auth == "taosocks "+gKey {
		w.WriteHeader(http.StatusSwitchingProtocols)
		conn, bio, _ := w.(http.Hijacker).Hijack()
		doRelay(conn, bio)
	} else {
		doForward(w, req)
	}
}

func main() {
	flag.StringVar(&gListen, "listen", "0.0.0.0:443", "listen address(host:port)")
	flag.StringVar(&gForward, "forward", "https://example.com", "delegate website, format must be https://example.com")
	flag.StringVar(&gKey, "key", "", "the key")
	flag.Parse()

	http.HandleFunc("/", handleRequest)

	if err := http.ListenAndServeTLS(gListen,
		"config/server.crt", "config/server.key",
		nil,
	); err != nil {
		panic(err)
	}
}
