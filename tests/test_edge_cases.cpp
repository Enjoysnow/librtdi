#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>

namespace {

struct IEmpty {
    virtual ~IEmpty() = default;
};

struct Empty : IEmpty {};

} // namespace

TEST_CASE("empty registry builds", "[edge_cases]") {
    librtdi::registry reg;
    auto r = reg.build();
    REQUIRE(r != nullptr);
}

TEST_CASE("self-registration (interface == impl)", "[edge_cases]") {
    struct Concrete {
        virtual ~Concrete() = default;
        int val() const { return 10; }
    };

    librtdi::registry reg;
    reg.add_singleton<Concrete, Concrete>();
    auto r = reg.build({.validate_on_build = false});

    auto& c = r->get<Concrete>();
    REQUIRE(c.val() == 10);
}

TEST_CASE("resolver shared_ptr outlives registry", "[edge_cases]") {
    std::shared_ptr<librtdi::resolver> r;
    {
        librtdi::registry reg;
        reg.add_singleton<IEmpty, Empty>();
        r = reg.build({.validate_on_build = false});
    }
    // Registry destroyed, resolver still works
    auto* e = r->try_get<IEmpty>();
    REQUIRE(e != nullptr);
}

TEST_CASE("large registration count", "[edge_cases]") {
    // Register many different concrete types under same interface as collection
    struct IItem {
        virtual ~IItem() = default;
        virtual int id() const = 0;
    };

    struct Item0 : IItem { int id() const override { return 0; } };
    struct Item1 : IItem { int id() const override { return 1; } };
    struct Item2 : IItem { int id() const override { return 2; } };
    struct Item3 : IItem { int id() const override { return 3; } };
    struct Item4 : IItem { int id() const override { return 4; } };

    librtdi::registry reg;
    reg.add_collection<IItem, Item0>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IItem, Item1>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IItem, Item2>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IItem, Item3>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IItem, Item4>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IItem>();
    REQUIRE(all.size() == 5);
}

TEST_CASE("move registry", "[edge_cases]") {
    librtdi::registry reg;
    reg.add_singleton<IEmpty, Empty>();

    librtdi::registry reg2 = std::move(reg);
    auto r = reg2.build({.validate_on_build = false});
    REQUIRE(r->try_get<IEmpty>() != nullptr);
}

// ---------------------------------------------------------------
// descriptors() accessor
// ---------------------------------------------------------------

TEST_CASE("descriptors accessor returns registrations", "[edge_cases]") {
    librtdi::registry reg;
    reg.add_singleton<IEmpty, Empty>();
    const auto& descs = reg.descriptors();
    REQUIRE(descs.size() == 1);
    REQUIRE(descs[0].component_type == std::type_index(typeid(IEmpty)));
    REQUIRE(descs[0].lifetime == librtdi::lifetime_kind::singleton);
    REQUIRE(descs[0].is_collection == false);
}

TEST_CASE("descriptors accessor reflects collection", "[edge_cases]") {
    struct IPlugin {
        virtual ~IPlugin() = default;
    };
    struct PluginA : IPlugin {};
    struct PluginB : IPlugin {};

    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::transient);
    const auto& descs = reg.descriptors();
    REQUIRE(descs.size() == 2);
    REQUIRE(descs[0].is_collection == true);
    REQUIRE(descs[0].lifetime == librtdi::lifetime_kind::singleton);
    REQUIRE(descs[1].is_collection == true);
    REQUIRE(descs[1].lifetime == librtdi::lifetime_kind::transient);
}

// ---------------------------------------------------------------
// erased_ptr basic behavior
// ---------------------------------------------------------------

TEST_CASE("erased_ptr default is null", "[edge_cases]") {
    librtdi::erased_ptr ep;
    REQUIRE(ep.get() == nullptr);
    REQUIRE(!ep);
}

TEST_CASE("erased_ptr owns and deletes", "[edge_cases]") {
    static int dtor_count = 0;
    struct Tracked {
        ~Tracked() { ++dtor_count; }
    };
    dtor_count = 0;
    {
        auto ep = librtdi::make_erased<Tracked>();
        REQUIRE(ep);
        REQUIRE(ep.get() != nullptr);
    }
    REQUIRE(dtor_count == 1);
}

TEST_CASE("erased_ptr move transfers ownership", "[edge_cases]") {
    static int dtor_count = 0;
    struct Tracked {
        ~Tracked() { ++dtor_count; }
    };
    dtor_count = 0;
    {
        auto ep1 = librtdi::make_erased<Tracked>();
        auto ep2 = std::move(ep1);
        REQUIRE(!ep1);
        REQUIRE(ep2);
    }
    REQUIRE(dtor_count == 1);
}

TEST_CASE("erased_ptr release transfers without deleting", "[edge_cases]") {
    static int dtor_count = 0;
    struct Tracked {
        ~Tracked() { ++dtor_count; }
    };
    dtor_count = 0;
    void* raw = nullptr;
    {
        auto ep = librtdi::make_erased<Tracked>();
        raw = ep.release();
        REQUIRE(!ep);
    }
    REQUIRE(dtor_count == 0);
    // Manual cleanup
    delete static_cast<Tracked*>(raw);
    REQUIRE(dtor_count == 1);
}

TEST_CASE("erased_ptr non-owning (null deleter)", "[edge_cases]") {
    int x = 42;
    {
        librtdi::erased_ptr ep(static_cast<void*>(&x), nullptr);
        REQUIRE(ep);
        REQUIRE(ep.get() == static_cast<void*>(&x));
        // reset should NOT crash — deleter is null
        ep.reset();
        REQUIRE(!ep);
    }
    REQUIRE(x == 42); // x is still valid (not deleted)
}
