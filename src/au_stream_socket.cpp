#include "au_stream_socket.h"
#include "endian.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>

using std::make_unique;
using std::min;
using std::runtime_error;
using std::string;
using std::to_string;
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

in_addr_t au_base_socket::get_addr(hostname host) {
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

	in_addr_t result(reinterpret_cast<sockaddr_in const*>(res[0].ai_addr)->sin_addr.s_addr);
	freeaddrinfo(res);
	return result;
}

in_addr_t au_base_socket::local_by_remote(in_addr_t remote_addr) {
	int const buffersize(128);
	char buffer[buffersize];
	int ret;

	if (inet_ntop(AF_INET, &remote_addr, buffer, buffersize) == nullptr)
		throw error("ntop");

	string output, cmd("ip route get " + string(buffer) + " | grep -Po '(?<=src )[^ ]+'");
	FILE* f(popen(cmd.c_str(), "r"));
	if (f == nullptr)
		throw error("popen");
	while (!feof(f)) {
		if (fgets(buffer, buffersize, f) != nullptr)
			output += buffer;
	}
	output.resize(output.size() - 1); // trim EOL ._.
	ret = WEXITSTATUS(pclose(f));
	if (ret != 0)
		throw error("pclose");

	in_addr_t result;
	if (inet_pton(AF_INET, output.c_str(), &result) != 1)
		throw error("pton");
	return result;
}

int8_t au_base_socket::checksum(au_packet const& packet) {
	uint8_t sum(0);
	uint8_t const* ptr(reinterpret_cast<uint8_t const*>(&packet));
	uint8_t const* end(ptr + sizeof(au_packet_header) + packet.header.len);
	while (ptr != end) {
		sum ^= *ptr;
		ptr++;
	}
	return sum;
}

au_base_socket::au_base_socket(
		in_addr_t local_addr, au_stream_port local_port,
		in_addr_t remote_addr, au_stream_port remote_port
):
   au_base_socket(local_addr, local_port, remote_addr, remote_port, CLOSED)
{
}

au_base_socket::au_base_socket(
		in_addr_t local_addr, au_stream_port local_port,
		in_addr_t remote_addr, au_stream_port remote_port,
		au_socket_state state
):
		local_port(local_port), remote_port(remote_port),
		local_addr(local_addr), remote_addr(remote_addr),
		state(state)
{
	pthread_mutex_init(&mutex, nullptr);
	pthread_mutex_guard guard(mutex);

	if (pipe2(worker_pipe, O_NONBLOCK) != 0)
		throw error("Failed to open pipe");

	if (pthread_create(&worker, nullptr, work_wrap, this))
		throw error("Failed to start worker");
}

au_base_socket::~au_base_socket() {
	try {
		to_worker(WORKER_STOP);
	} catch (...) {
	}
	pthread_join(worker, nullptr);
	::close(worker_pipe[1]);
}

int au_base_socket::send_packet(au_packet& pack) {
	unique_ptr<uint8_t[]> packet(new uint8_t[IP_MAXPACKET]);
	struct ip& ip = *reinterpret_cast<struct ip*>(packet.get());
	au_packet& au = *reinterpret_cast<struct au_packet*>(packet.get() + sizeof(struct ip));

	pack.header.src = local_port;
	pack.header.dst = remote_port;
	pack.header.sum = 0;
	pack.header.sum = checksum(pack);

	ip.ip_hl = sizeof(struct ip) / sizeof(uint32_t);
	ip.ip_v = 4;
	ip.ip_tos = 0;
	ip.ip_len = hton<decltype(ip.ip_len)>(sizeof(au) + pack.header.len);
	ip.ip_id = 0; // Filled in
	ip.ip_off = 0;
	ip.ip_ttl = 255;
	ip.ip_p = IP_PROTO;
	ip.ip_sum = 0; // Filled in
	ip.ip_src.s_addr = local_addr;
	ip.ip_dst.s_addr = remote_addr;

	au = pack;

	if (au.header.src != local_port)
		throw error("Illegal argument");

	ssize_t len(sizeof(struct ip) + sizeof(au_packet_header) + pack.header.len);
	struct sockaddr_in to;
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = remote_addr;
	ssize_t ret(::sendto(socket_fd, packet.get(), len, 0, reinterpret_cast<sockaddr const*>(&to), sizeof(to)));
	if (ret == len)
		return 0;
	else
		return -1;
}

