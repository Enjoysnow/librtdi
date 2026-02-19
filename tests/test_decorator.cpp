#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace librtdi;

// ---------------------------------------------------------------
// Test type hierarchy
// ---------------------------------------------------------------

struct ILogger {
    virtual ~ILogger() = default;
    virtual std::string Log(std::string_view msg) const = 0;
};

struct ConsoleLogger : ILogger {
    std::string Log(std::string_view msg) const override {
        return "console:" + std::string(msg);
    }
};

struct FileLogger : ILogger {
    std::string Log(std::string_view msg) const override {
        return "file:" + std::string(msg);
    }
};

// Decorator 1: prepends timestamp
struct TimestampLogger : ILogger {
    std::shared_ptr<ILogger> inner_;
    explicit TimestampLogger(std::shared_ptr<ILogger> inner) : inner_(std::move(inner)) {}
    std::string Log(std::string_view msg) const override {
        return "[TIME]" + inner_->Log(msg);
    }
};

// Decorator 2: prepends prefix
struct PrefixLogger : ILogger {
    std::shared_ptr<ILogger> inner_;
    explicit PrefixLogger(std::shared_ptr<ILogger> inner) : inner_(std::move(inner)) {}
    std::string Log(std::string_view msg) const override {
        return "[PFX]" + inner_->Log(msg);
    }
};

// Extra dependency for decorator with deps
struct IFormatter {
    virtual ~IFormatter() = default;
    virtual std::string Format(std::string_view s) const = 0;
};

