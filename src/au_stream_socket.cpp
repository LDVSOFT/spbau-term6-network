#include "au_stream_socket.h"
#include "endian.h"

#include <stdexcept>
#include <string>
#include <algorithm>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

using std::runtime_error;
using std::string;
using std::min;
using std::unique_ptr;
using namespace std::literals;

struct pthread_mutex_guard {
private:
	pthread_mutex_t& mutex;

public:
	pthread_mutex_guard(pthread_mutex_t& mutex): mutex(mutex) {
		pthread_mutex_lock(&mutex);
	}

	~pthread_mutex_guard() {
		pthread_mutex_unlock(&mutex);
	}
};

static runtime_error error(char const* s, char const* comment) {
	return runtime_error(string(s) + ": "s + string(comment));
}

static runtime_error error(char const* s) {
	return error(s, strerror(errno));
}

template<size_t size>
au_buffer<size>::au_buffer() {
	pthread_mutex_init(&mutex, nullptr);
}

template<size_t size>
au_buffer<size>::~au_buffer() {
	pthread_mutex_destroy(&mutex);
}

template<size_t size>
size_t au_buffer<size>::transfer(uint8_t* dst, uint8_t const* src, size_t len) {
	pthread_mutex_guard(mutex);

	if (start <= len)
		len = min(len, end - start);
	else
		len = min(len, end - start + size);
	memcpy(dst, src, len);
	return len;
}

template<size_t size>
size_t au_buffer<size>::read(uint8_t* buffer, size_t len) {
	return transfer(buffer, data, len);
}

template<size_t size>
size_t au_buffer<size>::write(uint8_t const* buffer, size_t len) {
	return transfer(data, buffer, len);
}

void au_base_socket::fill_addr(hostname host, struct sockaddr_in& addr) {
	struct addrinfo hints;
	struct addrinfo* res;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_RAW;
	hints.ai_protocol = IP_PROTO;
	hints.ai_flags = 0;

	int ret;
	if ((ret = getaddrinfo(host, NULL, &hints, &res)) != 0) {
		if (ret == EAI_SYSTEM)
			throw error("Bad hostname");
		else
			throw error("Bad hostname", gai_strerror(ret));
	}
	if (res[0].ai_addrlen != sizeof(struct sockaddr_in) || res[0].ai_addr->sa_family != AF_INET) {
		freeaddrinfo(res);
		throw runtime_error("Wtf");
	}
	memcpy(&addr, res[0].ai_addr, res[0].ai_addrlen);
	addr.sin_port = 0;
	freeaddrinfo(res);
}

au_base_socket::au_base_socket(au_stream_port local_port): local_port(local_port), state(CLOSED) {
	pthread_mutex_init(&mutex, nullptr);
	pthread_mutex_guard guard(mutex);

	if (pipe2(worker_pipe, O_NONBLOCK) != 0)
		throw error("Failed to open pipe");

	if (pthread_create(&worker, nullptr, work_wrap, this))
		throw error("Failed to start worker");
}

au_base_socket::~au_base_socket() {
	int const msg(WORKER_STOP);
	write(worker_pipe[0], &msg, sizeof(int));
	pthread_join(worker, nullptr);
	::close(worker_pipe[0]);
}

int au_base_socket::sendto(au_packet const& pack, sockaddr_in const& to) {
	unique_ptr<uint8_t[]> packet(new uint8_t[IP_MAXPACKET]);
	struct ip& ip = *reinterpret_cast<struct ip*>(packet.get());
	au_packet& au = *reinterpret_cast<struct au_packet*>(packet.get() + sizeof(struct ip));

	ip.ip_hl = sizeof(struct ip) / sizeof(uint32_t);
	ip.ip_v = 4;
	ip.ip_tos = 0;
	ip.ip_len = hton<decltype(ip.ip_len)>(sizeof(au) + pack.header.len);
	ip.ip_id = 0; // Filled in
	ip.ip_off = 0;
	ip.ip_ttl = 255;
	ip.ip_p = IP_PROTO;
	ip.ip_sum = 0; // Filled in
	ip.ip_src = in_addr{0}; // Filled in
	ip.ip_dst = to.sin_addr;

	au = pack;

	if (au.header.src != local_port)
		throw error("Illegal argument");

	ssize_t len(sizeof(struct ip) + sizeof(au) + pack.header.len);
	ssize_t ret(::sendto(socket_fd, packet.get(), len, 0, reinterpret_cast<sockaddr const*>(&to), sizeof(to)));
	if (ret == len)
		return 0;
	else
		return -1;
}

