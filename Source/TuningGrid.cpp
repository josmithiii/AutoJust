#include "TuningGrid.h"

#include <cmath>
#include <algorithm>

namespace autojust
{

JustGrid5Limit::JustGrid5Limit()
{
    // 5-limit ratios (cents = 1200 · log2(ratio)):
    //   1/1     0.0000
    //   16/15 111.7313
    //    9/8  203.9100
    //    6/5  315.6413
    //    5/4  386.3137
    //    4/3  498.0450
    //   45/32 590.2237
    //    3/2  701.9550
    //    8/5  813.6863
    //    5/3  884.3587
    //   16/9  996.0900
    //   15/8 1088.2687
    ratiosCents = {
           0.0000f,  111.7313f,  203.9100f,  315.6413f,
         386.3137f,  498.0450f,  590.2237f,  701.9550f,
         813.6863f,  884.3587f,  996.0900f, 1088.2687f
    };
}

float JustGrid5Limit::nearestDeltaCents (float centsModOctave) const
{
    // Wrap input to [0, 1200).
    float c = std::fmod (centsModOctave, 1200.0f);
    if (c < 0.0f) c += 1200.0f;

    float bestSigned = 0.0f;
    float bestAbs    = 1.0e30f;
    for (const float r : ratiosCents)
    {
        float d = r - c;
        if (d >  600.0f) d -= 1200.0f;
        if (d < -600.0f) d += 1200.0f;
        const float ad = std::abs (d);
        if (ad < bestAbs) { bestAbs = ad; bestSigned = d; }
    }
    return bestSigned;
}

} // namespace autojust
