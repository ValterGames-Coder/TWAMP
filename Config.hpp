#include <fstream>
#include <sstream>
#include <string>

struct Config 
{
    std::string ip = "0.0.0.0";
    int port = 862;
};

Config load_config(const std::string& filename)
{
    
}