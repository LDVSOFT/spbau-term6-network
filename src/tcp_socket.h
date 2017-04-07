#pragma once

#include "stream_socket.h"

#include <arpa/inet.h>
#include <sys/socket.h>

using tcp_port = short;

static constexpr tcp_port DEFAULT_TCP_PORT(40001);

struct tcp_base_socket {
	enum: int {
		NO_FD = -1
	};
	static void fill_addr(hostname host, tcp_port port, struct sockaddr_in& a, bool passive = false);

	int fd;
	tcp_base_socket(int fd = NO_FD);
	virtual ~tcp_base_socket();
};

struct tcp_socket: virtual stream_socket, tcp_base_socket {
	tcp_socket(int fd = NO_FD);
	virtual void send(const void* buf, size_t size);
	virtual void recv(void* buf, size_t size);
	virtual ~tcp_socket();
};

struct tcp_client_socket: virtual stream_client_socket, tcp_socket {
	struct sockaddr_in addr;

	tcp_client_socket(hostname host, tcp_port port);
	virtual void connect();
	virtual ~tcp_client_socket();
};

struct tcp_server_socket: virtual stream_server_socket, tcp_base_socket {
	tcp_server_socket(hostname host, tcp_port port);
	virtual stream_socket* accept_one_client();
	virtual ~tcp_server_socket();
};
