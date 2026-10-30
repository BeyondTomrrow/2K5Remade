/* Included after shared presentation/game/popup declarations.
 * The source is FFBA0's completed live unit. Ownership begins only when
 * that same ticker actually reaches 1770C0. Native queues, updates, cameras,
 * ring advancement and completion callbacks always remain native. */
struct NativeLineupOwner {
    bool prepared = false, active = false, queued = false, delivered = false, acknowledged = false;
    bool offense = false;
    uint32_t token = 0, source_serial = 0, generation = 0, ctx = 0, resource = 0, scene = 0, clock = 0;
    uint32_t rosters[2]{}, selected_roster = 0, records[11]{}, positions[11]{};
    int package = -1, side = -1;
    std::string players, abbr[2];
};
static std::mutex s_native_lineup_lock;
static NativeLineupOwner s_native_lineup_owner;
static std::atomic<uint32_t> s_native_lineup_token{0};
static std::atomic<uint32_t> s_native_lineup_source_serial{0};
static std::atomic<int> s_native_lineup_host_package{-1}, s_native_lineup_page_permitted{0};

static bool native_lineup_package_current(const NativeLineupOwner &o)
{
    int pk = s_sel_pkg.load();
    return o.package == pk && pk == s_native_lineup_host_package.load() &&
        o.generation == s_pregame_host_generation.load() && pk > 0 && pk < (int)s_pkgs.size() &&
        s_pkgs[pk].html && s_pkgs[pk].lineup_cards_ack;
}

static bool native_lineup_seed_matches(const NativeLineupOwner &o, const GameState &g)
{
    if (!o.prepared || !g.valid || g.period < 1 || is_final(g) || s_native_match_lifecycle.load() != 1 ||
        o.side < 0 || o.side > 1 || !o.clock || o.clock != rd32(0x00E6028Cu) ||
        !o.ctx || o.ctx != rd32(0x00BA3534u) || !o.resource || !o.scene ||
        o.resource != rd32(0x00BA3538u) || rd32(o.resource) != o.scene ||
        rd32(0x00BA3210u) != 1 || o.selected_roster != rd32(0x00BA3544u) ||
        rd32(o.ctx + 0x150u) != 0x000FFE70u || rd32(o.ctx + 0x154u) != 0 ||
        rd32(o.ctx + 0x158u) != o.resource || rd32(o.ctx + 0x15Cu) != 0 ||
        rd32(o.ctx + 0x160u) != 0x00BA3550u || o.players.empty()) return false;
    for (int s = 0; s < 2; ++s)
        if (!o.rosters[s] || o.rosters[s] != rd32(k_team_obj[s] + 0x1Cu) || o.abbr[s] != g.t[s].abbr) return false;
    return o.rosters[0] != o.rosters[1] && o.selected_roster == o.rosters[o.side];
}

static bool native_lineup_visible(const NativeLineupOwner &o, const GameState &g)
{
    return native_lineup_seed_matches(o, g) && rd32(0x00BA3214u) == 1 && rd32(0x00BA3218u) == 1 &&
        rd32(0x00BDE77Cu) == o.ctx && !native_pause_active() && !native_replay_active(g);
}

static void native_lineup_event(const NativeLineupOwner &o, bool hide)
{
    PendingPopupEvent ev;
    ev.name = hide ? "LINEUP_HIDE" : "LINEUP"; ev.side = o.side;
    ev.lineup_token = o.token; ev.lineup_generation = o.generation; ev.lineup_package = o.package;
    ev.extra = ",\"source\":\"native\",\"nativeVisible\":" + std::string(hide ? "false" : "true") +
        ",\"nativeLineupToken\":" + json_str(std::to_string(o.token));
    if (!hide) ev.extra += ",\"unit\":" + json_str(o.offense ? "offense" : "defense") + ",\"players\":[" + o.players + "]";
    std::lock_guard<std::mutex> lock(s_popup_lock);
    s_popup_events.push_back(std::move(ev));
}

static void native_lineup_release(const char *reason, bool discard_seed = true, bool notify = true, uint32_t expected_source = 0)
{
    NativeLineupOwner old;
    { std::lock_guard<std::mutex> lock(s_native_lineup_lock);
      if (expected_source && s_native_lineup_owner.source_serial != expected_source) return;
      old = s_native_lineup_owner;
      if (discard_seed) s_native_lineup_owner = {};
      else { auto &o = s_native_lineup_owner; o.active = o.queued = o.delivered = o.acknowledged = false; o.token = 0; } }
    if (!old.active) return;
    if (notify && (old.queued || old.delivered)) native_lineup_event(old, true);
    fprintf(stderr, "[LINEUP-OWNER] release token=%u retained=%u reason=%s\n", old.token, discard_seed ? 0u : 1u, reason);
}

/* FFBA0 source boundary only. SP points at its completed eleven actual
 * roster records/position codes before the guest frame unwinds. */