int au_base_socket::recv_packet(au_packet& pack, in_addr_t& from) {
	unique_ptr<uint8_t[]> packet(new uint8_t[IP_MAXPACKET]);
	struct sockaddr_in dest;
	socklen_t dest_len(sizeof(dest));
	int ret(::recvfrom(socket_fd, packet.get(), IP_MAXPACKET, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&dest), &dest_len));
	if (ret <= 0) {
		if (ret == 0)
			return -2;
		return (errno == EAGAIN || errno == EWOULDBLOCK) ? -2 : -1;
	}
	in_addr_t source(dest.sin_addr.s_addr);
	struct ip const& ip(*reinterpret_cast<struct ip const*>(packet.get()));
	au_packet const& au(*reinterpret_cast<au_packet const*>(packet.get() + sizeof(uint32_t) * ip.ip_hl));

	if (checksum(au) != 0)
		return -2;
	if (au.header.dst != local_port)
		return -2;
	if (state != LISTEN)
		if (au.header.src != remote_port || source != remote_addr)
			return -2;

	pack = au;
	from = source;
	return 0;
}

void au_base_socket::listen(size_t backlog) {
	pthread_mutex_guard guard(mutex);
	state = LISTEN;
	this->backlog = backlog;
}

void au_base_socket::connect() {
	pthread_mutex_guard guard(mutex);
	if (state != CLOSED)
		return;
	state = SYN_SENT;
	to_worker(WORKER_CONNECT);
	while (state != ESTABLISHED && state != CLOSED)
		pthread_cond_wait(&update, &mutex);
	if (state != ESTABLISHED)
		throw error("failed to connect");
}

au_base_socket* au_base_socket::accept() {
	while (true) {
		au_base_socket* result;
		/* raw accept */ {
			pthread_mutex_guard guard(mutex);
			while (pending_clients.size() == 0 && state == LISTEN)
				pthread_cond_wait(&update, &mutex);
			result = *pending_clients.begin();
			pending_clients.erase(pending_clients.begin());
		}
		pthread_mutex_guard guard(result->mutex);
		while (result->state != CLOSED && result->state != ESTABLISHED)
			pthread_cond_wait(&result->update, &result->mutex);
		if (result->state == ESTABLISHED) {
			return result;
		}
	}
}

void au_base_socket::send(uint8_t const* data, size_t len) {
	pthread_mutex_guard guard(mutex);
	size_t pos(0);
	while (pos < len) {
		size_t dlen(len - pos);
		if (dlen > BUFFER_SIZE - (send_pos - send_acked))
			dlen = BUFFER_SIZE - (send_pos - send_acked);
		if (dlen == 0 && state == ESTABLISHED) {
			pthread_cond_wait(&update, &mutex);
			continue;
		}
		if (state != ESTABLISHED)
			throw error("closed!", "");
		size_t buffer_start(send_pos % BUFFER_SIZE);
		if (dlen > BUFFER_SIZE - buffer_start)
			dlen = BUFFER_SIZE - buffer_start;
		memcpy(send_buffer + buffer_start, data, dlen);
		send_pos += dlen;
		pos += dlen;
		to_worker(WORKER_SEND_DATA);
	}
}

void au_base_socket::recv(uint8_t* data, size_t len) {
	pthread_mutex_guard guard(mutex);
	size_t pos(0);
	while (pos < len) {
		size_t dlen(len - pos);
		if (dlen > recv_pos - recv_taken)
			dlen = recv_pos - recv_taken;
		if (dlen == 0 && state == ESTABLISHED) {
			pthread_cond_wait(&update, &mutex);
			continue;
		}
		if (state != ESTABLISHED)
			throw error("closed!", "");
		size_t buffer_start(recv_taken % BUFFER_SIZE);
		if (dlen > BUFFER_SIZE - buffer_start)
			dlen = BUFFER_SIZE - buffer_start;
		memcpy(data, send_buffer + buffer_start, dlen);
		recv_taken += dlen;
	}
}
void* au_base_socket::work_wrap(void* arg) {
	au_base_socket* self(static_cast<au_base_socket*>(arg));
	try {
		self->work();
	} catch (std::exception& e) {
		fprintf(stderr, "WORKER AT PORT %d: EXCEPTION: %s\n", self->local_port, e.what());
		::close(self->worker_pipe[0]);
		exit(1);
	}
	::close(self->worker_pipe[0]);
	return nullptr;
}

void au_base_socket::timer_setup() {
	itimerspec newval;
	newval.it_value.tv_sec = 1;
	newval.it_value.tv_nsec = 0;
	newval.it_interval = newval.it_value;
	if (timerfd_settime(timer_fd, 0, &newval, nullptr) != 0)
		throw error("timerfd_settime (setup)");
	timeouts = 0;
}

