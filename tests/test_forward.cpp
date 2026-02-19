#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <string>

using namespace librtdi;

// ---------------------------------------------------------------
// Test type hierarchy
// ---------------------------------------------------------------

struct IAnimal {
    virtual ~IAnimal() = default;
    virtual std::string Species() const = 0;
};

struct IMammal : IAnimal {
    virtual int Legs() const = 0;
};

struct Dog : IMammal {
    std::string Species() const override { return "Dog"; }
    int Legs() const override { return 4; }
};

// Multi-interface: Foo implements IBar
struct IBar {
    virtual ~IBar() = default;
    virtual int Value() const = 0;
};

struct Foo : IBar {
    int val_;
    Foo() : val_(42) {}
    int Value() const override { return val_; }
};

// Separate interface for second forward test
struct IBaz {
    virtual ~IBaz() = default;
    virtual std::string Tag() const = 0;
};

struct Qux : IBaz {
    std::string Tag() const override { return "qux"; }
};

// Multi-inheritance hierarchy for testing forward with multiple bases
struct IFlyable {
    virtual ~IFlyable() = default;
    virtual std::string Fly() const = 0;
};

struct ISwimmable {
    virtual ~ISwimmable() = default;
    virtual std::string Swim() const = 0;
};

struct Duck : IAnimal, IFlyable, ISwimmable {
    std::string Species() const override { return "Duck"; }
    std::string Fly() const override { return "flap flap"; }
    std::string Swim() const override { return "splash"; }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("forward: interface and concrete resolve to same instance", "[forward]") {
    registry registry;
    registry.add_singleton<Foo, Foo>();
    registry.forward<IBar, Foo>();
    auto resolver = registry.build();

    auto bar = resolver->resolve<IBar>();
    auto foo = resolver->resolve<Foo>();

    // forward delegates to resolve<Foo>(), so IBar and Foo share the singleton
    REQUIRE(foo.get() == static_cast<Foo*>(bar.get()));
    REQUIRE(bar->Value() == 42);
}

TEST_CASE("forward: three interface levels resolve to same instance", "[forward]") {
    registry registry;
    registry.add_singleton<Dog, Dog>();
    registry.forward<IMammal, Dog>();
    registry.forward<IAnimal, Dog>();
    auto resolver = registry.build();

    auto dog    = resolver->resolve<Dog>();
    auto mammal = resolver->resolve<IMammal>();
    auto animal = resolver->resolve<IAnimal>();

    REQUIRE(dog.get() == dynamic_cast<Dog*>(mammal.get()));
    REQUIRE(dog.get() == dynamic_cast<Dog*>(animal.get()));
}

TEST_CASE("forward: inherits target's singleton lifetime", "[forward]") {
    registry registry;
    registry.add_singleton<Foo, Foo>();
    registry.forward<IBar, Foo>();
    auto resolver = registry.build();

    auto bar1 = resolver->resolve<IBar>();
    auto bar2 = resolver->resolve<IBar>();
    // Because Foo is Singleton, forward always returns the same instance
    REQUIRE(bar1.get() == bar2.get());
}

TEST_CASE("forward: inherits target's scoped lifetime", "[forward]") {
    registry registry;
    registry.add_scoped<Foo, Foo>();
    registry.forward<IBar, Foo>();
    auto resolver = registry.build();

    auto scope1 = resolver->create_scope();
    auto scope2 = resolver->create_scope();

    auto bar1a = scope1->get_resolver().resolve<IBar>();
    auto bar1b = scope1->get_resolver().resolve<IBar>();
    auto bar2  = scope2->get_resolver().resolve<IBar>();

    // Same scope → same instance (because Foo is Scoped)
    REQUIRE(bar1a.get() == bar1b.get());
    // Different scope → different instance
    REQUIRE(bar1a.get() != bar2.get());
}

TEST_CASE("forward: unregistered target fails validation", "[forward]") {
    registry registry;
    // forward to Dog which is NOT registered
    registry.forward<IAnimal, Dog>();

    REQUIRE_THROWS_AS(
        registry.build(),
        not_found);
}

TEST_CASE("forward: with registration_policy::single prevents duplicate forward", "[forward]") {
    registry registry;
    registry.add_singleton<Foo, Foo>();
    registry.forward<IBar, Foo>(registration_policy::single);

    const auto& descs = registry.descriptors();
    const descriptor* fwd_desc = nullptr;
    for (auto& d : descs) {
        if (d.component_type == typeid(IBar) && d.forward_target.has_value()) {
            fwd_desc = &d;
            break;
        }
    }
    REQUIRE(fwd_desc != nullptr);
    REQUIRE(fwd_desc->is_single_slot);

    // Second forward for same interface should throw
    REQUIRE_THROWS_AS(
        (registry.forward<IBar, Foo>(registration_policy::single)),
        duplicate_registration);
}

TEST_CASE("forward: Single slot integrity checked after expansion", "[forward]") {
    // IBar is locked as Single, but Foo has two registrations.
    // forward expansion would create 2 IBar descriptors, so build must fail.
    registry registry;
    registry.add_singleton<Foo, Foo>();
    registry.add_singleton<Foo, Foo>();
    registry.forward<IBar, Foo>(registration_policy::single);

    REQUIRE_THROWS_AS(registry.build(), duplicate_registration);
}

TEST_CASE("forward: with registration_policy::skip is idempotent", "[forward]") {
    registry registry;
    registry.add_singleton<Foo, Foo>();
    registry.forward<IBar, Foo>(registration_policy::skip);
    // Second forward silently skipped
    REQUIRE_NOTHROW(registry.forward<IBar, Foo>(registration_policy::skip));

    auto resolver = registry.build();
    REQUIRE(resolver->resolve<IBar>() != nullptr);
    // Should be only one registration for IBar
    REQUIRE(resolver->resolve_all<IBar>().size() == 1);
}

TEST_CASE("forward: lifetime copied from target at build", "[forward]") {
    registry registry;
    registry.add_singleton<Foo, Foo>();
    registry.forward<IBar, Foo>();

    // Before build, we can check the descriptors
    const auto& descs = registry.descriptors();
    // Find the forward descriptor for IBar
    const descriptor* fwd_desc = nullptr;
    for (auto& d : descs) {
        if (d.component_type == typeid(IBar) && d.forward_target.has_value()) {
            fwd_desc = &d;
        }
    }
    REQUIRE(fwd_desc != nullptr);
    // Before build, forward has placeholder Transient
    REQUIRE(fwd_desc->lifetime == lifetime_kind::transient);

    auto resolver = registry.build();
    // After build, the descriptor's lifetime should have been updated
    // We verify by observing singleton behavior: same instance each time
    auto bar1 = resolver->resolve<IBar>();
    auto bar2 = resolver->resolve<IBar>();
    REQUIRE(bar1.get() == bar2.get());
}

TEST_CASE("forward: singleton depending on forwarded singleton passes validation", "[forward]") {
    // If forward copies Singleton lifetime, then another Singleton depending on
    // the forwarded interface should NOT trigger lifetime_mismatch.
    struct IService {
        virtual ~IService() = default;
    };
    struct ServiceImpl : IService {
        std::shared_ptr<IBar> bar_;
        explicit ServiceImpl(std::shared_ptr<IBar> bar) : bar_(std::move(bar)) {}
    };

    registry registry;
    registry.add_singleton<Foo, Foo>();
    registry.forward<IBar, Foo>();
    registry.add_singleton<IService, ServiceImpl>(deps<IBar>);

    // Should NOT throw — IBar has Singleton lifetime (copied from Foo)
    REQUIRE_NOTHROW(registry.build());
}

TEST_CASE("forward: singleton depending on forwarded scoped fails validation", "[forward]") {
    // If forward copies Scoped lifetime from target, then a Singleton depending
    // on the forwarded interface SHOULD trigger lifetime_mismatch.
    struct IService {
        virtual ~IService() = default;
    };
    struct ServiceImpl : IService {
        std::shared_ptr<IBar> bar_;
        explicit ServiceImpl(std::shared_ptr<IBar> bar) : bar_(std::move(bar)) {}
    };

    registry registry;
    registry.add_scoped<Foo, Foo>();
    registry.forward<IBar, Foo>();
    registry.add_singleton<IService, ServiceImpl>(deps<IBar>);

    // Should throw — Singleton depends on Scoped (via forward)
    REQUIRE_THROWS_AS(registry.build(), lifetime_mismatch);
}

TEST_CASE("forward: expands to all target registrations", "[forward]") {
    // forward expands into N descriptors, one per target registration.
    // With 2 Foo registrations, IBar gets 2 descriptors.
    registry registry;
    registry.add_singleton<Foo, Foo>();    // first registration
    registry.add_singleton<Foo, Foo>();    // second registration
    registry.forward<IBar, Foo>();
    auto resolver = registry.build();

    // 2 registrations → resolve throws ambiguous_component
    REQUIRE_THROWS_AS(resolver->resolve<IBar>(), ambiguous_component);

    // resolve_any returns the last one
    auto bar = resolver->resolve_any<IBar>();
    REQUIRE(bar->Value() == 42);

    // resolve_all mirrors the target registrations
    auto bars = resolver->resolve_all<IBar>();
    auto foos = resolver->resolve_all<Foo>();
    REQUIRE(bars.size() == 2);
    REQUIRE(foos.size() == 2);

    // Each IBar shares the same Singleton instance as its corresponding Foo
    REQUIRE(bars[0].get() == foos[0].get());
    REQUIRE(bars[1].get() == foos[1].get());
}

TEST_CASE("forward: Scoped instances shared per registration per scope", "[forward]") {
    registry registry;
    registry.add_scoped<Foo, Foo>();
    registry.add_scoped<Foo, Foo>();
    registry.forward<IBar, Foo>();
    auto resolver = registry.build();

    auto scope1 = resolver->create_scope();
    auto scope2 = resolver->create_scope();

    auto bars1 = scope1->get_resolver().resolve_all<IBar>();
    auto foos1 = scope1->get_resolver().resolve_all<Foo>();
    auto bars2 = scope2->get_resolver().resolve_all<IBar>();

    REQUIRE(bars1.size() == 2);
    // Same scope: IBar[k] shares instance with Foo[k]
    REQUIRE(bars1[0].get() == foos1[0].get());
    REQUIRE(bars1[1].get() == foos1[1].get());
    // Different scope: different instances
    REQUIRE(bars1[0].get() != bars2[0].get());
    REQUIRE(bars1[1].get() != bars2[1].get());
}

TEST_CASE("forward: multi-inheritance with correct pointer adjustment", "[forward]") {
    // Duck inherits IAnimal, IFlyable, ISwimmable — three bases.
    // Without two-step cast, second+ bases would get wrong pointer offsets.
    registry registry;
    registry.add_singleton<Duck, Duck>();
    registry.forward<IAnimal, Duck>();
    registry.forward<IFlyable, Duck>();
    registry.forward<ISwimmable, Duck>();
    auto resolver = registry.build();

    auto duck     = resolver->resolve<Duck>();
    auto animal   = resolver->resolve<IAnimal>();
    auto flyable  = resolver->resolve<IFlyable>();
    auto swimmable = resolver->resolve<ISwimmable>();

    // All should be the same Duck instance, verified via dynamic_cast
    REQUIRE(dynamic_cast<Duck*>(animal.get()) == duck.get());
    REQUIRE(dynamic_cast<Duck*>(flyable.get()) == duck.get());
    REQUIRE(dynamic_cast<Duck*>(swimmable.get()) == duck.get());

    // Verify actual method dispatch works (not just pointer equality)
    REQUIRE(animal->Species() == "Duck");
    REQUIRE(flyable->Fly() == "flap flap");
    REQUIRE(swimmable->Swim() == "splash");
}
