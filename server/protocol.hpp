#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>

inline std::vector<uint8_t> make_ws_frame(const std::string& payload, uint8_t opcode = 0x81) {
    std::vector<uint8_t> frame;
    frame.push_back(opcode); // 0x81 = Text, 0x89 = Ping
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

inline std::string escape_json(const std::string& input) {
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

inline std::string extract_json_val(const std::string& json, const std::string& key) {
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