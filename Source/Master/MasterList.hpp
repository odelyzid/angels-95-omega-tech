#ifndef OMEGA_MASTER_LIST_HPP
#define OMEGA_MASTER_LIST_HPP

// Master-server address discovery for the client. Reads
// System/Angels95.ini [MasterServers] (Master=comma,list and/or Master1,
// Master2, ...) and falls back to a localhost default for development.

#include "MasterProtocol.hpp"
#include "../IniConfig.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace master {

inline std::vector<std::string> DefaultMasterUrls() {
    return { "http://127.0.0.1:27950" };
}

inline void AppendCommaSeparated(const std::string& list, std::vector<std::string>& out) {
    std::string cur;
    for (char c : list) {
        if (c == ',') {
            std::string t = Trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    std::string t = Trim(cur);
    if (!t.empty()) out.push_back(t);
}

inline std::vector<std::string> LoadMasterUrls(const char* ini_path) {
    std::vector<std::string> urls;
    IniConfig cfg;
    if (cfg.Load(ini_path)) {
        const char* S = "MasterServers";
        AppendCommaSeparated(cfg.Get(S, "Master"), urls);
        for (int i = 1; i <= 32; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "Master%d", i);
            AppendCommaSeparated(cfg.Get(S, key), urls);
        }
    }

    std::vector<std::string> out;
    for (auto& u : urls) {
        bool dup = false;
        for (auto& e : out) if (e == u) { dup = true; break; }
        if (!dup) out.push_back(u);
    }
    if (out.empty()) out = DefaultMasterUrls();
    return out;
}

} // namespace master

#endif // OMEGA_MASTER_LIST_HPP
