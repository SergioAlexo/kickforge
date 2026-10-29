#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Curve.h"

using Catch::Matchers::WithinAbs;
using kf::Curve;

TEST_CASE ("Curve linear eval and clamping")
{
    Curve c ({ { 0, 0, 0 }, { 1, 10, 0 } });
    REQUIRE_THAT (c.eval (0.5), WithinAbs (5.0, 1e-12));
    REQUIRE_THAT (c.eval (-1), WithinAbs (0.0, 1e-12));
    REQUIRE_THAT (c.eval (2), WithinAbs (10.0, 1e-12));
    REQUIRE (Curve().eval (0.3) == 0.0);
}

TEST_CASE ("Curve tension shapes the segment")
{
    Curve pos ({ { 0, 0, 1 }, { 1, 1, 0 } });
    Curve neg ({ { 0, 0, -1 }, { 1, 1, 0 } });
    REQUIRE (pos.eval (0.5) < 0.1); // slow start
    REQUIRE (neg.eval (0.5) > 0.9); // fast start
    REQUIRE_THAT (pos.eval (1.0), WithinAbs (1.0, 1e-12));
}

TEST_CASE ("Curve points sort and edit")
{
    Curve c;
    c.addPoint (1.0, 0.0);
    c.addPoint (0.0, 1.0);
    c.addPoint (0.5, 0.5);
    REQUIRE (c.getPoints()[1].time == 0.5);
    REQUIRE_THAT (c.eval (0.25), WithinAbs (0.75, 1e-12));
    REQUIRE_THAT (c.eval (0.75), WithinAbs (0.25, 1e-12));
    const int idx = c.movePoint (1, 2.0, 3.0);
    REQUIRE (idx == 2);
    c.removePoint (0);
    REQUIRE (c.size() == 2);
}

TEST_CASE ("Curve walker and fill match eval")
{
    Curve c ({ { 0, 1, 0.4 }, { 0.3, 0.2, -0.6 }, { 1, 0.7, 0 } });
    auto w = c.walker();
    std::vector<double> buf (200);
    c.fill (buf.data(), 200, 0.0, 1.0 / 199);
    for (int i = 0; i < 200; ++i)
    {
        const double t = i / 199.0;
        REQUIRE_THAT (w.at (t), WithinAbs (c.eval (t), 1e-12));
        REQUIRE_THAT (buf[(size_t) i], WithinAbs (c.eval (t), 1e-12));
    }
    REQUIRE_THAT (w.at (0.1), WithinAbs (c.eval (0.1), 1e-12)); // backwards jump
}

TEST_CASE ("Curve ValueTree round trip")
{
    Curve c ({ { 0, 1, 0.25 }, { 0.2, 0.5, -0.5 }, { 1, 0, 0 } });
    auto xml = c.toValueTree().toXmlString();
    auto back = Curve::fromValueTree (juce::ValueTree::fromXml (xml));
    REQUIRE (back == c);
    REQUIRE (Curve::fromValueTree (juce::ValueTree ("OTHER")).empty());
}
