#ifndef MYSERVER_UTILS_H
#define MYSERVER_UTILS_H

#include <string>
#include <chrono>
#include <thread>
#include <position.pb.h>

void log(const std::string& message, bool is_error = false);

std::vector<std::string> split(const std::string& s, char delimiter);

Trade parse_trade(const std::string& input);

#endif //MYSERVER_UTILS_H