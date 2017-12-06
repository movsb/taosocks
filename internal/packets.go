package internal

type OpenPacket struct {
    Addr    string
}

type OpenAckPacket struct {
    Status  bool
}

type RelayPacket struct {
    Data    []byte
}

