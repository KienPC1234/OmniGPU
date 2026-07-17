#pragma once

#include <cstdint>

namespace omnigpu {

class Client;

namespace init {

bool initialize_guest(const char* host_hint = nullptr, uint16_t port_hint = 0);
bool connect_to_host();
void shutdown_guest();
void disconnect_client_only();

// Access client for synchronous queries (vkGetBufferDeviceAddress, etc.)
Client* get_client();

} // namespace init
} // namespace omnigpu
