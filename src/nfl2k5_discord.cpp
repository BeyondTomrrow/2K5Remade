/* Discord Rich Presence.
 *
 * Shows what is being played on the player's Discord profile: the teams and
 * score, the quarter and clock, or the menu they are in. It speaks Discord's
 * local IPC protocol over the named pipe the Discord desktop app opens
 * (\\?\pipe\discord-ipc-0..9), so there is no SDK or network access here.
 *
 * Settings: mods\discord.ini
 *     enabled = 1
 *     client_id = <the Application ID of a Discord application>
 *     large_image = nfl2k5        ; an art asset uploaded to that application
 * Create the application at https://discord.com/developers/applications
 * (New Application, name it "ESPN NFL 2K5"; the Application ID is on the
 * General Information page; Rich Presence > Art Assets takes the picture).
 * With no client_id the feature stays off and says so in the log.
 *
 * The protocol, per Discord's RPC documentation: a frame is a 32-bit opcode
 * and a 32-bit length (little endian) then JSON. Opcode 0 is the handshake
 * {"v":1,"client_id":...}, 1 a frame, 2 close, 3 ping, 4 pong. The activity
 * is set with {"cmd":"SET_ACTIVITY","args":{"pid":N,"activity":{...}}}.
 */
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "nfl2k5_broadcast.h"

namespace {

struct Config {
    bool enabled = true;
    std::string client_id;
    std::string large_image = "nfl2k5";
};

static Config load_config()
{
    Config c;
    FILE *f = fopen("mods\\discord.ini", "r");
    if (!f) return c;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char key[64], val[400];
        if (line[0] == ';' || line[0] == '#') continue;
        if (sscanf(line, " %63[a-z_] = %399[^;\r\n]", key, val) != 2) continue;
        std::string v = val;
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        if (!strcmp(key, "enabled")) c.enabled = atoi(v.c_str()) != 0;
        else if (!strcmp(key, "client_id")) c.client_id = v;
        else if (!strcmp(key, "large_image")) c.large_image = v;
    }
    fclose(f);
    return c;
}

static std::string json_escape(const std::string &s)
{
    std::string o;
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += (char)ch;
    }
    return o;
}

static bool write_frame(HANDLE pipe, unsigned op, const std::string &json)
{
    std::string buf(8 + json.size(), '\0');
    unsigned len = (unsigned)json.size();
    memcpy(&buf[0], &op, 4);
    memcpy(&buf[4], &len, 4);
    memcpy(&buf[8], json.data(), json.size());
    DWORD wrote = 0;
    return WriteFile(pipe, buf.data(), (DWORD)buf.size(), &wrote, nullptr) && wrote == buf.size();
}

/* One frame back from Discord (opcode, payload); false on a broken pipe. */
static bool read_frame(HANDLE pipe, unsigned *op, std::string *payload)
{
    unsigned hdr[2];
    DWORD got = 0;
    if (!ReadFile(pipe, hdr, 8, &got, nullptr) || got != 8) return false;
    *op = hdr[0];
    payload->assign(hdr[1] <= (1u << 20) ? hdr[1] : 0, '\0');
    if (hdr[1] > (1u << 20)) return false;
    DWORD done = 0;
    while (done < hdr[1]) {
        if (!ReadFile(pipe, &(*payload)[done], hdr[1] - done, &got, nullptr) || !got) return false;
        done += got;
    }
    return true;
}

