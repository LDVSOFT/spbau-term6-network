#include "server.h"
#include "protocol.h"

#include <atomic>
#include <vector>
#include <set>
#include <algorithm>
#include <iostream>
#include <pthread.h>

using std::cout;
using std::endl;

static std::atomic_bool working(true);
static pthread_mutex_t primes_mutex;
static std::vector<protocol::value> primes;
static std::set<protocol::value> pending;
static protocol::value last(2);

struct client_handler {
	pthread_t thread;
	std::unique_ptr<protocol> prot;
	std::set<protocol::value> tasks;
	std::atomic_bool done;

	client_handler(std::unique_ptr<protocol>&& prot):
		prot(std::move(prot)) {
	}

	static void* enter(void* arg) {
		std::unique_ptr<client_handler> self((client_handler*)(arg));
		try {
			self->handle();
		} catch (std::exception const& e) {
		}
		// Client was given a task, and he has not sent the result
		cout << "Client gone, left " << self->tasks.size() << " tasks unsolved" << endl;
		pthread_mutex_lock(&primes_mutex);
		pending.insert(self->tasks.begin(), self->tasks.end());
		pthread_mutex_unlock(&primes_mutex);
		return nullptr;
	}

	void handle() {
		while (working) {
			protocol::message_type type;
			protocol::result res;
			protocol::value task;

			type = prot->read_message_type();
			switch (type) {
				case protocol::CLI_RESULT:
					res = prot->read_result();
					cout << "Client sent result " << res.res << " for " << res.task << endl;
					if (res.res) {
						pthread_mutex_lock(&primes_mutex);
						primes.push_back(res.task);
						pthread_mutex_unlock(&primes_mutex);
					}
					tasks.erase(res.task);
					break;
				case protocol::CLI_PARTICIPATE:
					pthread_mutex_lock(&primes_mutex);
					if (pending.size() == 0) {
						task = last++;
					} else {
						task = *pending.begin();
						pending.erase(pending.begin());
					}
					pthread_mutex_unlock(&primes_mutex);
					cout << "Client asked for task, sending " << task << endl;
					tasks.insert(task);
					prot->write_task(task);
					break;
				case protocol::CLI_ASK:
					cout << "Client asked for status" << endl;
					try {
						pthread_mutex_lock(&primes_mutex);
						protocol::status status(primes.data());
						protocol::status_len len(primes.size());
						if (primes.size() > protocol::MAX_STATUS) {
							status = primes.data() + primes.size() - protocol::MAX_STATUS;
							len = protocol::MAX_STATUS;
						}
						prot->write_status(len, status);
					} catch (std::exception const& e) {
						pthread_mutex_unlock(&primes_mutex);
						throw;
					}
					pthread_mutex_unlock(&primes_mutex);
					break;
				default:
					throw std::runtime_error("Wrong message type");
			}
		}
	}
};

void server(stream_server_socket* server_socket) {
	pthread_mutex_init(&primes_mutex, nullptr);
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	while (working) {
		stream_socket* socket;
		try {
			socket = server_socket->accept_one_client();
		} catch (std::exception const& e) {
			if (working)
				throw;
			else
				break;
		}
		cout << "Hey, a client!" << endl;
		client_handler* handler = new client_handler(std::make_unique<protocol>(std::unique_ptr<stream_socket>(socket)));
		pthread_create(&handler->thread, &attr, client_handler::enter, (void*)(handler));
	}

	delete server_socket;
	pthread_mutex_destroy(&primes_mutex);
	pthread_attr_destroy(&attr);
}
