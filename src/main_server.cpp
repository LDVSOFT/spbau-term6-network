#include "server.h"
#include "tcp_socket.h"

#include <stdlib.h>

int main(int argc, char* argv[]) {
	tcp_port port(DEFAULT_TCP_PORT);
	hostname host(DEFAULT_HOST);
	if (argc >= 2) {
		host = argv[1];
	}
	if (argc >= 3) {
		port = strtol(argv[2], NULL, 10);
	}
	server(new tcp_server_socket(host, port));
	return 0;
}

