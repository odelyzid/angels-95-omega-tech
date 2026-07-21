#include "Log.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>

std::vector<LogLine> Log::s_buffer;
std::mutex Log::s_mutex;
static FILE* g_logFile = nullptr;

// Open log file (truncate on first call, append thereafter)
static void EnsureLogFile() {
    if (g_logFile) return;
    g_logFile = fopen("System/angels95.log", "w");
    if (!g_logFile) {
        // Fallback: try current directory
        g_logFile = fopen("angels95.log", "w");
    }
    if (g_logFile) {
        time_t now = time(nullptr);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(g_logFile, "=== Angels95 Log started %s ===\n", ts);
        fflush(g_logFile);
    }
}

void Log::write(LogLevel level, const char* file, int line, const char* fmt, ...) {
    char buf[LOG_WINDOW_MAX_LINE_LENGTH];
    char prefix[32];
    const char* level_str;
    switch (level) {
        case LogLevel::TRACE: level_str = "TRACE"; break;
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
        case LogLevel::INFO:  level_str = "INFO";  break;
        case LogLevel::WARN:  level_str = "WARN";  break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
        case LogLevel::FATAL: level_str = "FATAL"; break;
        default:              level_str = "?";     break;
    }

    // Format prefix: [LEVEL] file:line
    int plen = snprintf(prefix, sizeof(prefix), "[%s] %s:%d: ", level_str, file, line);

    // Copy prefix into buf
    std::strncpy(buf, prefix, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Format user message after prefix
    va_list args;
    va_start(args, fmt);
    int mlen = vsnprintf(buf + plen, sizeof(buf) - plen - 1, fmt, args);
    va_end(args);
    if (mlen < 0) return;
    size_t total_len = std::min(static_cast<size_t>(plen + mlen), sizeof(buf) - 1);
    buf[total_len] = '\0';

    // Print to stderr
    fprintf(stderr, "%s\n", buf);

    // Write to file
    EnsureLogFile();
    if (g_logFile) {
        time_t now = time(nullptr);
        char ts[64];
        strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
        fprintf(g_logFile, "[%s] %s\n", ts, buf);
        fflush(g_logFile);
    }

    // Store in ring buffer
    const std::lock_guard<std::mutex> lock(s_mutex);
    LogLine ll;
    ll.level = level;
    size_t cp = std::min(total_len, sizeof(ll.text) - 1);
    memcpy(ll.text, buf, cp);
    ll.text[cp] = '\0';

    s_buffer.push_back(ll);
    if (s_buffer.size() > LOG_WINDOW_MAX_LINES)
        s_buffer.erase(s_buffer.begin());
}

void Log::raw(LogLevel level, const char* msg) {
    // Write to file
    EnsureLogFile();
    if (g_logFile) {
        time_t now = time(nullptr);
        char ts[64];
        strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
        fprintf(g_logFile, "[%s] [RAW] %s\n", ts, msg);
        fflush(g_logFile);
    }

    const std::lock_guard<std::mutex> lock(s_mutex);
    LogLine ll;
    ll.level = level;
    size_t cp = std::min(strlen(msg), sizeof(ll.text) - 1);
    memcpy(ll.text, msg, cp);
    ll.text[cp] = '\0';
    s_buffer.push_back(ll);
    if (s_buffer.size() > LOG_WINDOW_MAX_LINES)
        s_buffer.erase(s_buffer.begin());
}

const std::vector<LogLine>& Log::get_buffer() {
    return s_buffer;
}

void Log::clear() {
    const std::lock_guard<std::mutex> lock(s_mutex);
    s_buffer.clear();
}