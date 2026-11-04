#include "Transport.h"

NAMESPACE_UPP

NetworkDisplayTransport::NetworkDisplayTransport()
{
	socket.Timeout(0); // non-blocking from the start (both client- and server-side use)
}

bool NetworkDisplayTransport::AcceptFrom(Socket& listener)
{
	socket.Timeout(0); // Socket::Accept() waits using *this* socket's timeout field
	if(!socket.Accept(listener))
		return false;
	socket.Timeout(0); // Accept() re-Init()s the socket; re-assert non-blocking defensively
	socket.NoDelay();
	peer = socket.GetPeerAddr();
	return true;
}

bool NetworkDisplayTransport::Connect(const String& endpoint)
{
	int colon = endpoint.ReverseFind(':');
	if(colon < 0)
		return false;
	String host = endpoint.Mid(0, colon);
	int port = StrInt(endpoint.Mid(colon + 1));
	socket.Timeout(3000); // allow a brief block to establish the TCP handshake
	bool ok = socket.Connect(host, port);
	socket.Timeout(0); // steady-state: non-blocking Send/Receive
	if(ok) {
		socket.NoDelay();
		peer = socket.GetPeerAddr();
	}
	return ok;
}

bool NetworkDisplayTransport::IsOpen() const
{
	return socket.IsOpen() && !socket.IsError();
}

void NetworkDisplayTransport::Close()
{
	socket.Close();
}

bool NetworkDisplayTransport::Send(const void *data, int64 len)
{
	if(!IsOpen())
		return false;
	return socket.PutAll(data, (int)len);
}

int NetworkDisplayTransport::Receive(void *buffer, int maxlen)
{
	if(!socket.IsOpen())
		return -1;
	int n = socket.Get(buffer, maxlen);
	if(socket.IsError())
		return -1;
	if(n == 0 && socket.IsEof())
		return -1;
	return n;
}

String NetworkDisplayTransport::Describe() const
{
	return peer.IsEmpty() ? String("tcp:<unknown>") : "tcp:" + peer;
}

bool NetworkListener::Listen(int port)
{
	socket.Timeout(0);
	open = socket.Listen(port, 5, false, true);
	return open;
}

One<NetworkDisplayTransport> NetworkListener::Accept()
{
	One<NetworkDisplayTransport> t;
	if(!open)
		return t;
	t.Create();
	if(t->AcceptFrom(socket))
		return t;
	t.Clear();
	return t;
}

void NetworkListener::Close()
{
	socket.Close();
	open = false;
}

END_UPP_NAMESPACE
