// Network packet serialization test — standalone, no raylib dependency
#include "../Source/Network/Network.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

int test_count = 0, pass_count = 0;

static void test_magic_constant() {
    test_count++;
    printf("  TEST MAGIC constant... ");
    if (net::MAGIC == 0x4F5A574F) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: wrong MAGIC value\n");
}

static void test_message_header_size() {
    test_count++;
    printf("  TEST NetworkMessage size... ");
    size_t expected = sizeof(uint32_t) * 5 + net::MAX_MESSAGE_SIZE;
    if (sizeof(net::NetworkMessage) == expected) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: expected %zu, got %zu\n", expected, sizeof(net::NetworkMessage));
}

static void test_message_type_string() {
    test_count++;
    printf("  TEST message_type_string known types... ");
    bool ok = true;
    if (std::strcmp(net::message_type_string(net::MessageType::PING), "PING") != 0) ok = false;
    if (std::strcmp(net::message_type_string(net::MessageType::PONG), "PONG") != 0) ok = false;
    if (std::strcmp(net::message_type_string(net::MessageType::PLAYER_JOIN), "PLAYER_JOIN") != 0) ok = false;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: wrong string for known type\n");
}

static void test_message_type_string_unknown() {
    test_count++;
    printf("  TEST message_type_string unknown type... ");
    auto u = static_cast<net::MessageType>(999);
    if (std::strcmp(net::message_type_string(u), "UNKNOWN") == 0) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: should return UNKNOWN\n");
}

static void test_player_update_data_size() {
    test_count++;
    printf("  TEST PlayerUpdateData size... ");
    if (sizeof(net::PlayerUpdateData) <= net::MAX_MESSAGE_SIZE) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: exceeds MAX_MESSAGE_SIZE\n");
}

static void test_is_valid_ip() {
    test_count++;
    printf("  TEST is_valid_ip valid... ");
    if (net::is_valid_ip("192.168.1.1")) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: should be valid\n");
}

static void test_is_valid_ip_invalid() {
    test_count++;
    printf("  TEST is_valid_ip invalid... ");
    if (!net::is_valid_ip("not.an.ip")) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: should be invalid\n");
}

static void test_find_free_port() {
    test_count++;
    printf("  TEST find_free_port... ");
    uint16_t port = net::find_free_port();
    if (port > 0) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: should return a non-zero port\n");
}

int main() {
    printf("Network packet tests:\n");
    test_magic_constant();
    test_message_header_size();
    test_message_type_string();
    test_message_type_string_unknown();
    test_player_update_data_size();
    test_is_valid_ip();
    test_is_valid_ip_invalid();
    test_find_free_port();

    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