static bool native_lineup_snapshot(uint32_t sp, const std::string &players, unsigned count)
{
    int pk = s_sel_pkg.load();
    if (pk <= 0 || pk >= (int)s_pkgs.size() || !s_pkgs[pk].html || !s_pkgs[pk].lineup_cards_ack) return false;
    /* An incomplete new source must revoke an old unit, not retain old
     * player data merely because the native ticker reused its allocation. */
    native_lineup_release("new-native-unit");
    auto reject = [count](const char *reason) {
        const uint32_t ticker = rd32(0x00BA3534u);
        /* This is a one-shot source boundary, never a per-mesh log. */
        fprintf(stderr, "[LINEUP-OWNER] native-only reason=%s count=%u ctx=%08X roster=%08X resource=%08X flags=%u/%u/%u callback=%08X drawcb=%08X camera=%08X\n",
            reason, count, ticker, rd32(0x00BA3544u), rd32(0x00BA3538u), rd32(0x00BA3210u), rd32(0x00BA3214u), rd32(0x00BA3218u),
            ticker ? rd32(ticker + 0x150u) : 0, ticker ? rd32(ticker + 0x154u) : 0, ticker ? rd32(ticker + 0x160u) : 0);
        return false;
    };
    if (count != 11 || players.empty() || players.size() > 32768u) return reject("incomplete-unit");
    GameState g; if (!read_state(g) || !g.valid || g.period < 1 || is_final(g)) return reject("invalid-match");
    NativeLineupOwner o;
    o.prepared = true; o.package = pk; o.generation = s_pregame_host_generation.load();
    o.ctx = rd32(0x00BA3534u); o.resource = rd32(0x00BA3538u); o.scene = o.resource ? rd32(o.resource) : 0;
    o.clock = rd32(0x00E6028Cu); o.selected_roster = rd32(0x00BA3544u); o.players = players;
    for (int s = 0; s < 2; ++s) {
        o.rosters[s] = rd32(k_team_obj[s] + 0x1Cu); o.abbr[s] = g.t[s].abbr;
        if (o.rosters[s] && o.selected_roster == o.rosters[s]) { if (o.side != -1) return reject("ambiguous-roster"); o.side = s; }
    }
    if (o.side < 0 || !native_lineup_seed_matches(o, g)) return reject("ticker-or-roster-identity");
    const uint32_t possession_object = rd32(0x00E60280u);
    const uint32_t offense_roster = possession_object ? rd32(possession_object + 0x1Cu) : 0;
    if (!offense_roster || (offense_roster != o.rosters[0] && offense_roster != o.rosters[1])) return reject("offense-roster");
    o.offense = o.selected_roster == offense_roster;
    uint8_t roster_count = 0;
    if (!gread(o.selected_roster + 0x11Cu, &roster_count, sizeof roster_count) || !roster_count || roster_count > 71) return reject("roster-count");
    for (unsigned i = 0; i < 11; ++i) {
        o.records[i] = rd32(sp + 0x1Cu + i * 4u); o.positions[i] = rd32(sp + 0x48u + i * 4u);
        if (!o.records[i] || o.positions[i] >= 64 || gtext(rd32(0x004F2688u + o.positions[i] * 4u), 8).empty() ||
            gtext(0x00BA3274u + i * 0x40u, 32).empty() || ((rd32(o.records[i] + 0x20u) >> 3) & 0x7Fu) > 99) return reject("incomplete-player");
        bool member = false;
        for (unsigned j = 0; j < roster_count; ++j) if (rd32(o.selected_roster + j * 4u) == o.records[i]) { member = true; break; }
        if (!member) return reject("foreign-player");
        for (unsigned j = 0; j < i; ++j) if (o.records[j] == o.records[i]) return reject("duplicate-player");
    }
    do { o.source_serial = ++s_native_lineup_source_serial; } while (!o.source_serial);
    const int side = o.side; const bool offense = o.offense;
    { std::lock_guard<std::mutex> lock(s_native_lineup_lock); s_native_lineup_owner = std::move(o); }
    fprintf(stderr, "[LINEUP-OWNER] prepared ctx=%08X roster=%08X team=%d unit=%s players=11\n",
        rd32(0x00BA3534u), rd32(0x00BA3544u), side, offense ? "offense" : "defense");
    return true;
}

/* Called only at the actual 1770C0 render boundary, with its EAX ticker. */
static void native_lineup_frame(uint32_t ctx)
{
    NativeLineupOwner copy;
    { std::lock_guard<std::mutex> lock(s_native_lineup_lock); const auto &o = s_native_lineup_owner;
      if (!o.prepared || o.ctx != ctx || !native_lineup_package_current(o)) return;
      copy = o; }
    GameState g; if (!read_state(g)) return;
    if (!native_lineup_seed_matches(copy, g)) { native_lineup_release("source-or-match-changed", true, true, copy.source_serial); return; }
    if (!native_lineup_visible(copy, g)) { native_lineup_release("native-not-visible", false, true, copy.source_serial); return; }
    if (copy.active) return;
    do { copy.token = ++s_native_lineup_token; } while (!copy.token);
    copy.active = true;
    { std::lock_guard<std::mutex> lock(s_native_lineup_lock);
      auto &o = s_native_lineup_owner;
      if (!o.prepared || o.active || o.source_serial != copy.source_serial || o.ctx != copy.ctx || o.resource != copy.resource || o.generation != copy.generation) return;
      o = copy; }
    fprintf(stderr, "[LINEUP-OWNER] visible t=%llu token=%u ctx=%08X resource=%08X scene=%08X team=%d unit=%s\n",
        (unsigned long long)GetTickCount64(), copy.token, copy.ctx, copy.resource, copy.scene, copy.side, copy.offense ? "offense" : "defense");
}

