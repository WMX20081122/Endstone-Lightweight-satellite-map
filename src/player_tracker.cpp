#include "bdslm/player_tracker.h"
#include <fstream>

namespace bdslm {

void PlayerTracker::onJoin(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);
    players_[name] = PlayerInfo{name, 0, 0, 0, "overworld"};
}

void PlayerTracker::onQuit(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);
    players_.erase(name);
}

void PlayerTracker::onMove(const std::string &name, double x, double y, double z, const std::string &dim) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (players_.count(name)) {
        auto &p = players_[name];
        p.x = x;
        p.y = y;
        p.z = z;
        p.dimension = dim;
    }
}

nlohmann::json PlayerTracker::toJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &[name, info] : players_) {
        arr.push_back({
            {"name", info.name},
            {"x", info.x},
            {"y", info.y},
            {"z", info.z},
            {"dimension", info.dimension}
        });
    }
    return arr;
}

void PlayerTracker::updateFile(const std::filesystem::path &path) {
    try {
        auto json = toJson();
        std::ofstream f(path);
        f << json.dump();
    } catch (const std::exception &) {}
}

}  // namespace bdslm