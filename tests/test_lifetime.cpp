#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>

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
