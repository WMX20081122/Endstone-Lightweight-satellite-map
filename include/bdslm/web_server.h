#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <filesystem>

namespace httplib {
class Server;
}

namespace bdslm {

class ConfigManager;
class PlayerTracker;

class WebServer {
public:
    WebServer(ConfigManager &config, PlayerTracker &tracker);
    ~WebServer();

    bool start();
    void stop();
    bool isRunning() const { return running_; }

private:
    ConfigManager &config_;
    PlayerTracker &tracker_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace bdslm
