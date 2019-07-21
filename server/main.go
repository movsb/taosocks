package main

import (
	"bufio"
	"encoding/gob"
	"flag"
	"io"
	"net"
	"net/http"
	"sync"

	"github.com/movsb/taosocks/common"
)

var gVersion = "taosocks/20190722"
var gForward = "https://example.com"
var gListen string
var gKey string
var gPath string
var tslog = &common.TSLog{}

func doRelay(conn net.Conn, bio *bufio.ReadWriter) error {
	var err error

	defer conn.Close()

	enc := gob.NewEncoder(bio)
	dec := gob.NewDecoder(bio)

	var openMsg common.OpenMessage
	if err = dec.Decode(&openMsg); err != nil {
		return err
	}

	tslog.Log("> %s", openMsg.Addr)

	outConn, err := net.Dial("tcp", openMsg.Addr)
	if err != nil {
		enc.Encode(common.OpenAckMessage{
			Status: false,
		})
		bio.Flush()
		return err
	}

	defer outConn.Close()

	enc.Encode(common.OpenAckMessage{
		Status: true,
	})

	bio.Flush()

	wg := &sync.WaitGroup{}
	wg.Add(2)

	go func() {
		defer wg.Done()

		buf := make([]byte, common.ReadBufSize)
		for {
			n, err := outConn.Read(buf)
			if err != nil {
				if err != io.EOF {
					tslog.Red("%s", err.Error())
				}
				return
			}

			var msg common.RelayMessage
			msg.Data = buf[:n]

			if err := enc.Encode(&msg); err != nil {
				tslog.Red("%s", err.Error())
				return
			}

			bio.Flush()
		}
	}()

	go func() {
		defer wg.Done()

		for {
			var msg common.RelayMessage
			if err := dec.Decode(&msg); err != nil {
				if err != io.EOF {
					tslog.Red("%s", err.Error())
				}
				return
			}

			_, err := outConn.Write(msg.Data)
			if err != nil {
				tslog.Red("%s", err.Error())
				return
			}
		}
	}()

	wg.Wait()

	tslog.Gray("< %s", openMsg.Addr)

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
	path := req.URL.Path

	if path == gPath && ver == gVersion && auth == "taosocks "+gKey {
		w.WriteHeader(http.StatusSwitchingProtocols)
		conn, bio, _ := w.(http.Hijacker).Hijack()
		doRelay(conn, bio)
	} else {
		doForward(w, req)
	}
}

func main() {
	flag.StringVar(&gListen, "listen", "127.0.0.1:1081", "listen address(host:port)")
	flag.StringVar(&gForward, "forward", "https://example.com", "delegate website, format must be https://example.com")
	flag.StringVar(&gKey, "key", "", "the key")
	flag.StringVar(&gPath, "path", "/", "/your/path")
	flag.Parse()

	http.HandleFunc("/", handleRequest)

	if err := http.ListenAndServeTLS(gListen,
		"config/server.crt", "config/server.key",
		nil,
	); err != nil {
		panic(err)
	}
}
