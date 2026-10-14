/* Discord Rich Presence (Cross-platform: Win32 Named Pipe / POSIX Unix Socket).
 *
 * Settings: mods/discord.ini
 *     enabled = 1
 *     client_id = <the Application ID of a Discord application>
 *     large_image = nfl2k5
 */
#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

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
    FILE *f = fopen("mods/discord.ini", "r");
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

#if defined(_WIN32)
using IpcHandle = HANDLE;
static const IpcHandle kInvalidIpc = INVALID_HANDLE_VALUE;

static void close_ipc(IpcHandle h)
{
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static bool write_frame(IpcHandle pipe, unsigned op, const std::string &json)
{
    std::string buf(8 + json.size(), '\0');
    unsigned len = (unsigned)json.size();
    memcpy(&buf[0], &op, 4);
    memcpy(&buf[4], &len, 4);
    memcpy(&buf[8], json.data(), json.size());
    DWORD wrote = 0;
    return WriteFile(pipe, buf.data(), (DWORD)buf.size(), &wrote, nullptr) && wrote == buf.size();
}

static bool read_frame(IpcHandle pipe, unsigned *op, std::string *payload)
{
    unsigned hdr[2];
    DWORD got = 0;
    if (!ReadFile(pipe, hdr, 8, &got, nullptr) || got != 8) return false;
    *op = hdr[0];
    if (hdr[1] > (1u << 20)) return false;
    payload->assign(hdr[1], '\0');
    DWORD done = 0;
    while (done < hdr[1]) {
        if (!ReadFile(pipe, &(*payload)[done], hdr[1] - done, &got, nullptr) || !got) return false;
        done += got;
    }
    return true;
}

static bool has_incoming_frame(IpcHandle pipe)
{
    DWORD avail = 0;
    return PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr) && avail >= 8;
}

static IpcHandle open_ipc()
{
    for (int i = 0; i < 10; i++) {
        char name[64];
        snprintf(name, sizeof name, "\\\\?\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
    }
    return INVALID_HANDLE_VALUE;
}

static unsigned long current_pid()
{
    return (unsigned long)GetCurrentProcessId();
}

#else /* POSIX Unix domain socket */

using IpcHandle = int;
static const IpcHandle kInvalidIpc = -1;

static void close_ipc(IpcHandle fd)
{
    if (fd >= 0) close(fd);
}

static bool write_all(int fd, const void *data, size_t len)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    size_t done = 0;
    while (done < len) {
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fd, p + done, len - done, MSG_NOSIGNAL);
#else
        ssize_t n = write(fd, p + done, len - done);
#endif
        if (n <= 0) return false;
        done += (size_t)n;
    }
    return true;
}

static bool read_all(int fd, void *data, size_t len)
{
    uint8_t *p = static_cast<uint8_t *>(data);
    size_t done = 0;
    while (done < len) {
        ssize_t n = recv(fd, p + done, len - done, 0);
        if (n <= 0) return false;
        done += (size_t)n;
    }
    return true;
}

static bool write_frame(IpcHandle fd, unsigned op, const std::string &json)
{
    std::string buf(8 + json.size(), '\0');
    unsigned len = (unsigned)json.size();
    memcpy(&buf[0], &op, 4);
    memcpy(&buf[4], &len, 4);
    memcpy(&buf[8], json.data(), json.size());
    return write_all(fd, buf.data(), buf.size());
}

static bool read_frame(IpcHandle fd, unsigned *op, std::string *payload)
{
    unsigned hdr[2];
    if (!read_all(fd, hdr, 8)) return false;
    *op = hdr[0];
    if (hdr[1] > (1u << 20)) return false;
    payload->assign(hdr[1], '\0');
    if (hdr[1] > 0 && !read_all(fd, &(*payload)[0], hdr[1])) return false;
    return true;
}

static bool has_incoming_frame(IpcHandle fd)
{
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
}

static IpcHandle open_ipc()
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *dirs[4] = {
        runtime ? runtime : "/tmp",
        "/tmp",
        getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp",
        getenv("TEMP") ? getenv("TEMP") : "/tmp"
    };
    const char *subpaths[3] = {
        "",
        "/app/com.discordapp.Discord",
        "/snap.discord"
    };

    for (const char *base : dirs) {
        for (const char *sub : subpaths) {
            for (int i = 0; i < 10; i++) {
                struct sockaddr_un addr{};
                addr.sun_family = AF_UNIX;
                snprintf(addr.sun_path, sizeof addr.sun_path, "%s%s/discord-ipc-%d", base, sub, i);
                int fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0) continue;
                if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) == 0) {
                    return fd;
                }
                close(fd);
            }
        }
    }
    return kInvalidIpc;
}

