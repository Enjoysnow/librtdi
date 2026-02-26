/// @file basic_usage.cpp
/// Demonstrates the core features of librtdi.

#include <librtdi.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------
// Interface definitions
// ---------------------------------------------------------------

struct ILogger {
    virtual ~ILogger() = default;
    virtual void log(const std::string& msg) const = 0;
};

struct IRepository {
    virtual ~IRepository() = default;
    virtual std::string fetch(int id) const = 0;
};

struct IService {
    virtual ~IService() = default;
    virtual void run() = 0;
};

// ---------------------------------------------------------------
// Implementations
// ---------------------------------------------------------------

struct ConsoleLogger : ILogger {
    void log(const std::string& msg) const override {
        std::cout << "[LOG] " << msg << "\n";
    }
};

struct InMemoryRepository : IRepository {
    std::string fetch(int id) const override {
        return "Item-" + std::to_string(id);
    }
};

/// Service depends on ILogger (singleton ref) and IRepository (singleton ref).
struct AppService : IService {
    ILogger& logger_;
    IRepository& repo_;

    AppService(ILogger& logger, IRepository& repo)
        : logger_(logger), repo_(repo) {}

    void run() override {
        logger_.log("Fetching item 42...");
        auto result = repo_.fetch(42);
        logger_.log("Got: " + result);
    }
};

// ---------------------------------------------------------------
// Plugin system (collection demo)
// ---------------------------------------------------------------

struct IPlugin {
    virtual ~IPlugin() = default;
    virtual std::string name() const = 0;
};

struct PluginA : IPlugin {
    std::string name() const override { return "Alpha"; }
};

struct PluginB : IPlugin {
    std::string name() const override { return "Beta"; }
};

/// Aggregates all plugins.
struct PluginManager {
    virtual ~PluginManager() = default;
    std::vector<IPlugin*> plugins_;

    explicit PluginManager(std::vector<IPlugin*> plugins)
        : plugins_(std::move(plugins)) {}

    void list_all() const {
        std::cout << "Loaded plugins:\n";
        for (auto* p : plugins_) {
            std::cout << "  - " << p->name() << "\n";
        }
    }
};

// ---------------------------------------------------------------
// Decorator demo
// ---------------------------------------------------------------

struct TimingLogger : ILogger {
    librtdi::decorated_ptr<ILogger> inner_;

    explicit TimingLogger(librtdi::decorated_ptr<ILogger> inner)
        : inner_(std::move(inner)) {}

    void log(const std::string& msg) const override {
        std::cout << "[TIMING] ";
        inner_->log(msg);
    }
};

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main() {
    using namespace librtdi;

    registry reg;

    // Singleton services
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_singleton<IRepository, InMemoryRepository>();
    reg.add_singleton<IService, AppService>(deps<ILogger, IRepository>);

    // Collection of plugins (singleton lifetime)
    reg.add_collection<IPlugin, PluginA>(lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginB>(lifetime_kind::singleton);

    // PluginManager depends on collection<IPlugin>
    reg.add_singleton<PluginManager, PluginManager>(deps<collection<IPlugin>>);

    // Decorate ILogger with timing wrapper
    reg.decorate<ILogger, TimingLogger>();

    // Build and validate
    auto resolver = reg.build();

    // Use the services
    auto& svc = resolver->get<IService>();
    svc.run();

    std::cout << "\n";

    auto& pm = resolver->get<PluginManager>();
    pm.list_all();

    return 0;
}
