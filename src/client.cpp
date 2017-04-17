#include "client.h"
#include "protocol.h"

#include <exception>
#include <string>
#include <iostream>
#include <atomic>
#include <pthread.h>

using std::string;
using std::atomic_bool;
using std::getline;
using std::cin;
using std::cout;
using std::endl;
using std::flush;
using std::exception;
using std::exception_ptr;
using std::current_exception;
using std::rethrow_exception;
using namespace std::literals;

static atomic_bool working(true);
static protocol* prot;
static pthread_mutex_t mutex;

static void help() {
	cout << "Commands: quit, ask, participate" << endl;
}

static bool check(protocol::value n) {
	for (protocol::value i(2); i * i <= n; ++i)
		if (n % i == 0)
			return false;
	return true;
}

static exception_ptr thread_ex(nullptr);

static void *receiver(void* unused) {
	(void) unused;
	try {
		while (working) {
			protocol::message_type type;
			protocol::value status[protocol::MAX_STATUS], task;
			protocol::status_len len;
			protocol::result res;
			try {
				type = prot->read_message_type();
			} catch (exception const& e) {
				// EOF
				if (working)
					throw;
				else
					break;
			}
			switch (type) {
				case protocol::SRV_STATUS:
					len = prot->read_status(status);
					cout << "Got latest status (" << int(len) << "):";
					for (int i(0); i < len; ++i)
						cout << " " << status[i];
					cout << endl;
					break;
				case protocol::SRV_TASK:
					task = prot->read_task();
					cout << "Got task: " << task << endl;
					res.task = task;
					res.res = check(task);
					if (res.res)
						cout << "Found prime: " << task << endl;
					try {
						pthread_mutex_lock(&mutex);
						prot->write_result(res);
					} catch (exception const& e) {
						pthread_mutex_unlock(&mutex);
						throw;
					}
					pthread_mutex_unlock(&mutex);
					break;
				default:
					throw std::runtime_error("Wrong message type");
			}
		}
	} catch (exception const& e) {
		working = false;
		thread_ex = current_exception();
	}
	return nullptr;
}

void client(std::unique_ptr<stream_client_socket> socket) {
	try {
		socket->connect();
	} catch (exception const& e) {
		cout << "Failed to connect: " << e.what() << endl;
		return;
	}
	prot = new protocol(std::move(socket));

	pthread_t thread;
	pthread_create(&thread, NULL, receiver, NULL);

	try {
		string s;
		help();
		while (working) {
			if (!getline(cin, s))
				break;
			if (s == "quit"s)
				break;
			if (s == "ask"s) {
				try {
					pthread_mutex_lock(&mutex);
					prot->write_ask();
				} catch (exception const& e) {
					pthread_mutex_unlock(&mutex);
					throw;
				}
				pthread_mutex_unlock(&mutex);
				continue;
			}
			if (s == "participate") {
				try {
					pthread_mutex_lock(&mutex);
					prot->write_participate();
				} catch (exception const& e) {
					pthread_mutex_unlock(&mutex);
					throw;
				}
				pthread_mutex_unlock(&mutex);
				continue;
			}
			cout << "Unknown command" << endl;
			help();
		}
	} catch (exception const& e) {
		cout << "Error: " << e.what() << endl;
	}

	working = false;
	delete prot;
	pthread_join(thread, nullptr);
	if (thread_ex != nullptr) {
		try {
			rethrow_exception(thread_ex);
		} catch (exception const& e) {
			cout << "Error (from bg): " << e.what() << endl;
		}
	}
}
