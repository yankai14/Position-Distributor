#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

#include "utils.h"

void log(const std::string& message, bool is_error) {
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> lock(log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %X ") << message;

    if (is_error) std::cerr << ss.str() << std::endl;
    else std::cout << ss.str() << std::endl;
}

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

Trade parse_trade(const std::string& input) {
    Trade trade;

    std::vector<std::string> parts = split(input, ' ');

    if (parts.size() != 2) {
        throw std::invalid_argument("Invalid input format. Expected: SYMBOL POSITION");
    }

    trade.set_symbol(parts[0]);

    try {
        double position = stod(parts[1]);
        trade.set_position(position);
    } catch (const std::exception& e) {
        throw std::invalid_argument("Invalid position value: " + std::string(e.what()));
    }

    return trade;
}