void au_base_socket::timer_shutdown() {
	itimerspec newval;
	newval.it_value.tv_sec = 0;
	newval.it_value.tv_nsec = 0;
	newval.it_interval = newval.it_value;
	if (timerfd_settime(timer_fd, 0, &newval, nullptr) != 0)
		throw error("timerfd_settime (shutdown)");
}

void au_base_socket::work() {
	/* Pure POSIX is not as powerfull as Linux ;) */
	constexpr int MAX_EVENTS(10);
	struct epoll_event event, events[MAX_EVENTS];

	socket_fd = socket(AF_INET, SOCK_RAW, IP_PROTO);
	if (socket_fd < 0) {
		throw error("Failed to open socket");
	}
	int const value(1);
	if (setsockopt(socket_fd, IPPROTO_IP, IP_HDRINCL, &value, sizeof(value)) < 0) {
		::close(socket_fd);
		throw error("Failed to setsockopt");
	}

	epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		::close(socket_fd);
		throw error("Failed to create epoll");
	}

	timer_fd = timerfd_create(CLOCK_BOOTTIME, TFD_NONBLOCK);
	if (timer_fd < 0) {
		::close(epoll_fd);
		::close(socket_fd);
		throw error("Failed to create timer");
	}

	event.events = EPOLLIN;
	event.data.fd = timer_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &event) == -1) {
		::close(timer_fd);
		::close(epoll_fd);
		::close(socket_fd);
		throw error("Failed to add timer to epoll");
	}

	event.data.fd = socket_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) == -1) {
		::close(timer_fd);
		::close(epoll_fd);
		::close(socket_fd);
		throw error("Failed to add socket to epoll");
	}

	event.data.fd = worker_pipe[0];
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, worker_pipe[0], &event) == -1) {
		::close(timer_fd);
		::close(epoll_fd);
		::close(socket_fd);
		throw error("Failed to add pipe to epoll");
	}

	bool working(true);
	au_packet in_packet, out_packet;
	in_addr_t remote;
	while (working) {
		int n(epoll_wait(epoll_fd, events, MAX_EVENTS, -1));
		if (n < 0)
			throw error("epoll_wait");
		for (int i(0); i != n; ++i) {
			int const fd(events[i].data.fd);
			if (fd == socket_fd) {
				int ret(recv_packet(in_packet, remote));
				if (ret == -1) {
					throw error("Failed to accept packet");
				}
				if (ret == -2)
					continue;
				pthread_mutex_guard guard(mutex);
				switch (state) {
					case CLOSED:
						break;
					case LISTEN:
						if (in_packet.header.flags != AU_SYN)
							break;
						if (pending_clients.size() < backlog) {
							au_base_socket* client_socket(new au_base_socket(local_addr, local_port, remote, in_packet.header.src, SYN_RECEIVED));
							pthread_mutex_guard client_guard(client_socket->mutex);
							client_socket->recv_pos = in_packet.header.seq;
							client_socket->to_worker(WORKER_SEND_SYNACK);
							pending_clients.insert(client_socket);
							pthread_cond_broadcast(&update);
						}
						break;
					case SYN_SENT:
						if (in_packet.header.flags != AU_SYNACK)
							break;
						if (in_packet.header.ack != send_pos)
							break;
						recv_pos = in_packet.header.seq;
						out_packet.header.len = 0;
						out_packet.header.flags = AU_ACK;
						out_packet.header.ack = recv_pos;
						send_packet(out_packet);
						state = ESTABLISHED;
						timer_setup();
						pthread_cond_broadcast(&update);
						break;
					case SYN_RECEIVED:
						if (in_packet.header.flags != AU_ACK)
							break;
						if (in_packet.header.ack != send_pos)
							break;
						state = ESTABLISHED;
						timer_setup();
						pthread_cond_broadcast(&update);
						break;
					case ESTABLISHED:
						if (in_packet.header.flags == AU_ACK) {
							if (send_acked < in_packet.header.ack) {
								send_acked = in_packet.header.ack;
								pthread_cond_broadcast(&update);
								to_worker(WORKER_SEND_DATA);
								if (send_acked == send_pos) {
									timer_shutdown();
								} else {
									timer_setup();
								}
							}
						} else if (in_packet.header.flags == 0 && in_packet.header.seq == recv_pos) {
							au_stream_pos accepted(in_packet.header.len);
							if (accepted > BUFFER_SIZE - (recv_pos - recv_taken))
								accepted = BUFFER_SIZE - (recv_pos - recv_taken);
							size_t buffer_start(recv_pos % BUFFER_SIZE), buffer_end((recv_pos + accepted) % BUFFER_SIZE);
							if (buffer_start < buffer_end) {
								memcpy(recv_buffer + buffer_start, in_packet.data, accepted);
							} else {
								memcpy(recv_buffer + buffer_start, in_packet.data, BUFFER_SIZE - buffer_start);
								memcpy(recv_buffer, in_packet.data + (BUFFER_SIZE - buffer_start), buffer_end);
							}
							recv_pos += accepted;
							pthread_cond_broadcast(&update);
							out_packet.header.len = 0;
							out_packet.header.flags = AU_ACK;
							out_packet.header.ack = accepted;
						}
						break;
				}
			} else if (fd == timer_fd) {
				uint64_t msg;
				read(timer_fd, &msg, sizeof(msg));
				pthread_mutex_guard guard(mutex);
				timeouts += msg;
				switch (state) {
					case SYN_SENT:
					case SYN_RECEIVED:
						/* Timeout */
						state = CLOSED;
						pthread_cond_broadcast(&update);
						break;
					case ESTABLISHED:
						if (timeouts >= 10) {
							state = CLOSED;
							pthread_cond_broadcast(&update);
						} else {
							to_worker(WORKER_SEND_DATA);
						}
						break;
					case CLOSED:
					case LISTEN:
						break;
				}
			} else if (fd == worker_pipe[0]) {
				worker_msg msg;
				if (read(worker_pipe[0], &msg, sizeof(int)) != sizeof(int))
					throw("Failed to read msg");
				pthread_mutex_guard guard(mutex);
				switch (msg) {
					case WORKER_STOP:
						working = false;
						break;
					case WORKER_CLOSE:
						/* FIXME */
						state = CLOSED;
						pthread_cond_broadcast(&update);
						break;
					case WORKER_SEND_DATA:
						/* scope */ {
							au_stream_pos pos(send_acked);
							while (pos < send_pos) {
								au_stream_pos end(pos + mtu - au_packet::HEADERS);
								if (end > send_pos)
									end = send_pos;
								out_packet.header.len = end - pos;
								out_packet.header.flags = AU_NONE;
								out_packet.header.seq = pos;
								size_t buffer_start(pos % BUFFER_SIZE), buffer_end(end % BUFFER_SIZE);
								if (buffer_end > buffer_start) {
									memcpy(out_packet.data, send_buffer + buffer_start, buffer_end - buffer_start);
								} else {
									memcpy(out_packet.data, send_buffer + buffer_start, BUFFER_SIZE - buffer_start);
									memcpy(out_packet.data + BUFFER_SIZE - buffer_start, send_buffer, buffer_end);
								}
								send_packet(out_packet);
								pos = end;
							}
						}
						break;
					case WORKER_SEND_SYNACK:
						out_packet.header.len = 0;
						out_packet.header.flags = AU_SYNACK;
						out_packet.header.ack = recv_pos;
						out_packet.header.seq = send_pos;
						timer_setup();
						send_packet(out_packet);
						break;
					case WORKER_CONNECT:
						out_packet.header.len = 0;
						out_packet.header.flags = AU_SYN;
						out_packet.header.seq = send_pos;
						timer_setup();
						send_packet(out_packet);
						break;
				}
			} else
				throw error("Wrong fd");
		}
	}

	::close(timer_fd);
	::close(epoll_fd);
	::close(socket_fd);
}

