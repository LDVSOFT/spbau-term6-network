#pragma once

#include "stream_socket.h"

#include <cstdint>
#include <memory>

struct protocol {
	enum message_type: uint8_t {
		CLI_PARTICIPATE = 0,
		CLI_ASK         = 1,
		CLI_RESULT      = 2,
		SRV_STATUS      = 3,
		SRV_TASK        = 4
	};

	using value = uint64_t;
	using status_len = uint8_t;
	using result = struct {
		value task;
		uint8_t res;
	};
	static constexpr status_len MAX_STATUS = 10;
	using status = value*;

	std::unique_ptr<stream_socket> socket;

	protocol(std::unique_ptr<stream_socket> socket);
	
	void write_participate();
	void write_ask();
	void write_result(result res);
	void write_status(status_len len, status stat);
	void write_task(value task);

	message_type read_message_type();
	result read_result();
	status_len read_status(status stat);
	value read_task();
};
