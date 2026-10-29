#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Tuning.h"

using Catch::Matchers::WithinAbs;
using namespace kf::tuning;

TEST_CASE ("Tuning midi <-> Hz")
{
    REQUIRE_THAT (midiToHz (69), WithinAbs (440.0, 1e-9));
    REQUIRE_THAT (midiToHz (69, 432.0), WithinAbs (432.0, 1e-9));
    REQUIRE_THAT (midiToHz (33), WithinAbs (55.0, 1e-9));
    REQUIRE_THAT (hzToMidi (261.6255653), WithinAbs (60.0, 1e-5));
    REQUIRE (hzToMidi (0) == 0.0);
}

TEST_CASE ("Tuning note names and cents")
{
    auto a = hzToNote (55.0);
    REQUIRE (a.name == "A1");
    REQUIRE_THAT (a.cents, WithinAbs (0.0, 1e-6));
    auto b = hzToNote (midiToHz (36 + 0.3));
    REQUIRE (b.name == "C2");
    REQUIRE_THAT (b.cents, WithinAbs (30.0, 1e-6));
    REQUIRE (hzToNote (midiToHz (46, 432.0), 432.0).name == "A#2");
    REQUIRE (midiToName (60) == "C4");
    REQUIRE (midiToName (0) == "C-1");
}
