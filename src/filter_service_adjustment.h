#pragma once
#include "filter_calibration.h"
#include <cstring>
#include <charconv>
namespace eii::host {
// UI only: keep the exact double anchor, never round-trip through a trackbar.
inline bool serviceContinuous(const analog::FilterParameter& d) {
    return std::strcmp(d.unit,"0/1")!=0&&std::strcmp(d.unit,"code")!=0;
}
inline double serviceFineOffset(const analog::FilterParameter& d,double anchor,double normalizedTravel) {
    if(!serviceContinuous(d)||normalizedTravel==0)return anchor;
    constexpr double sensitivity=0.01;
    const double delta=normalizedTravel*sensitivity;
    if(d.logarithmic) {
        const double position=std::clamp(std::log(anchor/d.lo)/std::log(d.hi/d.lo)+delta,0.0,1.0);
        return std::clamp(d.lo*std::pow(d.hi/d.lo,position),d.lo,d.hi);
    }
    return std::clamp(anchor+delta*(d.hi-d.lo),d.lo,d.hi);
}
inline std::string serviceValueText(double value) {
    char text[64];const auto result=std::to_chars(text,text+sizeof(text),value);
    return std::string(text,result.ptr); // shortest exact round-trip, no decimal loop
}
}
