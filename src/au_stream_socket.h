#pragma once

#include "stream_socket.h"

#include <cstdint>
#include <set>
#include <memory>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <pthread.h>

using au_stream_port = uint16_t;
using au_buf_size = uint32_t;
using au_stream_pos = uint32_t;

enum au_flags: uint8_t {
	AU_SYN = 0b00000001,
	AU_ACK = 0b00000010,
	AU_FIN = 0b00000100,

	AU_SYNACK = AU_SYN | AU_ACK,
};

template<au_buf_size size>
struct au_buffer {
private:
	pthread_mutex_t mutex;
	pthread_cond_t update;
	au_buf_size start = 0, end = 0;
	uint8_t data[size];

public:
	au_buffer();
	~au_buffer();

	size_t read(uint8_t* buffer, size_t len);
	size_t write(uint8_t const* buffer, size_t len);
};

struct au_packet_header {
	au_stream_port src;
	au_stream_port dst;
	au_flags flags;
	uint8_t sum;
	uint8_t _padding[2];
	au_stream_pos recv_wind;
	au_stream_pos seq;
	au_stream_pos ack;
	au_stream_pos len;
} __attribute__((packed));

struct au_packet {
	au_packet_header header;
	enum: size_t {
		MAX_DATA = IP_MAXPACKET - sizeof(au_packet_header)
	};
	uint8_t data[MAX_DATA];
} __attribute__((packed));

class au_base_socket {
public:
	enum au_socket_state {
		CLOSED,
		LISTEN,
		SYN_SENT,
		SYN_RECEIVED,
		ESTABLISHED,
		// FIXME ...
	};
	static in_addr_t get_addr(hostname host);
	static in_addr_t local_by_remote(in_addr_t remote_addr);

private:
	enum: uint8_t {
		IP_PROTO = 199
	};
	enum: size_t {
		BUFFER_SIZE = 1 * 1024 * 1024
	};

	pthread_t worker;
	pthread_mutex_t mutex;
	pthread_cond_t update;
	int worker_pipe[2], socket_fd;
	enum: int {
		WORKER_STOP = 255,
		WORKER_SEND_SYNACK = 0,
		WORKER_CONNECT = 1,
		WORKER_NEWDATA = 2,
		WORKER_CLOSE = 3,
	};

	au_stream_port local_port, remote_port;
	in_addr_t local_addr, remote_addr;
	au_socket_state state = CLOSED;

	au_stream_pos recv_pos = 0, send_pos = 0, send_acked = 0;
	au_buffer<BUFFER_SIZE> recv_buffer, send_buffer;
	size_t backlog;
	std::set<au_base_socket*> pending_clients;

	static void* work_wrap(void* arg);
	void work();
	void to_worker(int msg);

	static uint64_t checksum(au_packet const& packet);

	int send_packet(au_packet& pack, in_addr_t const& to);
	int send_packet(au_packet& pack);
	int recv_packet(au_packet& pack, in_addr_t& from);
	int recv_packet(au_packet& pack);

public:
	au_base_socket(
			in_addr_t local_addr, au_stream_port local_port, 
			in_addr_t remote_addr, au_stream_port remote_port, 
			au_socket_state state = CLOSED
		);
	virtual ~au_base_socket();

	void listen(size_t backlog);
	au_base_socket* accept();
	void connect();
	void close();
};

class au_stream_socket: virtual public stream_socket {
protected:
	std::unique_ptr<au_base_socket> base_socket;
public:
	au_stream_socket(au_base_socket* base_socket);
	virtual void send(void const* buf, size_t size) override = 0;
	virtual void recv(void* buf, size_t size) override = 0;
	virtual ~au_stream_socket() override = default;
};

struct au_client_stream_socket: virtual public stream_client_socket, public au_stream_socket {
	au_client_stream_socket(hostname host, au_stream_port port);
	virtual void connect() override;
	virtual ~au_client_stream_socket() override = default;
};

struct au_server_stream_socket: virtual public stream_server_socket {
private:
	hostname remote_host;
	au_stream_port remote_port;
	std::unique_ptr<au_base_socket> base_socket;
public:
	au_server_stream_socket(hostname host, au_stream_port port);
	virtual stream_socket* accept_one_client() override;
	virtual ~au_server_stream_socket() override = default;
};
