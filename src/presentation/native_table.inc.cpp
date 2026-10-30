/* Included by nfl2k5_presentation.cpp. Exact native team-table ownership;
 * no stat recomputation, guessed period scope, guest writes or guest calls. */
struct NativeTableCell {
    uint32_t type = 0, style = 0, source = 0;
    std::string text;
};
struct NativeTableOwner {
    bool active = false, queued = false, delivered = false, acknowledged = false, closing = false;
    uint32_t token = 0, generation = 0, ctx = 0, layout = 0, callback = 0;
    uint32_t clock = 0, rosters[2]{}, resource = 0, scene = 0;
    uint32_t columns = 0, rows = 0;
    int package = -1, slot = -1;
    std::string title;
    std::vector<uint32_t> subjects;
    std::vector<int> sides;
    std::vector<std::string> headers;
    std::vector<NativeTableCell> cells;
};
static std::mutex s_native_table_lock;
static NativeTableOwner s_native_table_owner;
static std::atomic<uint32_t> s_native_table_token{0};
static std::atomic<int> s_native_table_host_package{-1}, s_native_table_page_permitted{0};

static bool native_table_text(uint32_t va, std::string &text, bool empty)
{
    uint16_t first = 0;
    if (!va || !gread(va, &first, sizeof first)) return false;
    if (!first) { text.clear(); return empty; }
    text = gtext(va, 64);
    return !text.empty();
}

static bool native_table_catalog(uint32_t layout)
{
    if (layout != 3 && layout != 4 && layout != 27) return false;
    const uint32_t catalog = rd32(0x00BE5214u);
    if (catalog != 0x00AAD180u) return false;
    const uint32_t descriptor = catalog + layout * 0x6Cu;
    const uint32_t name = layout == 3 ? 0x00E861DCu : layout == 4 ? 0x00E861F8u : 0x00E8656Cu;
    const uint32_t group = layout == 27 ? 0x00E85B20u : 0x00E861A8u;
    /* 8A170 uses +24 for the subtype table. Layout27 instead paints its
     * fixed 3x5 grid through independently verified MRKS text callbacks;
     * native_table_scene_cells checks that actual loaded resource below. */
    return rd32(descriptor) == name && rd32(descriptor + 4u) == group &&
        (layout == 27 ? rd32(descriptor + 0x24u) == 0 && rd32(descriptor + 0x30u) == 0x0050D268u :
            rd32(descriptor + 0x24u) != 0);
}

static bool native_table_scene_cells(uint32_t layout, uint32_t resource)
{
    if (layout != 27) return true;
    /* Owned halftime.iff MRKS chunk7: two text records per cell, then two
     * title records. +80 is exactly the hash dispatched by BCE50->C0CD0
     * (15 cells) or C0AF0 (literal native title). No other text is erased. */
    static const uint32_t expected[] = {
        0xD59992C8u, 0xC72C3D26u, 0x7F905A43u,
        0x9AD8040Fu, 0x886DABE1u, 0x30D1CC84u,
        0x83C3354Eu, 0x91769AA0u, 0x29CAFDC5u,
        0xA8EE668Du, 0xBA5BC963u, 0x02E7AE06u,
        0xB1F557CCu, 0xA340F822u, 0x1BFC9F47u, 0xAE7481A6u
    };
    if (!resource || rd32(resource + 4u) != 32) return false;
    const uint32_t records = rd32(resource + 8u);
    if (!records) return false;
    for (uint32_t i = 0; i < 32; ++i)
        if (rd32(records + i * 0x90u + 0x80u) != expected[i / 2u]) return false;
    return true;
}

