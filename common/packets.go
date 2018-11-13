package common

// This file contains the packet structures communicated between server and client

// OpenPacket specifies the host and port to be opened
type OpenPacket struct {
	Addr string
}

// OpenAckPacket is the open status for OpenPacket
type OpenAckPacket struct {
	Status bool
}

// RelayPacket is used for tcp data relay
type RelayPacket struct {
	Data []byte
}
