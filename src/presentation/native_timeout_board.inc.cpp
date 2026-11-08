/* Included after the shared game, HTML and native popup declarations.
 * A00FB proves an accepted team timeout. It only prepares a seed: the break
 * panel is admitted by FC360's completed, eligible original scorebug draw
 * while the native timeout presentation is actually state 0x19. This is the
 * ordinary native scorebug during a timeout, not an invented large popup.
 * All native clocks, cameras, update paths and close decisions keep running. */
struct NativeTimeoutBoardOwner {
    bool prepared = false, active = false, queued = false, delivered = false, acknowledged = false;
    bool bug_eligible = false;
    uint32_t token = 0, source_serial = 0, generation = 0, team = 0, clock = 0, rosters[2]{};
    int package = -1, side = -1, seed_period = 0, quarter = 0;
    float clock_seconds = 0;
    std::string abbr[2];
};
static std::mutex s_native_timeout_lock;
static NativeTimeoutBoardOwner s_native_timeout_owner;
static std::atomic<uint32_t> s_native_timeout_token{0}, s_native_timeout_source_serial{0};
static std::atomic<int> s_native_timeout_host_package{-1}, s_native_timeout_page_permitted{0};
static std::atomic<int> s_native_timeout_observing{0};

static bool native_timeout_package_current(const NativeTimeoutBoardOwner &o)
{
    const int pk = s_sel_pkg.load();
    return o.package == pk && pk == s_native_timeout_host_package.load() &&
        o.generation == s_pregame_host_generation.load() && pk > 0 && pk < (int)s_pkgs.size() &&
        s_pkgs[pk].html && s_pkgs[pk].timeout_boards_ack;
}

/* Guest callbacks only. The accepted period is an identity check; the event's
 * period and clock value are captured separately at the first actual draw. */
static bool native_timeout_seed_matches(const NativeTimeoutBoardOwner &o, const GameState &g)
{
    if (!o.prepared || !g.valid || o.side < AWAY || o.side > HOME ||
        s_native_match_lifecycle.load() != 1 || o.seed_period < 1 || g.period != o.seed_period ||
        g.flow_state != 0x13 || rd32(0x00E602B8u) != 0x13 ||
        !o.clock || o.clock != rd32(0x00E6028Cu) || o.team != k_team_obj[o.side] ||
        o.team != rd32(0x00E602F8u)) return false;
    for (int side = AWAY; side <= HOME; ++side)
        if (!o.rosters[side] || o.rosters[side] != rd32(k_team_obj[side] + 0x1Cu) ||
            o.abbr[side].empty() || o.abbr[side] != g.t[side].abbr) return false;
    return o.rosters[AWAY] != o.rosters[HOME];
}

static void native_timeout_event(const NativeTimeoutBoardOwner &o, bool hide)
{
    if (!o.active || !o.token) return;
    PendingPopupEvent ev;
    ev.name = hide ? "TIMEOUT_BOARD_HIDE" : "TIMEOUT_BOARD_SHOW";
    ev.side = o.side;
    ev.timeout_token = o.token; ev.timeout_generation = o.generation; ev.timeout_package = o.package;
    ev.extra = ",\"source\":\"native\",\"nativeVisible\":" + std::string(hide ? "false" : "true") +
        ",\"nativeTimeoutToken\":" + json_str(std::to_string(o.token)) +
        ",\"quarter\":" + std::to_string(o.quarter) +
        ",\"clockSeconds\":" + std::to_string(o.clock_seconds);
    std::lock_guard<std::mutex> lock(s_popup_lock);
    s_popup_events.push_back(std::move(ev));
}

static void native_timeout_release(const char *reason, bool notify = true, uint32_t expected_source = 0)
{
    NativeTimeoutBoardOwner old;
    {
        std::lock_guard<std::mutex> lock(s_native_timeout_lock);
        if (expected_source && s_native_timeout_owner.source_serial != expected_source) return;
        old = std::move(s_native_timeout_owner); s_native_timeout_owner = {};
        s_native_timeout_observing = 0;
    }
    if (notify && old.active && (old.queued || old.delivered)) native_timeout_event(old, true);
    if (old.prepared && presentation_trace_enabled())
        fprintf(stderr, "[TIMEOUT-BOARD] release token=%u source=%u reason=%s\n", old.token, old.source_serial, reason);
}

/* After A00A0's real B8810 call returns at A00FB; rejected branches never
 * reach this call. Null-team warning/injury/review paths cannot seed it. */