struct UpperFormatter : IFormatter {
    std::string Format(std::string_view s) const override {
        std::string result(s);
        for (auto& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return result;
    }
};

// Decorator with extra dependency
struct FormattedLogger : ILogger {
    std::shared_ptr<ILogger> inner_;
    std::shared_ptr<IFormatter> fmt_;
    FormattedLogger(std::shared_ptr<ILogger> inner, std::shared_ptr<IFormatter> fmt)
        : inner_(std::move(inner)), fmt_(std::move(fmt)) {}
    std::string Log(std::string_view msg) const override {
        return inner_->Log(fmt_->Format(msg));
    }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("Decorator: basic single decoration", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.decorate<ILogger, TimestampLogger>();
    auto resolver = registry.build();

    auto logger = resolver->resolve<ILogger>();
    REQUIRE(logger->Log("hi") == "[TIME]console:hi");
}

TEST_CASE("Decorator: onion layering (D1 then D2)", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.decorate<ILogger, TimestampLogger>();  // first wrap
    registry.decorate<ILogger, PrefixLogger>();     // second wrap (outermost)
    auto resolver = registry.build();

    auto logger = resolver->resolve<ILogger>();
    // PrefixLogger wraps TimestampLogger wraps ConsoleLogger
    REQUIRE(logger->Log("hi") == "[PFX][TIME]console:hi");
}

TEST_CASE("Decorator: repeated decoration (D1, D2, D1)", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.decorate<ILogger, TimestampLogger>();
    registry.decorate<ILogger, PrefixLogger>();
    registry.decorate<ILogger, TimestampLogger>();  // D1 again
    auto resolver = registry.build();

    auto logger = resolver->resolve<ILogger>();
    // TimestampLogger(PrefixLogger(TimestampLogger(ConsoleLogger)))
    REQUIRE(logger->Log("x") == "[TIME][PFX][TIME]console:x");
}

TEST_CASE("Decorator: with extra dependency", "[decorator]") {
    registry registry;
    registry.add_singleton<IFormatter, UpperFormatter>();
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.decorate<ILogger, FormattedLogger>(deps<IFormatter>);
    auto resolver = registry.build();

    auto logger = resolver->resolve<ILogger>();
    REQUIRE(logger->Log("hello") == "console:HELLO");
}

TEST_CASE("Decorator: decorates all implementations (global)", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.add_singleton<ILogger, FileLogger>();
    registry.decorate<ILogger, TimestampLogger>();  // global: decorates both
    auto resolver = registry.build();

    auto all = resolver->resolve_all<ILogger>();
    REQUIRE(all.size() == 2);
    REQUIRE(all[0]->Log("a") == "[TIME]console:a");
    REQUIRE(all[1]->Log("b") == "[TIME]file:b");
}

TEST_CASE("Decorator: decorates specific implementation only", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.add_singleton<ILogger, FileLogger>();
    registry.decorate<ILogger, TimestampLogger>(typeid(ConsoleLogger));
    auto resolver = registry.build();

    auto all = resolver->resolve_all<ILogger>();
    REQUIRE(all.size() == 2);
    REQUIRE(all[0]->Log("a") == "[TIME]console:a");  // ConsoleLogger decorated
    REQUIRE(all[1]->Log("b") == "file:b");            // FileLogger untouched
}

TEST_CASE("Decorator: targeted + global mix (both applied in order)", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.add_singleton<ILogger, FileLogger>();
    // First: target ConsoleLogger only
    registry.decorate<ILogger, PrefixLogger>(typeid(ConsoleLogger));
    // Second: global
    registry.decorate<ILogger, TimestampLogger>();
    auto resolver = registry.build();

    auto all = resolver->resolve_all<ILogger>();
    REQUIRE(all.size() == 2);
    // ConsoleLogger: both applied in order → TimestampLogger(PrefixLogger(ConsoleLogger))
    REQUIRE(all[0]->Log("a") == "[TIME][PFX]console:a");
    // FileLogger: only global applied → TimestampLogger(FileLogger)
    REQUIRE(all[1]->Log("b") == "[TIME]file:b");
}

TEST_CASE("Decorator: inherits singleton lifetime", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.decorate<ILogger, TimestampLogger>();
    auto resolver = registry.build();

    auto a = resolver->resolve<ILogger>();
    auto b = resolver->resolve<ILogger>();
    REQUIRE(a.get() == b.get());
}

TEST_CASE("Decorator: inherits scoped lifetime", "[decorator]") {
    registry registry;
    registry.add_scoped<ILogger, ConsoleLogger>();
    registry.decorate<ILogger, TimestampLogger>();
    auto resolver = registry.build();

    auto scope1 = resolver->create_scope();
    auto scope2 = resolver->create_scope();
    auto a = scope1->get_resolver().resolve<ILogger>();
    auto b = scope1->get_resolver().resolve<ILogger>();
    auto c = scope2->get_resolver().resolve<ILogger>();

    REQUIRE(a.get() == b.get());   // same scope
    REQUIRE(a.get() != c.get());   // different scope
}

TEST_CASE("Decorator: missing extra dep fails validation", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    // FormattedLogger needs IFormatter, which is NOT registered
    registry.decorate<ILogger, FormattedLogger>(deps<IFormatter>);

    REQUIRE_THROWS_AS(registry.build(), not_found);
}

TEST_CASE("Decorator: still applies after Replace", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.decorate<ILogger, TimestampLogger>();
    // Replace: removes ConsoleLogger, adds FileLogger
    registry.add_singleton<ILogger, FileLogger>(registration_policy::replace);
    auto resolver = registry.build();

    auto logger = resolver->resolve<ILogger>();
    // Decorator should apply to the replacement (FileLogger)
    REQUIRE(logger->Log("x") == "[TIME]file:x");
}

TEST_CASE("Decorator: targeted for removed impl silently no-ops", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    // Target ConsoleLogger specifically
    registry.decorate<ILogger, TimestampLogger>(typeid(ConsoleLogger));
    // Replace removes ConsoleLogger, adds FileLogger
    registry.add_singleton<ILogger, FileLogger>(registration_policy::replace);
    auto resolver = registry.build();

    auto logger = resolver->resolve<ILogger>();
    // Targeted decorator for ConsoleLogger has no effect (ConsoleLogger gone)
    REQUIRE(logger->Log("x") == "file:x");
}

TEST_CASE("Decorator: keyed registrations are also decorated", "[decorator]") {
    registry registry;
    registry.add_singleton<ILogger, ConsoleLogger>();
    registry.add_singleton<ILogger, FileLogger>("file-key");
    // Global decorator — applies to all ILogger descriptors including keyed
    registry.decorate<ILogger, TimestampLogger>();
    auto resolver = registry.build();

    auto non_keyed = resolver->resolve<ILogger>();
    auto keyed = resolver->resolve<ILogger>("file-key");
    REQUIRE(non_keyed->Log("a") == "[TIME]console:a");
    REQUIRE(keyed->Log("b") == "[TIME]file:b");
}

TEST_CASE("Decorator: wraps forward descriptor", "[decorator]") {
    // forward<ILogger, ConsoleLogger> creates a descriptor for ILogger.
    // Decorating ILogger should also wrap the forward descriptor.
    registry registry;
    registry.add_singleton<ConsoleLogger, ConsoleLogger>();
    registry.forward<ILogger, ConsoleLogger>();
    registry.decorate<ILogger, TimestampLogger>();
    auto resolver = registry.build();

    auto logger = resolver->resolve<ILogger>();
    REQUIRE(logger->Log("hi") == "[TIME]console:hi");
}
