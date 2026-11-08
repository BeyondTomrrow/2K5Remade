/* Included by nfl2k5_presentation.cpp after its shared popup declarations.
 * Narrow ownership for actual accepted layout-14 injury/penalty instances.
 * The game retains all selection, football, timeout and completion code.
 * This file does not execute guest code or write guest memory. */
struct NativeNoticeCell {
    std::string text;
    uint32_t style = 0;
};
struct NativeNoticeRow {
    uint32_t callback = 0;
    std::vector<NativeNoticeCell> cells;
};
struct NativeNoticeOwner {
    bool active = false, queued = false, delivered = false, acknowledged = false, closing = false;
    bool interactive = false;
    uint32_t token = 0, generation = 0, ctx = 0, clock = 0, rosters[2]{};
    uint32_t callback = 0, resource = 0, scene = 0, selected = 0, input_side = 0, default_row = 0;
    int package = -1, slot = -1, phase = -1;
    std::string kind, title, text;
    std::vector<NativeNoticeRow> rows;
};
static std::mutex s_notice_lock;
static NativeNoticeOwner s_notice_owner;
static std::atomic<uint32_t> s_notice_token{0};
static std::atomic<int> s_notice_host_package{-1}, s_notice_page_permitted{0};

static bool native_notice_text(uint32_t va, std::string &out, bool allow_empty)
{
    uint16_t first = 0;
    if (!gread(va, &first, sizeof first)) return false;
    if (!first) { out.clear(); return allow_empty; }
    out = gtext(va, 64);
    return !out.empty(); /* Includes malformed, non-ASCII and unterminated rejection. */
}

static bool native_notice_payload(uint32_t ctx, NativeNoticeOwner &o)
{
    uint32_t count = rd32(ctx + 0x230u);
    if (!count || count > 2 || rd32(ctx + 0x228u) != 2 || rd32(ctx + 0x40u) != 0 ||
        !native_notice_text(ctx + 0x4Cu, o.title, false) ||
        !native_notice_text(ctx + 0xCCu, o.text, false)) return false;
    o.selected = rd32(ctx + 0x22Cu); o.input_side = rd32(ctx + 0x684u);
    o.default_row = rd32(ctx + 0x688u); o.callback = rd32(ctx + 0xF14u);
    if (o.selected >= count || o.default_row >= count || o.input_side > 2) return false;
    o.rows.clear();
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t row = ctx + 0x234u + i * 0x228u;
        const uint32_t cells = rd32(row + 0x224u);
        if (!cells || cells > 3) return false;
        NativeNoticeRow value; value.callback = rd32(row);
        for (uint32_t j = 0; j < cells; ++j) {
            NativeNoticeCell cell; cell.style = rd32(row + 0x84u + j * 0x88u);
            if (cell.style < 1 || cell.style > 3 ||
                !native_notice_text(row + 4u + j * 0x88u, cell.text, j != 0)) return false;
            value.cells.push_back(std::move(cell));
        }
        o.rows.push_back(std::move(value));
    }
    o.interactive = false;
    if (o.callback == 0x00117770u) {
        if (count != 1 || o.rows[0].cells.size() != 1 || o.rows[0].callback || o.input_side) return false;
        o.kind = "injury";
    } else if (o.callback == 0x001180C0u) {
        if (count != 2 || o.input_side || o.rows[0].callback || o.rows[1].callback) return false;
        const size_t columns = o.rows[0].cells.size();
        if ((columns != 1 && columns != 2) || o.rows[1].cells.size() != columns) return false;
        o.kind = "penalty_result";
    } else {
        if (count != 2 || o.rows[0].callback != 0x00117C10u || o.rows[1].callback != 0x00117C30u ||
            o.rows[0].cells.size() != 3 || o.rows[1].cells.size() != 3 ||
            (o.callback != 0 && o.callback != 0x00117C10u && o.callback != 0x00117C30u)) return false;
        o.kind = "penalty_choice"; o.interactive = true;
    }
    return true;
}

static bool native_notice_package_current(const NativeNoticeOwner &o)
{
    int pk = s_sel_pkg.load();
    return o.package == pk && pk == s_notice_host_package.load() && pk > 0 && pk < (int)s_pkgs.size() &&
        o.generation == s_pregame_host_generation.load() && s_pkgs[pk].html && s_pkgs[pk].notice_cards_ack;
}

