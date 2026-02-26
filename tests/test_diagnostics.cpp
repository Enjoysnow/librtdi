#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <librtdi.hpp>
#include <string>

namespace {

struct IService {
    virtual ~IService() = default;
};
struct ServiceImpl : IService {};

} // namespace

TEST_CASE("not_found includes type name", "[diagnostics]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    try {
        r->get<IService>();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("IService"));
    }
}

TEST_CASE("duplicate_registration includes type name", "[diagnostics]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceImpl>();

    try {
        reg.add_singleton<IService, ServiceImpl>();
        FAIL("Expected duplicate_registration");
    } catch (const librtdi::duplicate_registration& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("IService"));
    }
}

TEST_CASE("cyclic_dependency includes cycle path", "[diagnostics]") {
    struct IA { virtual ~IA() = default; };
    struct IB { virtual ~IB() = default; };
    struct A : IA { explicit A(IB&) {} };
    struct B : IB { explicit B(IA&) {} };

    librtdi::registry reg;
    reg.add_singleton<IA, A>(librtdi::deps<IB>);
    reg.add_singleton<IB, B>(librtdi::deps<IA>);

    try {
        reg.build();
        FAIL("Expected cyclic_dependency");
    } catch (const librtdi::cyclic_dependency& e) {
        REQUIRE(e.cycle().size() >= 2);
    }
}

TEST_CASE("cyclic_dependency message format is correct", "[diagnostics]") {
    struct IX { virtual ~IX() = default; };
    struct IY { virtual ~IY() = default; };
    struct XImpl : IX { explicit XImpl(IY&) {} };
    struct YImpl : IY { explicit YImpl(IX&) {} };

    librtdi::registry reg;
    reg.add_singleton<IX, XImpl>(librtdi::deps<IY>);
    reg.add_singleton<IY, YImpl>(librtdi::deps<IX>);

    try {
        reg.build();
        FAIL("Expected cyclic_dependency");
    } catch (const librtdi::cyclic_dependency& e) {
        std::string msg = e.what();
        // Cycle path should be [A -> B -> A], NOT [A -> B -> A -> A]
        // Count occurrences of " -> " — should be exactly 2 for a 2-node cycle
        std::size_t arrow_count = 0;
        std::size_t pos = 0;
        while ((pos = msg.find(" -> ", pos)) != std::string::npos) {
            ++arrow_count;
            pos += 4;
        }
        // e.g. "Cyclic dependency detected: IX -> IY -> IX [at ...]" → 2 arrows
        REQUIRE(arrow_count == 2);
    }
}

TEST_CASE("di_error carries source_location", "[diagnostics]") {
    try {
        throw librtdi::di_error("test error");
    } catch (const librtdi::di_error& e) {
        // source_location should point to this file
        std::string loc = e.location().file_name();
        REQUIRE(!loc.empty());
    }
}

// ---------------------------------------------------------------
// lifetime_mismatch diagnostic includes type names
// ---------------------------------------------------------------

TEST_CASE("lifetime_mismatch includes consumer and dependency", "[diagnostics]") {
    struct ISingleton {
        virtual ~ISingleton() = default;
    };
    struct ITransient {
        virtual ~ITransient() = default;
    };
    struct TransientImpl : ITransient {};
    struct SingletonImpl : ISingleton {
        explicit SingletonImpl(std::unique_ptr<ITransient> /*t*/) {}
    };

    librtdi::registry reg;
    reg.add_transient<ITransient, TransientImpl>();
    reg.add_singleton<ISingleton, SingletonImpl>(
        librtdi::deps<librtdi::transient<ITransient>>);

    try {
        reg.build({.validate_lifetimes = true});
        FAIL("Expected lifetime_mismatch");
    } catch (const librtdi::lifetime_mismatch& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("singleton"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("transient"));
    }
}

// ---------------------------------------------------------------
// resolution_error diagnostic includes type name and inner message
// ---------------------------------------------------------------