static void native_timeout_accepted(uint32_t team)
{
    const int pk = s_sel_pkg.load();
    if (pk <= 0 || pk >= (int)s_pkgs.size() || pk != s_native_timeout_host_package.load() ||
        !s_pkgs[pk].html || !s_pkgs[pk].timeout_boards_ack) {
        if (s_native_timeout_observing.load()) native_timeout_release("unsupported-package");
        return;
    }
    native_timeout_release("next-accepted-timeout");
    int side = team == k_team_obj[AWAY] ? AWAY : team == k_team_obj[HOME] ? HOME : -1;
    if (side < 0) return;
    GameState g;
    if (!read_state(g)) return;
    NativeTimeoutBoardOwner o;
    o.prepared = true; o.package = pk; o.generation = s_pregame_host_generation.load();
    o.team = team; o.side = side; o.clock = rd32(0x00E6028Cu); o.seed_period = g.period;
    for (int s = AWAY; s <= HOME; ++s) {
        o.rosters[s] = rd32(k_team_obj[s] + 0x1Cu); o.abbr[s] = g.t[s].abbr;
    }
    if (!native_timeout_seed_matches(o, g)) return;
    do { o.source_serial = ++s_native_timeout_source_serial; } while (!o.source_serial);
    {
        std::lock_guard<std::mutex> lock(s_native_timeout_lock);
        s_native_timeout_owner = std::move(o); s_native_timeout_observing = 1;
    }
}

/* Called once at FC360's common completed return FC6A3, after its early
 * gates and all original drawing. A per-call local is set at FC396. This
 * avoids publishing false at entry and true later in the same native draw. */
static void native_timeout_bug_frame(int eligible)
{
    if (!s_native_timeout_observing.load()) return;
    NativeTimeoutBoardOwner copy;
    { std::lock_guard<std::mutex> lock(s_native_timeout_lock); copy = s_native_timeout_owner; }
    if (!native_timeout_package_current(copy)) { native_timeout_release("package-changed", true, copy.source_serial); return; }
    GameState g;
    if (!read_state(g) || !native_timeout_seed_matches(copy, g)) {
        native_timeout_release("native-match-or-flow-changed", true, copy.source_serial); return;
    }
    /* The ordinary pause-menu Timeout call reaches A00FB before that menu
     * pops. Keep its accepted seed, but never activate underneath the menu. */
    if (native_pause_active() || native_replay_active(g)) {
        if (copy.active) native_timeout_release("native-context-hidden", true, copy.source_serial);
        return;
    }
    const uint32_t presentation = rd32(0x00B616C0u);
    if (copy.active && (presentation != 0x19 || !eligible)) {
        native_timeout_release(presentation != 0x19 ? "native-presentation-exit" : "native-bug-hidden", true, copy.source_serial); return;
    }
    /* Keep the pending accepted seed through the native 2.5-second delayed
     * presentation request. Neither a request nor an ineligible draw binds. */
    if (!eligible || presentation != 0x19) return;
    bool acquired = false;
    NativeTimeoutBoardOwner delivery;
    {
        std::lock_guard<std::mutex> lock(s_native_timeout_lock);
        auto &o = s_native_timeout_owner;
        if (!o.prepared || o.source_serial != copy.source_serial || !native_timeout_package_current(o)) return;
        if (!o.active) {
            if (!std::isfinite(g.clock) || g.clock < 0 || g.clock > 86400.0f) return;
            do { o.token = ++s_native_timeout_token; } while (!o.token);
            o.active = true; o.quarter = g.period; o.clock_seconds = g.clock;
            acquired = true;
        }
        o.bug_eligible = true;
        if (s_native_timeout_page_permitted.load() && !o.queued && !o.delivered) {
            o.queued = true; delivery = o;
        }
        copy = o;
    }
    if (acquired && presentation_trace_enabled()) {
        fprintf(stderr, "[TIMEOUT-BOARD] bind t=%llu token=%u team=%d quarter=%d clock=%.3f presentation=25\n",
            (unsigned long long)GetTickCount64(), copy.token, copy.side, copy.quarter, copy.clock_seconds);
        if (getenv("NFL2K5_REC")) xbox_PresentRecord(8);
    }
    if (delivery.active) native_timeout_event(delivery, false);
}

/* 892C1, after the original committed presentation transition. The state
 * may enter 0x19 before A00FB prepares its seed; a missing seed is a no-op. */