static bool native_notice_same_payload(const NativeNoticeOwner &a, const NativeNoticeOwner &b)
{
    if (a.kind != b.kind || a.title != b.title || a.text != b.text || a.interactive != b.interactive ||
        a.input_side != b.input_side || a.default_row != b.default_row || a.rows.size() != b.rows.size()) return false;
    for (size_t i = 0; i < a.rows.size(); ++i) {
        if (a.rows[i].callback != b.rows[i].callback || a.rows[i].cells.size() != b.rows[i].cells.size()) return false;
        for (size_t j = 0; j < a.rows[i].cells.size(); ++j)
            if (a.rows[i].cells[j].text != b.rows[i].cells[j].text ||
                a.rows[i].cells[j].style != b.rows[i].cells[j].style) return false;
    }
    return true;
}

static bool native_notice_record_matches(const NativeNoticeOwner &o, const GameState &g)
{
    if (!o.active || !g.valid || o.slot < 0 || o.slot > 1 || o.phase != g.phase ||
        o.ctx != 0x00B61B50u + (uint32_t)o.slot * 0xF20u || !o.clock ||
        o.clock != rd32(0x00E6028Cu) || rd32(o.ctx) != (uint32_t)o.slot || rd32(o.ctx + 4u) != 1 ||
        rd32(o.ctx + 0x48u) != 14 || rd32(o.ctx + 0xF08u) != 0 ||
        rd32(o.ctx + 0x1D8u) != 5 || rd32(o.ctx + 0xF1Cu) != 1 ||
        !o.resource || !o.scene || rd32(o.ctx + 0x1DCu) != o.resource || rd32(o.resource) != o.scene ||
        (o.closing ? rd32(o.ctx + 0xCu) == 0 : rd32(o.ctx + 0xCu) != 0)) return false;
    for (int i = 0; i < 2; ++i)
        if (!o.rosters[i] || o.rosters[i] != rd32(k_team_obj[i] + 0x1Cu)) return false;
    NativeNoticeOwner current;
    if (!native_notice_payload(o.ctx, current) || !native_notice_same_payload(o, current)) return false;
    if (current.callback != o.callback) {
        /* Native 1BE290 installs the chosen row's completion immediately
         * before 8A340. Only a closing, already-owned choice may accept it. */
        if (!o.interactive || !o.closing || o.callback != 0 ||
            current.callback != o.rows[current.selected].callback) return false;
    }
    return true;
}

static PendingPopupEvent native_notice_make_event(const NativeNoticeOwner &o, bool hide)
{
    PendingPopupEvent ev;
    ev.name = hide ? "BROADCAST_NOTICE_HIDE" : "BROADCAST_NOTICE";
    ev.notice_token = o.token; ev.notice_generation = o.generation; ev.notice_package = o.package;
    ev.extra = ",\"source\":\"native\",\"nativeVisible\":" + std::string(hide ? "false" : "true") +
        ",\"replacementToken\":" + std::to_string(o.token) + ",\"slot\":" + std::to_string(o.slot) +
        ",\"phase\":" + std::to_string(o.phase) + ",\"noticeKind\":" + json_str(o.kind) +
        ",\"interactive\":" + std::string(o.interactive ? "true" : "false") +
        ",\"title\":" + json_str(o.title) + ",\"text\":" + json_str(o.text) + ",\"choices\":[";
    std::string button = o.interactive && o.input_side ? native_confirm_prompt() : "";
    for (size_t i = 0; i < o.rows.size(); ++i) {
        if (i) ev.extra += ",";
        ev.extra += "{\"text\":" + json_str(o.rows[i].cells[0].text) + ",\"selected\":" +
            std::string(o.interactive && i == o.selected ? "true" : "false");
        if (!button.empty() && i == o.selected) ev.extra += ",\"button\":" + json_str(button);
        ev.extra += ",\"cells\":[";
        for (size_t j = 1; j < o.rows[i].cells.size(); ++j) {
            if (j > 1) ev.extra += ",";
            ev.extra += json_str(o.rows[i].cells[j].text);
        }
        ev.extra += "]}";
    }
    ev.extra += "]";
    return ev;
}