void au_base_socket::to_worker(worker_msg msg) {
	if (write(worker_pipe[1], &msg, sizeof(msg)) != sizeof(msg))
		throw error("WORKER can't send message");
}

au_stream_socket::au_stream_socket(au_base_socket* base_socket): base_socket(base_socket) {
}

void au_stream_socket::send(void const* buf, size_t size) {
	base_socket->send(static_cast<uint8_t const*>(buf), size);
}

void au_stream_socket::recv(void* buf, size_t size) {
	base_socket->recv(static_cast<uint8_t*>(buf), size);
}

au_stream_client_socket::au_stream_client_socket(hostname host, au_stream_port local_port, au_stream_port remote_port):
		au_stream_socket(nullptr)
{
	in_addr_t remote_addr(au_base_socket::get_addr(host)), local_addr(au_base_socket::local_by_remote(remote_addr));
	base_socket = make_unique<au_base_socket>(local_addr, local_port, remote_addr, remote_port);
}

void au_stream_client_socket::connect() {
	base_socket->connect();
}

au_stream_server_socket::au_stream_server_socket(hostname host, au_stream_port port) {
	in_addr_t local_addr(au_base_socket::get_addr(host));
	base_socket = make_unique<au_base_socket>(local_addr, port, 0, 0);
	base_socket->listen();
}

stream_socket* au_stream_server_socket::accept_one_client() {
	au_base_socket* accepted(base_socket->accept());
	return new au_stream_socket(accepted);
}
