#include "protocol.h"
#include "endian.h"

#include <type_traits>
#include <stdexcept>

using std::enable_if_t;
using std::remove_reference_t;
using std::underlying_type_t;
//using std::is_integral_v;
//using std::is_enum_v;
template<typename T>
constexpr bool is_integral_v = std::is_integral<T>::value;
template<typename T>
constexpr bool is_enum_v = std::is_enum<T>::value;
using std::unique_ptr;
using std::runtime_error;

// IO-functions for numerics and enums
template<typename T>
static void write_to(stream_socket& socket, T value_, enable_if_t<is_integral_v<T>>* _x = nullptr) {
	(void) _x;
	remove_reference_t<T> value = hton(value_);
	socket.send((void*)(&value), sizeof(value));
}

template<typename T>
static void write_to(stream_socket& socket, T value, enable_if_t<is_enum_v<T>>* _x = nullptr) {
	(void) _x;
	write_to(socket, static_cast<underlying_type_t<T>>(value));
}

template<typename T>
static remove_reference_t<T> read_from(stream_socket& socket, enable_if_t<is_integral_v<T>, remove_reference_t<T>> value = 0) {
	socket.recv((void*)(&value), sizeof(value));
	return ntoh(value);
}

template<typename T>
static remove_reference_t<T> read_from(stream_socket& socket, enable_if_t<is_enum_v<T>>* _x = nullptr) {
	(void) _x;
	return static_cast<remove_reference_t<T>>(read_from<underlying_type_t<T>>(socket));
}

// Protocol
protocol::protocol(unique_ptr<stream_socket> socket): socket(std::move(socket)) {}

void protocol::write_participate() {
	write_to(*socket, CLI_PARTICIPATE);
}

void protocol::write_ask() {
	write_to(*socket, CLI_ASK);
}

void protocol::write_result(result res) {
	write_to(*socket, CLI_RESULT);
	write_to(*socket, res.task);
	write_to(*socket, res.res);
}

void protocol::write_status(status_len len, status stat) {
	write_to(*socket, SRV_STATUS);
	if (len > MAX_STATUS)
		throw runtime_error("Too long status");
	write_to(*socket, len);
	for (status_len i(0); i < len; ++i)
		write_to(*socket, stat[i]);
}

void protocol::write_task(value task) {
	write_to(*socket, SRV_TASK);
	write_to(*socket, task);
}

protocol::message_type protocol::read_message_type() {
	return read_from<message_type>(*socket);
}

protocol::result protocol::read_result() {
	result res;
	res.task = read_from<value>(*socket);
	res.res = read_from<decltype(res.res)>(*socket);
	return res;
}

protocol::status_len protocol::read_status(status stat) {
	status_len len(read_from<status_len>(*socket));
	for (status_len i(0); i < len; ++i)
		stat[i] = read_from<value>(*socket);
	return len;
}

protocol::value protocol::read_task() {
	return read_from<value>(*socket);
}