/* 0 = FFDA0/177640 temporary hide; 1 = FFE70 completion/FFDC0 cleanup. */
static void native_lineup_hide(unsigned completed)
{
    native_lineup_release(completed ? "native-unit-completed" : "native-hide", completed != 0);
}

static void native_lineup_hud_context(const GameState &g, const HtmlContext &cx)
{
    s_native_lineup_page_permitted = g.valid && g.period >= 1 && !is_final(g) &&
        !cx.paused && !cx.play_selection && !cx.replay_active && !native_replay_active(g) ? 1 : 0;
    NativeLineupOwner copy;
    { std::lock_guard<std::mutex> lock(s_native_lineup_lock); if (!s_native_lineup_owner.prepared) return; copy = s_native_lineup_owner; }
    if (!native_lineup_package_current(copy) || !native_lineup_seed_matches(copy, g)) { native_lineup_release("source-match-or-page-changed", true, true, copy.source_serial); return; }
    if (!s_native_lineup_page_permitted.load() || !native_lineup_visible(copy, g)) { native_lineup_release("native-context-hidden", false, true, copy.source_serial); return; }
    if (!copy.active || copy.queued || copy.delivered) return;
    { std::lock_guard<std::mutex> lock(s_native_lineup_lock); auto &o = s_native_lineup_owner;
      if (!o.active || o.token != copy.token || o.queued || o.delivered) return;
      o.queued = true; copy = o; }
    native_lineup_event(copy, false);
}

static bool native_lineup_event_current(const PendingPopupEvent &ev, const GameState &g)
{
    if (!ev.lineup_token) return true;
    if (ev.lineup_package != s_sel_pkg.load() || ev.lineup_package != s_native_lineup_host_package.load() ||
        ev.lineup_generation != s_pregame_host_generation.load()) return false;
    if (ev.name == "LINEUP_HIDE") return true;
    std::lock_guard<std::mutex> lock(s_native_lineup_lock); auto &o = s_native_lineup_owner;
    if (!o.active || o.token != ev.lineup_token || !native_lineup_package_current(o)) return false;
    o.queued = false;
    if (!s_native_lineup_page_permitted.load() || !native_lineup_visible(o, g)) return false;
    o.delivered = true; return true;
}

static void native_lineup_page_message(int package, uint32_t generation, const std::string &message)
{
    const char *show = "NFL2K5_LINEUP_SHOW:", *hide = "NFL2K5_LINEUP_HIDE:";
    bool visible; size_t prefix;
    if (message.compare(0, strlen(show), show) == 0) { visible = true; prefix = strlen(show); }
    else if (message.compare(0, strlen(hide), hide) == 0) { visible = false; prefix = strlen(hide); }
    else return;
    if (message.size() <= prefix || message.size() - prefix > 10 || package != s_sel_pkg.load() ||
        package != s_native_lineup_host_package.load() || generation != s_pregame_host_generation.load()) return;
    uint64_t token = 0;
    for (size_t i = prefix; i < message.size(); ++i) {
        unsigned char c = (unsigned char)message[i]; if (c < '0' || c > '9') return;
        token = token * 10 + c - '0';
    }
    if (!token || token > UINT32_MAX || message.substr(prefix) != std::to_string(token)) return;
    /* Browser thread: copied flags only, no guest reads/writes or callbacks. */
    std::lock_guard<std::mutex> lock(s_native_lineup_lock); auto &o = s_native_lineup_owner;
    if (!o.active || o.token != token || o.package != package || o.generation != generation || !o.delivered) return;
    const bool acknowledged = visible && s_native_lineup_page_permitted.load();
    if (acknowledged != o.acknowledged)
        fprintf(stderr, "[LINEUP-OWNER] page t=%llu token=%u visible=%u\n", (unsigned long long)GetTickCount64(), o.token, (unsigned)acknowledged);
    o.acknowledged = acknowledged;
}

static int native_lineup_draw_replaced(uint32_t ctx)
{
    if (!s_native_lineup_page_permitted.load()) return 0;
    { std::lock_guard<std::mutex> lock(s_native_lineup_lock); const auto &o = s_native_lineup_owner;
      if (!o.active || o.ctx != ctx || !o.acknowledged || !native_lineup_package_current(o)) return 0; }
    GameState g; if (!read_state(g)) return 0;
    std::lock_guard<std::mutex> lock(s_native_lineup_lock); const auto &o = s_native_lineup_owner;
    return o.active && o.ctx == ctx && o.acknowledged && native_lineup_package_current(o) && native_lineup_visible(o, g) ? 1 : 0;
}

static void native_lineup_reset()
{
    s_native_lineup_page_permitted = 0; s_native_lineup_host_package = -1;
    native_lineup_release("page-reset", true, false);
}