static void native_timeout_committed()
{
    if (!s_native_timeout_observing.load()) return;
    NativeTimeoutBoardOwner copy;
    { std::lock_guard<std::mutex> lock(s_native_timeout_lock); copy = s_native_timeout_owner; }
    if (!native_timeout_package_current(copy)) { native_timeout_release("package-changed", true, copy.source_serial); return; }
    if (copy.active && rd32(0x00B616C0u) != 0x19)
        native_timeout_release("native-presentation-exit", true, copy.source_serial);
}

static void native_timeout_hud_context(const GameState &g, const HtmlContext &cx)
{
    const int pk = s_sel_pkg.load();
    s_native_timeout_page_permitted = pk == s_native_timeout_host_package.load() &&
        g.valid && g.period >= 1 && !cx.paused && !cx.play_selection && !cx.replay_active ? 1 : 0;
    if (!s_native_timeout_observing.load()) return;
    NativeTimeoutBoardOwner copy;
    { std::lock_guard<std::mutex> lock(s_native_timeout_lock); copy = s_native_timeout_owner; }
    if (!native_timeout_package_current(copy) || !native_timeout_seed_matches(copy, g) ||
        (copy.active && (!s_native_timeout_page_permitted.load() || native_pause_active() ||
                        native_replay_active(g) || rd32(0x00B616C0u) != 0x19))) {
        native_timeout_release("native-context-match-or-page-changed", true, copy.source_serial); return;
    }
    if (!copy.active || !copy.bug_eligible || copy.queued || copy.delivered) return;
    {
        std::lock_guard<std::mutex> lock(s_native_timeout_lock); auto &o = s_native_timeout_owner;
        if (!o.active || o.token != copy.token || o.source_serial != copy.source_serial || o.queued || o.delivered) return;
        o.queued = true; copy = o;
    }
    native_timeout_event(copy, false);
}

static bool native_timeout_event_current(const PendingPopupEvent &ev, const GameState &g)
{
    if (!ev.timeout_token) return true;
    if (ev.timeout_package != s_sel_pkg.load() || ev.timeout_package != s_native_timeout_host_package.load() ||
        ev.timeout_generation != s_pregame_host_generation.load()) return false;
    if (ev.name == "TIMEOUT_BOARD_HIDE") return true;
    std::lock_guard<std::mutex> lock(s_native_timeout_lock); auto &o = s_native_timeout_owner;
    if (!o.active || o.token != ev.timeout_token || !native_timeout_package_current(o)) return false;
    o.queued = false;
    if (!s_native_timeout_page_permitted.load() || !o.bug_eligible ||
        !native_timeout_seed_matches(o, g) || native_pause_active() || native_replay_active(g) ||
        rd32(0x00B616C0u) != 0x19) return false;
    o.delivered = true; return true;
}

static void native_timeout_page_message(int package, uint32_t generation, const std::string &message)
{
    const char *show = "NFL2K5_TIMEOUT_BOARD_SHOW:", *hide = "NFL2K5_TIMEOUT_BOARD_HIDE:";
    bool visible; size_t prefix;
    if (message.compare(0, strlen(show), show) == 0) { visible = true; prefix = strlen(show); }
    else if (message.compare(0, strlen(hide), hide) == 0) { visible = false; prefix = strlen(hide); }
    else return;
    if (message.size() <= prefix || message.size() - prefix > 10 || package != s_sel_pkg.load() ||
        package != s_native_timeout_host_package.load() || generation != s_pregame_host_generation.load()) return;
    uint64_t token = 0;
    for (size_t i = prefix; i < message.size(); ++i) {
        const unsigned char c = (unsigned char)message[i]; if (c < '0' || c > '9') return;
        token = token * 10u + c - '0';
    }
    if (!token || token > UINT32_MAX || message.substr(prefix) != std::to_string(token)) return;
    /* Browser thread: copied token and permission fields only. No guest reads,
     * native callbacks or timing changes belong to page acknowledgements. */
    std::lock_guard<std::mutex> lock(s_native_timeout_lock); auto &o = s_native_timeout_owner;
    if (!o.active || o.token != token || o.package != package || o.generation != generation || !o.delivered) return;
    o.acknowledged = visible && o.bug_eligible && s_native_timeout_page_permitted.load();
}

static void native_timeout_reset()
{
    s_native_timeout_page_permitted = 0; s_native_timeout_host_package = -1;
    native_timeout_release("page-reset", false);
}
