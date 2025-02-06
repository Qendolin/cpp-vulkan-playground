#include "Logger.h"

#include <iostream>
#include <source_location>
#include <filesystem>

std::string Logger::shortFileName(std::string file_name) {
    std::replace(file_name.begin(), file_name.end(), '\\', '/');
    size_t start = file_name.find("/src/");
    if (start == std::string::npos)
        start = 0;
    else
        start += 1;
    return file_name.substr(start);
}

std::string_view Logger::shortFunctionName(std::string_view function_name) {
    size_t start = 0;
    size_t end = function_name.length();
    for (size_t i = start; i < end; i++) {
        if (function_name[i] == ' ')
            start = i + 1;
        if (function_name[i] == '(')
            end = i;
    }

    return function_name.substr(start, end - start);
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
    if (be_true == true) return;
    std::clog << "[CHK "
            << shortFileName(location.file_name()) << ':'
            << location.line() << ':'
            << shortFunctionName(location.function_name()) << "]: "
            << message << std::endl;
}

__attribute__((noinline)) void Logger::panic(std::string_view message, const cpptrace::stacktrace &trace) {
    // don't know why but trace.to_string fives a linker error on linux
    std::ostringstream oss;
    trace.print(oss, true);
    const std::string string = oss.str();
    std::string_view sv = string;
    if (sv.ends_with("\n")) sv.remove_suffix(1);
    throw std::runtime_error(std::format("PANIC: {}\n{}", message, sv));
}