static void native_notice_event(const NativeNoticeOwner &o, bool hide)
{
    PendingPopupEvent ev = native_notice_make_event(o, hide);
    std::lock_guard<std::mutex> lock(s_popup_lock); s_popup_events.push_back(std::move(ev));
}

static void native_notice_release(const char *reason, bool notify = true)
{
    NativeNoticeOwner old;
    { std::lock_guard<std::mutex> lock(s_notice_lock); old = s_notice_owner; s_notice_owner = {}; }
    if (!old.active) return;
    if (notify && (old.queued || old.delivered)) native_notice_event(old, true);
    fprintf(stderr, "[BROADCAST-NOTICE] release token=%u reason=%s\n", old.token, reason);
}

static void native_notice_frame(uint32_t ctx)
{
    int pk = s_sel_pkg.load();
    if (pk <= 0 || pk >= (int)s_pkgs.size() || pk != s_notice_host_package.load() ||
        !s_pkgs[pk].html || !s_pkgs[pk].notice_cards_ack) return;
    GameState g; if (!read_state(g)) return;
    bool stale = false;
    { std::lock_guard<std::mutex> lock(s_notice_lock);
      stale = s_notice_owner.active && (!native_notice_package_current(s_notice_owner) ||
          !native_notice_record_matches(s_notice_owner, g)); }
    if (stale) native_notice_release("native-slot-match-or-page-changed");
    int slot = ctx == 0x00B61B50u ? 0 : ctx == 0x00B62A70u ? 1 : -1;
    if (!s_notice_page_permitted.load() || slot < 0 || !g.valid || pk != s_notice_host_package.load() ||
        pk <= 0 || pk >= (int)s_pkgs.size() || !s_pkgs[pk].html || !s_pkgs[pk].notice_cards_ack ||
        rd32(ctx) != (uint32_t)slot || rd32(ctx + 4u) != 1 || rd32(ctx + 0xCu) != 0 ||
        rd32(ctx + 0x48u) != 14 || rd32(ctx + 0xF08u) || rd32(ctx + 0xF1Cu) != 1 ||
        rd32(ctx + 0x1D8u) != 5) return;
    NativeNoticeOwner fresh;
    if (!native_notice_payload(ctx, fresh) || (fresh.interactive && fresh.callback != 0)) return;
    fresh.active = true; fresh.ctx = ctx; fresh.slot = slot; fresh.package = pk;
    fresh.generation = s_pregame_host_generation.load(); fresh.phase = g.phase;
    fresh.clock = rd32(0x00E6028Cu); fresh.resource = rd32(ctx + 0x1DCu);
    fresh.scene = fresh.resource ? rd32(fresh.resource) : 0;
    for (int i = 0; i < 2; ++i) fresh.rosters[i] = rd32(k_team_obj[i] + 0x1Cu);
    if (!native_notice_record_matches(fresh, g)) return;
    NativeNoticeOwner update;
    { std::lock_guard<std::mutex> lock(s_notice_lock); auto &o = s_notice_owner;
      if (o.active) {
          if (o.ctx != ctx || o.closing) return; /* Do not alternate two live slots every frame. */
          if (o.selected != fresh.selected) {
              o.selected = fresh.selected;
              if (o.delivered || o.queued) { o.queued = true; update = o; }
          }
          if (!update.active) return;
      } else {
          do { fresh.token = ++s_notice_token; } while (!fresh.token);
          s_notice_owner = fresh;
      } }
    if (update.active) { native_notice_event(update, false); return; }
    fprintf(stderr, "[BROADCAST-NOTICE] accepted t=%llu token=%u slot=%d kind=%s callback=%08X rows=%u title='%s' text='%s'\n",
        (unsigned long long)GetTickCount64(), fresh.token, fresh.slot, fresh.kind.c_str(), fresh.callback,
        (unsigned)fresh.rows.size(), fresh.title.c_str(), fresh.text.c_str());
}

