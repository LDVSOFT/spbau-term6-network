#include "tcp_socket.h"

#include <stdexcept>
#include <string>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

using std::runtime_error;
using std::string;
using namespace std::literals;

static runtime_error error(char const* s, char const* comment) {
	return runtime_error(string(s) + ": "s + string(comment));
}

static runtime_error error(char const* s) {
	return error(s, strerror(errno));
}

void tcp_base_socket::fill_addr(hostname host, tcp_port port, struct sockaddr_in& addr, bool passive) {
	struct addrinfo hints;
	struct addrinfo* res;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_flags = (passive ? AI_PASSIVE : 0);

	int ret;
	if ((ret = getaddrinfo(host, NULL, &hints, &res)) != 0) {
		if (ret == EAI_SYSTEM)
			throw error("Bad hostname");
		else
			throw error("Bad hostname", gai_strerror(ret));
	}
	if (res[0].ai_addrlen != sizeof(addr)) {
		freeaddrinfo(res);
		throw runtime_error("Wtf");
	}
	memcpy(&addr, res[0].ai_addr, res[0].ai_addrlen);
	addr.sin_port = htons(port);
	freeaddrinfo(res);
}

tcp_base_socket::tcp_base_socket(int _fd): fd(_fd) {
	if (fd == NO_FD) {
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
			throw error("Failed to open socket");
		int v(1);
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)) < 0) {
			close(fd);
			throw error("Failed to set options");
		}
	}
}

tcp_base_socket::~tcp_base_socket() {
	shutdown(fd, SHUT_RDWR);
	close(fd);
}

tcp_socket::tcp_socket(int fd): tcp_base_socket(fd) {}

void tcp_socket::send(const void* buf, size_t size) {
	for (size_t sent(0); sent < size; ) {
		ssize_t ret = ::send(fd, (char*)(buf) + sent, size - sent, MSG_NOSIGNAL);
		if (ret <= 0)
			throw error("Failed send");
		sent += ret;
	}
}

void tcp_socket::recv(void* buf, size_t size) {
	for (size_t received(0); received < size; ) {
		ssize_t ret = ::recv(fd, (char*)(buf) + received, size - received, MSG_NOSIGNAL);
		if (ret <= 0)
			throw error("Failed recv");
		received += ret;
	}
}

tcp_socket::~tcp_socket() {}

tcp_client_socket::tcp_client_socket(hostname host, tcp_port port) {
	fill_addr(host, port, addr);
}

void tcp_client_socket::connect() {
	if (::connect(fd, (struct sockaddr*)(&addr), sizeof(addr)) < 0)
		if (errno != EISCONN)
		throw error("Failed to connect");
}

tcp_client_socket::~tcp_client_socket() {}

tcp_server_socket::tcp_server_socket(hostname host, tcp_port port) {
	struct sockaddr_in addr;
	fill_addr(host, port, addr);

	if (bind(fd, (struct sockaddr*)(&addr), sizeof(addr)) < 0) {
		close(fd);
		throw error("Failed to bind socket");
	}
	if (listen(fd, 16) < 0) {
		close(fd);
		throw error("Failed to listen on socket");
	}
}

stream_socket* tcp_server_socket::accept_one_client() {
	int client = accept(fd, NULL, NULL);
	if (client < 0)
		throw error("Failed to accept a client");
	return new tcp_socket(client);
}

tcp_server_socket::~tcp_server_socket() {}