static HANDLE open_pipe()
{
    for (int i = 0; i < 10; i++) {
        char name[64];
        snprintf(name, sizeof name, "\\\\?\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
    }
    return INVALID_HANDLE_VALUE;
}

static std::string team_label(const Nfl2k5BroadcastTeamState &t)
{
    std::string n = t.name[0] ? t.name : t.abbreviation;
    return n;
}

/* What to show for the game as it is now. */
static void describe(std::string *details, std::string *state, bool *in_game)
{
    Nfl2k5BroadcastState g{};
    *in_game = nfl2k5_broadcast_get_state(&g) != 0;
    if (!*in_game) {
        *details = "In the menus";
        state->clear();
        return;
    }
    char buf[256];
    snprintf(buf, sizeof buf, "%s %d - %d %s", team_label(g.away).c_str(), g.away.score, g.home.score,
             team_label(g.home).c_str());
    *details = buf;
    if (g.quarter == 0) {
        *state = "Pregame";
        return;
    }
    const char *q = g.quarter == 1 ? "1st" : g.quarter == 2 ? "2nd" : g.quarter == 3 ? "3rd" : g.quarter == 4 ? "4th" : "OT";
    int secs = (int)g.game_clock;
    if (g.quarter >= 4 && g.game_clock <= 0.0f) {
        *state = "Final";
        return;
    }
    if (secs < 0) secs = 0;
    snprintf(buf, sizeof buf, "%s quarter, %d:%02d", q, secs / 60, secs % 60);
    *state = buf;
}

static DWORD WINAPI worker(void *)
{
    Config cfg = load_config();
    if (!cfg.enabled) return 0;
    if (cfg.client_id.empty()) {
        fprintf(stderr, "[DISCORD] no client_id in mods\\discord.ini; Rich Presence is off "
                        "(see the notes at the top of src/nfl2k5_discord.cpp)\n");
        return 0;
    }
    const long long started = (long long)time(nullptr);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::string last_details, last_state;
    bool last_in_game = false;
    ULONGLONG last_sent = 0;
    unsigned nonce = 1;
    for (;;) {
        if (pipe == INVALID_HANDLE_VALUE) {
            pipe = open_pipe();
            if (pipe != INVALID_HANDLE_VALUE) {
                char hs[160];
                snprintf(hs, sizeof hs, "{\"v\":1,\"client_id\":\"%s\"}", json_escape(cfg.client_id).c_str());
                unsigned op = 0;
                std::string reply;
                if (!write_frame(pipe, 0, hs) || !read_frame(pipe, &op, &reply)) {
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                } else if (reply.find("\"READY\"") == std::string::npos) {
                    fprintf(stderr, "[DISCORD] handshake refused (check client_id): %.160s\n", reply.c_str());
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                    Sleep(60000);
                    continue;
                } else {
                    fprintf(stderr, "[DISCORD] connected; showing Rich Presence\n");
                    last_details.clear();
                    last_state.clear();
                    last_sent = 0;
                }
            }
            if (pipe == INVALID_HANDLE_VALUE) {          /* Discord is not running (yet) */
                Sleep(30000);
                continue;
            }
        }
        std::string details, state;
        bool in_game;
        describe(&details, &state, &in_game);
        ULONGLONG now = GetTickCount64();
        /* Discord allows about five updates per 20 s: send on a change, at
         * most every 5 s, and every 2 minutes regardless. */
        bool changed = details != last_details || state != last_state || in_game != last_in_game;
        if ((changed && now - last_sent >= 5000) || now - last_sent >= 120000 || !last_sent) {
            std::string act = "{\"details\":\"" + json_escape(details) + "\"";
            if (!state.empty()) act += ",\"state\":\"" + json_escape(state) + "\"";
            act += ",\"timestamps\":{\"start\":" + std::to_string(started) + "}";
            act += ",\"assets\":{\"large_image\":\"" + json_escape(cfg.large_image) + "\",\"large_text\":\"ESPN NFL 2K5\"}";
            act += ",\"instance\":false}";
            char head[128];
            snprintf(head, sizeof head, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\",\"args\":{\"pid\":%lu,\"activity\":",
                     nonce++, (unsigned long)GetCurrentProcessId());
            if (!write_frame(pipe, 1, std::string(head) + act + "}}")) {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
                continue;
            }
            /* The reply; a PING in between must be answered. */
            unsigned op = 0;
            std::string reply;
            DWORD avail = 0;
            while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr) && avail >= 8) {
                if (!read_frame(pipe, &op, &reply)) { avail = 0; CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; break; }
                if (op == 3) write_frame(pipe, 4, reply);
                else if (op == 2) { CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; break; }
            }
            last_details = details; last_state = state; last_in_game = in_game;
            last_sent = now;
        }
        Sleep(2000);
    }
}

} // namespace

extern "C" void nfl2k5_discord_init(void)
{
    if (getenv("NFL2K5_NO_DISCORD")) return;
    HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (t) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); }
}