static unsigned long current_pid()
{
    return (unsigned long)getpid();
}
#endif

static std::string team_label(const Nfl2k5BroadcastTeamState &t)
{
    std::string n = t.name[0] ? t.name : t.abbreviation;
    return n;
}

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

static int SDLCALL worker(void *)
{
    Config cfg = load_config();
    if (!cfg.enabled) return 0;
    if (cfg.client_id.empty()) {
        fprintf(stderr, "[DISCORD] no client_id in mods/discord.ini; Rich Presence is off "
                        "(see the notes at the top of src/nfl2k5_discord.cpp)\n");
        return 0;
    }
    const long long started = (long long)time(nullptr);
    IpcHandle pipe = kInvalidIpc;
    std::string last_details, last_state;
    bool last_in_game = false;
    Uint64 last_sent = 0;
    unsigned nonce = 1;
    for (;;) {
        if (pipe == kInvalidIpc) {
            pipe = open_ipc();
            if (pipe != kInvalidIpc) {
                char hs[160];
                snprintf(hs, sizeof hs, "{\"v\":1,\"client_id\":\"%s\"}", json_escape(cfg.client_id).c_str());
                unsigned op = 0;
                std::string reply;
                if (!write_frame(pipe, 0, hs) || !read_frame(pipe, &op, &reply)) {
                    close_ipc(pipe);
                    pipe = kInvalidIpc;
                } else if (reply.find("\"READY\"") == std::string::npos) {
                    fprintf(stderr, "[DISCORD] handshake refused (check client_id): %.160s\n", reply.c_str());
                    close_ipc(pipe);
                    pipe = kInvalidIpc;
                    SDL_Delay(60000);
                    continue;
                } else {
                    fprintf(stderr, "[DISCORD] connected; showing Rich Presence\n");
                    last_details.clear();
                    last_state.clear();
                    last_sent = 0;
                }
            }
            if (pipe == kInvalidIpc) {
                SDL_Delay(30000);
                continue;
            }
        }
        std::string details, state;
        bool in_game;
        describe(&details, &state, &in_game);
        Uint64 now = SDL_GetTicks();
        bool changed = details != last_details || state != last_state || in_game != last_in_game;
        if ((changed && now - last_sent >= 5000) || now - last_sent >= 120000 || !last_sent) {
            std::string act = "{\"details\":\"" + json_escape(details) + "\"";
            if (!state.empty()) act += ",\"state\":\"" + json_escape(state) + "\"";
            act += ",\"timestamps\":{\"start\":" + std::to_string(started) + "}";
            act += ",\"assets\":{\"large_image\":\"" + json_escape(cfg.large_image) + "\",\"large_text\":\"ESPN NFL 2K5\"}";
            act += ",\"instance\":false}";
            char head[128];
            snprintf(head, sizeof head, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\",\"args\":{\"pid\":%lu,\"activity\":",
                     nonce++, current_pid());
            if (!write_frame(pipe, 1, std::string(head) + act + "}}")) {
                close_ipc(pipe);
                pipe = kInvalidIpc;
                continue;
            }
            unsigned op = 0;
            std::string reply;
            while (has_incoming_frame(pipe)) {
                if (!read_frame(pipe, &op, &reply)) { close_ipc(pipe); pipe = kInvalidIpc; break; }
                if (op == 3) write_frame(pipe, 4, reply);
                else if (op == 2) { close_ipc(pipe); pipe = kInvalidIpc; break; }
            }
            last_details = details; last_state = state; last_in_game = in_game;
            last_sent = now;
        }
        SDL_Delay(2000);
    }
    return 0;
}

} // namespace

extern "C" void nfl2k5_discord_init(void)
{
    if (getenv("NFL2K5_NO_DISCORD")) return;
    SDL_Thread *t = SDL_CreateThread(worker, "discord_rpc", nullptr);
    if (t) SDL_DetachThread(t);
}