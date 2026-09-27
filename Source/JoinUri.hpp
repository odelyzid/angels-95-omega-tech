#pragma once

// Deep-link / direct-connect address parsing for the AETHERNET web portal
// (angels95.tribewarez.com), which emits links of the form:
//     angels95://join/<ip>:<port>
// The same parser handles bare "host:port" strings used by --join and the
// in-game /connect command. No engine dependencies, so it is unit-testable.

#include <string>
#include <cctype>

namespace JoinUri
{
    inline constexpr int kDefaultPort = 27015;

    inline std::string Trim(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    inline std::string Lower(const std::string& s)
    {
        std::string out = s;
        for (char& c : out) c = (char)std::tolower((unsigned char)c);
        return out;
    }

    inline bool ParsePort(const std::string& s, int& port)
    {
        if (s.empty() || s.size() > 5) return false;
        long v = 0;
        for (char c : s)
        {
            if (!std::isdigit((unsigned char)c)) return false;
            v = v * 10 + (c - '0');
        }
        if (v < 1 || v > 65535) return false;
        port = (int)v;
        return true;
    }

    inline bool IsHostChar(char c)
    {
        unsigned char u = (unsigned char)c;
        return std::isalnum(u) || c == '.' || c == '-' || c == '_' ||
               c == ':' || c == '[' || c == ']';
    }

    // Parse "host", "host:port", "[v6]", "[v6]:port" or bare "v6".
    // Port stays at defaultPort when the string carries none.
    inline bool ParseHostPort(const std::string& in, std::string& host, int& port,
                              int defaultPort = kDefaultPort)
    {
        std::string s = Trim(in);
        if (s.empty()) return false;

        if (s[0] == '[')
        {
            size_t close = s.find(']');
            if (close == std::string::npos) return false;
            std::string after = s.substr(close + 1);
            if (!after.empty())
            {
                if (after[0] != ':') return false;
                if (!ParsePort(after.substr(1), port)) return false;
            }
            host = s.substr(0, close + 1);
        }
        else
        {
            size_t colon = s.rfind(':');
            if (colon != std::string::npos && s.find(':') == colon)
            {
                if (!ParsePort(s.substr(colon + 1), port)) return false;
                s = s.substr(0, colon);
            }
            host = Trim(s);
        }

        if (host.empty() || host.size() > 253) return false;
        for (char c : host)
            if (!IsHostChar(c)) return false;
        if (port == 0) port = defaultPort;
        return true;
    }

    // Parse a full deep link: angels95://join/<host>[:<port>] (scheme and
    // "join" are matched case-insensitively; a trailing slash is tolerated
    // because Windows ShellExecute appends one to URI arguments).
    inline bool Parse(const std::string& uri, std::string& host, int& port,
                      int defaultPort = kDefaultPort)
    {
        const std::string s = Trim(uri);
        const std::string scheme = "angels95://";
        if (s.size() <= scheme.size()) return false;
        if (Lower(s.substr(0, scheme.size())) != scheme) return false;

        std::string rest = s.substr(scheme.size());
        std::string restLower = Lower(rest);
        if (restLower.rfind("join/", 0) == 0)
            rest = rest.substr(5);
        else if (restLower.rfind("join?", 0) == 0)
            return false;

        size_t cut = rest.find_first_of("?#");
        if (cut != std::string::npos) rest = rest.substr(0, cut);
        while (!rest.empty() && rest.back() == '/') rest.pop_back();

        return ParseHostPort(rest, host, port, defaultPort);
    }
}