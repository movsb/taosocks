package main

import (
	"bufio"
	"log"
	"net"
	"os"
	"regexp"
	"strings"
)

type ProxyType uint

const (
	_ ProxyType = iota
	proxyTypeDefault
	proxyTypeDirect
	proxyTypeProxy
	proxyTypeReject
)

type AddrType uint

const (
	_ AddrType = iota
	IPv4
	Domain
)

var reIsComment = regexp.MustCompile(`^[ \t]*#`)

func isComment(line string) bool {
	return reIsComment.MatchString(line)
}

type HostFilter struct {
	chAdd chan string
	hosts map[string]ProxyType
	cidrs map[*net.IPNet]ProxyType
}

func (f *HostFilter) Init(path string) {
	f.chAdd = make(chan string)
	f.hosts = make(map[string]ProxyType)
	f.cidrs = make(map[*net.IPNet]ProxyType)

	go f.addRoutine()

	file, err := os.Open(path)
	if err != nil {
		logf("rule file not found: %s\n", path)
		return
	}

	scanner := bufio.NewScanner(file)

	for scanner.Scan() {
		rule := scanner.Text()
		if isComment(rule) {
			continue
		}
		toks := strings.Split(rule, ",")
		if len(toks) == 3 {
			var ty ProxyType
			switch toks[2] {
			case "direct":
				ty = proxyTypeDirect
			case "proxy":
				ty = proxyTypeProxy
			case "reject":
				ty = proxyTypeReject
			default:
				continue
			}

			if toks[0] == "host" {
				f.hosts[toks[1]] = ty
			} else if toks[0] == "cidr" {
				_, ipnet, err := net.ParseCIDR(toks[1])
				if err == nil {
					f.cidrs[ipnet] = ty
				} else {
					logf("bad cidr: %s\n", toks[1])
				}
			}
		}
	}
}

func (f *HostFilter) Add(host string) {
	f.chAdd <- host
}

func (f *HostFilter) addRoutine() {
	for s := ""; ; {
		s = <-f.chAdd
		if s == "" {
			break
		}
		f.hosts[s] = proxyTypeProxy
		log.Printf("+ 添加代理规则：%s\n", s)
	}
}

func (f *HostFilter) Test(host string) ProxyType {
	if colon := strings.IndexByte(host, ':'); colon != -1 {
		host = host[:colon]
	}
	host = strings.ToLower(host)

	// if is TopLevel
	if !strings.Contains(host, ".") {
		return proxyTypeDirect
	}

	aty := Domain
	if net.ParseIP(host).To4() != nil {
		aty = IPv4
	}

	if aty == IPv4 {
		ip := net.ParseIP(host)
		for ipnet, ty := range f.cidrs {
			if ipnet.Contains(ip) {
				return ty
			}
		}
	} else if aty == Domain {
		if ty, ok := f.hosts[host]; ok {
			return ty
		}
	}

	return proxyTypeDefault
}