static void native_notice_slot_closing(int slot)
{
    NativeNoticeOwner old;
    { std::lock_guard<std::mutex> lock(s_notice_lock); auto &o = s_notice_owner;
      if (!o.active || o.closing || o.slot != slot) return;
      o.closing = true; old = o; }
    native_notice_event(old, true);
}

static void native_notice_hud_context(const GameState &g, const HtmlContext &cx)
{
    s_notice_page_permitted = g.valid && !cx.paused && !cx.play_selection ? 1 : 0;
    bool stale = false; NativeNoticeOwner delivery;
    { std::lock_guard<std::mutex> lock(s_notice_lock); auto &o = s_notice_owner;
      if (!o.active) return;
      stale = !s_notice_page_permitted.load() || !native_notice_package_current(o) || !native_notice_record_matches(o, g);
      if (!stale && !o.closing && !o.queued && !o.delivered) { o.queued = true; delivery = o; } }
    if (stale) native_notice_release("slot-match-page-or-context-changed");
    else if (delivery.active) native_notice_event(delivery, false);
}

static bool native_notice_event_current(const PendingPopupEvent &ev, const GameState &g)
{
    if (!ev.notice_token) return true;
    if (ev.notice_package != s_sel_pkg.load() || ev.notice_package != s_notice_host_package.load() ||
        ev.notice_generation != s_pregame_host_generation.load()) return false;
    if (ev.name == "BROADCAST_NOTICE_HIDE") return true;
    std::lock_guard<std::mutex> lock(s_notice_lock); auto &o = s_notice_owner;
    if (o.token != ev.notice_token || !native_notice_package_current(o) ||
        ev.extra != native_notice_make_event(o, false).extra) return false;
    o.queued = false;
    if (o.closing || !s_notice_page_permitted.load() || !native_notice_record_matches(o, g)) return false;
    o.delivered = true; return true;
}

static void native_notice_page_message(int package, uint32_t generation, const std::string &message)
{
    const char *show = "NFL2K5_NOTICE_SHOW:", *hide = "NFL2K5_NOTICE_HIDE:";
    bool visible; size_t prefix;
    if (message.compare(0, strlen(show), show) == 0) { visible = true; prefix = strlen(show); }
    else if (message.compare(0, strlen(hide), hide) == 0) { visible = false; prefix = strlen(hide); }
    else return;
    if (message.size() <= prefix || message.size() - prefix > 10 || package != s_sel_pkg.load() ||
        package != s_notice_host_package.load() || generation != s_pregame_host_generation.load()) return;
    uint64_t token = 0;
    for (size_t i = prefix; i < message.size(); ++i) {
        unsigned char c = (unsigned char)message[i]; if (c < '0' || c > '9') return;
        token = token * 10 + c - '0';
    }
    if (!token || token > UINT32_MAX || message.substr(prefix) != std::to_string(token)) return;
    /* Browser thread: copied synchronized host fields only, no RAM reads. */
    std::lock_guard<std::mutex> lock(s_notice_lock); auto &o = s_notice_owner;
    if (!o.active || o.token != token || o.package != package || o.generation != generation ||
        !o.delivered || o.closing) return;
    const bool acknowledged = visible && s_notice_page_permitted.load();
    if (o.acknowledged != acknowledged)
        fprintf(stderr, "[BROADCAST-NOTICE] page t=%llu token=%u visible=%u\n",
            (unsigned long long)GetTickCount64(), o.token, (unsigned)acknowledged);
    o.acknowledged = acknowledged;
}

static int native_notice_draw_replaced(uint32_t ctx)
{
    if (!s_notice_page_permitted.load()) return 0;
    {
        std::lock_guard<std::mutex> lock(s_notice_lock);
        if (!s_notice_owner.active || s_notice_owner.ctx != ctx || !s_notice_owner.acknowledged) return 0;
    }
    GameState g; if (!read_state(g)) return 0;
    std::lock_guard<std::mutex> lock(s_notice_lock); const auto &o = s_notice_owner;
    return o.ctx == ctx && o.acknowledged && native_notice_package_current(o) && native_notice_record_matches(o, g) ? 1 : 0;
}

static void native_notice_reset()
{
    s_notice_page_permitted = 0; s_notice_host_package = -1;
    native_notice_release("page-reset", false);
}