TEST_CASE("resolution_error includes type and inner message", "[diagnostics]") {
    struct IFailing {
        virtual ~IFailing() = default;
    };
    struct FailingImpl : IFailing {
        FailingImpl() { throw std::runtime_error("intentional failure"); }
    };

    librtdi::registry reg;
    reg.add_singleton<IFailing, FailingImpl>();
    auto r = reg.build({.validate_on_build = false,
                        .eager_singletons = false});

    try {
        r->get<IFailing>();
        FAIL("Expected resolution_error");
    } catch (const librtdi::resolution_error& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("intentional failure"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("IFailing"));
    }
}

// ---------------------------------------------------------------
// not_found with key includes key in message
// ---------------------------------------------------------------

TEST_CASE("not_found with key includes key string", "[diagnostics]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    try {
        r->get<IService>("my_key");
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("my_key"));
    }
}

// ---------------------------------------------------------------
// Step 1: source_location points to user call site, not library
// ---------------------------------------------------------------

TEST_CASE("duplicate_registration source_location points to user code", "[diagnostics]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceImpl>();

    try {
        reg.add_singleton<IService, ServiceImpl>();  // this line
        FAIL("Expected duplicate_registration");
    } catch (const librtdi::duplicate_registration& e) {
        std::string file = e.location().file_name();
        REQUIRE_THAT(file, Catch::Matchers::ContainsSubstring("test_diagnostics"));
    }
}

TEST_CASE("build() source_location points to user code", "[diagnostics]") {
    librtdi::registry reg;
    reg.build();
    // second build should throw with user location
    try {
        reg.build();  // this line
        FAIL("Expected di_error");
    } catch (const librtdi::di_error& e) {
        std::string file = e.location().file_name();
        REQUIRE_THAT(file, Catch::Matchers::ContainsSubstring("test_diagnostics"));
    }
}

// ---------------------------------------------------------------
// Step 2: not_found from validation includes consumer info
// ---------------------------------------------------------------

TEST_CASE("validation not_found includes consumer type", "[diagnostics]") {
    struct IMissing { virtual ~IMissing() = default; };
    struct IConsumer { virtual ~IConsumer() = default; };
    struct ConsumerImpl : IConsumer {
        explicit ConsumerImpl(IMissing&) {}
    };

    librtdi::registry reg;
    reg.add_singleton<IConsumer, ConsumerImpl>(librtdi::deps<IMissing>);

    try {
        reg.build();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        // Should mention missing type
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("IMissing"));
        // Should mention who requires it
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("required by"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("IConsumer"));
    }
}

TEST_CASE("validation not_found includes impl type", "[diagnostics]") {
    struct IMissing { virtual ~IMissing() = default; };
    struct IConsumer { virtual ~IConsumer() = default; };
    struct MyConsumerImpl : IConsumer {
        explicit MyConsumerImpl(IMissing&) {}
    };

    librtdi::registry reg;
    reg.add_singleton<IConsumer, MyConsumerImpl>(librtdi::deps<IMissing>);

    try {
        reg.build();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("MyConsumerImpl"));
    }
}

TEST_CASE("validation not_found includes registration location", "[diagnostics]") {
    struct IMissing { virtual ~IMissing() = default; };
    struct IConsumer { virtual ~IConsumer() = default; };
    struct ConsumerImpl2 : IConsumer {
        explicit ConsumerImpl2(IMissing&) {}
    };

    librtdi::registry reg;
    reg.add_singleton<IConsumer, ConsumerImpl2>(librtdi::deps<IMissing>);

    try {
        reg.build();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("registered at"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("test_diagnostics"));
    }
}

// ---------------------------------------------------------------
// Step 3: lifetime_mismatch includes impl type
// ---------------------------------------------------------------

