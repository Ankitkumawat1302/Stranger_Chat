#include "matchmaker.hpp"
#include "session.hpp"
#include "protocol.hpp"
#include <iostream>

Matchmaker g_matchmaker;

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
    for (auto it = waiting_queue_.begin(); it != waiting_queue_.end(); ++it) {
        if (it->get() == session.get()) {
            waiting_queue_.erase(it);
            std::cout << "[Matchmaker] Removed client from waiting queue." << std::endl;
            return;
        }
    }

    auto it = pairs_.find(session.get());
    if (it != pairs_.end()) {
        auto partner = it->second;
        pairs_.erase(it);
        pairs_.erase(partner.get());
        partner->send_text("{\"type\":\"stranger_disconnected\"}");
        std::cout << "[Matchmaker] Stranger disconnected, partner notified." << std::endl;
    }
}