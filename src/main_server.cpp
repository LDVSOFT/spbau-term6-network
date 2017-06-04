#include "server.h"
#include "tcp_socket.h"
#include "au_stream_socket.h"

#include <stdlib.h>
#include <string.h>

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
		server(new au_stream_server_socket(host, port));
	else
		server(new tcp_server_socket(host, port));
	return 0;
}

