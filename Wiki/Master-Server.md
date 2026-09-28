# Master Server & Internet Discovery

Angels95 / OmegaTech ships three cooperating pieces for internet server browsing,
mirroring the classic Unreal Tournament / GameSpy model:

1. **`AngelMaster`** — a standalone master server. Receives heartbeats from
   public game servers and serves the live server list as JSON.
2. **`AngelServ` uplink** — the dedicated server periodically announces itself
   to one or more masters (`--master`, `--master-http`).
3. **Client Internet browser** — the title menu's Multiplayer → Join page has a
   `LAN` / `Internet` source toggle. Internet mode fetches the master list and
   then queries each game server directly for live player counts and RTT.

LAN discovery (UDP 27100) is unchanged and still works alongside this.

---

## 1. Master server — `AngelMaster`

```bash
./AngelMaster [--port 27900] [--http-port 27950] [--max-servers 4096]
```

| Flag | Default | Meaning |
|---|---|---|
| `--port` | `27900` | UDP heartbeat listener |
| `--http-port` | `27950` | HTTP list/API port |
| `--max-servers` | `4096` | Cap on tracked servers |
| `--gamename` | `angels95` | Filter default for list queries |

### HTTP API

| Endpoint | Description |
|---|---|
| `GET /api/servers?gamename=angels95` | JSON list of live servers |
| `GET /api/stats` | Lightweight `{servers, players}` summary |
| `POST /api/heartbeat` | Same payload as the UDP heartbeat (body = `key=value;...`) |

`Access-Control-Allow-Origin: *` is set on all responses so the web portal can
consume the list directly.

Example response:

```json
{"ok":true,"gamename":"angels95","count":1,"servers":[
  {"ip":"159.195.20.100","port":27015,"httpport":8080,"name":"Etherrealm EU",
   "map":"world_ether","gamever":"0.2.1","players":3,"maxplayers":16,
   "password":false,"lastseen":1790551982}]}
```

Entries expire after **90 s** (3 × the 30 s heartbeat). Heartbeats are
rate-limited to one per source IP per 1.5 s, the server cap is enforced, and
display strings are sanitized — but heartbeats are unauthenticated by design
(like classic masters). Run the master on a trusted host / behind a reverse
proxy if you need stronger guarantees.

---

## 2. Game server uplink — `AngelServ`

```bash
./AngelServ --port 27015 --http-port 8080 --dir GameData \
            --server-name "Etherrealm EU" \
            --master 127.0.0.1:27900 \
            --master-http http://127.0.0.1:27950 \
            --public-ip 203.0.113.7
```

| Flag | Meaning |
|---|---|
| `--server-name NAME` | Display name announced to masters (default `Angels95 Server`) |
| `--master host[:port]` | UDP heartbeat target, repeatable (default port `27900`) |
| `--master-http http://host:port` | HTTP heartbeat URL, repeatable |
| `--public-ip IP` | Public IP to announce when behind NAT (optional) |

- Heartbeats are sent immediately on start and then every **30 s**.
- Payload: `gamename`, `gamever`, `host`, `map`, `port`, `httpport`, `players`,
  `maxplayers`, `password` (and optional `publicip`).
- The uplink runs on a background thread, so the 10 Hz game tick is never
  blocked by DNS or HTTP.
- Without any `--master`/`--master-http`, the server logs a warning and only
  uses LAN discovery.

The dedicated server also exposes live status (used by the client's direct
RTT/status query) at `GET /status`:

```json
{"ok":true,"server_name":"Etherrealm EU","players":0,"max_players":32,
 "uptime_seconds":42,"worlds":3,"map":"Dessert_Dreams","gamever":"0.2.1",
 "password":false}
```

If `--auth-token` is set, `/status` is protected by the same Bearer gate as the
other HTTP endpoints (see `Wiki/Engine-Overview.md`).

---

## 3. Client configuration — `System/Angels95.ini`

The client reads master addresses from the `[MasterServers]` section. Both a
comma-separated list and numbered keys are supported:

```ini
[MasterServers]
Master=https://angels95.tribewarez.com/master,http://127.0.0.1:27950
Master1=http://backup.example.net:27950
```

- If the section is missing/empty, the client falls back to the official public
  master `https://angels95.tribewarez.com/master`.
- `http://` and `https://` are both supported. TLS needs no vendored crypto:
  Windows uses WinHTTP (Schannel), Linux/macOS shell out to `curl`.
- When hosting from the menu (Host Game → Start Server), the client forwards
  these addresses to the launched `AngelServ` as `--master` / `--master-http`.

### Using the browser

1. Open **Multiplayer → Join Game**.
2. Set **Source** to `Internet` — the master list is fetched automatically on
   first open (**Scan Internet** re-fetches on demand).
3. The list is populated from every reachable master; each row shows
   `name ip:port (players/max) map RTT`. RTT is the real round trip to the game
   server's `/status` endpoint.
4. Click a row to fill IP/port, then **Connect**. The join path is identical to
   LAN/direct connect.

> Official master: `https://angels95.tribewarez.com/master` — game servers
> announce with `--master-http https://angels95.tribewarez.com/master/api/heartbeat`
> (or `--master <host>:27900` for the UDP path).

---

## 4. Running the three pieces locally

Terminal 1 — master:

```bash
./AngelMaster --port 27900 --http-port 27950
```

Terminal 2 — dedicated server announcing to the master:

```bash
./AngelServ --port 27015 --http-port 8080 --dir GameData \
            --server-name "Local Test" \
            --master 127.0.0.1:27900 \
            --master-http http://127.0.0.1:27950
```

Verify with curl:

```bash
curl "http://127.0.0.1:27950/api/servers?gamename=angels95"
curl "http://127.0.0.1:8080/status"
```

Terminal 3 — client: launch `Angels95`, go to Multiplayer → Join → Source
`Internet` → **Scan Internet** (the default master list already points at
`http://127.0.0.1:27950`).

For a public deployment, run `AngelMaster` on a reachable host (e.g.
`master.example.com:27900/27950`), point game servers at it, and add that URL
to each client's `[MasterServers]` section.

---

## 5. Implementation map

| Piece | File |
|---|---|
| Shared wire codec (heartbeat + JSON list) | `Source/Master/MasterProtocol.hpp` |
| Minimal HTTP client (plain HTTP, no TLS) | `Source/Master/MasterHttp.hpp` |
| Server → master uplink thread | `Source/Master/MasterClient.hpp` |
| Master daemon | `Source/Master/Master.cpp` |
| Master address/INI loader | `Source/Master/MasterList.hpp` |
| Client internet browser (async) | `Source/Menu/InternetBrowser.hpp` |
| Client browser UI | `Source/Menu/TitleMenu.hpp` |
| Uplink wiring | `Source/Server/Server.cpp` |
| Tests | `tests/Master.test.cpp` (`make test_master`) |

### Limitations

- Heartbeats/lists use plain HTTP and unauthenticated UDP — spoofing is
  mitigated only by rate limiting and expiry.
- No TLS (`https://` masters unsupported) to avoid adding a crypto dependency.
- The pure GameSpy binary/`\status\` query protocol is **not** implemented; the
  JSON/HTTP path is the supported integration. The wire format is small and
  documented above if a GameSpy-compatible endpoint is added later.
