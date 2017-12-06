package main

import (
    "log"
    "strings"
    "io/ioutil"
    "encoding/json"
)

const (
    _           uint = iota
    Direct
    Proxy
    Forbidden
)

var logn = log.Println
var logf = log.Printf

type Filter struct {
    maps    map[string]uint
}

func (f *Filter) Init(file string) {
    f.maps = make(map[string]uint)

    str, err := ioutil.ReadFile(file)
    if err != nil {
        logn(err)
        return
    }

    var sets map[string][]string
    err = json.Unmarshal(str, &sets)
    if err != nil {
        logn(err)
        return
    }

    for k, v := range sets {
        var ty uint = 0
        switch k {
        case "proxy":
            ty = Proxy
        case "forbidden":
            ty = Forbidden
        }
        if ty != 0 {
            for _, h := range v {
                f.maps[h] = ty
            }
        }
    }
}

func (f *Filter) Test(host string) uint {
    host_lower := strings.ToLower(host)

    for k, v := range f.maps {
        if strings.HasSuffix(host_lower, k) {
            return v
        }
    }

    return Direct
}

func main() {
    var f Filter
    f.Init("filters.json")

    var list = []string{
        "baidu.com",
        "google.com",
        "sogou.com",
    }

    for _, h := range list {
        r := f.Test(h)
        logf("host: %s, result: %d\n", h, r)
    }
}

