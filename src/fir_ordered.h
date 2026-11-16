#pragma once
#include <array>
namespace eii::analog {
// Split the reverse ring traversal into two contiguous segments.
// Multiplication and accumulation retain the original coefficient order.
inline double orderedFir127(const std::array<double,127>& c,const std::array<double,127>& h,unsigned cursor) noexcept {
    double out=0;unsigned n=0;
    const auto segment=[&](unsigned end,unsigned count){

        while(count--){out+=c[n++]*h[--end];}
    };
    segment(cursor,cursor);segment(127,127-cursor);return out;
}
}
