/* uwp_css_compat.h -- CSS rewrites for the EdgeHTML WebView (uwp_webview_host.cpp).
 * Header-only so tools/uwp/test-css-compat.cpp can test it. */
#pragma once
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace uwp_css {

/* EdgeHTML 18 does not know the CSS `inset` shorthand, which the packages use
 * a lot (57 times, 2026-10-31): rewrite "inset: a [b [c [d]]]" into
 * top/right/bottom/left as the stylesheets and inline styles are served. */
inline void expand_inset(std::string &css)
{
    std::string out;
    out.reserve(css.size() + 256);
    size_t i = 0;
    while (i < css.size()) {
        size_t at = css.find("inset", i);
        if (at == std::string::npos) break;
        size_t p = at + 5;
        while (p < css.size() && (css[p] == ' ' || css[p] == '\t')) p++;
        bool word_start = at == 0 || !(isalnum((unsigned char)css[at - 1]) || css[at - 1] == '-');
        if (!word_start || p >= css.size() || css[p] != ':') {
            out.append(css, i, at + 5 - i);
            i = at + 5;
            continue;
        }
        size_t end = css.find_first_of(";}\"", p + 1);
        if (end == std::string::npos) end = css.size();
        std::string value = css.substr(p + 1, end - p - 1);
        std::string important;
        size_t imp = value.find("!important");
        if (imp != std::string::npos) { important = " !important"; value.erase(imp); }
        std::vector<std::string> v;
        for (size_t k = 0; k < value.size();) {
            while (k < value.size() && isspace((unsigned char)value[k])) k++;
            size_t s = k;
            int depth = 0;
            while (k < value.size() && (depth > 0 || !isspace((unsigned char)value[k]))) {
                if (value[k] == '(') depth++;
                if (value[k] == ')') depth--;
                k++;
            }
            if (k > s) v.push_back(value.substr(s, k - s));
        }
        if (v.empty() || v.size() > 4) {
            out.append(css, i, end - i);
            i = end;
            continue;
        }
        const std::string &t = v[0], &r = v.size() > 1 ? v[1] : v[0], &b = v.size() > 2 ? v[2] : v[0],
                          &l = v.size() > 3 ? v[3] : (v.size() > 1 ? v[1] : v[0]);
        out.append(css, i, at - i);
        out += "top:" + t + important + ";right:" + r + important + ";bottom:" + b + important + ";left:" + l + important;
        i = end;
    }
    out.append(css, i, std::string::npos);
    css.swap(out);
}

/* A literal sRGB colour: #rgb, #rrggbb, #rrggbbaa, rgb()/rgba() with numbers,
 * white/black/transparent. */
inline bool parse_color(std::string s, float c[4])
{
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    while (!s.empty() && isspace((unsigned char)s.front())) s.erase(0, 1);
    for (auto &ch : s) ch = (char)tolower((unsigned char)ch);
    c[3] = 1;
    if (s == "white") { c[0] = c[1] = c[2] = 255; return true; }
    if (s == "black") { c[0] = c[1] = c[2] = 0; return true; }
    if (s == "transparent") { c[0] = c[1] = c[2] = 0; c[3] = 0; return true; }
    if (s.size() > 1 && s[0] == '#') {
        std::string h = s.substr(1);
        for (char ch : h) if (!isxdigit((unsigned char)ch)) return false;
        if (h.size() == 3 || h.size() == 4) {
            std::string e;
            for (char ch : h) { e += ch; e += ch; }
            h = e;
        }
        if (h.size() != 6 && h.size() != 8) return false;
        for (int k = 0; k < (int)h.size() / 2; k++) {
            float v = (float)std::stoi(h.substr(k * 2, 2), nullptr, 16);
            if (k < 3) c[k] = v; else c[3] = v / 255.0f;
        }
        return true;
    }
    if (s.compare(0, 4, "rgb(") == 0 || s.compare(0, 5, "rgba(") == 0) {
        size_t a = s.find('('), b = s.rfind(')');
        if (b == std::string::npos || b < a) return false;
        std::string in = s.substr(a + 1, b - a - 1);
        for (auto &ch : in) if (ch == ',' || ch == '/') ch = ' ';
        float v[4] = {0, 0, 0, 1};
        int n = 0;
        for (size_t k = 0; k < in.size() && n < 4;) {
            while (k < in.size() && isspace((unsigned char)in[k])) k++;
            size_t st = k;
            while (k < in.size() && !isspace((unsigned char)in[k])) k++;
            if (k == st) break;
            std::string tok = in.substr(st, k - st);
            char *end = nullptr;
            float f = strtof(tok.c_str(), &end);
            if (end == tok.c_str()) return false;
            if (*end == '%') f = n < 3 ? f * 2.55f : f / 100.0f;
            v[n++] = f;
        }
        if (n < 3) return false;
        for (int k = 0; k < 4; k++) c[k] = v[k];
        return true;
    }
    return false;
}

/* EdgeHTML 18 has no color-mix(), and drops every declaration that uses it:
 * the packages' card bars and glows lost their backgrounds (26 uses in the
 * SNF scorebug, 2026-10-31). Two literal colours are mixed here exactly;
 * a mix involving var() (a team colour) becomes its larger share. */
inline void fallback_color_mix(std::string &css)
{
    for (size_t at = css.rfind("color-mix("); at != std::string::npos;
         at = at ? css.rfind("color-mix(", at - 1) : std::string::npos) {
        size_t open = at + 9, k = open + 1;
        int depth = 1;
        std::vector<std::string> parts(1);
        for (; k < css.size() && depth > 0; k++) {
            char ch = css[k];
            if (ch == '(') depth++;
            else if (ch == ')') { if (--depth == 0) break; }
            if (ch == ',' && depth == 1) { parts.emplace_back(); continue; }
            parts.back() += ch;
        }
        if (depth != 0 || parts.size() != 3) continue;
        float pct[2] = {-1, -1};
        std::string col[2];
        for (int j = 0; j < 2; j++) {
            std::string p = parts[j + 1];
            while (!p.empty() && isspace((unsigned char)p.back())) p.pop_back();
            size_t sp = p.find_last_of(" \t");
            if (!p.empty() && p.back() == '%' && sp != std::string::npos) {
                pct[j] = strtof(p.c_str() + sp + 1, nullptr);
                p.erase(sp);
            }
            col[j] = p;
        }
        if (pct[0] < 0 && pct[1] < 0) pct[0] = pct[1] = 50;
        else if (pct[0] < 0) pct[0] = 100 - pct[1];
        else if (pct[1] < 0) pct[1] = 100 - pct[0];
        float a[4], b[4];
        std::string repl;
        float tot = pct[0] + pct[1];
        if (tot > 0 && parse_color(col[0], a) && parse_color(col[1], b)) {
            float w = pct[0] / tot, m[4];
            for (int j = 0; j < 4; j++) m[j] = a[j] * w + b[j] * (1 - w);
            char buf[64];
            snprintf(buf, sizeof buf, "rgba(%d,%d,%d,%.3g)", (int)(m[0] + 0.5f), (int)(m[1] + 0.5f), (int)(m[2] + 0.5f), m[3]);
            repl = buf;
        } else {
            repl = pct[0] >= pct[1] ? col[0] : col[1];
            while (!repl.empty() && isspace((unsigned char)repl.front())) repl.erase(0, 1);
        }
        css.replace(at, k + 1 - at, repl);
    }
}

/* No conic-gradient() either: the same colour stops left to right. */
inline void fallback_conic(std::string &css)
{
    const std::string key = "conic-gradient(";
    for (size_t at = css.find(key); at != std::string::npos; at = css.find(key, at + 1)) {
        size_t a = at + key.size();
        while (a < css.size() && isspace((unsigned char)css[a])) a++;
        if (css.compare(a, 4, "from") == 0 || css.compare(a, 2, "at") == 0) continue;
        css.replace(at, key.size(), "linear-gradient(90deg,");
    }
}

/* An SVG with a viewBox but no width/height has no intrinsic size; EdgeHTML
 * then draws it far too small inside an <img> with object-fit (the SNF
 * wordmark, 2026-10-31). Give the root element the viewBox's size. */
inline void svg_intrinsic_size(std::string &svg)
{
    size_t at = svg.find("<svg");
    if (at == std::string::npos) return;
    size_t end = svg.find('>', at);
    if (end == std::string::npos) return;
    std::string tag = svg.substr(at, end - at);
    auto has_attr = [&](const char *name) {
        for (size_t p = tag.find(name); p != std::string::npos; p = tag.find(name, p + 1)) {
            char before = p ? tag[p - 1] : ' ';
            size_t q = p + strlen(name);
            while (q < tag.size() && tag[q] == ' ') q++;
            if (isspace((unsigned char)before) && q < tag.size() && tag[q] == '=') return true;
        }
        return false;
    };
    if (has_attr("width") || has_attr("height")) return;
    size_t vb = tag.find("viewBox");
    if (vb == std::string::npos) return;
    size_t q1 = tag.find_first_of("\"'", vb);
    if (q1 == std::string::npos) return;
    size_t q2 = tag.find(tag[q1], q1 + 1);
    if (q2 == std::string::npos) return;
    float v[4] = {0, 0, 0, 0};
    std::string box = tag.substr(q1 + 1, q2 - q1 - 1);
    for (auto &ch : box) if (ch == ',') ch = ' ';
    if (sscanf(box.c_str(), "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]) != 4 || v[2] <= 0 || v[3] <= 0) return;
    char attrs[64];
    snprintf(attrs, sizeof attrs, " width=\"%g\" height=\"%g\"", v[2], v[3]);
    svg.insert(at + 4, attrs);
}

/* A gradient colour stop with two positions ("#fff 0 38%") is newer than
 * EdgeHTML 18, which then drops the whole background: the SNF clock digits
 * (gradient text) vanished (2026-10-31). Write it as two stops. */
inline void split_double_stops(std::string &css)
{
    static const char *fns[] = {"linear-gradient(", "radial-gradient(", "repeating-linear-gradient(",
                                "repeating-radial-gradient("};
    auto is_pos = [](const std::string &t) {
        if (t.empty()) return false;
        char c = t[0];
        return isdigit((unsigned char)c) || c == '.' || c == '-' || c == '+' || t.compare(0, 5, "calc(") == 0;
    };
    for (const char *fn : fns) {
        size_t flen = strlen(fn);
        for (size_t at = css.find(fn); at != std::string::npos; at = css.find(fn, at + 1)) {
            if (at > 0 && (isalnum((unsigned char)css[at - 1]) || css[at - 1] == '-')) continue;
            size_t open = at + flen - 1, k = open + 1;
            int depth = 1;
            for (; k < css.size() && depth > 0; k++) {
                if (css[k] == '(') depth++;
                else if (css[k] == ')') depth--;
            }
            if (depth != 0) break;
            size_t close = k - 1;
            std::string inner = css.substr(open + 1, close - open - 1), out;
            std::vector<std::string> parts(1);
            depth = 0;
            for (char ch : inner) {
                if (ch == '(') depth++;
                else if (ch == ')') depth--;
                if (ch == ',' && depth == 0) { parts.emplace_back(); continue; }
                parts.back() += ch;
            }
            bool changed = false;
            for (size_t p = 0; p < parts.size(); p++) {
                std::vector<std::string> tok;
                depth = 0;
                std::string cur;
                for (char ch : parts[p]) {
                    if (ch == '(') depth++;
                    else if (ch == ')') depth--;
                    if (isspace((unsigned char)ch) && depth == 0) { if (!cur.empty()) tok.push_back(cur), cur.clear(); continue; }
                    cur += ch;
                }
                if (!cur.empty()) tok.push_back(cur);
                if (p) out += ',';
                bool shape = p == 0 && (parts[p].find(" at ") != std::string::npos || parts[p].find("circle") != std::string::npos ||
                                        parts[p].find("ellipse") != std::string::npos || parts[p].find("closest") != std::string::npos ||
                                        parts[p].find("farthest") != std::string::npos);
                if (!shape && tok.size() >= 3 && !is_pos(tok[0]) && is_pos(tok[tok.size() - 2]) && is_pos(tok.back())) {
                    std::string col;
                    for (size_t t = 0; t + 2 < tok.size(); t++) col += (t ? " " : "") + tok[t];
                    out += col + " " + tok[tok.size() - 2] + "," + col + " " + tok.back();
                    changed = true;
                } else {
                    out += parts[p];
                }
            }
            if (changed) css.replace(open + 1, close - open - 1, out);
        }
    }
}

/* Everything the WebView host applies to served HTML and CSS. */
inline void edgehtml_compat(std::string &css)
{
    expand_inset(css);
    split_double_stops(css);
    fallback_color_mix(css);
    fallback_conic(css);
}

} // namespace uwp_css
