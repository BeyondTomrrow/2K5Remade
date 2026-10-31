/* uwp_css_compat.h -- CSS rewrites for the EdgeHTML WebView (uwp_webview_host.cpp).
 * Header-only so tools/uwp/test-css-compat.cpp can test it. */
#pragma once
#include <cctype>
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

} // namespace uwp_css
