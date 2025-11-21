#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <iostream>

// Three logging levels for our binary
enum class LogLevel {
    SILENT, // --clear
    NORMAL, // Default
    DEBUG   // --debug
};

// Global variable for the current logging level.
// "extern" means it will be defined in one of the .cpp files.
extern LogLevel g_log_level;

// Function for DEBUG output
inline void log_debug(const std::string& message) {
    if (g_log_level == LogLevel::DEBUG) {
        std::cerr << "[DEBUG] " << message << std::endl;
    }
}

// Function for NORMAL and DEBUG output
inline void log_normal(const std::string& message) {
    if (g_log_level >= LogLevel::NORMAL) {
        std::cout << message << std::endl;
    }
}

// Function for error output (always visible, except in SILENT)
inline void log_error(const std::string& message) {
    if (g_log_level >= LogLevel::NORMAL) {
        std::cerr << "ERROR: " << message << std::endl;
    }
}

#endif // LOGGER_H
