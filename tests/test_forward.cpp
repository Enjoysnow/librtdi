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
// Forward singleton + decorator: decorated_ptr handles ownership safely
// ---------------------------------------------------------------

TEST_CASE("forward singleton CAN be decorated via decorated_ptr", "[forward][decorator]") {
    struct BaseDecorator : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit BaseDecorator(librtdi::decorated_ptr<IBase> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IBase, BaseDecorator>();
    auto r = reg.build({.validate_on_build = false});

    // Forward-singleton IS now decorated thanks to decorated_ptr
    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 142);  // 42 + 100
    // No crash on resolver destruction (decorated_ptr does not double-free)
}

TEST_CASE("forward transient CAN be decorated", "[forward][decorator]") {
    struct BaseDecorator : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit BaseDecorator(librtdi::decorated_ptr<IBase> inner)
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
        librtdi::decorated_ptr<IDerived> inner_;
        explicit DerivedDecorator(librtdi::decorated_ptr<IDerived> inner)
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

// ---------------------------------------------------------------
// Per-interface decoration: only forward interface is decorated
// ---------------------------------------------------------------

TEST_CASE("forward-singleton per-interface decoration", "[forward][decorator]") {
    struct BaseDecorator : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit BaseDecorator(librtdi::decorated_ptr<IBase> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    // Only decorate IBase, NOT IDerived
    reg.decorate<IBase, BaseDecorator>();
    auto r = reg.build({.validate_on_build = false});

    // IBase (forward) should be decorated
    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 142);  // 42 + 100

    // IDerived (original) should NOT be decorated
    auto& derived = r->get<IDerived>();
    REQUIRE(derived.value() == 42);
}

// ---------------------------------------------------------------
// decorated_ptr::owns() reflects ownership semantics
// ---------------------------------------------------------------

TEST_CASE("decorated_ptr owns flag reflects lifetime", "[forward][decorator]") {
    struct OwnershipChecker : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        bool inner_owns_;
        explicit OwnershipChecker(librtdi::decorated_ptr<IBase> inner)
            : inner_(std::move(inner)), inner_owns_(inner_.owns()) {}
        int value() const override { return inner_->value(); }
        bool was_owning() const { return inner_owns_; }
    };

    SECTION("forward-singleton: non-owning") {
        librtdi::registry reg;
        reg.add_singleton<IDerived, Impl>();
        reg.forward<IBase, IDerived>();
        reg.decorate<IBase, OwnershipChecker>();
        auto r = reg.build({.validate_on_build = false});

        auto& base = r->get<IBase>();
        auto* checker = dynamic_cast<const OwnershipChecker*>(&base);
        REQUIRE(checker != nullptr);
        REQUIRE_FALSE(checker->was_owning());
    }

    SECTION("transient: owning") {
        librtdi::registry reg;
        reg.add_transient<IBase, Impl>();
        reg.decorate<IBase, OwnershipChecker>();
        auto r = reg.build({.validate_on_build = false});

        auto ptr = r->create<IBase>();
        auto* checker = dynamic_cast<const OwnershipChecker*>(ptr.get());
        REQUIRE(checker != nullptr);
        REQUIRE(checker->was_owning());
    }

    SECTION("regular singleton: owning") {
        librtdi::registry reg;
        reg.add_singleton<IBase, Impl>();
        reg.decorate<IBase, OwnershipChecker>();
        auto r = reg.build({.validate_on_build = false});

        auto& base = r->get<IBase>();
        auto* checker = dynamic_cast<const OwnershipChecker*>(&base);
        REQUIRE(checker != nullptr);
        REQUIRE(checker->was_owning());
    }
}

// ---------------------------------------------------------------
// Multiple decorators stacked on forward-singleton
// ---------------------------------------------------------------

TEST_CASE("multiple decorators on forward singleton", "[forward][decorator]") {
    struct DecA : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit DecA(librtdi::decorated_ptr<IBase> inner) : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };
    struct DecB : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit DecB(librtdi::decorated_ptr<IBase> inner) : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IBase, DecA>();
    reg.decorate<IBase, DecB>();
    auto r = reg.build({.validate_on_build = false});

    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 242);  // 42 + 100 + 100
}

// ---------------------------------------------------------------
// Multiple decorators stacked on forward-transient
// ---------------------------------------------------------------

TEST_CASE("multiple decorators on forward transient", "[forward][decorator]") {
    struct DecA : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit DecA(librtdi::decorated_ptr<IBase> inner) : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };
    struct DecB : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit DecB(librtdi::decorated_ptr<IBase> inner) : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_transient<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IBase, DecA>();
    reg.decorate<IBase, DecB>();
    auto r = reg.build({.validate_on_build = false});

    auto ptr = r->create<IBase>();
    REQUIRE(ptr->value() == 242);  // 42 + 100 + 100
}

