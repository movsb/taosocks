package main

import (
    "log"
    "strings"
    "os"
    "bufio"
    "net"
)

type ProxyType uint

const (
    _           ProxyType = iota
    Direct
    Proxy
    Reject
)

type AddrType uint

const (
    _           AddrType = iota
    IPv4
    Domain
)

var logn = log.Println
var logf = log.Printf

type Filter struct {
    suffix  map[string]ProxyType
    match   map[string]ProxyType
    cidr    map[*net.IPNet]ProxyType
}

func (f *Filter) Init(path string) {
    f.suffix    = make(map[string]ProxyType)
    f.match     = make(map[string]ProxyType)
    f.cidr      = make(map[*net.IPNet]ProxyType)

    file, err := os.Open(path)
    if err != nil {
        return
    }

    scanner := bufio.NewScanner(file)

    for scanner.Scan() {
        rule := scanner.Text()
        toks := strings.Split(rule, ",")
        if len(toks) == 3 {
            var ty ProxyType
            switch toks[2] {
            case "direct":
                ty = Direct
            case "proxy":
                ty = Proxy
            case "reject":
                ty = Reject
            default:
                continue
            }

            if toks[0] == "suffix" {
                f.suffix[toks[1]] = ty
            } else if toks[0] == "match" {
                f.match[toks[1]] = ty
            } else if toks[0] == "cidr" {
                _, ipnet, err := net.ParseCIDR(toks[1])
                if err != nil {
                    f.cidr[ipnet] = ty
                }
            }
        }
    }
}

func (f *Filter) Test(host string, aty AddrType) ProxyType {
    // host_lower := strings.ToLower(host)

    if aty == IPv4 {
        for ipnet, ty := range f.cidr {
            if ipnet.Contains(net.ParseIP(host)) {
                return ty
            }
        }
    } else if aty == Domain {
        if ty, ok := f.match[host]; ok {
            return ty
        }
        for sfx, ty := range f.suffix {
            if host == sfx || strings.HasSuffix(host, "." + sfx) {
                return ty
            }
        }
    }

    return Direct
}

