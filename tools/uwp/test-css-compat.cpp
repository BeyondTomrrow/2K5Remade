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
