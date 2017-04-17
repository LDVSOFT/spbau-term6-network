#pragma once

#include <cstddef>

/*
 * All the functions in the interface are blocking.
 */

struct stream_socket
{
    /*
     * Either sends all the data in buf or throws.
     * If exception is thrown then this socket would probably
     * throw them on all further sends.
     * Also in case of exception there is no guarantee about
     * the portion of data that has reached the other endpoint
     * and that has been processed by it.
     * You can't call this method in >1 threads simultaneously
     * - locking required;
     */
    virtual void send(const void *buf, size_t size) = 0;
    /*
     * Either reads size bytes of data into buf or throws.
     * Buf may be modified even if exception was thrown.
     * If exception is thrown then this socket would probably
     * throw them on all further recvs.
     * You can't call this method in >1 threads simultaneously
     * - locking required;
     */
    virtual void recv(void *buf, size_t size) = 0;
    virtual ~stream_socket() {};
};

struct stream_client_socket: virtual stream_socket
{
    /*
     * If exception is thrown then this socket would probably
     * throw them on all further connects.
     */
    virtual void connect() = 0;
};

struct stream_server_socket
{
    /*
     * If exception is thrown then this socket would probably
     * throw them on all further accepts.
     */
    virtual stream_socket* accept_one_client() = 0;
    virtual ~stream_server_socket() {};
};

typedef const char* hostname;
static constexpr hostname DEFAULT_HOST("localhost");

