package main

import (
	"bufio"
	"net"
	"os"
	"strings"
    "regexp"
)

type ProxyType uint

const (
	_ ProxyType = iota
	Direct
	Proxy
	Reject
)

type AddrType uint

const (
	_ AddrType = iota
	IPv4
	Domain
)

var reSplit = regexp.MustCompile(`[[:alnum:]-]+\.([[:alnum:]]+)$`)

type HostFilter struct {
	tlds    map[string]ProxyType
	slds    map[string]ProxyType
	cidrs   map[*net.IPNet]ProxyType
}

func (f *HostFilter) Init(path string) {
	f.tlds = make(map[string]ProxyType)
	f.slds = make(map[string]ProxyType)
	f.cidrs = make(map[*net.IPNet]ProxyType)

	file, err := os.Open(path)
	if err != nil {
		logf("rule file not found: %s\n", path)
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

			if toks[0] == "tld" {
				f.tlds[toks[1]] = ty
			} else if toks[0] == "sld" {
				f.slds[toks[1]] = ty
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

func (f *HostFilter) Test(host string, aty AddrType) ProxyType {
	host = strings.ToLower(host)

	// if is toplevel
	if !strings.Contains(host, ".") {
		return Direct
	}

	if aty == IPv4 {
		ip := net.ParseIP(host)
		for ipnet, ty := range f.cidrs {
			if ipnet.Contains(ip) {
				return ty
			}
		}
	} else if aty == Domain {
        matches := reSplit.FindStringSubmatch(host)
        sld := matches[0]
        tld := matches[1]

		if ty, ok := f.tlds[tld]; ok {
			return ty
		}

		if ty, ok := f.slds[sld]; ok {
            return ty
		}
	}

	return Proxy
}
