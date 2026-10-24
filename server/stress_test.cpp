#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <asio.hpp>

using asio::ip::tcp;

std::atomic<int> g_successful_connections{0};
std::atomic<int> g_messages_exchanged{0};
std::atomic<int> g_connection_failures{0};

void run_simulated_client(int id, const std::string& host, short port, int messages_to_send) {
    try {
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        tcp::socket socket(ioc);

        auto endpoints = resolver.resolve(host, std::to_string(port));
        asio::connect(socket, endpoints);

        // Perform RFC 6455 Handshake
        std::string request = 
            "GET /chat HTTP/1.1\r\n"
            "Host: " + host + ":" + std::to_string(port) + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";

        asio::write(socket, asio::buffer(request));

        asio::streambuf response;
        asio::read_until(socket, response, "\r\n\r\n");

        g_successful_connections++;

        // Send a burst of chat frames
        for (int i = 0; i < messages_to_send; ++i) {
            std::string payload = "{\"type\":\"chat\",\"message\":\"Stress burst #" + std::to_string(i) + " from client " + std::to_string(id) + "\"}";
            
            // Client-to-server frames MUST be masked per RFC 6455
            std::vector<uint8_t> frame;
            frame.push_back(0x81); // Text frame FIN
            
            size_t len = payload.size();
            frame.push_back(static_cast<uint8_t>(len | 0x80)); // Masked bit set
            
            // 4-byte mask key
            uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
            frame.insert(frame.end(), mask, mask + 4);

            for (size_t j = 0; j < len; ++j) {
                frame.push_back(static_cast<uint8_t>(payload[j] ^ mask[j % 4]));
            }

            asio::write(socket, asio::buffer(frame));
            g_messages_exchanged++;
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }

        // Clean socket shutdown
        std::error_code ec;
        socket.shutdown(tcp::socket::shutdown_both, ec);
        socket.close(ec);

    } catch (const std::exception& e) {
        g_connection_failures++;
    }
}

int main() {
    const int NUM_CLIENTS = 40;       // 40 concurrent clients (20 pairs)
    const int MSGS_PER_CLIENT = 10;   // 10 messages each -> 400 total messages

    std::cout << "=================================================\n";
    std::cout << ">>> Launching C++ Asio WebSocket Concurrency Test\n";
    std::cout << ">>> Simulated Clients: " << NUM_CLIENTS << "\n";
    std::cout << ">>> Total Frames:      " << (NUM_CLIENTS * MSGS_PER_CLIENT) << "\n";
    std::cout << "=================================================\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workers;
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        workers.emplace_back(run_simulated_client, i + 1, "127.0.0.1", 8080, MSGS_PER_CLIENT);
    }

    for (auto& th : workers) {
        if (th.joinable()) th.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

    std::cout << "\n--- TEST RESULTS ---\n";
    std::cout << "Successful Connections: " << g_successful_connections.load() << " / " << NUM_CLIENTS << "\n";
    std::cout << "Connection Failures:    " << g_connection_failures.load() << "\n";
    std::cout << "Total Frames Transferred: " << g_messages_exchanged.load() << "\n";
    std::cout << "Total Execution Time:   " << elapsed.count() << " ms\n";
    std::cout << "Average Throughput:     " << (g_messages_exchanged.load() / (elapsed.count() / 1000.0)) << " frames/sec\n";
    std::cout << "=================================================\n";

    return 0;
}