#include "Logger.h"

#include <iostream>
#include <source_location>
#include <filesystem>

std::string Logger::shortFileName(const std::string_view file_name) {
    std::string path = std::string(file_name);
    std::replace(path.begin(), path.end(), '\\', '/');
    size_t start = path.find("/src/");
    if (start == std::string::npos)
        start = 0;
    else
        start += 1;
    return path.substr(start);
}

std::string Logger::shortFunctionName(const std::string_view function_name) {
    size_t start = 0;
    size_t end = function_name.length();
    for (size_t i = start; i < end; i++) {
        if (function_name[i] == ' ')
            start = i + 1;
        if (function_name[i] == '(')
            end = i;
    }

    return std::string(function_name.substr(start, end - start));
}

void Logger::info(std::string_view message, std::source_location location) {
    std::clog << "[LOG "
            << shortFileName(location.file_name()) << ':'
            << location.line() << "]: "
            << message << std::endl;
}

void Logger::debug(std::string_view message, std::source_location location) {
    std::string func_name = location.function_name();
    std::clog << "[DBG "
            << shortFileName(location.file_name()) << ':'
            << location.line() << ':'
            << shortFunctionName(location.function_name()) << "]: "
            << message << std::endl;
}

void Logger::warning(std::string_view message, std::source_location location) {
    std::clog << "[\u001B[33mWRN\u001B[0m "
            << shortFileName(location.file_name()) << ':'
            << location.line() << "]: "
            << message << std::endl;
}

void Logger::error(std::string_view message, std::source_location location) {
    std::clog << "[ERR "
            << shortFileName(location.file_name()) << ':'
            << location.line() << ':'
            << shortFunctionName(location.function_name()) << "]: "
            << message << std::endl;
}

void Logger::check(bool be_true, std::string_view message, std::source_location location) {
    if(be_true == true) return;
    std::clog << "[CHK "
            << shortFileName(location.file_name()) << ':'
            << location.line() << ':'
            << shortFunctionName(location.function_name()) << "]: "
            << message << std::endl;
}

void Logger::panic(std::string_view message, std::stacktrace trace) {
    throw std::runtime_error("PANIC: " + std::string(message) + "\n" + std::to_string(trace));
}