int au_base_socket::recvfrom(au_packet& pack, struct sockaddr_in& to) {
	unique_ptr<uint8_t[]> packet(new uint8_t[IP_MAXPACKET]);
	struct sockaddr_in dest;
	if (::recvfrom(socket_fd, packet.get(), IP_MAXPACKET, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&dest), nullptr) < 0) {
		return (errno == EAGAIN || errno == EWOULDBLOCK) ? -2 : -1;
	}
	struct ip const& ip(*reinterpret_cast<struct ip const*>(packet.get()));
	au_packet const& au(*reinterpret_cast<au_packet const*>(packet.get() + sizeof(uint32_t) * ip.ip_hl));

	if (au.header.dst != local_port)
		return -2;

	pack = au;
	to = dest;
	return 0;
}

void* au_base_socket::work_wrap(void* arg) {
	au_base_socket* self(static_cast<au_base_socket*>(arg));
	try {
		self->work();
	} catch (std::exception& e) {
		fprintf(stderr, "WORKER AT PORT %d: EXCEPTION: %s\n", self->local_port, e.what());
		::close(self->worker_pipe[1]);
		exit(1);
	}
	::close(self->worker_pipe[1]);
	return nullptr;
}

void au_base_socket::work() {
	/* Pure POSIX is not as powerfull as Linux ;) */
	constexpr int MAX_EVENTS(10);
	struct epoll_event event, events[MAX_EVENTS];

	int socket_fd = socket(AF_INET, SOCK_RAW, IP_PROTO);
	if (socket_fd < 0) {
		throw error("Failed to open socket");
	}
	int const value(1);
	if (setsockopt(socket_fd, IPPROTO_IP, IP_HDRINCL, &value, sizeof(value)) < 0) {
		::close(socket_fd);
		throw error("Failed to setsockopt");
	}

	int epoll(epoll_create1(0));
	if (epoll < 0) {
		::close(socket_fd);
		throw error("Failed to create epoll");
	}

	int timer_fd(timerfd_create(CLOCK_BOOTTIME, TFD_NONBLOCK));
	if (timer_fd < 0) {
		::close(epoll);
		::close(socket_fd);
		throw error("Failed to create timer");
	}

	event.events = EPOLLIN;
	event.data.fd = timer_fd;
	if (epoll_ctl(epoll, EPOLL_CTL_ADD, timer_fd, &event) == -1) {
		::close(timer_fd);
		::close(epoll);
		::close(socket_fd);
		throw error("Failed to add timer to epoll");
	}

	event.data.fd = socket_fd;
	if (epoll_ctl(epoll, EPOLL_CTL_ADD, socket_fd, &event) == -1) {
		::close(timer_fd);
		::close(epoll);
		::close(socket_fd);
		throw error("Failed to add socket to epoll");
	}

	event.data.fd = worker_pipe[1];
	if (epoll_ctl(epoll, EPOLL_CTL_ADD, worker_pipe[1], &event) == -1) {
		::close(timer_fd);
		::close(epoll);
		::close(socket_fd);
		throw error("Failed to add pipe to epoll");
	}

	bool working(true);
	static au_packet packet;
	static sockaddr_in remote;
	while (working) {
		int n(epoll_wait(epoll, events, MAX_EVENTS, -1));
		if (n < 0)
			throw error("epoll_wait");
		for (int i(0); i != n; ++i) {
			int const fd(events[i].data.fd);
			if (fd == socket_fd) {
				int ret(recvfrom(packet, remote));
				if (ret == -1) {
					throw error("Failed to accept packet");
				}
				if (ret == -2)
					continue;
				switch (state) {
				}
			} else if (fd == timer_fd) {
				// TODO retransmission
			} else if (fd == worker_pipe[1]) {
				int msg;
				if (read(worker_pipe[1], &msg, sizeof(int)) != sizeof(int))
					throw("Failed to read msg");
				switch (msg) {
					case WORKER_STOP:
						working = false;
						break;
				}
			} else
				throw error("Wrong fd");
		}
	}

	::close(timer_fd);
	::close(epoll);
	::close(socket_fd);
	::close(worker_pipe[1]);
}
