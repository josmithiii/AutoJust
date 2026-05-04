// Verifies JustGrid5Limit produces the expected 5-limit cents values and
// returns shortest-path signed deltas for arbitrary inputs.

#include "../Source/TuningGrid.h"

#include <cmath>
#include <cstdio>

namespace
{

struct Result { const char* name; bool ok; };

Result expectDelta (const char* name,
                    const autojust::TuningGrid& g,
                    float input, float expected, float tol = 0.01f)
{
    const float got = g.nearestDeltaCents (input);
    const bool  ok  = std::abs (got - expected) < tol;
    std::printf ("  %-44s in=%8.3f  expected=%+8.3f  got=%+8.3f  %s\n",
                 name, input, expected, got, ok ? "PASS" : "*** FAIL");
    return { name, ok };
}

} // namespace

int main()
{
    autojust::JustGrid5Limit g;
    int passed = 0, total = 0;
    auto run = [&] (Result r) { if (r.ok) ++passed; ++total; };

    std::printf ("TuningGrid (5-limit Just) test\n");

    // Inputs exactly on grid points → delta 0.
    run (expectDelta ("on tonic 0",                   g,    0.0f,    0.000f));
    run (expectDelta ("on 5/4 (386.31)",              g,  386.31f,  0.0037f));
    run (expectDelta ("on 3/2 (701.96)",              g,  701.96f,  -0.005f));

    // ET intervals snap to nearest 5-limit (signed direction matters).
    run (expectDelta ("ET m3 400 → JI 5/4 (386.31)",  g, 400.00f, -13.686f));
    run (expectDelta ("ET P5 700 → JI 3/2 (701.96)",  g, 700.00f,  +1.955f));
    run (expectDelta ("ET m6 800 → JI 8/5 (813.69)",  g, 800.00f, +13.686f));
    run (expectDelta ("ET M2 200 → JI 9/8 (203.91)",  g, 200.00f,  +3.910f));

    // Octave wrap: 1190 → up to 1200 (= 0), shortest path is +10.
    run (expectDelta ("near octave 1190 → 1200 (=0)", g, 1190.00f, +10.000f));

    // Halfway between 0 and 111.73: midpoint is 55.86; goes back to 0.
    // (55 cents → 0 is closer than 55 → 111.73; delta = -55.)
    run (expectDelta ("55 cents → 0",                 g,   55.00f,  -55.000f));
    run (expectDelta ("60 cents → 111.73",            g,   60.00f, +51.731f));

    std::printf ("\n%d / %d tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
