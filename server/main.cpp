#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <mutex>
#include <deque>
#include <unordered_map>
#include <atomic>
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

std::string escape_json(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b";  break;
            case '\f': ss << "\\f";  break;
            case '\n': ss << "\\n";  break;
            case '\r': ss << "\\r";  break;
            case '\t': ss << "\\t";  break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    // skip raw control chars
                } else {
                    ss << c;
                }
        }
    }
    return ss.str();
}

std::string extract_json_val(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    size_t start = json.find('"', pos);
    if (start == std::string::npos) return "";
    
    std::string result;
    bool escape = false;
    for (size_t i = start + 1; i < json.size(); ++i) {
        char c = json[i];
        if (escape) {
            result += c;
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

class Session;

class Matchmaker {
    std::mutex mtx_;
    std::deque<std::shared_ptr<Session>> waiting_queue_;
    std::unordered_map<Session*, std::shared_ptr<Session>> pairs_;

public:
    void join_queue(std::shared_ptr<Session> session);
    void handle_message(std::shared_ptr<Session> session, const std::string& raw_msg);
    void leave(std::shared_ptr<Session> session);
};

Matchmaker g_matchmaker;

class Session : public std::enable_shared_from_this<Session> {
    tcp::socket socket_;
    asio::streambuf buffer_;
    std::atomic<bool> is_closed_{false};

public:
    Session(tcp::socket socket) : socket_(std::move(socket)) {}

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
                                }
                            });
                    }
                }
            });
    }

    void send_text(const std::string& text) {
        if (is_closed_) return;
        auto self = shared_from_this();
        auto frame = std::make_shared<std::vector<uint8_t>>(make_ws_frame(text));
        asio::post(socket_.get_executor(), [this, self, frame]() {
            if (is_closed_) return;
            asio::async_write(socket_, asio::buffer(*frame),
                [](std::error_code, std::size_t) {});
        });
    }

    void close() {
        if (is_closed_.exchange(true)) return;
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
                bool masked = ((*header)[1] & 0x80) != 0;
                uint64_t payload_len = (*header)[1] & 0x7F;

                if (payload_len == 126) {
                    auto ext = std::make_shared<std::vector<uint8_t>>(2);
                    asio::async_read(socket_, asio::buffer(*ext),
                        [this, self, masked, ext](std::error_code ec, std::size_t) {
                            if (!ec) {
                                uint64_t len = ((*ext)[0] << 8) | (*ext)[1];
                                read_payload(masked, len);
                            } else {
                                g_matchmaker.leave(self);
                                close();
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
                            } else {
                                g_matchmaker.leave(self);
                                close();
                            }
                        });
                } else {
                    read_payload(masked, payload_len);
                }
            });
    }

    void read_payload(bool masked, uint64_t payload_len) {
        if (is_closed_) return;
        auto self = shared_from_this();
        size_t extra = masked ? 4 : 0;
        auto buf = std::make_shared<std::vector<uint8_t>>(payload_len + extra);

        asio::async_read(socket_, asio::buffer(*buf),
            [this, self, buf, masked, payload_len](std::error_code ec, std::size_t) {
                if (ec) {
                    g_matchmaker.leave(self);
                    close();
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

void Matchmaker::join_queue(std::shared_ptr<Session> session) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!waiting_queue_.empty()) {
        auto partner = waiting_queue_.front();
        waiting_queue_.pop_front();

        pairs_[session.get()] = partner;
        pairs_[partner.get()] = session;

        std::cout << "[Matchmaker] Strangers paired!" << std::endl;
        session->send_text("{\"type\":\"paired\"}");
        partner->send_text("{\"type\":\"paired\"}");
    } else {
        waiting_queue_.push_back(session);
        std::cout << "[Matchmaker] Stranger placed in queue." << std::endl;
        session->send_text("{\"type\":\"waiting\"}");
    }
}

void Matchmaker::handle_message(std::shared_ptr<Session> session, const std::string& raw_msg) {
    std::string type = extract_json_val(raw_msg, "type");

    if (type == "chat") {
        std::string text = extract_json_val(raw_msg, "message");
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = pairs_.find(session.get());
        if (it != pairs_.end()) {
            std::string forward_json = "{\"type\":\"chat\",\"message\":\"" + escape_json(text) + "\"}";
            it->second->send_text(forward_json);
        }
    } else if (type == "typing") {
        std::string is_typing = extract_json_val(raw_msg, "isTyping");
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = pairs_.find(session.get());
        if (it != pairs_.end()) {
            std::string forward_json = "{\"type\":\"typing\",\"isTyping\":\"" + is_typing + "\"}";
            it->second->send_text(forward_json);
        }
    } else if (type == "next") {
        leave(session);
        join_queue(session);
    }
}

void Matchmaker::leave(std::shared_ptr<Session> session) {
    std::lock_guard<std::mutex> lock(mtx_);
    // Remove from waiting queue if pending
    for (auto it = waiting_queue_.begin(); it != waiting_queue_.end(); ++it) {
        if (it->get() == session.get()) {
            waiting_queue_.erase(it);
            std::cout << "[Matchmaker] Removed client from waiting queue." << std::endl;
            return;
        }
    }

    // Remove from paired sessions
    auto it = pairs_.find(session.get());
    if (it != pairs_.end()) {
        auto partner = it->second;
        pairs_.erase(it);
        pairs_.erase(partner.get());
        partner->send_text("{\"type\":\"stranger_disconnected\"}");
        std::cout << "[Matchmaker] Paired stranger left. Notified remaining partner and cleaned session map." << std::endl;
    }
}

class Server {
    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    tcp::acceptor acceptor_;
public:
    Server(short port)
        : work_guard_(asio::make_work_guard(io_context_)),
          acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) {
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