// ---------------------------------------------------------------
// Forward singleton collection + decorator
// ---------------------------------------------------------------

TEST_CASE("forward singleton collection + decorator", "[forward][decorator]") {
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

    struct PluginDecorator : IPlugin {
        librtdi::decorated_ptr<IPlugin> inner_;
        explicit PluginDecorator(librtdi::decorated_ptr<IPlugin> inner)
            : inner_(std::move(inner)) {}
        std::string name() const override { return "dec(" + inner_->name() + ")"; }
    };

    librtdi::registry reg;
    reg.add_collection<IDerivedPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IDerivedPlugin, PluginB>(librtdi::lifetime_kind::singleton);
    reg.forward<IPlugin, IDerivedPlugin>();
    reg.decorate<IPlugin, PluginDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IPlugin>();
    REQUIRE(all.size() == 2);

    bool found_dec_a = false;
    bool found_dec_b = false;
    for (auto* p : all) {
        if (p->name() == "dec(A)") found_dec_a = true;
        if (p->name() == "dec(B)") found_dec_b = true;
    }
    REQUIRE(found_dec_a);
    REQUIRE(found_dec_b);
}

// ---------------------------------------------------------------
// Forward transient collection + decorator
// ---------------------------------------------------------------

TEST_CASE("forward transient collection + decorator", "[forward][decorator]") {
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

    struct PluginDecorator : IPlugin {
        librtdi::decorated_ptr<IPlugin> inner_;
        explicit PluginDecorator(librtdi::decorated_ptr<IPlugin> inner)
            : inner_(std::move(inner)) {}
        std::string name() const override { return "dec(" + inner_->name() + ")"; }
    };

    librtdi::registry reg;
    reg.add_collection<IDerivedPlugin, PluginA>(librtdi::lifetime_kind::transient);
    reg.add_collection<IDerivedPlugin, PluginB>(librtdi::lifetime_kind::transient);
    reg.forward<IPlugin, IDerivedPlugin>();
    reg.decorate<IPlugin, PluginDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->create_all<IPlugin>();
    REQUIRE(all.size() == 2);

    bool found_dec_a = false;
    bool found_dec_b = false;
    for (auto& p : all) {
        if (p->name() == "dec(A)") found_dec_a = true;
        if (p->name() == "dec(B)") found_dec_b = true;
    }
    REQUIRE(found_dec_a);
    REQUIRE(found_dec_b);
}

// ---------------------------------------------------------------
// Both original and forward interface decorated independently
// ---------------------------------------------------------------

TEST_CASE("both original and forward decorated independently", "[forward][decorator]") {
    struct DerivedDecorator : IDerived {
        librtdi::decorated_ptr<IDerived> inner_;
        explicit DerivedDecorator(librtdi::decorated_ptr<IDerived> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 200; }
    };

    struct BaseDecorator : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit BaseDecorator(librtdi::decorated_ptr<IBase> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IDerived, DerivedDecorator>();
    reg.decorate<IBase, BaseDecorator>();
    auto r = reg.build({.validate_on_build = false});

    // IDerived: Impl(42) → DerivedDecorator(242)
    auto& derived = r->get<IDerived>();
    REQUIRE(derived.value() == 242);

    // IBase: forward → get<IDerived>()==242 → BaseDecorator(342)
    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 342);
}

// ---------------------------------------------------------------
// Forward + decorator with extra deps
// ---------------------------------------------------------------

TEST_CASE("forward + decorator with extra deps", "[forward][decorator]") {
    struct IConfig {
        virtual ~IConfig() = default;
        virtual int multiplier() const = 0;
    };
    struct Config : IConfig {
        int multiplier() const override { return 10; }
    };

    struct MultiplierDecorator : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        IConfig& config_;
        MultiplierDecorator(librtdi::decorated_ptr<IBase> inner, IConfig& config)
            : inner_(std::move(inner)), config_(config) {}
        int value() const override {
            return inner_->value() + config_.multiplier();
        }
    };

    librtdi::registry reg;
    reg.add_singleton<IConfig, Config>();
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IBase, MultiplierDecorator>(librtdi::deps<IConfig>);
    auto r = reg.build({.validate_on_build = false});

    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 52);  // 42 + 10
}

// ---------------------------------------------------------------
// Decorating original propagates through forward transient
// ---------------------------------------------------------------

