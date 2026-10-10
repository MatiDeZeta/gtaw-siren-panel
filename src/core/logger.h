#pragma once

#include <string>

// A single log file next to the .asi. The plugin talks to a debugger endpoint and injects a
// script; a user who wants to know exactly what it did needs to be able to read that back
// without a debugger, so every connection attempt, install and failure is written here.
namespace logging {

void init();
void write(const char* level, const char* fmt, ...);
void shutdown();

}  // namespace logging

#define LOG_INFO(...) logging::write("INFO",  __VA_ARGS__)
#define LOG_WARN(...) logging::write("WARN",  __VA_ARGS__)
#define LOG_ERR(...)  logging::write("ERROR", __VA_ARGS__)