TEST_CASE("lifetime_mismatch includes impl type name", "[diagnostics]") {
    struct ISingleton2 { virtual ~ISingleton2() = default; };
    struct ITransient2 { virtual ~ITransient2() = default; };
    struct TransientImpl2 : ITransient2 {};
    struct MySingletonImpl : ISingleton2 {
        explicit MySingletonImpl(std::unique_ptr<ITransient2>) {}
    };

    librtdi::registry reg;
    reg.add_transient<ITransient2, TransientImpl2>();
    reg.add_singleton<ISingleton2, MySingletonImpl>(
        librtdi::deps<librtdi::transient<ITransient2>>);

    try {
        reg.build({.validate_lifetimes = true});
        FAIL("Expected lifetime_mismatch");
    } catch (const librtdi::lifetime_mismatch& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("MySingletonImpl"));
    }
}

// ---------------------------------------------------------------
// Step 4: validation source_location points to build() call site
// ---------------------------------------------------------------

TEST_CASE("cyclic_dependency source_location points to build()", "[diagnostics]") {
    struct IA2 { virtual ~IA2() = default; };
    struct IB2 { virtual ~IB2() = default; };
    struct A2 : IA2 { explicit A2(IB2&) {} };
    struct B2 : IB2 { explicit B2(IA2&) {} };

    librtdi::registry reg;
    reg.add_singleton<IA2, A2>(librtdi::deps<IB2>);
    reg.add_singleton<IB2, B2>(librtdi::deps<IA2>);

    try {
        reg.build();  // this line
        FAIL("Expected cyclic_dependency");
    } catch (const librtdi::cyclic_dependency& e) {
        std::string file = e.location().file_name();
        REQUIRE_THAT(file, Catch::Matchers::ContainsSubstring("test_diagnostics"));
    }
}

// ---------------------------------------------------------------
// Step 6: resolution_error includes registration location
// ---------------------------------------------------------------

TEST_CASE("resolution_error includes registration location", "[diagnostics]") {
    struct IFailing2 { virtual ~IFailing2() = default; };
    struct FailingImpl2 : IFailing2 {
        FailingImpl2() { throw std::runtime_error("boom"); }
    };

    librtdi::registry reg;
    reg.add_singleton<IFailing2, FailingImpl2>();
    auto r = reg.build({.validate_on_build = false, .eager_singletons = false});

    try {
        r->get<IFailing2>();
        FAIL("Expected resolution_error");
    } catch (const librtdi::resolution_error& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("registered at"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("test_diagnostics"));
    }
}

TEST_CASE("non-std-exception from factory propagates as-is", "[diagnostics]") {
    struct ICrash { virtual ~ICrash() = default; };
    struct CrashImpl : ICrash {
        CrashImpl() { throw 42; }  // non-std::exception
    };

    librtdi::registry reg;
    reg.add_transient<ICrash, CrashImpl>();
    auto r = reg.build({.validate_on_build = false, .eager_singletons = false});

    try {
        r->create<ICrash>();
        FAIL("Expected int exception");
    } catch (int val) {
        REQUIRE(val == 42);
    } catch (...) {
        FAIL("Expected int, got something else");
    }
}

// ---------------------------------------------------------------
// Step 7: slot_hint content tests
// ---------------------------------------------------------------

TEST_CASE("slot_hint: singleton requested via create()", "[diagnostics]") {
    struct ISlotS { virtual ~ISlotS() = default; };
    struct SlotSImpl : ISlotS {};

    librtdi::registry reg;
    reg.add_singleton<ISlotS, SlotSImpl>();
    auto r = reg.build({.validate_on_build = false, .eager_singletons = false});

    try {
        r->create<ISlotS>();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("singleton"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("get<T>()"));
    }
}

TEST_CASE("slot_hint: transient requested via get()", "[diagnostics]") {
    struct ISlotT { virtual ~ISlotT() = default; };
    struct SlotTImpl : ISlotT {};

    librtdi::registry reg;
    reg.add_transient<ISlotT, SlotTImpl>();
    auto r = reg.build({.validate_on_build = false, .eager_singletons = false});

    try {
        r->get<ISlotT>();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("transient"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("create<T>()"));
    }
}

