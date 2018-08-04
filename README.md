# taosocks

A smart tunnel proxy that helps you bypass firewalls.

## Usage

Please run both server and client in the root directory of the project.

### On Server

```none
$ ./server/server -h
Usage of ./server/server:
  -key string
        the key
  -listen string
        listen address(host:port) (default "0.0.0.0:1081")
```

```
$ ./server/server --key=<Your Password>
```

### On Client

```none
$ ./client/client -h
Usage of ./client/client:
  -insecure
        don't verify server certificate (default true)
  -key string
        login key
  -listen string
        listen address(host:port) (default "0.0.0.0:1080")
  -server string
        server address(host:port) (default "127.0.0.1:1081")
```

```none
$ ./client/client --server=<host:port> --key=<Your Password>
```

### On WebBrowser

Now set your web browser proxy settings to use SOCKS5 proxy at 127.0.0.1:1080(by default). Don't forget to bypass DNS resolving.

Since it's a smart proxy tool, there is no need to use any browser proxy extension. Disable them all.
