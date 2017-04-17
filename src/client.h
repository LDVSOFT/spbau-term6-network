#pragma once

#include "stream_socket.h"

#include <memory>

void client(std::unique_ptr<stream_client_socket> socket);
