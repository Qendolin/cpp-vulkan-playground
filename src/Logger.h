#pragma once

#include <source_location>
#include <string>
#include <cpptrace/cpptrace.hpp>

class Logger {
private:
    static std::string shortFileName(std::string file_name);

    static std::string_view shortFunctionName(std::string_view function_name);

public:
    static void info(std::string_view message, std::source_location location = std::source_location::current());

    static void debug(std::string_view message, std::source_location location = std::source_location::current());

    static void warning(std::string_view message, std::source_location location = std::source_location::current());

    static void error(std::string_view message, std::source_location location = std::source_location::current());

    static void check(bool be_true, std::string_view message, std::source_location location = std::source_location::current());

    [[noreturn]] static void panic(std::string_view message, const cpptrace::stacktrace &trace = cpptrace::generate_trace());
};