TEST_CASE("decorating original propagates through forward transient", "[forward][decorator]") {
    struct DerivedDecorator : IDerived {
        librtdi::decorated_ptr<IDerived> inner_;
        explicit DerivedDecorator(librtdi::decorated_ptr<IDerived> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 200; }
    };

    librtdi::registry reg;
    reg.add_transient<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IDerived, DerivedDecorator>();
    auto r = reg.build({.validate_on_build = false});

    // Direct IDerived is decorated
    auto derived = r->create<IDerived>();
    REQUIRE(derived->value() == 242);

    // Forward transient: creates IDerived (decorated → 242), casts to IBase
    auto base = r->create<IBase>();
    REQUIRE(base->value() == 242);
}

// ---------------------------------------------------------------
// decorate_target on forwarded impl type
// ---------------------------------------------------------------

TEST_CASE("decorate_target on forwarded impl type", "[forward][decorator]") {
    struct ImplDecorator : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit ImplDecorator(librtdi::decorated_ptr<IBase> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    // Target Impl specifically — the forwarded descriptor carries impl_type=Impl
    reg.decorate_target<IBase, ImplDecorator, Impl>();
    auto r = reg.build({.validate_on_build = false});

    auto& base = r->get<IBase>();
    REQUIRE(base.value() == 142);  // 42 + 100
}

// ---------------------------------------------------------------
// Forward all-slots (singleton + transient) + decorate
// ---------------------------------------------------------------

TEST_CASE("forward all slots + decorate", "[forward][decorator]") {
    struct BaseDecorator : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit BaseDecorator(librtdi::decorated_ptr<IBase> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.add_transient<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IBase, BaseDecorator>();
    auto r = reg.build({.validate_on_build = false});

    // Both forward-singleton and forward-transient should be decorated
    auto& single = r->get<IBase>();
    REQUIRE(single.value() == 142);  // 42 + 100

    auto trans = r->create<IBase>();
    REQUIRE(trans->value() == 142); // 42 + 100
}

// ---------------------------------------------------------------
// Both interfaces decorated independently (transient)
// ---------------------------------------------------------------

TEST_CASE("both original and forward decorated independently (transient)", "[forward][decorator]") {
    struct DerivedDecorator : IDerived {
        librtdi::decorated_ptr<IDerived> inner_;
        explicit DerivedDecorator(librtdi::decorated_ptr<IDerived> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 200; }
    };

    struct BaseDecorator : IBase {
        librtdi::decorated_ptr<IBase> inner_;
        explicit BaseDecorator(librtdi::decorated_ptr<IBase> inner)
            : inner_(std::move(inner)) {}
        int value() const override { return inner_->value() + 100; }
    };

    librtdi::registry reg;
    reg.add_transient<IDerived, Impl>();
    reg.forward<IBase, IDerived>();
    reg.decorate<IDerived, DerivedDecorator>();
    reg.decorate<IBase, BaseDecorator>();
    auto r = reg.build({.validate_on_build = false});

    // IDerived: Impl(42) → DerivedDecorator(242)
    auto derived = r->create<IDerived>();
    REQUIRE(derived->value() == 242);

    // IBase: forward creates IDerived(decorated=242), cast → BaseDecorator(342)
    auto base = r->create<IBase>();
    REQUIRE(base->value() == 342);
}

// ---------------------------------------------------------------
// Forward collection + multiple decorators
// ---------------------------------------------------------------

TEST_CASE("forward collection + multiple decorators", "[forward][decorator]") {
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

    struct DecX : IPlugin {
        librtdi::decorated_ptr<IPlugin> inner_;
        explicit DecX(librtdi::decorated_ptr<IPlugin> inner)
            : inner_(std::move(inner)) {}
        std::string name() const override { return "x(" + inner_->name() + ")"; }
    };
    struct DecY : IPlugin {
        librtdi::decorated_ptr<IPlugin> inner_;
        explicit DecY(librtdi::decorated_ptr<IPlugin> inner)
            : inner_(std::move(inner)) {}
        std::string name() const override { return "y(" + inner_->name() + ")"; }
    };

    librtdi::registry reg;
    reg.add_collection<IDerivedPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IDerivedPlugin, PluginB>(librtdi::lifetime_kind::singleton);
    reg.forward<IPlugin, IDerivedPlugin>();
    reg.decorate<IPlugin, DecX>();
    reg.decorate<IPlugin, DecY>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IPlugin>();
    REQUIRE(all.size() == 2);

    bool found_yx_a = false;
    bool found_yx_b = false;
    for (auto* p : all) {
        if (p->name() == "y(x(A))") found_yx_a = true;
        if (p->name() == "y(x(B))") found_yx_b = true;
    }
    REQUIRE(found_yx_a);
    REQUIRE(found_yx_b);
}
