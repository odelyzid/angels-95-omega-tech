// JoinUri deep-link parsing tests - standalone, no raylib dependency
#include "../Source/JoinUri.hpp"
#include <cstdio>
#include <string>

int test_count = 0, pass_count = 0;

static void expect(bool ok, const char* name, const std::string& detail = "") {
    test_count++;
    printf("  TEST %s... ", name);
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL%s%s\n", detail.empty() ? "" : ": ", detail.c_str());
}

static void test_full_uri() {
    std::string host; int port = 0;
    bool ok = JoinUri::Parse("angels95://join/159.195.20.100:27015", host, port, 27015)
        && host == "159.195.20.100" && port == 27015;
    expect(ok, "full URI", host + ":" + std::to_string(port));
}

static void test_full_uri_custom_port() {
    std::string host; int port = 0;
    bool ok = JoinUri::Parse("angels95://join/10.0.0.7:28001", host, port, 27015)
        && host == "10.0.0.7" && port == 28001;
    expect(ok, "full URI custom port", host + ":" + std::to_string(port));
}

static void test_uri_default_port() {
    std::string host; int port = 0;
    bool ok = JoinUri::Parse("angels95://join/game.example.com", host, port, 27015)
        && host == "game.example.com" && port == 27015;
    expect(ok, "URI without port defaults to 27015", host + ":" + std::to_string(port));
}

static void test_uri_trailing_slash() {
    std::string host; int port = 0;
    bool ok = JoinUri::Parse("angels95://join/1.2.3.4:27015/", host, port, 27015)
        && host == "1.2.3.4" && port == 27015;
    expect(ok, "URI trailing slash (ShellExecute)", host + ":" + std::to_string(port));
}

static void test_uri_case_insensitive() {
    std::string host; int port = 0;
    bool ok = JoinUri::Parse("ANGELS95://JOIN/1.2.3.4:27016", host, port, 27015)
        && host == "1.2.3.4" && port == 27016;
    expect(ok, "scheme/join case-insensitive", host + ":" + std::to_string(port));
}

static void test_uri_without_join_segment() {
    std::string host; int port = 0;
    bool ok = JoinUri::Parse("angels95://1.2.3.4:27015", host, port, 27015)
        && host == "1.2.3.4" && port == 27015;
    expect(ok, "URI without join segment", host + ":" + std::to_string(port));
}

static void test_uri_rejects_other_scheme() {
    std::string host; int port = 0;
    expect(!JoinUri::Parse("http://join/1.2.3.4:27015", host, port, 27015),
           "rejects non-angels95 scheme");
}

static void test_uri_rejects_missing_host() {
    std::string host; int port = 0;
    expect(!JoinUri::Parse("angels95://join/", host, port, 27015),
           "rejects missing host");
}

static void test_uri_rejects_bad_port() {
    std::string host; int port = 0;
    bool bad0 = !JoinUri::Parse("angels95://join/1.2.3.4:0", host, port, 27015);
    int port2 = 0;
    bool bad1 = !JoinUri::Parse("angels95://join/1.2.3.4:70000", host, port2, 27015);
    int port3 = 0;
    bool bad2 = !JoinUri::Parse("angels95://join/1.2.3.4:abc", host, port3, 27015);
    expect(bad0 && bad1 && bad2, "rejects port 0/70000/non-numeric");
}

static void test_host_port_plain() {
    std::string host; int port = 0;
    bool ok = JoinUri::ParseHostPort("192.168.1.50:27016", host, port, 27015)
        && host == "192.168.1.50" && port == 27016;
    expect(ok, "host:port", host + ":" + std::to_string(port));
}

static void test_host_port_default() {
    std::string host; int port = 0;
    bool ok = JoinUri::ParseHostPort("  localhost  ", host, port, 27015)
        && host == "localhost" && port == 27015;
    expect(ok, "host only + whitespace trim", host + ":" + std::to_string(port));
}

static void test_ipv6_brackets() {
    std::string host; int port = 0;
    bool ok = JoinUri::ParseHostPort("[::1]:27017", host, port, 27015)
        && host == "[::1]" && port == 27017;
    expect(ok, "bracketed IPv6 + port", host + ":" + std::to_string(port));
}

static void test_ipv6_bare() {
    std::string host; int port = 0;
    bool ok = JoinUri::ParseHostPort("::1", host, port, 27015)
        && host == "::1" && port == 27015;
    expect(ok, "bare IPv6 defaults port", host + ":" + std::to_string(port));
}

static void test_rejects_garbage() {
    std::string host; int port = 0;
    bool a = !JoinUri::ParseHostPort("", host, port, 27015);
    int p2 = 0;
    bool b = !JoinUri::ParseHostPort("bad host", host, p2, 27015);
    int p3 = 0;
    bool c = !JoinUri::ParseHostPort("host:", host, p3, 27015);
    expect(a && b && c, "rejects empty/spaces/trailing colon");
}

static void test_cli_style_target() {
    std::string host; int port = 0;
    bool ok = JoinUri::ParseHostPort("159.195.20.100:27015", host, port, 27015)
        && host == "159.195.20.100" && port == 27015;
    expect(ok, "--join style target", host + ":" + std::to_string(port));
}

int main() {
    printf("JoinUri tests:\n");
    test_full_uri();
    test_full_uri_custom_port();
    test_uri_default_port();
    test_uri_trailing_slash();
    test_uri_case_insensitive();
    test_uri_without_join_segment();
    test_uri_rejects_other_scheme();
    test_uri_rejects_missing_host();
    test_uri_rejects_bad_port();
    test_host_port_plain();
    test_host_port_default();
    test_ipv6_brackets();
    test_ipv6_bare();
    test_rejects_garbage();
    test_cli_style_target();

    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}