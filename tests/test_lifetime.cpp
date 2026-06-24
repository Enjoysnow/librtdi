#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct ICounter {
    virtual ~ICounter() = default;
    virtual int next() = 0;
};

struct Counter : ICounter {
    int n = 0;
    int next() override { return ++n; }
};

} // namespace

TEST_CASE("singleton returns same instance", "[lifetime]") {
    librtdi::registry reg;
    reg.add_singleton<ICounter, Counter>();
    auto r = reg.build({.validate_on_build = false});

    auto& a = r->get<ICounter>();
    auto& b = r->get<ICounter>();
    REQUIRE(&a == &b);
    REQUIRE(a.next() == 1);
    REQUIRE(b.next() == 2); // same instance
}

TEST_CASE("transient returns new instance each time", "[lifetime]") {
    librtdi::registry reg;
    reg.add_transient<ICounter, Counter>();
    auto r = reg.build({.validate_on_build = false});

    auto a = r->create<ICounter>();
    auto b = r->create<ICounter>();
    REQUIRE(a.get() != b.get());
    REQUIRE(a->next() == 1);
    REQUIRE(b->next() == 1); // independent instances
}

TEST_CASE("singleton collection returns same instances", "[lifetime]") {
    librtdi::registry reg;
    reg.add_collection<ICounter, Counter>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});

    auto all1 = r->get_all<ICounter>();
    auto all2 = r->get_all<ICounter>();
    REQUIRE(all1.size() == 1);
    REQUIRE(all2.size() == 1);
    REQUIRE(all1[0] == all2[0]); // same pointer
}

TEST_CASE("transient collection returns new instances", "[lifetime]") {
    librtdi::registry reg;
    reg.add_collection<ICounter, Counter>(librtdi::lifetime_kind::transient);
    auto r = reg.build({.validate_on_build = false});

    auto all1 = r->create_all<ICounter>();
    auto all2 = r->create_all<ICounter>();
    REQUIRE(all1.size() == 1);
    REQUIRE(all2.size() == 1);
    REQUIRE(all1[0].get() != all2[0].get()); // different instances
}

TEST_CASE("same type supports singleton + transient independently", "[lifetime]") {
    librtdi::registry reg;
    reg.add_singleton<ICounter, Counter>();
    reg.add_transient<ICounter, Counter>();
    auto r = reg.build({.validate_on_build = false});

    auto& single = r->get<ICounter>();
    auto trans = r->create<ICounter>();
    REQUIRE(&single != trans.get());
    REQUIRE(single.next() == 1);
    REQUIRE(trans->next() == 1);
}

TEST_CASE("singleton destruction order destroys consumer before dependency", "[lifetime][destruction]") {
    static std::vector<std::string> events;
    events.clear();

    struct IDependency {
        virtual ~IDependency() = default;
        virtual void ping() const = 0;
    };

    struct Dependency : IDependency {
        ~Dependency() override { events.push_back("dependency destroyed"); }
        void ping() const override { events.push_back("dependency ping"); }
    };

    struct IConsumer {
        virtual ~IConsumer() = default;
    };

    struct Consumer : IConsumer {
        explicit Consumer(IDependency& dep) : dep_(dep) {}

        ~Consumer() override {
            events.push_back("consumer destroying");
            dep_.ping();
            events.push_back("consumer destroyed");
        }

        IDependency& dep_;
    };

    {
        librtdi::registry reg;
        reg.add_singleton<IDependency, Dependency>();
        reg.add_singleton<IConsumer, Consumer>(librtdi::deps<IDependency>);
        auto r = reg.build({.validate_on_build = false});
        r->get<IConsumer>();
    }

    REQUIRE(events == std::vector<std::string>{
                          "consumer destroying",
                          "dependency ping",
                          "consumer destroyed",
                          "dependency destroyed",
                      });
}

TEST_CASE("singleton destruction order tears down chains from leaf to root",
          "[lifetime][destruction]") {
    static std::vector<std::string> events;
    events.clear();

    struct IC {
        virtual ~IC() = default;
        virtual void ping() const = 0;
    };
    struct IB {
        virtual ~IB() = default;
        virtual void ping() const = 0;
    };
    struct IA {
        virtual ~IA() = default;
    };

    struct C final : IC {
        ~C() override { events.push_back("C destroyed"); }
        void ping() const override { events.push_back("C ping"); }
    };

    struct B final : IB {
        explicit B(IC& c) : c_(c) {}
        ~B() override {
            events.push_back("B destroying");
            c_.ping();
            events.push_back("B destroyed");
        }
        void ping() const override { events.push_back("B ping"); }
        IC& c_;
    };

    struct A final : IA {
        explicit A(IB& b) : b_(b) {}
        ~A() override {
            events.push_back("A destroying");
            b_.ping();
            events.push_back("A destroyed");
        }
        IB& b_;
    };

    {
        librtdi::registry reg;
        reg.add_singleton<IC, C>();
        reg.add_singleton<IB, B>(librtdi::deps<IC>);
        reg.add_singleton<IA, A>(librtdi::deps<IB>);
        auto r = reg.build({.validate_on_build = false});
        static_cast<void>(r->get<IA>());
    }

    REQUIRE(events == std::vector<std::string>{
        "A destroying",
        "B ping",
        "A destroyed",
        "B destroying",
        "C ping",
        "B destroyed",
        "C destroyed"
    });
}

TEST_CASE("singleton destruction order tears down collection consumers first",
          "[lifetime][destruction]") {
    static std::vector<std::string> events;
    events.clear();

    struct IPlugin {
        virtual ~IPlugin() = default;
        virtual void ping() const = 0;
    };

    struct PluginA final : IPlugin {
        ~PluginA() override { events.push_back("PluginA destroyed"); }
        void ping() const override { events.push_back("PluginA ping"); }
    };

    struct PluginB final : IPlugin {
        ~PluginB() override { events.push_back("PluginB destroyed"); }
        void ping() const override { events.push_back("PluginB ping"); }
    };

    struct IHost {
        virtual ~IHost() = default;
    };

    struct Host final : IHost {
        explicit Host(std::vector<IPlugin*> plugins) : plugins_(std::move(plugins)) {}
        ~Host() override {
            events.push_back("Host destroying");
            for (auto* plugin : plugins_) plugin->ping();
            events.push_back("Host destroyed");
        }
        std::vector<IPlugin*> plugins_;
    };

    {
        librtdi::registry reg;
        reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::singleton);
        reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::singleton);
        reg.add_singleton<IHost, Host>(librtdi::deps<librtdi::collection<IPlugin>>);
        auto r = reg.build({.validate_on_build = false});
        static_cast<void>(r->get<IHost>());
    }

    const auto event_index = [&](const std::string& event) {
        const auto it = std::find(events.begin(), events.end(), event);
        REQUIRE(it != events.end());
        return static_cast<std::size_t>(std::distance(events.begin(), it));
    };

    const auto host_destroying = event_index("Host destroying");
    const auto host_destroyed = event_index("Host destroyed");
    const auto plugin_a_ping = event_index("PluginA ping");
    const auto plugin_b_ping = event_index("PluginB ping");
    const auto plugin_a_destroyed = event_index("PluginA destroyed");
    const auto plugin_b_destroyed = event_index("PluginB destroyed");

    REQUIRE(host_destroying < plugin_a_ping);
    REQUIRE(host_destroying < plugin_b_ping);
    REQUIRE(plugin_a_ping < host_destroyed);
    REQUIRE(plugin_b_ping < host_destroyed);
    REQUIRE(host_destroyed < plugin_a_destroyed);
    REQUIRE(host_destroyed < plugin_b_destroyed);
}
