#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <memory>
#include <cstring>
#include <pthread.h>

#include "stream_socket.h"
#include "tcp_socket.h"
#include "au_stream_socket.h"

#define TEST_TCP_STREAM_SOCKET
//#define TEST_AU_STREAM_SOCKET

const char *TEST_ADDR = "localhost";
const tcp_port TCP_TEST_PORT = 40002;
const au_stream_port AU_TEST_CLIENT_PORT = 40001;
const au_stream_port AU_TEST_SERVER_PORT = 301;

static std::unique_ptr<stream_client_socket> client;
static std::unique_ptr<stream_server_socket> server;
static std::unique_ptr<stream_socket> server_client;

#define STREAM_TEST_VOLUME (1024 * 1024 * 1 / 4)

static void* test_stream_sockets_datapipe_thread_func(void*)
{
    uint64_t i = 0;
    const uint64_t max_i = STREAM_TEST_VOLUME / sizeof(uint64_t);
    constexpr size_t BUF_ITEMS = 1024;
    uint64_t buf[BUF_ITEMS];

    client->connect();
    while (i < max_i) {
        client->recv(buf, sizeof(buf));
        for (size_t buf_ix = 0; buf_ix < BUF_ITEMS; ++buf_ix, ++i) {
            if (buf[buf_ix] != i)
                std::cout << "READ FAILED AT "<< i << " " << buf[buf_ix] << std::endl;
            assert(buf[buf_ix] == i);
        }
    }

    return 0;
}

static void test_stream_sockets_datapipe()
{
    uint64_t i = 0;
    const uint64_t max_i = STREAM_TEST_VOLUME / sizeof(uint64_t);
    const size_t i_portion = 1024;
    uint64_t buf[i_portion];

    pthread_t th;
    pthread_create(&th, NULL, test_stream_sockets_datapipe_thread_func, NULL);

    server_client.reset(server->accept_one_client());
    while (i < max_i) {
        for (size_t buf_ix = 0; buf_ix < i_portion; ++buf_ix, ++i)
            buf[buf_ix] = i;
        server_client->send(buf, sizeof(buf));
    }
}

static void* test_stream_sockets_partial_data_sent_thread_func(void *)
{
    char buf[4];

    client->connect();
    buf[0] = 'H';
    buf[1] = 'e';
    buf[2] = 'l';
    buf[3] = 'l';
    client->send(buf, 2);
    client.reset();

    return NULL;
}

static void test_stream_sockets_partial_data_sent()
{
    char buf[4];

    pthread_t th;
    pthread_create(&th, NULL, test_stream_sockets_partial_data_sent_thread_func, NULL);

    bool thrown = false;
    server_client.reset(server->accept_one_client());
    try {
        server_client->recv(buf, 4);
    } catch (...) {
        // No data in the socket now
        // Check that error is returned
        thrown = true;
    }
    assert(thrown);
}

static void test_tcp_stream_sockets()
{
#ifdef TEST_TCP_STREAM_SOCKET
    server.reset(new tcp_server_socket(TEST_ADDR, TCP_TEST_PORT));
    client.reset(new tcp_client_socket(TEST_ADDR, TCP_TEST_PORT));

    //test_stream_sockets_datapipe();
	test_stream_sockets_partial_data_sent();
#endif
}

static void test_au_stream_sockets()
{
#ifdef TEST_AU_STREAM_SOCKET
    server.reset(new au_stream_server_socket(TEST_ADDR, AU_TEST_SERVER_PORT));
    client.reset(new au_stream_client_socket(TEST_ADDR, AU_TEST_CLIENT_PORT, AU_TEST_SERVER_PORT));

    test_stream_sockets_datapipe();
    test_stream_sockets_partial_data_sent();
#endif
}

int main()
{
    test_tcp_stream_sockets();
    test_au_stream_sockets();

    return 0;
}
