#include "client.h"
#include "tcp_socket.h"
#include "au_stream_socket.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
	if (strcmp(getenv("STREAM_SOCK_TYPE") ?: "", "au") == 0)
		client(std::make_unique<au_stream_client_socket>(host, getpid(), port));
	else
		client(std::make_unique<tcp_client_socket>(host, port));
	return 0;
}
