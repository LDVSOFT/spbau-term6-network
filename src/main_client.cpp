#include "client.h"
#include "tcp_socket.h"

#include <stdlib.h>
#include <memory>

int main(int argc, char* argv[]) {
	tcp_port port(DEFAULT_TCP_PORT);
	hostname host(DEFAULT_HOST);
	if (argc >= 2) {
		host = argv[1];
	}
	if (argc >= 3) {
		port = strtol(argv[2], NULL, 10);
	}
	client(std::make_unique<tcp_client_socket>(host, port));
	return 0;
}
