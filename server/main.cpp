#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <asio.hpp>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

using asio::ip::tcp;

std::string base64_encode(const unsigned char* buffer, size_t length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_write(bio, buffer, static_cast<int>(length));
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return result;
}

std::string compute_ws_accept(const std::string& key) {
    std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(magic.c_str()), magic.length(), hash);
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

std::vector<uint8_t> make_ws_frame(const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + Text frame
    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

class Session : public std::enable_shared_from_this<Session> {
    tcp::socket socket_;
    asio::streambuf buffer_;
public:
    Session(tcp::socket socket) : socket_(std::move(socket)) {}

    void start() {
        auto self = shared_from_this();
        asio::async_read_until(socket_, buffer_, "\r\n\r\n",
            [this, self](std::error_code ec, std::size_t bytes_transferred) {
                if (!ec) {
                    std::istream request_stream(&buffer_);
                    std::string line;
                    std::string sec_key;
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
                                    std::cout << "[Server] Client handshake complete!" << std::endl;
                                    read_frame();
                                }
                            });
                    }
                }
            });
    }

    void read_frame() {
        auto self = shared_from_this();
        auto header = std::make_shared<std::vector<uint8_t>>(2);
        asio::async_read(socket_, asio::buffer(*header),
            [this, self, header](std::error_code ec, std::size_t) {
                if (ec) {
                    std::cout << "[Server] Client disconnected." << std::endl;
                    return;
                }
                bool masked = ((*header)[1] & 0x80) != 0;
                uint64_t payload_len = (*header)[1] & 0x7F;

                if (payload_len == 126) {
                    auto ext = std::make_shared<std::vector<uint8_t>>(2);
                    asio::async_read(socket_, asio::buffer(*ext),
                        [this, self, masked, ext](std::error_code ec, std::size_t) {
                            if (!ec) {
                                uint64_t len = ((*ext)[0] << 8) | (*ext)[1];
                                read_payload(masked, len);
                            }
                        });
                } else if (payload_len == 127) {
                    auto ext = std::make_shared<std::vector<uint8_t>>(8);
                    asio::async_read(socket_, asio::buffer(*ext),
                        [this, self, masked, ext](std::error_code ec, std::size_t) {
                            if (!ec) {
                                uint64_t len = 0;
                                for (int i = 0; i < 8; ++i) len = (len << 8) | (*ext)[i];
                                read_payload(masked, len);
                            }
                        });
                } else {
                    read_payload(masked, payload_len);
                }
            });
    }

    void read_payload(bool masked, uint64_t payload_len) {
        auto self = shared_from_this();
        size_t extra = masked ? 4 : 0;
        auto buf = std::make_shared<std::vector<uint8_t>>(payload_len + extra);

        asio::async_read(socket_, asio::buffer(*buf),
            [this, self, buf, masked, payload_len](std::error_code ec, std::size_t) {
                if (ec) return;
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
                std::cout << "[Server] Message received: " << msg << std::endl;

                // Send echo back
                std::string echo = "Echo: " + msg;
                auto frame = std::make_shared<std::vector<uint8_t>>(make_ws_frame(echo));
                asio::async_write(socket_, asio::buffer(*frame),
                    [this, self, frame](std::error_code ec, std::size_t) {
                        if (!ec) read_frame();
                    });
            });
    }
};

class Server {
    asio::io_context io_context_;
    tcp::acceptor acceptor_;
public:
    Server(short port)
        : acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

    void run() {
        io_context_.run();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::cout << "[Server] Client connected!" << std::endl;
                    std::make_shared<Session>(std::move(socket))->start();
                }
                do_accept();
            });
    }
};

int main() {
    try {
        std::cout << ">>> Stranger Chat Server running on ws://localhost:8080/chat" << std::endl;
        Server server(8080);
        server.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}