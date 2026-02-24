#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>

namespace {

struct ILogger {
    virtual ~ILogger() = default;
    virtual std::string name() const = 0;
};

struct ConsoleLogger : ILogger {
    std::string name() const override { return "console"; }
};

struct IService {
    virtual ~IService() = default;
    virtual int value() const = 0;
};

// Service that depends on ILogger (singleton dep)
struct Service : IService {
    ILogger& logger_;
    explicit Service(ILogger& logger) : logger_(logger) {}
    int value() const override { return 42; }
};

// Service that depends on transient ILogger
struct TransientDepService : IService {
    std::unique_ptr<ILogger> logger_;
    explicit TransientDepService(std::unique_ptr<ILogger> logger)
        : logger_(std::move(logger)) {}
    int value() const override { return 99; }
};

} // namespace

TEST_CASE("auto-wire singleton dep via deps<>", "[auto_wiring]") {
    librtdi::registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_singleton<IService, Service>(librtdi::deps<ILogger>);
    auto r = reg.build();

    auto& svc = r->get<IService>();
    REQUIRE(svc.value() == 42);
}

TEST_CASE("auto-wire transient dep via deps<transient<>>", "[auto_wiring]") {
    librtdi::registry reg;
    reg.add_transient<ILogger, ConsoleLogger>();
    reg.add_transient<IService, TransientDepService>(
        librtdi::deps<librtdi::transient<ILogger>>);
    auto r = reg.build({.validate_lifetimes = false});

    auto svc = r->create<IService>();
    REQUIRE(svc != nullptr);
    REQUIRE(svc->value() == 99);
}

TEST_CASE("auto-wire singleton dep into transient impl", "[auto_wiring]") {
    librtdi::registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_transient<IService, Service>(librtdi::deps<ILogger>);
    auto r = reg.build();

    auto svc1 = r->create<IService>();
    auto svc2 = r->create<IService>();
    REQUIRE(svc1.get() != svc2.get());
    REQUIRE(svc1->value() == 42);
}

TEST_CASE("multi-dep auto-wiring", "[auto_wiring]") {
    struct IRepo {
        virtual ~IRepo() = default;
    };
    struct Repo : IRepo {};

    struct IApp {
        virtual ~IApp() = default;
        virtual int val() const = 0;
    };

    struct App : IApp {
        explicit App(ILogger& /*log*/, IRepo& /*repo*/) {}
        int val() const override { return 7; }
    };

    librtdi::registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_singleton<IRepo, Repo>();
    reg.add_singleton<IApp, App>(librtdi::deps<ILogger, IRepo>);
    auto r = reg.build();

    REQUIRE(r->get<IApp>().val() == 7);
}

// ---------------------------------------------------------------
// singleton<T> explicit marker (equivalent to bare T)
// ---------------------------------------------------------------

TEST_CASE("explicit singleton<T> marker same as bare T", "[auto_wiring]") {
    struct ISvc {
        virtual ~ISvc() = default;
        virtual int val() const = 0;
    };
    struct Svc : ISvc {
        ILogger& logger_;
        explicit Svc(ILogger& logger) : logger_(logger) {}
        int val() const override { return 55; }
    };

    librtdi::registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_singleton<ISvc, Svc>(librtdi::deps<librtdi::singleton<ILogger>>);
    auto r = reg.build();

    auto& svc = r->get<ISvc>();
    REQUIRE(svc.val() == 55);
    // the logger injected is the same singleton
    auto& logger = r->get<ILogger>();
    REQUIRE(&static_cast<Svc&>(svc).logger_ == &logger);
}

// ---------------------------------------------------------------
// collection<singleton<T>> dep wrapper
// ---------------------------------------------------------------

TEST_CASE("collection<singleton<T>> dep wrapper", "[auto_wiring]") {
    struct IPlugin {
        virtual ~IPlugin() = default;
        virtual std::string name() const = 0;
    };
    struct PluginA : IPlugin { std::string name() const override { return "A"; } };
    struct PluginB : IPlugin { std::string name() const override { return "B"; } };

    struct IHost {
        virtual ~IHost() = default;
        virtual std::size_t count() const = 0;
    };
    struct Host : IHost {
        std::vector<IPlugin*> plugins_;
        explicit Host(std::vector<IPlugin*> plugins)
            : plugins_(std::move(plugins)) {}
        std::size_t count() const override { return plugins_.size(); }
    };

    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::singleton);
    // Use collection<singleton<T>> — should behave identically to collection<T>
    reg.add_singleton<IHost, Host>(
        librtdi::deps<librtdi::collection<librtdi::singleton<IPlugin>>>);
    auto r = reg.build();

    REQUIRE(r->get<IHost>().count() == 2);
}

// ---------------------------------------------------------------
// Mixed dep wrappers in single constructor
// ---------------------------------------------------------------

TEST_CASE("mixed deps: bare + transient + collection", "[auto_wiring]") {
    struct IPlugin {
        virtual ~IPlugin() = default;
        virtual std::string name() const = 0;
    };
    struct PluginX : IPlugin { std::string name() const override { return "X"; } };

    struct IRepo {
        virtual ~IRepo() = default;
    };
    struct Repo : IRepo {};

    struct IApp {
        virtual ~IApp() = default;
        virtual int val() const = 0;
    };

    struct App : IApp {
        ILogger& logger_;
        std::unique_ptr<IRepo> repo_;
        std::vector<IPlugin*> plugins_;
        App(ILogger& logger, std::unique_ptr<IRepo> repo,
            std::vector<IPlugin*> plugins)
            : logger_(logger), repo_(std::move(repo)),
              plugins_(std::move(plugins)) {}
        int val() const override {
            return repo_ ? static_cast<int>(plugins_.size()) + 1 : 0;
        }
    };

    librtdi::registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_transient<IRepo, Repo>();
    reg.add_collection<IPlugin, PluginX>(librtdi::lifetime_kind::singleton);
    reg.add_singleton<IApp, App>(
        librtdi::deps<ILogger, librtdi::transient<IRepo>,
                      librtdi::collection<IPlugin>>);
    auto r = reg.build({.validate_lifetimes = false});

    REQUIRE(r->get<IApp>().val() == 2); // 1 plugin + 1
}

// ---------------------------------------------------------------
// collection<transient<T>> as dep in transient impl
// ---------------------------------------------------------------

TEST_CASE("collection<transient<T>> dep in transient impl", "[auto_wiring]") {
    struct IPlugin {
        virtual ~IPlugin() = default;
        virtual std::string name() const = 0;
    };
    struct PluginA : IPlugin { std::string name() const override { return "A"; } };
    struct PluginB : IPlugin { std::string name() const override { return "B"; } };

    struct IRunner {
        virtual ~IRunner() = default;
        virtual std::size_t count() const = 0;
    };
    struct Runner : IRunner {
        std::vector<std::unique_ptr<IPlugin>> plugins_;
        explicit Runner(std::vector<std::unique_ptr<IPlugin>> ps)
            : plugins_(std::move(ps)) {}
        std::size_t count() const override { return plugins_.size(); }
    };

    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::transient);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::transient);
    reg.add_transient<IRunner, Runner>(
        librtdi::deps<librtdi::collection<librtdi::transient<IPlugin>>>);
    auto r = reg.build({.validate_on_build = false});

    auto runner1 = r->create<IRunner>();
    auto runner2 = r->create<IRunner>();
    REQUIRE(runner1->count() == 2);
    REQUIRE(runner2->count() == 2);
}
