#include "Config.h"
#include <fstream>
#include <algorithm>
#include <cctype>

Config::Config(const std::string& filename) : filename_(filename) {}

bool Config::load() {
    std::ifstream file(filename_);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        parseLine(line);
    }
    
    return true;
}

void Config::parseLine(const std::string& line) {
    std::string trimmed = line;
    trim(trimmed);
    
    if (trimmed.empty() || trimmed[0] == '#') {
        return;
    }
    
    size_t equalsPos = trimmed.find('=');
    if (equalsPos == std::string::npos) {
        return;
    }
    
    std::string key = trimmed.substr(0, equalsPos);
    std::string value = trimmed.substr(equalsPos + 1);
    
    trim(key);
    trim(value);
    
    if (!key.empty()) {
        settings_[key] = value;
    }
}

std::string Config::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = settings_.find(key);
    return it != settings_.end() ? it->second : defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) const {
    auto it = settings_.find(key);
    if (it == settings_.end()) {
        return defaultValue;
    }
    
    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    auto it = settings_.find(key);
    if (it == settings_.end()) {
        return defaultValue;
    }
    
    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    
    return value == "true" || value == "yes" || value == "1";
}

std::vector<std::string> Config::getKeys() const {
    std::vector<std::string> keys;
    for (const auto& pair : settings_) {
        keys.push_back(pair.first);
    }
    return keys;
}

void Config::trim(std::string& str) const {
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](int ch) {
        return !std::isspace(ch);
    }));
    
    str.erase(std::find_if(str.rbegin(), str.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), str.end());
}