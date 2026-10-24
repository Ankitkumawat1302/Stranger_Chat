#pragma once
#include <memory>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <string>

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

extern Matchmaker g_matchmaker;