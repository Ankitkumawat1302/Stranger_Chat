#include <iostream>
#include <memory>
#include <asio.hpp>
#include "session.hpp"

using asio::ip::tcp;

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