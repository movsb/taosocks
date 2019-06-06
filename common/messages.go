package common

// This file contains the messages communicated between the client and the server.

// OpenMessage specifies the host and port to be opened
type OpenMessage struct {
	Addr string
}

// OpenAckMessage is the open status for OpenMessage
type OpenAckMessage struct {
	Status bool
}

// RelayMessage is used for tcp data relay
type RelayMessage struct {
	Data []byte
}