static bool native_table_payload(uint32_t ctx, const GameState &g, NativeTableOwner &o)
{
    o.layout = rd32(ctx + 0x48u);
    if (!native_table_catalog(o.layout) || rd32(ctx + 0x228u) != 1 ||
        !native_table_text(ctx + 0x4Cu, o.title, false)) return false;
    /* A meaningful second caption is not part of this typed table contract.
     * Keep native artwork rather than silently dropping that message. */
    std::string message;
    if (!native_table_text(ctx + 0xCCu, message, true) || !message.empty()) return false;
    const uint32_t count = rd32(ctx + 0x40u);
    o.columns = rd32(ctx + 0x22Cu); o.rows = rd32(ctx + 0x230u);
    if (!count || count > 2 || o.columns != count + 1 || !o.rows || o.rows > 24 ||
        (o.layout == 27 && (count != 2 || o.columns != 3 || o.rows != 5)) ||
        (uint64_t)o.columns * o.rows > 24 || rd32(ctx + 0xEF4u) != o.columns * o.rows) return false;
    o.subjects.clear(); o.sides.clear(); o.headers = {""};
    for (uint32_t i = 0; i < count; ++i) {
        if (rd32(ctx + 0x10u + i * 8u) != 2) return false;
        const uint32_t subject = rd32(ctx + 0x14u + i * 8u);
        int side = -1;
        for (int s = 0; s < 2; ++s)
            if (subject && subject == rd32(k_team_obj[s] + 0x1Cu)) {
                if (side >= 0) return false; /* Ambiguous same-roster sides. */
                side = s;
            }
        if (side < 0 || g.t[side].abbr.empty() || g.t[side].abbr.size() > 16 ||
            std::find(o.subjects.begin(), o.subjects.end(), subject) != o.subjects.end()) return false;
        o.subjects.push_back(subject); o.sides.push_back(side); o.headers.push_back(g.t[side].abbr);
    }
    o.cells.clear();
    for (uint32_t i = 0; i < o.columns * o.rows; ++i) {
        const uint32_t address = ctx + 0x234u + i * 0x88u;
        NativeTableCell cell; cell.type = rd32(address); cell.style = rd32(address + 4u);
        if (cell.type == 0) cell.source = address + 8u;
        else if (cell.type == 1) cell.source = rd32(address + 8u);
        else return false;
        if (!native_table_text(cell.source, cell.text, true)) return false;
        o.cells.push_back(std::move(cell));
    }
    return true;
}

static bool native_table_package_current(const NativeTableOwner &o)
{
    const int pk = s_sel_pkg.load();
    return o.package == pk && pk == s_native_table_host_package.load() && pk > 0 && pk < (int)s_pkgs.size() &&
        o.generation == s_pregame_host_generation.load() && s_pkgs[pk].html && s_pkgs[pk].native_tables_ack;
}

static bool native_table_payload_matches(const NativeTableOwner &a, const NativeTableOwner &b)
{
    if (a.layout != b.layout || a.title != b.title || a.columns != b.columns || a.rows != b.rows ||
        a.subjects != b.subjects || a.sides != b.sides || a.headers != b.headers || a.cells.size() != b.cells.size()) return false;
    for (size_t i = 0; i < a.cells.size(); ++i)
        if (a.cells[i].type != b.cells[i].type || a.cells[i].style != b.cells[i].style ||
            a.cells[i].source != b.cells[i].source || a.cells[i].text != b.cells[i].text) return false;
    return true;
}

static bool native_table_record_matches(const NativeTableOwner &o, const GameState &g)
{
    if (!o.active || !g.valid || o.slot < 0 || o.slot > 1 ||
        o.ctx != 0x00B61B50u + (uint32_t)o.slot * 0xF20u || !o.clock || o.clock != rd32(0x00E6028Cu) ||
        rd32(o.ctx) != (uint32_t)o.slot || rd32(o.ctx + 4u) != 1 || rd32(o.ctx + 0xF08u) ||
        rd32(o.ctx + 0x1D8u) != 5 || rd32(o.ctx + 0xF1Cu) != 1 ||
        (o.closing ? rd32(o.ctx + 0xCu) == 0 : rd32(o.ctx + 0xCu) != 0) ||
        rd32(o.ctx + 0xF14u) != o.callback || !o.resource || !o.scene ||
        rd32(o.ctx + 0x1DCu) != o.resource || rd32(o.resource) != o.scene ||
        !native_table_scene_cells(o.layout, o.resource)) return false;
    for (int s = 0; s < 2; ++s)
        if (!o.rosters[s] || o.rosters[s] != rd32(k_team_obj[s] + 0x1Cu)) return false;
    NativeTableOwner current;
    return native_table_payload(o.ctx, g, current) && native_table_payload_matches(o, current);
}

