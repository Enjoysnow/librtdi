#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <string>

namespace {

struct IBase {
    virtual ~IBase() = default;
    virtual int value() const = 0;
};

struct IDerived : IBase {};

struct Impl : IDerived {
    int value() const override { return 42; }
};

} // namespace

TEST_CASE("forward singleton slot", "[forward]") {
    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    auto r = reg.build({.validate_on_build = false});

    auto& derived = r->get<IDerived>();
    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 42);
    // Forward returns the same underlying singleton
    REQUIRE(&static_cast<IBase&>(derived) == &base);
}

TEST_CASE("forward transient slot", "[forward]") {
    librtdi::registry reg;
    reg.add_transient<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    auto r = reg.build({.validate_on_build = false});

    auto base = r->create<IBase>();
    REQUIRE(base != nullptr);
    REQUIRE(base->value() == 42);
}

TEST_CASE("forward propagates all slots", "[forward]") {
    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.add_transient<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    auto r = reg.build({.validate_on_build = false});

    // Both singleton and transient slots should exist for IBase
    auto& single = r->get<IBase>();
    REQUIRE(single.value() == 42);

    auto trans = r->create<IBase>();
    REQUIRE(trans != nullptr);
    REQUIRE(trans->value() == 42);
}

TEST_CASE("forward with collection slot", "[forward]") {
    struct IPlugin {
        virtual ~IPlugin() = default;
        virtual std::string name() const = 0;
    };

    struct IDerivedPlugin : IPlugin {};

    struct PluginA : IDerivedPlugin {
        std::string name() const override { return "A"; }
    };
    struct PluginB : IDerivedPlugin {
        std::string name() const override { return "B"; }
    };

    librtdi::registry reg;
    reg.add_collection<IDerivedPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IDerivedPlugin, PluginB>(librtdi::lifetime_kind::singleton);
    reg.forward<IPlugin, IDerivedPlugin>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IDerivedPlugin>();
    REQUIRE(all.size() == 2);

    auto base_all = r->get_all<IPlugin>();
    REQUIRE(base_all.size() == 2);
}

// ---------------------------------------------------------------
// Forward transient returns different instances each time
// ---------------------------------------------------------------

TEST_CASE("forward transient returns new instances", "[forward]") {
    librtdi::registry reg;
    reg.add_transient<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    auto r = reg.build({.validate_on_build = false});

    auto a = r->create<IBase>();
    auto b = r->create<IBase>();
    REQUIRE(a.get() != b.get());
    REQUIRE(a->value() == 42);
    REQUIRE(b->value() == 42);
}

// ---------------------------------------------------------------
// Forward to unregistered type produces placeholder → validation catches it
// ---------------------------------------------------------------

TEST_CASE("forward to unregistered type fails validation", "[forward]") {
    struct IUnreg {
        virtual ~IUnreg() = default;
    };

    librtdi::registry reg;
    // No registration for IDerived at all
    reg.forward<IBase, IDerived>();
    // The placeholder descriptor depends on IDerived which is missing
    REQUIRE_THROWS_AS(reg.build(), librtdi::not_found);
}

// ---------------------------------------------------------------
// Forward with transient collection
// ---------------------------------------------------------------

TEST_CASE("forward with transient collection", "[forward]") {
    struct IPlugin {
        virtual ~IPlugin() = default;
        virtual std::string name() const = 0;
    };

    struct IDerivedPlugin : IPlugin {};

    struct PluginA : IDerivedPlugin {
        std::string name() const override { return "A"; }
    };
    struct PluginB : IDerivedPlugin {
        std::string name() const override { return "B"; }
    };

    librtdi::registry reg;
    reg.add_collection<IDerivedPlugin, PluginA>(librtdi::lifetime_kind::transient);
    reg.add_collection<IDerivedPlugin, PluginB>(librtdi::lifetime_kind::transient);
    reg.forward<IPlugin, IDerivedPlugin>();
    auto r = reg.build({.validate_on_build = false});

    auto all1 = r->create_all<IPlugin>();
    auto all2 = r->create_all<IPlugin>();
    REQUIRE(all1.size() == 2);
    REQUIRE(all2.size() == 2);
    // Each call creates fresh instances
    REQUIRE(all1[0].get() != all2[0].get());
}

// ---------------------------------------------------------------
// Forward singleton + decorator: decorator skips forward-singleton
// (forward-singleton returns non-owning erased_ptr, decorating would double-free)
// ---------------------------------------------------------------

TEST_CASE("forward singleton is not decorated (ownership safety)", "[forward][decorator]") {
    struct BaseDecorator : IBase {
        std::unique_ptr<IBase> inner_;
        explicit BaseDecorator(std::unique_ptr<IBase> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    // This decorator targets IBase — should NOT be applied to the
    // forward-expanded singleton descriptor (would cause double-free)
    reg.decorate<IBase, BaseDecorator>();
    auto r = reg.build({.validate_on_build = false});

    // The forward-singleton should return the raw value (not decorated)
    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 42);
    // No crash on resolver destruction (the critical assertion)
}

TEST_CASE("forward transient CAN be decorated", "[forward][decorator]") {
    struct BaseDecorator : IBase {
        std::unique_ptr<IBase> inner_;
        explicit BaseDecorator(std::unique_ptr<IBase> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_transient<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IBase, BaseDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto ptr = r->create<IBase>();
    REQUIRE(ptr->value() == 142);  // 42 + 100
}

TEST_CASE("decorating original propagates through forward singleton", "[forward][decorator]") {
    struct DerivedDecorator : IDerived {
        std::unique_ptr<IDerived> inner_;
        explicit DerivedDecorator(std::unique_ptr<IDerived> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 200; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    // Decorate the ORIGINAL registration (IDerived) — this should be
    // visible through the forward since they share the same instance.
    reg.decorate<IDerived, DerivedDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto& derived = r->get<IDerived>();
    REQUIRE(derived.value() == 242);  // 42 + 200

    // Forward singleton shares the same instance, so also decorated
    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 242);
}
