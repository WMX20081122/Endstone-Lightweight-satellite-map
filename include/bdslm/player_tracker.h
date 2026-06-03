#pragma once

#include <string>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace bdslm {

struct PlayerInfo {
    std::string name;
    double x = 0, y = 0, z = 0;
    std::string dimension = "overworld";
};

class PlayerTracker {
public:
    PlayerTracker() = default;

    void onJoin(const std::string &name);
    void onQuit(const std::string &name);
    void onMove(const std::string &name, double x, double y, double z, const std::string &dim);

    void updateFile(const std::filesystem::path &path);
    nlohmann::json toJson() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, PlayerInfo> players_;
};

}  // namespace bdslm
