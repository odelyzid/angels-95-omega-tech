// Network packet serialization test — standalone, no raylib dependency
#include "../Source/Network/Network.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>

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

// Stance (crouch/sprint) must survive the wire copy used for relay.
static void test_player_update_stance() {
    test_count++;
    printf("  TEST PlayerUpdateData stance... ");
    net::PlayerUpdateData pud{};
    pud.stance = net::STANCE_CROUCH;
    net::PlayerUpdateData copy{};
    std::memcpy(&copy, &pud, sizeof(pud));
    bool ok = (copy.stance == net::STANCE_CROUCH) &&
              (net::STANCE_STAND == 0) &&
              (net::STANCE_SPRINT == 2);
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: stance not preserved\n");
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

// S4 regression: the client must stay in the connecting state until the server
// confirms auth (first non-challenge message), and a re-auth must be re-acked
// without duplicating the player server-side.
static int test_auth_deferred_confirm() {
    test_count++;
    printf("  TEST auth deferred confirm (S4)... ");

    uint16_t port = net::find_free_port();
    int join_count = 0, connect_count = 0;

    net::NetworkServer server;
    if (!server.init(port, 4)) { printf("FAIL: server.init\n"); return 1; }

    net::ServerCallbacks scbs;
    scbs.on_player_join = [&](net::NetworkPlayer& p) {
        join_count++;
        net::NetworkMessage ping;
        ping.magic = net::MAGIC;
        ping.type = static_cast<uint32_t>(net::MessageType::PING);
        ping.size = 0;
        ping.sequence = 0;
        ping.timestamp = 0;
        server.send_message(p, ping); // doubles as the auth confirm packet
    };
    scbs.on_player_leave = [](net::NetworkPlayer&) {};
    scbs.on_message_received = [](const net::NetworkMessage&, const net::NetworkPlayer&) {};
    server.set_callbacks(std::move(scbs));

    if (!server.start()) { printf("FAIL: server.start\n"); return 1; }

    net::NetworkClient client;
    net::ClientCallbacks ccbs;
    ccbs.on_connected = [&]() { connect_count++; };
    ccbs.on_disconnected = [] {};
    ccbs.on_message_received = [](const net::NetworkMessage&) {};
    client.set_callbacks(std::move(ccbs));

    if (!client.connect("127.0.0.1", port)) { printf("FAIL: client.connect\n"); return 1; }

    // A client must NOT be connected merely because it sent CLIENT_AUTH.
    if (client.is_connected()) {
        printf("FAIL: connected before server confirm\n");
        return 1;
    }

    // Pump both ends (non-blocking UDP on loopback converges fast).
    for (int i = 0; i < 2000 && !client.is_connected(); ++i) {
        server.update();
        client.update();
    }

    if (!client.is_connected()) { printf("FAIL: never confirmed by server\n"); return 1; }
    if (connect_count != 1) { printf("FAIL: on_connected fired %d times\n", connect_count); return 1; }
    if (server.player_count() != 1) { printf("FAIL: server player_count=%u\n", server.player_count()); return 1; }

    // Re-auth from a connected client: server must re-play the join payload
    // for ack recovery without duplicating the player.
    int joins_before = join_count;
    net::NetworkMessage auth;
    auth.magic = net::MAGIC;
    auth.type = static_cast<uint32_t>(net::MessageType::CLIENT_AUTH);
    auth.size = 0;
    auth.sequence = 0;
    auth.timestamp = 0;
    if (!client.send_message(auth)) { printf("FAIL: send re-auth\n"); return 1; }
    server.update();
    if (join_count != joins_before + 1) { printf("FAIL: re-auth not re-acked\n"); return 1; }
    if (server.player_count() != 1) { printf("FAIL: re-auth duplicated player\n"); return 1; }

    client.disconnect();
    server.stop();
    pass_count++;
    printf("PASS\n");
    return 0;
}

// RTT: the client periodically sends PING (fast interval here) and pairs the
// server-echoed PONG by sequence to derive a real round-trip time.
static int test_client_ping_rtt() {
    test_count++;
    printf("  TEST client ping RTT... ");

    uint16_t port = net::find_free_port();
    net::NetworkServer server;
    if (!server.init(port, 4)) { printf("FAIL: server.init\n"); return 1; }

    net::ServerCallbacks scbs;
    scbs.on_player_join = [&](net::NetworkPlayer& p) {
        net::NetworkMessage ping;
        ping.magic = net::MAGIC;
        ping.type = static_cast<uint32_t>(net::MessageType::PING);
        ping.size = 0;
        ping.sequence = 0;
        ping.timestamp = 0;
        server.send_message(p, ping); // auth confirm packet
    };
    scbs.on_player_leave = [](net::NetworkPlayer&) {};
    scbs.on_message_received = [](const net::NetworkMessage&, const net::NetworkPlayer&) {};
    server.set_callbacks(std::move(scbs));
    if (!server.start()) { printf("FAIL: server.start\n"); return 1; }

    net::NetworkClient client;
    net::ClientCallbacks ccbs;
    ccbs.on_connected = [] {};
    ccbs.on_disconnected = [] {};
    ccbs.on_message_received = [](const net::NetworkMessage&) {};
    client.set_callbacks(std::move(ccbs));
    if (!client.connect("127.0.0.1", port)) { printf("FAIL: client.connect\n"); return 1; }

    // Pump until the handshake completes.
    for (int i = 0; i < 20000 && !client.is_connected(); ++i) {
        server.update();
        client.update();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    if (!client.is_connected()) { printf("FAIL: handshake never completed\n"); return 1; }

    // Fast ping cadence so wall-clock elapses within the loop budget.
    client.set_ping_interval(0.01);
    int iters = 0;
    for (; iters < 20000 && client.get_ping_ms() <= 0; ++iters) {
        server.update();
        client.update();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    if (client.get_ping_ms() <= 0) { printf("FAIL: no RTT measured\n"); return 1; }
    if (client.get_ping_ms() >= 1000) { printf("FAIL: implausible RTT %dms\n", client.get_ping_ms()); return 1; }

    client.disconnect();
    server.stop();
    pass_count++;
    printf("PASS (%dms after ~%.2fs)\n", client.get_ping_ms(), iters * 0.0002);
    return 0;
}

int main() {
    printf("Network packet tests:\n");
    test_magic_constant();
    test_message_header_size();
    test_message_type_string();
    test_message_type_string_unknown();
    test_player_update_data_size();
    test_player_update_stance();
    test_is_valid_ip();
    test_is_valid_ip_invalid();
    test_find_free_port();
    test_auth_deferred_confirm();
    test_client_ping_rtt();

    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
