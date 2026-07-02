# OmniGPU
OmniGPU is a high-performance, cross-platform C++ solution that enables virtual machines and headless servers to borrow host GPU power over LAN. By leveraging Mesa Zink to translate OpenGL to Vulkan, it intercepts Vulkan APIs, serializes data via FlatBuffers, and streams it over TCP with minimal software latency.
