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
	AU_NONE = 0,
	AU_SYN = 0b00000001,
	AU_ACK = 0b00000010,
	AU_FIN = 0b00000100,

	AU_SYNACK = AU_SYN | AU_ACK,
	AU_FINACK = AU_FIN | AU_ACK,
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
		HEADERS = sizeof(au_packet_header) + sizeof(ip),
		MAX_DATA = IP_MAXPACKET - HEADERS
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
		FIN_SENT,
		FIN_WAIT,
	};
	static in_addr_t get_addr(hostname host);
	static in_addr_t local_by_remote(in_addr_t remote_addr);
	static au_stream_port get_port();
	static void free_port(au_stream_port port);

private:
	enum: uint8_t {
		IP_PROTO = 199
	};
	enum: size_t {
		BUFFER_SIZE = 1 * 1024 * 1024
	};

	static int8_t checksum(au_packet const& packet);

	pthread_t worker;
	pthread_mutex_t mutex;
	pthread_cond_t update;
	int worker_pipe[2], socket_fd, timer_fd, epoll_fd;
	enum worker_msg: int {
		WORKER_STOP = 255,
		WORKER_SEND_SYNACK = 0,
		WORKER_CONNECT = 1,
		WORKER_SEND_DATA = 2,
		WORKER_SEND_FIN = 3,
	};

	au_stream_port local_port, remote_port;
	in_addr_t local_addr, remote_addr;
	au_socket_state state = CLOSED;

	au_stream_pos mtu = 1000;

	au_stream_pos recv_pos = 0, send_pos = 0, send_acked = 0, recv_taken = 0;
	int timeouts = 0;
	bool recv_fin = false;
	uint8_t recv_buffer[BUFFER_SIZE], send_buffer[BUFFER_SIZE];
	size_t backlog;
	std::set<au_base_socket*> pending_clients;

	static void* work_wrap(void* arg);
	void work();
	void to_worker(worker_msg msg);

	void timer_setup();
	void timer_shutdown();
	void send_data();

	au_base_socket(
			in_addr_t local_addr, au_stream_port local_port,
			in_addr_t remote_addr, au_stream_port remote_port,
			au_socket_state state
		);
	int send_packet(au_packet& pack);
	int recv_packet(au_packet& pack, in_addr_t& from);
	int setup_timer(int timer);

public:
	au_base_socket(
			in_addr_t local_addr, au_stream_port local_port,
			in_addr_t remote_addr, au_stream_port remote_port
		);
	virtual ~au_base_socket();

	void send(uint8_t const* data, size_t len);
	void recv(uint8_t* data, size_t len);
	void listen(size_t backlog = 16);
	au_base_socket* accept();
	void connect();
	void close();
};

class au_stream_socket: virtual public stream_socket {
protected:
	std::unique_ptr<au_base_socket> base_socket;
public:
	au_stream_socket(au_base_socket* base_socket);
	virtual void send(void const* buf, size_t size) override;
	virtual void recv(void* buf, size_t size) override;
	virtual ~au_stream_socket() override = default;
};

struct au_stream_client_socket: virtual public stream_client_socket, public au_stream_socket {
	au_stream_client_socket(hostname host, au_stream_port local_port, au_stream_port remote_port);
	virtual void connect() override;
	virtual ~au_stream_client_socket() override = default;
};

struct au_stream_server_socket: virtual public stream_server_socket {
private:
	std::unique_ptr<au_base_socket> base_socket;
public:
	au_stream_server_socket(hostname host, au_stream_port port);
	virtual stream_socket* accept_one_client() override;
	virtual ~au_stream_server_socket() override = default;
};