static PendingPopupEvent native_table_make_event(const NativeTableOwner &o, bool hide)
{
    PendingPopupEvent ev;
    ev.name = hide ? "NATIVE_TABLE_HIDE" : "NATIVE_TABLE_SHOW";
    ev.table_token = o.token; ev.table_generation = o.generation; ev.table_package = o.package;
    ev.extra = ",\"source\":\"native\",\"nativeVisible\":" + std::string(hide ? "false" : "true") +
        ",\"nativeTableToken\":" + json_str(std::to_string(o.token)) + ",\"slot\":" + std::to_string(o.slot) +
        ",\"layout\":" + std::to_string(o.layout) + ",\"title\":" + json_str(o.title) + ",\"headers\":[";
    for (size_t i = 0; i < o.headers.size(); ++i) { if (i) ev.extra += ","; ev.extra += json_str(o.headers[i]); }
    ev.extra += "],\"rows\":[";
    for (uint32_t row = 0; row < o.rows; ++row) {
        if (row) ev.extra += ","; ev.extra += "[";
        for (uint32_t column = 0; column < o.columns; ++column) {
            if (column) ev.extra += ",";
            ev.extra += json_str(o.cells[(size_t)row * o.columns + column].text);
        }
        ev.extra += "]";
    }
    ev.extra += "]";
    return ev;
}

static void native_table_event(const NativeTableOwner &o, bool hide)
{
    PendingPopupEvent ev = native_table_make_event(o, hide);
    std::lock_guard<std::mutex> lock(s_popup_lock); s_popup_events.push_back(std::move(ev));
}

static void native_table_release(const char *reason, bool notify = true, uint32_t expected = 0)
{
    NativeTableOwner old;
    { std::lock_guard<std::mutex> lock(s_native_table_lock);
      if (expected && s_native_table_owner.token != expected) return;
      old = s_native_table_owner; s_native_table_owner = {}; }
    if (!old.active) return;
    if (notify && (old.queued || old.delivered)) native_table_event(old, true);
    fprintf(stderr, "[NATIVE-TABLE] release token=%u reason=%s\n", old.token, reason);
}

static void native_table_frame(uint32_t ctx)
{
    const int pk = s_sel_pkg.load();
    if (pk <= 0 || pk >= (int)s_pkgs.size() || pk != s_native_table_host_package.load() ||
        !s_pkgs[pk].html || !s_pkgs[pk].native_tables_ack) return;
    GameState g; if (!read_state(g)) return;
    uint32_t stale = 0;
    { std::lock_guard<std::mutex> lock(s_native_table_lock); const auto &o = s_native_table_owner;
      if (o.active && (!native_table_package_current(o) || !native_table_record_matches(o, g))) stale = o.token; }
    if (stale) native_table_release("slot-payload-match-or-page-changed", true, stale);
    const int slot = ctx == 0x00B61B50u ? 0 : ctx == 0x00B62A70u ? 1 : -1;
    if (!s_native_table_page_permitted.load() || slot < 0 || !g.valid || pk != s_native_table_host_package.load() ||
        pk <= 0 || pk >= (int)s_pkgs.size() || !s_pkgs[pk].html || !s_pkgs[pk].native_tables_ack ||
        rd32(ctx) != (uint32_t)slot || rd32(ctx + 4u) != 1 || rd32(ctx + 0xCu) ||
        rd32(ctx + 0xF08u) || rd32(ctx + 0xF1Cu) != 1 || rd32(ctx + 0x1D8u) != 5) return;
    NativeTableOwner o;
    if (!native_table_payload(ctx, g, o)) return;
    o.active = true; o.ctx = ctx; o.slot = slot; o.package = pk; o.generation = s_pregame_host_generation.load();
    o.clock = rd32(0x00E6028Cu); o.resource = rd32(ctx + 0x1DCu); o.scene = o.resource ? rd32(o.resource) : 0;
    o.callback = rd32(ctx + 0xF14u);
    for (int s = 0; s < 2; ++s) o.rosters[s] = rd32(k_team_obj[s] + 0x1Cu);
    if (!native_table_record_matches(o, g)) return;
    { std::lock_guard<std::mutex> lock(s_native_table_lock);
      if (s_native_table_owner.active) return; /* One plate; never alternate two actual slots. */
      do { o.token = ++s_native_table_token; } while (!o.token);
      s_native_table_owner = o; }
    if (presentation_trace_enabled() && getenv("NFL2K5_REC")) xbox_PresentRecord(8);
    fprintf(stderr, "[NATIVE-TABLE] accepted t=%llu token=%u slot=%d layout=%u columns=%u rows=%u subjects=%u title='%s'\n",
        (unsigned long long)GetTickCount64(), o.token, o.slot, o.layout, o.columns, o.rows,
        (unsigned)o.subjects.size(), o.title.c_str());
}

