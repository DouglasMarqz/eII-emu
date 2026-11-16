#pragma once
#include <windows.h>
#include <gdiplus.h>
namespace eii::host {
// BeginPaint validates the window's update region. Preserve its exact shape
// first: PAINTSTRUCT::rcPaint is only the bounding rectangle of that shape.
class WindowsPaintClip {
    HRGN region_=CreateRectRgn(0,0,0,0);
    bool valid_=false;
public:
    explicit WindowsPaintClip(HWND window)noexcept {
        if(region_)valid_=GetUpdateRgn(window,region_,FALSE)!=ERROR;
    }
    ~WindowsPaintClip(){if(region_)DeleteObject(region_);}
    WindowsPaintClip(const WindowsPaintClip&)=delete;
    WindowsPaintClip& operator=(const WindowsPaintClip&)=delete;
    void apply(Gdiplus::Graphics& graphics,const RECT& bounds)const noexcept {
        if(valid_ && graphics.SetClip(region_)==Gdiplus::Ok)return;
        graphics.SetClip(Gdiplus::Rect(bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top));
    }
};
}
