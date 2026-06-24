#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <vector>

namespace {

struct IService {
    virtual ~IService() = default;
    virtual int value() const = 0;
};

struct DefaultService : IService {
    int value() const override { return 0; }
};

struct ServiceA : IService {
    int value() const override { return 1; }
};

struct ServiceB : IService {
    int value() const override { return 2; }
};

} // namespace

TEST_CASE("keyed singleton registration and resolution", "[keyed]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>("a");
    reg.add_singleton<IService, ServiceB>("b");
    auto r = reg.build({.validate_on_build = false});

    REQUIRE(r->get<IService>("a").value() == 1);
    REQUIRE(r->get<IService>("b").value() == 2);
}

TEST_CASE("keyed and non-keyed coexist", "[keyed]") {
    librtdi::registry reg;
    reg.add_singleton<IService, DefaultService>();
    reg.add_singleton<IService, ServiceA>("a");
    auto r = reg.build({.validate_on_build = false});

    REQUIRE(r->get<IService>().value() == 0);
    REQUIRE(r->get<IService>("a").value() == 1);
}

TEST_CASE("keyed try_get returns nullptr if key not found", "[keyed]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>("a");
    auto r = reg.build({.validate_on_build = false});

    REQUIRE(r->try_get<IService>("nonexistent") == nullptr);
}

TEST_CASE("keyed duplicate throws", "[keyed]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>("x");
    REQUIRE_THROWS_AS(
        (reg.add_singleton<IService, ServiceB>("x")),
        librtdi::duplicate_registration);
}

TEST_CASE("keyed transient", "[keyed]") {
    librtdi::registry reg;
    reg.add_transient<IService, ServiceA>("a");
    reg.add_transient<IService, ServiceB>("b");
    auto r = reg.build({.validate_on_build = false});

    auto a1 = r->create<IService>("a");
    auto a2 = r->create<IService>("a");
    REQUIRE(a1->value() == 1);
    REQUIRE(a2->value() == 1);
    REQUIRE(a1.get() != a2.get());

    auto b = r->create<IService>("b");
    REQUIRE(b->value() == 2);
}

TEST_CASE("keyed singleton identity", "[keyed]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>("a");
    auto r = reg.build({.validate_on_build = false});

    auto& a1 = r->get<IService>("a");
    auto& a2 = r->get<IService>("a");
    REQUIRE(&a1 == &a2);
}

// ---------------------------------------------------------------
// Keyed collection registration and resolution
// ---------------------------------------------------------------

TEST_CASE("keyed singleton collection", "[keyed]") {
    librtdi::registry reg;
    reg.add_collection<IService, ServiceA>("group1", librtdi::lifetime_kind::singleton);
    reg.add_collection<IService, ServiceB>("group1", librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IService>("group1");
    REQUIRE(all.size() == 2);

    // Non-keyed should be empty
    auto non_keyed = r->get_all<IService>();
    REQUIRE(non_keyed.empty());
}

TEST_CASE("keyed transient collection", "[keyed]") {
    librtdi::registry reg;
    reg.add_collection<IService, ServiceA>("pool", librtdi::lifetime_kind::transient);
    reg.add_collection<IService, ServiceB>("pool", librtdi::lifetime_kind::transient);
    auto r = reg.build({.validate_on_build = false});

    auto all1 = r->create_all<IService>("pool");
    auto all2 = r->create_all<IService>("pool");
    REQUIRE(all1.size() == 2);
    REQUIRE(all2.size() == 2);
    REQUIRE(all1[0].get() != all2[0].get());
}

TEST_CASE("keyed get throws not_found for wrong key", "[keyed]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>("x");
    auto r = reg.build({.validate_on_build = false});

    REQUIRE_THROWS_AS(r->get<IService>("y"), librtdi::not_found);
}

TEST_CASE("keyed create throws not_found for wrong key", "[keyed]") {
    librtdi::registry reg;
    reg.add_transient<IService, ServiceA>("x");
    auto r = reg.build({.validate_on_build = false});

    REQUIRE_THROWS_AS(r->create<IService>("y"), librtdi::not_found);
}

TEST_CASE("keyed singleton destruction order destroys consumer before dependency",
          "[keyed][destruction]") {
    static std::vector<const char*> events;
    events.clear();

    struct IDependency {
        virtual ~IDependency() = default;
        virtual void ping() const = 0;
    };

    struct Dependency final : IDependency {
        ~Dependency() override { events.push_back("dependency destroyed"); }
        void ping() const override { events.push_back("dependency ping"); }
    };

    struct IConsumer {
        virtual ~IConsumer() = default;
    };

    struct Consumer final : IConsumer {
        void attach(IDependency& dependency) { dependency_ = &dependency; }

        ~Consumer() override {
            events.push_back("consumer destroying");
            REQUIRE(dependency_ != nullptr);
            dependency_->ping();
            events.push_back("consumer destroyed");
        }

        IDependency* dependency_ = nullptr;
    };

    {
        librtdi::registry reg;
        reg.add_singleton<IDependency, Dependency>("dep");
        reg.add_singleton<IConsumer, Consumer>("consumer");

        auto r = reg.build({.validate_on_build = false});
        auto& consumer = r->get<IConsumer>("consumer");
        auto& dependency = r->get<IDependency>("dep");
        static_cast<Consumer&>(consumer).attach(dependency);
    }

    REQUIRE(events == std::vector<const char*>{
        "consumer destroying",
        "dependency ping",
        "consumer destroyed",
        "dependency destroyed",
    });
}
