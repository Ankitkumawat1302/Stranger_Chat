# Real-Time 1-on-1 Anonymous Chat Engine

A high-performance, low-latency stranger chat web platform featuring an asynchronous C++ networking backend and a lightweight vanilla web frontend. 

Rather than relying on high-level runtimes or third-party server frameworks, this system is built directly on POSIX network sockets and standard C++ data structures to maximize throughput, minimize memory footprint, and handle real-time message routing with sub-millisecond dispatch times.

---

## ⚡ Key Highlights & Architecture

- **Custom WebSocket Engine (RFC 6455):** Hand-crafted HTTP-to-WebSocket upgrade handshake (SHA-1 hashing + Base64 encoding) and bitwise frame parsing/unmasking directly over raw TCP sockets.
- **Asynchronous Concurrency:** Event-driven I/O loop utilizing Linux `epoll` / non-blocking sockets to manage concurrent connections without per-thread memory overhead.
- **O(1) Matchmaking Engine:** Decoupled pairing system utilizing `std::queue` for zero-wait FIFO discovery and `std::unordered_map` for instant socket routing, session tracking, and disconnect recovery.
- **Pure Zero-Dependency Frontend:** Semantic HTML5, responsive CSS3 Flexbox layout with dynamic bubble wrapping (`fit-content`), and vanilla JavaScript WebSocket client.
- **Resource Efficient:** Near-zero runtime memory footprint, direct pointer lifecycle management, and no garbage collection pauses.

---

## 🛠️ Tech Stack

- **Backend:** C++ (POSIX Sockets, `sys/epoll.h`, STL Containers, Bitwise Operations)
- **Networking Protocol:** TCP/IP, WebSockets (RFC 6455)
- **Frontend:** HTML5, CSS3 (Flexbox), Vanilla JavaScript (Browser WebSocket API)
- **Tooling:** GCC/Clang, Make / CMake, Git