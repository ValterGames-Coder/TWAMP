#ifndef TWAMP_CONFIG_H
#define TWAMP_CONFIG_H

#include <string>
#include <unordered_map>
#include <vector>

class Config {
public:
    Config(const std::string& filename);
    bool load();
    
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    
    std::vector<std::string> getKeys() const;

private:
    std::string filename_;
    std::unordered_map<std::string, std::string> settings_;
    
    void parseLine(const std::string& line);
    void trim(std::string& str) const;
};

#endif // TWAMP_CONFIG_H