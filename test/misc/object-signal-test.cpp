#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/object.hpp>
#include <wayfire/signal-provider.hpp>

#include <memory>
#include <string>

namespace
{
class test_object_t : public wf::object_base_t
{};

struct test_data_t : public wf::custom_data_t
{
    int value = 0;
};

struct test_signal
{
    int value;
};

class test_provider_t : public wf::signal::provider_t
{};
}

TEST_CASE("object base stores and releases custom data")
{
    test_object_t object;

    auto stored = std::make_unique<test_data_t>();
    stored->value = 42;
    object.store_data(std::move(stored), "custom");

    REQUIRE(object.has_data("custom"));
    REQUIRE(object.get_data<test_data_t>("custom"));
    REQUIRE(object.get_data<test_data_t>("custom")->value == 42);

    auto released = object.release_data<test_data_t>("custom");
    REQUIRE(released);
    REQUIRE(released->value == 42);
    REQUIRE_FALSE(object.has_data("custom"));

    auto safe = object.get_data_safe<test_data_t>();
    safe->value = 17;

    auto safe_again = object.get_data_safe<test_data_t>();
    REQUIRE(safe_again.get() == safe.get());
    REQUIRE(safe_again->value == 17);

    object.erase_data<test_data_t>();
    REQUIRE_FALSE(object.has_data<test_data_t>());
}

TEST_CASE("object base typed properties behave predictably")
{
    test_object_t object;

    REQUIRE(object.set_property("answer", 42));
    REQUIRE(object.has_property("answer"));
    REQUIRE(object.get_property<int>("answer") == 42);
    REQUIRE_FALSE(object.get_property<std::string>("answer").has_value());

    REQUIRE_FALSE(object.set_property("answer", std::string{"wrong type"}));
    REQUIRE(object.get_property<int>("answer") == 42);

    object.erase_property("answer");
    REQUIRE_FALSE(object.has_property("answer"));
    REQUIRE_FALSE(object.get_property<int>("answer").has_value());
}

TEST_CASE("signal provider supports disconnect during emission")
{
    test_provider_t provider;
    wf::signal::connection_t<test_signal> self_disconnect;
    wf::signal::connection_t<test_signal> peer_disconnect;

    int self_calls = 0;
    int peer_calls = 0;

    self_disconnect = [&] (test_signal*)
    {
        ++self_calls;
        self_disconnect.disconnect();
    };

    peer_disconnect = [&] (test_signal*)
    {
        ++peer_calls;
        self_disconnect.disconnect();
    };

    provider.connect(&self_disconnect);
    provider.connect(&peer_disconnect);

    test_signal signal{1};
    provider.emit(&signal);
    provider.emit(&signal);

    REQUIRE(self_calls == 1);
    REQUIRE(peer_calls == 2);
    REQUIRE_FALSE(self_disconnect.is_connected());
    REQUIRE(peer_disconnect.is_connected());
}

TEST_CASE("signal connections disconnect on scope exit and provider destruction")
{
    int scoped_calls = 0;
    auto provider    = std::make_unique<test_provider_t>();
    test_signal signal{1};

    {
        wf::signal::connection_t<test_signal> scoped;
        scoped = [&] (test_signal *ev)
        {
            scoped_calls += ev->value;
        };

        provider->connect(&scoped);
        provider->emit(&signal);
        REQUIRE(scoped_calls == 1);
        REQUIRE(scoped.is_connected());
    }

    provider->emit(&signal);
    REQUIRE(scoped_calls == 1);

    wf::signal::connection_t<test_signal> persistent;
    persistent = [] (test_signal*) {};
    provider->connect(&persistent);
    REQUIRE(persistent.is_connected());

    provider.reset();
    REQUIRE_FALSE(persistent.is_connected());
}
