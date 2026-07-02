# OmniGPU: Seamless API Forwarding over LAN

**OmniGPU** is like a **remote food delivery service**: A lightweight laptop (the Customer) doesn't need an expensive, heavy kitchen (the GPU); it simply sends a food order (Vulkan API commands) over a high-speed highway (TCP LAN) to a master chef (the Host GPU, such as an eGPU dock or an RTX 3050), who cooks the meal (renders the graphics), packs it tightly (LZ4 compression), and delivers it back instantly.

### 1. The Core Problem (The "Why")

* **The Struggle:** Virtual Machines (VMs) and Headless Linux Servers lack real graphics hardware. Running applications that require advanced OpenGL/Vulkan libraries usually causes them to crash instantly.
* **The Fix:** Borrow the host machine's physical GPU over the local network instead of fighting with complex, hardware-locked virtual drivers.

### 2. How It Works: Step-by-Step (The Flow)

* **Step 1: The Universal Translators (Mesa Zink & clvk)**
* *Metaphor:* Like having two bilingual waiters taking orders from French (OpenGL) and Spanish (OpenCL) customers, writing them all down in one single language, English (Vulkan), for the head chef.
* **Mesa Zink** (for graphics) and **clvk** (for computations) sit inside the VM, automatically translating all old commands into a single, modern **Vulkan** stream.


* **Step 2: The Vacuum Packer (FlatBuffers)**
* *Metaphor:* Vacuum-sealing food to save space in the delivery box.
* **FlatBuffers** packs the Vulkan commands into tight, zero-copy memory blocks so they can travel across the network lightning-fast.


* **Step 3: The Highway (TCP Socket)**
* *Metaphor:* A dedicated express lane with no traffic lights.
* The packed data travels through a **TCP LAN connection**. We enable `TCP_NODELAY` so the data packet leaves immediately without waiting for a full load.


* **Step 4: The Master Chef (Host Server)**
* *Metaphor:* A restaurant manager assigning different cooking stations to different cooks.
* A multi-threaded **C++ Server** on the host receives the data, assigns a dedicated thread to each VM, and executes the Vulkan commands on the physical GPU.


* **Step 5: The Return Trip (LZ4 / TurboJPEG)**
* *Metaphor:* Folding a huge poster into a tiny envelope.
* The finished picture (Framebuffer) is compressed instantly using **LZ4** or **TurboJPEG**, then sent back to the VM to be displayed on the screen.



### 3. Key Features (The Superpowers)

* **Multi-Platform Ready:** Built with modern C++, running seamlessly on Windows Guests/Hosts and Headless Linux Servers.
* **Zero-Hardware Passthrough:** Multiple VMs can share a single host GPU simultaneously without locking the hardware.
* **Smart Caching:** Like memorizing a restaurant menu, the VM caches hardware limits once on startup to kill network latency caused by synchronous "ask-and-wait" commands.
* **Command Batching:** Groups multiple small drawing commands into one large network packet to maintain high processing speeds.
* **Ready for Version Control:** File structure is modular and designed to easily integrate with automated build workflows on GitHub.

### 4. Development Roadmap (1-Year Plan)

* **Phase 1: TCP Foundation (Months 1-3):** Build the C++ Client-Server skeleton and ensure low-latency network communication.
* **Phase 2: OpenCL Compute (Months 4-6):** Send arrays of numbers for math calculations without needing a screen display.
* **Phase 3: Mesa Zink + Vulkan (Months 7-9):** Forward Vulkan commands and compress the returning images for real-time display.
* **Phase 4: Packaging (Months 10-12):** Wrap everything into portable `.dll` and `.so` files, perfect for deploying on any setup—from a thin Lenovo ThinkBook to a heavy workstation.

OmniGPU turns any weak virtual machine into a graphics powerhouse by seamlessly borrowing host GPU strength through the local network.