TEST_CASE("slot_hint: singleton collection requested via get()", "[diagnostics]") {
    struct ISlotSC { virtual ~ISlotSC() = default; };
    struct SlotSCImpl : ISlotSC {};

    librtdi::registry reg;
    reg.add_collection<ISlotSC, SlotSCImpl>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false, .eager_singletons = false});

    try {
        r->get<ISlotSC>();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("singleton collection"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("get_all<T>()"));
    }
}

TEST_CASE("slot_hint: transient collection requested via create()", "[diagnostics]") {
    struct ISlotTC { virtual ~ISlotTC() = default; };
    struct SlotTCImpl : ISlotTC {};

    librtdi::registry reg;
    reg.add_collection<ISlotTC, SlotTCImpl>(librtdi::lifetime_kind::transient);
    auto r = reg.build({.validate_on_build = false, .eager_singletons = false});

    try {
        r->create<ISlotTC>();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("transient collection"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("create_all<T>()"));
    }
}

// ---------------------------------------------------------------
// Step 8: error path coverage
// ---------------------------------------------------------------

TEST_CASE("cannot register after build", "[diagnostics]") {
    librtdi::registry reg;
    reg.build();

    SECTION("add_singleton after build") {
        REQUIRE_THROWS_AS(
            (reg.add_singleton<IService, ServiceImpl>()),
            librtdi::di_error);
    }
    SECTION("add_transient after build") {
        REQUIRE_THROWS_AS(
            (reg.add_transient<IService, ServiceImpl>()),
            librtdi::di_error);
    }
    SECTION("add_collection after build") {
        REQUIRE_THROWS_AS(
            (reg.add_collection<IService, ServiceImpl>(librtdi::lifetime_kind::singleton)),
            librtdi::di_error);
    }
    SECTION("forward after build") {
        REQUIRE_THROWS_AS(
            (reg.forward<IService, ServiceImpl>()),
            librtdi::di_error);
    }
    SECTION("decorate after build") {
        struct Decorator : IService {
            explicit Decorator(librtdi::decorated_ptr<IService>) {}
        };
        REQUIRE_THROWS_AS(
            (reg.decorate<IService, Decorator>()),
            librtdi::di_error);
    }
}

TEST_CASE("build() can only be called once", "[diagnostics]") {
    librtdi::registry reg;
    reg.build();
    REQUIRE_THROWS_AS(reg.build(), librtdi::di_error);
}

TEST_CASE("keyed duplicate_registration includes key", "[diagnostics]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceImpl>("k1");
    try {
        reg.add_singleton<IService, ServiceImpl>("k1");
        FAIL("Expected duplicate_registration");
    } catch (const librtdi::duplicate_registration& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("k1"));
    }
}

TEST_CASE("di_error re-thrown from factory propagates unchanged", "[diagnostics]") {
    struct IReThrow { virtual ~IReThrow() = default; };
    struct ReThrowImpl : IReThrow {
        ReThrowImpl() { throw librtdi::di_error("custom di error"); }
    };

    librtdi::registry reg;
    reg.add_transient<IReThrow, ReThrowImpl>();
    auto r = reg.build({.validate_on_build = false, .eager_singletons = false});

    try {
        r->create<IReThrow>();
        FAIL("Expected di_error");
    } catch (const librtdi::resolution_error&) {
        FAIL("Should not wrap di_error subclass in resolution_error");
    } catch (const librtdi::di_error& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("custom di error"));
    }
}

TEST_CASE("forward target not registered triggers not_found", "[diagnostics]") {
    struct IBase { virtual ~IBase() = default; };
    struct IDerived : IBase { virtual ~IDerived() = default; };

    librtdi::registry reg;
    reg.forward<IBase, IDerived>();

    REQUIRE_THROWS_AS(reg.build(), librtdi::not_found);
}
