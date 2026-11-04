#ifndef _DisplayServer_Net_Transport_h_
#define _DisplayServer_Net_Transport_h_

#include <Core/Core.h>

NAMESPACE_UPP

// Abstract per-connection channel a client and a DisplayServer session talk over.
// NetworkDisplayTransport (TCP, localhost-friendly) is the only real implementation
// built in NetworkDisplay/0006; SharedMemoryDisplayTransport is an explicitly
// incomplete stub marking where a future shared-memory transport would plug in --
// see its class comment below. Both the DisplayServer side and client processes
// (e.g. the TestClient in ../TestClient) talk to a connection purely through this
// interface, so a real client only ever needs Connect()+Send()+Receive().
class IDisplayTransport {
public:
	virtual ~IDisplayTransport() {}

	// Client-side: establish a connection to `endpoint` (transport-specific address
	// form; the network transport expects "host:port"). Not used server-side -- the
	// server obtains already-connected transports from NetworkListener::Accept().
	virtual bool   Connect(const String& endpoint) = 0;

	virtual bool   IsOpen() const = 0;
	virtual void   Close() = 0;

	// Sends `len` bytes. Our frames are small (a handful of draw commands at a
	// time), so implementations may do this as a short best-effort blocking write;
	// returns false only on a hard failure (peer gone).
	virtual bool   Send(const void *data, int64 len) = 0;
	bool           Send(const String& data) { return Send(data.Begin(), data.GetCount()); }

	// Non-blocking. Returns the number of bytes copied into `buffer` (0 = nothing
	// pending right now, keep polling), or -1 if the connection is closed/broken.
	virtual int    Receive(void *buffer, int maxlen) = 0;

	virtual String Describe() const = 0; // human-readable peer identity, for logging
};

// Real, working implementation: a non-blocking TCP connection. Loopback-only in this
// task (no auth/encryption needed per the task doc -- DisplayServer and its clients
// are trusted local processes).
class NetworkDisplayTransport : public IDisplayTransport {
	TcpSocket socket;
	String    peer;

public:
	NetworkDisplayTransport();

	// Server-side only: accept one pending connection on `listener` onto this
	// transport's own socket. Non-blocking (returns false immediately if nothing is
	// pending). Used by NetworkListener::Accept(), not meant for client use.
	bool   AcceptFrom(Socket& listener);

	bool   Connect(const String& endpoint) override; // client-side: "host:port"
	bool   IsOpen() const override;
	void   Close() override;
	using IDisplayTransport::Send; // un-hide the const String& convenience overload
	bool   Send(const void *data, int64 len) override;
	int    Receive(void *buffer, int maxlen) override;
	String Describe() const override;
};

// Server-side TCP listener producing NetworkDisplayTransport instances as clients
// connect. Deliberately not itself an IDisplayTransport -- accepting brand new
// connections isn't a per-connection Send/Receive operation.
class NetworkListener {
	TcpSocket socket;
	bool      open = false;

public:
	bool                          Listen(int port);
	// Non-blocking; returns an empty One<> if no connection is currently pending.
	One<NetworkDisplayTransport>  Accept();
	bool                          IsOpen() const { return open; }
	void                          Close();
};

// TODO: implement shared-memory transport. Sketch for a future task: a named POSIX
// shm_open() segment per connection holding a small ring buffer per direction, plus
// an eventfd (or named semaphore) per direction so a reader can block/poll instead of
// busy-spinning; frames would keep the same length-prefixed shape used by
// NetworkDisplayTransport above, just with the ring buffer standing in for the TCP
// byte stream. Left entirely unimplemented for NetworkDisplay/0006 -- every method
// below is a stub so the seam exists and compiles, nothing more.
class SharedMemoryDisplayTransport : public IDisplayTransport {
public:
	bool   Connect(const String& endpoint) override { return false; }
	bool   IsOpen() const override                  { return false; }
	void   Close() override                         {}
	using IDisplayTransport::Send; // un-hide the const String& convenience overload
	bool   Send(const void *, int64) override       { return false; }
	int    Receive(void *, int) override            { return -1; }
	String Describe() const override                { return "shm:unimplemented"; }
};

END_UPP_NAMESPACE

#endif
