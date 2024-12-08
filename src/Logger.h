#pragma once

#include <source_location>
#include <stacktrace>
#include <string>

class Logger {
private:
    static std::string shortFileName(std::string_view file_name);

    static std::string shortFunctionName(std::string_view function_name);

public:
    static void info(std::string_view message, std::source_location location = std::source_location::current());

    static void debug(std::string_view message, std::source_location location = std::source_location::current());

    static void warning(std::string_view message, std::source_location location = std::source_location::current());

    static void error(std::string_view message, std::source_location location = std::source_location::current());

    [[noreturn]] static void panic(std::string_view message, std::stacktrace trace = std::stacktrace::current());
};
