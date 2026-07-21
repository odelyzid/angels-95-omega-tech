#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

class IniConfig {
public:
    using Section = std::unordered_map<std::string, std::string>;
    using Data = std::unordered_map<std::string, Section>;

    Data data;

    bool Load(const char* path) {
        data.clear();
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::string line, currentSection;
        while (std::getline(f, line)) {
            // Trim
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            size_t end = line.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) line = line.substr(0, end + 1);
            // Skip empty / comment
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            // Section header
            if (line[0] == '[') {
                size_t close = line.find(']');
                if (close != std::string::npos)
                    currentSection = line.substr(1, close - 1);
                continue;
            }
            // Key=Value
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                val.erase(0, val.find_first_not_of(" \t"));
                val.erase(val.find_last_not_of(" \t\r") + 1);
                if (!currentSection.empty())
                    data[currentSection][key] = val;
            }
        }
        return true;
    }

    bool Save(const char* path) {
        std::ofstream f(path);
        if (!f.is_open()) return false;
        for (auto& [section, kv] : data) {
            f << "[" << section << "]" << std::endl;
            for (auto& [key, val] : kv)
                f << key << "=" << val << std::endl;
            f << std::endl;
        }
        return true;
    }

    // Getters with default
    std::string Get(const char* section, const char* key, const char* def = "") {
        if (data.count(section) && data[section].count(key))
            return data[section][key];
        return def;
    }

    int GetInt(const char* section, const char* key, int def = 0) {
        std::string v = Get(section, key);
        return v.empty() ? def : std::stoi(v);
    }

    float GetFloat(const char* section, const char* key, float def = 0.0f) {
        std::string v = Get(section, key);
        return v.empty() ? def : std::stof(v);
    }

    bool GetBool(const char* section, const char* key, bool def = false) {
        std::string v = Get(section, key);
        if (v.empty()) return def;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        return v == "true" || v == "1" || v == "yes";
    }

    // Setters
    void Set(const char* section, const char* key, const std::string& val) {
        data[section][key] = val;
    }

    void SetInt(const char* section, const char* key, int val) {
        data[section][key] = std::to_string(val);
    }

    void SetFloat(const char* section, const char* key, float val) {
        data[section][key] = std::to_string(val);
    }

    void SetBool(const char* section, const char* key, bool val) {
        data[section][key] = val ? "True" : "False";
    }
};
