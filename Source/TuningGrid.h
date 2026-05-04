// Tuning-grid abstraction. v1b ships a 5-limit Just grid; v2 will add others
// (Pythagorean, meantone variants, equal temperament for sanity, Scala-loaded).
//
// Convention: all positions in cents-mod-octave, [0, 1200). The "delta" returned
// by nearestDeltaCents is the SIGNED shift to apply — positive means the input
// pitch should be pulled UP (it's flat of the nearest grid point); negative
// means pulled DOWN. Wrapping at the octave boundary uses the shortest path
// (so 1190 cents pulls up to 0/1200, not down to ~1088).

#pragma once

#include <vector>

namespace autojust
{

class TuningGrid
{
public:
    virtual ~TuningGrid() = default;

    /** Signed cents shift to nearest grid point along the shortest circular
        path (so |result| ≤ 600). */
    virtual float nearestDeltaCents (float centsModOctave) const = 0;

    virtual const char* getName() const = 0;
};

/** Standard 5-limit Just Intonation 12-tone scale.
    Ratios used (octave-equivalent):
       1/1, 16/15, 9/8, 6/5, 5/4, 4/3, 45/32, 3/2, 8/5, 5/3, 16/9, 15/8 */
class JustGrid5Limit : public TuningGrid
{
public:
    JustGrid5Limit();
    float nearestDeltaCents (float centsModOctave) const override;
    const char* getName() const override { return "5-limit Just"; }

    /** Read-only access for diagnostics / tests. */
    const std::vector<float>& getRatiosCents() const noexcept { return ratiosCents; }

private:
    std::vector<float> ratiosCents;
};

} // namespace autojust
