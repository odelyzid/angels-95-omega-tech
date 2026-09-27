#ifndef OMEGA_MASTER_HTTP_HPP
#define OMEGA_MASTER_HTTP_HPP

// Minimal blocking HTTP/1.1 client used for master list/status queries and the
// HTTP heartbeat uplink. Plain HTTP only (no TLS) to avoid new dependencies.
// Cross-platform (WinSock2 / POSIX). No raylib or engine dependencies.

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define MHTTP_SOCK(fd) ((SOCKET)(intptr_t)(fd))
    #define MHTTP_CLOSE(fd) closesocket(MHTTP_SOCK(fd))
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
    #define MHTTP_SOCK(fd) (fd)
    #define MHTTP_CLOSE(fd) ::close(fd)
    #define INVALID_SOCKET (-1)
#endif

namespace master {

struct ParsedUrl {
    std::string host;
    uint16_t    port = 80;
    std::string path = "/";
};

inline bool ParseHttpUrl(const std::string& url, ParsedUrl& out) {
    std::string s = url;
    if (s.compare(0, 7, "http://") == 0) {
        s = s.substr(7);
    } else if (s.compare(0, 8, "https://") == 0) {
        return false; // TLS unsupported
    }
    size_t slash = s.find('/');
    std::string hostport = (slash == std::string::npos) ? s : s.substr(0, slash);
    out.path = (slash == std::string::npos) ? "/" : s.substr(slash);
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostport.substr(0, colon);
        long p = strtol(hostport.substr(colon + 1).c_str(), nullptr, 10);
        if (p <= 0 || p > 65535) return false;
        out.port = (uint16_t)p;
    } else {
        out.host = hostport;
        out.port = 80;
    }
    return !out.host.empty();
}

#ifdef _WIN32
inline void mhttp_wsa_init() {
    static bool done = false;
    if (!done) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        done = true;
    }
}
#else
inline void mhttp_wsa_init() {}
#endif

inline long long mhttp_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline int mhttp_connect(const ParsedUrl& url, int timeout_ms) {
    mhttp_wsa_init();
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)url.port);
    if (getaddrinfo(url.host.c_str(), portstr, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    long long deadline = mhttp_now_ms() + timeout_ms;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        int s = (int)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0) continue;
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(MHTTP_SOCK(s), FIONBIO, &nb);
#else
        int flags = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
        int rc = connect(MHTTP_SOCK(s), ai->ai_addr, (socklen_t)ai->ai_addrlen);
        if (rc == 0) { fd = s; break; }
        bool in_progress =
#ifdef _WIN32
            WSAGetLastError() == WSAEWOULDBLOCK;
#else
            errno == EINPROGRESS || errno == EWOULDBLOCK;
#endif
        if (!in_progress) { MHTTP_CLOSE(s); continue; }
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(MHTTP_SOCK(s), &wf);
        long long left = deadline - mhttp_now_ms();
        if (left <= 0) { MHTTP_CLOSE(s); break; }
        struct timeval tv;
        tv.tv_sec = (long)(left / 1000);
        tv.tv_usec = (long)((left % 1000) * 1000);
        int sel = select((int)(s + 1), nullptr, &wf, nullptr, &tv);
        if (sel > 0) {
            int err = 0;
            socklen_t elen = sizeof(err);
            getsockopt(MHTTP_SOCK(s), SOL_SOCKET, SO_ERROR, (char*)&err, &elen);
            if (err == 0) { fd = s; break; }
        }
        MHTTP_CLOSE(s);
    }
    freeaddrinfo(res);
    return fd;
}

inline bool mhttp_send_all(int fd, const std::string& data, long long deadline) {
    size_t off = 0;
    while (off < data.size()) {
        long long left = deadline - mhttp_now_ms();
        if (left <= 0) return false;
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(MHTTP_SOCK(fd), &wf);
        struct timeval tv;
        tv.tv_sec = (long)(left / 1000);
        tv.tv_usec = (long)((left % 1000) * 1000);
        if (select(fd + 1, nullptr, &wf, nullptr, &tv) <= 0) return false;
        int n = (int)send(MHTTP_SOCK(fd), data.c_str() + off, (int)(data.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

inline bool mhttp_recv_all(int fd, std::string& out, long long deadline, size_t max_bytes) {
    char buf[4096];
    while (out.size() < max_bytes) {
        long long left = deadline - mhttp_now_ms();
        if (left <= 0) break;
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(MHTTP_SOCK(fd), &rf);
        struct timeval tv;
        tv.tv_sec = (long)(left / 1000);
        tv.tv_usec = (long)((left % 1000) * 1000);
        int sel = select(fd + 1, &rf, nullptr, nullptr, &tv);
        if (sel <= 0) break;
        int n = (int)recv(MHTTP_SOCK(fd), buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, (size_t)n);
    }
    return !out.empty();
}

// Returns true on a complete HTTP response (even 4xx/5xx). out_status gets the
// status code, out_body the response body.
inline bool HttpRequest(const std::string& method, const std::string& url,
                        const std::string& body, const std::string& content_type,
                        int timeout_ms, std::string& out_body, int* out_status = nullptr) {
    ParsedUrl u;
    if (!ParseHttpUrl(url, u)) return false;
    int fd = mhttp_connect(u, timeout_ms);
    if (fd < 0) return false;

    std::string req = method + " " + u.path + " HTTP/1.1\r\n";
    req += "Host: " + u.host + ":" + std::to_string(u.port) + "\r\n";
    req += "User-Agent: Angels95-Master/1.0\r\n";
    req += "Accept: application/json\r\n";
    req += "Connection: close\r\n";
    if (!body.empty()) {
        req += "Content-Type: " + (content_type.empty() ? std::string("text/plain") : content_type) + "\r\n";
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    req += "\r\n";
    req += body;

    long long deadline = mhttp_now_ms() + timeout_ms;
    std::string raw;
    bool ok = mhttp_send_all(fd, req, deadline) &&
              mhttp_recv_all(fd, raw, deadline, 512 * 1024);
    MHTTP_CLOSE(fd);
    if (!ok) return false;

    size_t hdr_end = raw.find("\r\n\r\n");
    size_t skip = 4;
    if (hdr_end == std::string::npos) {
        hdr_end = raw.find("\n\n");
        skip = 2;
    }
    if (hdr_end == std::string::npos) return false;
    out_body = raw.substr(hdr_end + skip);

    // Grow the body if Content-Length says there is more (first recv may have
    // been partial, though Connection: close plus the read loop usually has it).
    if (out_status) {
        const char* sp = strchr(raw.c_str(), ' ');
        if (sp) *out_status = atoi(sp + 1);
    }
    return true;
}

} // namespace master

#endif // OMEGA_MASTER_HTTP_HPP