static void native_table_slot_closing(int slot)
{
    NativeTableOwner old;
    { std::lock_guard<std::mutex> lock(s_native_table_lock); auto &o = s_native_table_owner;
      if (!o.active || o.closing || o.slot != slot) return;
      o.closing = true; old = o; }
    native_table_event(old, true);
}

static void native_table_hud_context(const GameState &g, const HtmlContext &cx)
{
    s_native_table_page_permitted = g.valid && !cx.paused && !cx.play_selection ? 1 : 0;
    uint32_t stale = 0; NativeTableOwner delivery;
    { std::lock_guard<std::mutex> lock(s_native_table_lock); auto &o = s_native_table_owner;
      if (!o.active) return;
      if (!s_native_table_page_permitted.load() || !native_table_package_current(o) || !native_table_record_matches(o, g)) stale = o.token;
      else if (!o.closing && !o.queued && !o.delivered) { o.queued = true; delivery = o; } }
    if (stale) native_table_release("slot-payload-match-page-or-context-changed", true, stale);
    else if (delivery.active) native_table_event(delivery, false);
}

static bool native_table_event_current(const PendingPopupEvent &ev, const GameState &g)
{
    if (!ev.table_token) return true;
    if (ev.table_package != s_sel_pkg.load() || ev.table_package != s_native_table_host_package.load() ||
        ev.table_generation != s_pregame_host_generation.load()) return false;
    if (ev.name == "NATIVE_TABLE_HIDE") return true;
    std::lock_guard<std::mutex> lock(s_native_table_lock); auto &o = s_native_table_owner;
    if (o.token != ev.table_token || !native_table_package_current(o) || ev.extra != native_table_make_event(o, false).extra) return false;
    o.queued = false;
    if (o.closing || !s_native_table_page_permitted.load() || !native_table_record_matches(o, g)) return false;
    o.delivered = true; return true;
}

static void native_table_page_message(int package, uint32_t generation, const std::string &message)
{
    const char *show = "NFL2K5_NATIVE_TABLE_SHOW:", *hide = "NFL2K5_NATIVE_TABLE_HIDE:";
    bool visible; size_t prefix;
    if (message.compare(0, strlen(show), show) == 0) { visible = true; prefix = strlen(show); }
    else if (message.compare(0, strlen(hide), hide) == 0) { visible = false; prefix = strlen(hide); }
    else return;
    if (message.size() <= prefix || message.size() - prefix > 10 || package != s_sel_pkg.load() ||
        package != s_native_table_host_package.load() || generation != s_pregame_host_generation.load()) return;
    uint64_t token = 0;
    for (size_t i = prefix; i < message.size(); ++i) {
        unsigned char c = (unsigned char)message[i]; if (c < '0' || c > '9') return;
        token = token * 10 + c - '0';
    }
    if (!token || token > UINT32_MAX || message.substr(prefix) != std::to_string(token)) return;
    /* Browser lane updates only copied synchronized flags; zero guest reads. */
    std::lock_guard<std::mutex> lock(s_native_table_lock); auto &o = s_native_table_owner;
    if (!o.active || o.token != token || o.package != package || o.generation != generation || !o.delivered || o.closing) return;
    const bool acknowledged = visible && s_native_table_page_permitted.load();
    if (o.acknowledged != acknowledged)
        fprintf(stderr, "[NATIVE-TABLE] page t=%llu token=%u visible=%u\n",
            (unsigned long long)GetTickCount64(), o.token, (unsigned)acknowledged);
    o.acknowledged = acknowledged;
}

static int native_table_draw_replaced(uint32_t ctx)
{
    if (!s_native_table_page_permitted.load()) return 0;
    {
        std::lock_guard<std::mutex> lock(s_native_table_lock);
        if (!s_native_table_owner.active || s_native_table_owner.ctx != ctx || !s_native_table_owner.acknowledged) return 0;
    }
    GameState g; if (!read_state(g)) return 0;
    std::lock_guard<std::mutex> lock(s_native_table_lock); const auto &o = s_native_table_owner;
    return o.ctx == ctx && o.acknowledged && native_table_package_current(o) && native_table_record_matches(o, g) ? 1 : 0;
}

static void native_table_reset()
{
    s_native_table_page_permitted = 0; s_native_table_host_package = -1;
    native_table_release("page-reset", false);
}
