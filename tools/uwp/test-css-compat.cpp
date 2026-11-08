// Unit test for src/platform/uwp/uwp_css_compat.h (CSS rewrites for EdgeHTML),
// plus a run over every stylesheet/page in mods/presentations to show nothing
// else changes.  Build + run:
//   cl /nologo /EHsc /std:c++17 /I src\platform\uwp tools\uwp\test-css-compat.cpp && test-css-compat.exe mods\presentations
#include "uwp_css_compat.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

static int fails;
static void expect_mix(const std::string &in, const std::string &want)
{
    std::string s = in;
    uwp_css::fallback_color_mix(s);
    uwp_css::fallback_conic(s);
    bool ok = s == want;
    if (!ok) fails++;
    printf("%s %s\n   -> %s\n", ok ? "PASS" : "FAIL", in.c_str(), s.c_str());
    if (!ok) printf("   want %s\n", want.c_str());
}
static void expect(const std::string &in, const std::string &want)
{
    std::string s = in;
    uwp_css::expand_inset(s);
    bool ok = s == want;
    if (!ok) fails++;
    printf("%s %s\n   -> %s\n", ok ? "PASS" : "FAIL", in.c_str(), s.c_str());
    if (!ok) printf("   want %s\n", want.c_str());
}

int main(int argc, char **argv)
{
    expect(".a{inset:0}", ".a{top:0;right:0;bottom:0;left:0}");
    expect(".a{position:absolute; inset: 10px 20px;}", ".a{position:absolute; top:10px;right:20px;bottom:10px;left:20px;}");
    expect(".a{inset:1px 2px 3px}", ".a{top:1px;right:2px;bottom:3px;left:2px}");
    expect(".a{inset:1px 2px 3px 4px !important}", ".a{top:1px !important;right:2px !important;bottom:3px !important;left:4px !important}");
    expect(".a{inset:calc(50% - 10px) auto}", ".a{top:calc(50% - 10px);right:auto;bottom:calc(50% - 10px);left:auto}");
    expect(".a{box-shadow:inset 0 0 4px #000}", ".a{box-shadow:inset 0 0 4px #000}");
    expect(".a{inset-inline:0}", ".a{inset-inline:0}");
    expect("<div style=\"inset:0;opacity:1\">", "<div style=\"top:0;right:0;bottom:0;left:0;opacity:1\">");
    expect_mix("a{color:color-mix(in srgb, #ff0000 50%, #0000ff)}", "a{color:rgba(128,0,128,1)}");
    expect_mix("a{color:color-mix(in srgb, white 25%, black)}", "a{color:rgba(64,64,64,1)}");
    expect_mix("a{background:color-mix(in srgb, var(--t) 45%, #12151b)}", "a{background:#12151b}");
    expect_mix("a{background:color-mix(in srgb,var(--g) 55%,#c9dcff)}", "a{background:var(--g)}");
    expect_mix("a{b:color-mix(in srgb, color-mix(in srgb, #fff 50%, #000) 50%, #000)}", "a{b:rgba(64,64,64,1)}");
    expect_mix("a{background:conic-gradient(#7137a7,#168ed2)}", "a{background:linear-gradient(90deg,#7137a7,#168ed2)}");
    { std::string s = "<svg xmlns='x' viewBox=\"0 0 460 120\">"; uwp_css::svg_intrinsic_size(s); bool ok = s == "<svg width=\"460\" height=\"120\" xmlns='x' viewBox=\"0 0 460 120\">"; if (!ok) fails++; printf("%s svg size -> %s\n", ok ? "PASS" : "FAIL", s.c_str()); }
    { std::string s = "<svg width=\"10\" viewBox=\"0 0 4 1\">", a = s; uwp_css::svg_intrinsic_size(s); bool ok = s == a; if (!ok) fails++; printf("%s svg keeps width -> %s\n", ok ? "PASS" : "FAIL", s.c_str()); }
    { std::string s = "a{background:linear-gradient(to bottom,#fdfdff 0 38%,#f9fcfe 44%)}"; uwp_css::split_double_stops(s); bool ok = s == "a{background:linear-gradient(to bottom,#fdfdff 0,#fdfdff 38%,#f9fcfe 44%)}"; if (!ok) fails++; printf("%s double stop -> %s\n", ok ? "PASS" : "FAIL", s.c_str()); }
    { std::string s = "a{background:radial-gradient(circle at 50% 50%,rgba(0,0,0,.5) 10% 20%,#000 30%)}"; uwp_css::split_double_stops(s); bool ok = s == "a{background:radial-gradient(circle at 50% 50%,rgba(0,0,0,.5) 10%,rgba(0,0,0,.5) 20%,#000 30%)}"; if (!ok) fails++; printf("%s radial -> %s\n", ok ? "PASS" : "FAIL", s.c_str()); }
    if (argc > 1) {
        int files = 0, changed = 0;
        for (auto &e : std::filesystem::recursive_directory_iterator(argv[1])) {
            auto ext = e.path().extension().string();
            if (ext != ".css" && ext != ".html") continue;
            std::ifstream in(e.path(), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            std::string a = ss.str(), b = a;
            uwp_css::expand_inset(b);
            files++;
            if (a != b) changed++;
            size_t left = 0;
            for (size_t p = b.find("inset"); p != std::string::npos; p = b.find("inset", p + 5)) {
                size_t q = p + 5;
                while (q < b.size() && b[q] == ' ') q++;
                if (q < b.size() && b[q] == ':' && (p == 0 || !(isalnum((unsigned char)b[p - 1]) || b[p - 1] == '-'))) left++;
            }
            if (left) { printf("FAIL %s: %zu inset: left\n", e.path().string().c_str(), left); fails++; }
        }
        printf("%d files, %d rewritten\n", files, changed);
    }
    printf(fails ? "%d failure(s)\n" : "all passed\n", fails);
    return fails;
}
