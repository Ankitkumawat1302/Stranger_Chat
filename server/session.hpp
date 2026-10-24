#pragma once
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <iostream>
#include <asio.hpp>
#include "crypto.hpp"
#include "protocol.hpp"
#include "matchmaker.hpp"

using asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
    tcp::socket socket_;
    asio::streambuf buffer_;
    asio::steady_timer heartbeat_timer_;
    std::atomic<bool> is_closed_{false};

public:
    Session(tcp::socket socket)
        : socket_(std::move(socket)),
          heartbeat_timer_(socket_.get_executor()) {}

    void start() {
        auto self = shared_from_this();
        asio::async_read_until(socket_, buffer_, "\r\n\r\n",
            [this, self](std::error_code ec, std::size_t) {
                if (!ec) {
                    std::istream request_stream(&buffer_);
                    std::string line, sec_key;
                    while (std::getline(request_stream, line) && line != "\r") {
                        if (line.back() == '\r') line.pop_back();
                        if (line.find("Sec-WebSocket-Key: ") == 0) {
                            sec_key = line.substr(19);
                        }
                    }

                    if (!sec_key.empty()) {
                        std::string accept_key = compute_ws_accept(sec_key);
                        std::string response =
                            "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";

                        auto resp_buf = std::make_shared<std::string>(response);
                        asio::async_write(socket_, asio::buffer(*resp_buf),
                            [this, self, resp_buf](std::error_code ec, std::size_t) {
                                if (!ec) {
                                    g_matchmaker.join_queue(self);
                                    read_frame();
                                    start_heartbeat();
                                }
                            });
                    }
                }
            });
    }

    void send_text(const std::string& text) {
        send_raw_frame(make_ws_frame(text, 0x81));
    }

    void send_ping() {
        send_raw_frame(make_ws_frame("", 0x89));
    }

    void send_raw_frame(std::vector<uint8_t> frame_data) {
        if (is_closed_) return;
        auto self = shared_from_this();
        auto frame = std::make_shared<std::vector<uint8_t>>(std::move(frame_data));
        asio::post(socket_.get_executor(), [this, self, frame]() {
            if (is_closed_) return;
            asio::async_write(socket_, asio::buffer(*frame),
                [](std::error_code, std::size_t) {});
        });
    }

    void start_heartbeat() {
        if (is_closed_) return;
        auto self = shared_from_this();
        heartbeat_timer_.expires_after(std::chrono::seconds(25));
        heartbeat_timer_.async_wait([this, self](std::error_code ec) {
            if (!ec && !is_closed_) {
                send_ping();
                start_heartbeat();
            }
        });
    }

    void close() {
        if (is_closed_.exchange(true)) return;
        heartbeat_timer_.cancel();
        std::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

    void read_frame() {
        if (is_closed_) return;
        auto self = shared_from_this();
        auto header = std::make_shared<std::vector<uint8_t>>(2);
        asio::async_read(socket_, asio::buffer(*header),
            [this, self, header](std::error_code ec, std::size_t) {
                if (ec) {
                    g_matchmaker.leave(self);
                    close();
                    return;
                }

                uint8_t opcode = (*header)[0] & 0x0F;
                bool masked = ((*header)[1] & 0x80) != 0;
                uint64_t payload_len = (*header)[1] & 0x7F;

                if (opcode == 0x8) {
                    g_matchmaker.leave(self);
                    close();
                    return;
                }

                if (payload_len == 126) {
                    auto ext = std::make_shared<std::vector<uint8_t>>(2);
                    asio::async_read(socket_, asio::buffer(*ext),
                        [this, self, masked, ext, opcode](std::error_code ec, std::size_t) {
                            if (!ec) {
                                uint64_t len = ((*ext)[0] << 8) | (*ext)[1];
                                read_payload(masked, len, opcode);
                            } else {
                                g_matchmaker.leave(self);
                                close();
                            }
                        });
                } else if (payload_len == 127) {
                    auto ext = std::make_shared<std::vector<uint8_t>>(8);
                    asio::async_read(socket_, asio::buffer(*ext),
                        [this, self, masked, ext, opcode](std::error_code ec, std::size_t) {
                            if (!ec) {
                                uint64_t len = 0;
                                for (int i = 0; i < 8; ++i) len = (len << 8) | (*ext)[i];
                                read_payload(masked, len, opcode);
                            } else {
                                g_matchmaker.leave(self);
                                close();
                            }
                        });
                } else {
                    read_payload(masked, payload_len, opcode);
                }
            });
    }

    void read_payload(bool masked, uint64_t payload_len, uint8_t opcode) {
        if (is_closed_) return;
        auto self = shared_from_this();
        size_t extra = masked ? 4 : 0;
        auto buf = std::make_shared<std::vector<uint8_t>>(payload_len + extra);

        asio::async_read(socket_, asio::buffer(*buf),
            [this, self, buf, masked, payload_len, opcode](std::error_code ec, std::size_t) {
                if (ec) {
                    g_matchmaker.leave(self);
                    close();
                    return;
                }

                if (opcode == 0xA) {
                    read_frame();
                    return;
                }

                std::vector<uint8_t> mask(4, 0);
                size_t offset = 0;
                if (masked) {
                    std::copy(buf->begin(), buf->begin() + 4, mask.begin());
                    offset = 4;
                }
                std::string msg;
                msg.reserve(payload_len);
                for (size_t i = 0; i < payload_len; ++i) {
                    msg.push_back(static_cast<char>((*buf)[offset + i] ^ mask[i % 4]));
                }
                
                g_matchmaker.handle_message(self, msg);
                read_frame();
            });
    }
};