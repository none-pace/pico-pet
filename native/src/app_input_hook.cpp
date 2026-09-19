#define NOMINMAX
#include <windows.h>
#include <algorithm>

namespace {
HWND hostFor(HWND target){
    for(HWND h=target;h;h=(GetWindowLongPtrW(h,GWL_STYLE)&WS_CHILD)?GetParent(h):GetWindow(h,GW_OWNER)){
        wchar_t name[64]{};GetClassNameW(h,name,64);
        if(wcscmp(name,L"PicoPet.ApplicationHost")==0)return h;
        HWND host=static_cast<HWND>(GetPropW(h,L"PicoPet.ApplicationHost"));
        if(host && IsWindow(host) && IsChild(host,h))return host;
        host=static_cast<HWND>(GetPropW(h,L"PicoPet.PopupHost"));
        if(host && IsWindow(host))return host;
    }
    return nullptr;
}
}

// A redirected pointer lives on the television, not over the off-screen source
// HWND. Suppress only the source's automatic leave while that exact HWND is
// under the projected pointer. All other input, including native focus, is kept.
extern "C" __declspec(dllexport) LRESULT CALLBACK AppInputMessage(int code,WPARAM remove,LPARAM data){
    if(code>=0 && remove==PM_REMOVE){
        auto& message=*reinterpret_cast<MSG*>(data);
        if(message.message>=WM_MOUSEFIRST && message.message<=WM_MOUSELAST && hostFor(message.hwnd)){
            message.pt={static_cast<short>(LOWORD(message.lParam)),static_cast<short>(HIWORD(message.lParam))};
            if(message.message!=WM_MOUSEWHEEL && message.message!=WM_MOUSEHWHEEL)ClientToScreen(message.hwnd,&message.pt);
        }
        HWND owner=static_cast<HWND>(GetPropW(message.hwnd,L"PicoPet.ProjectedHover"));
        if(message.message==WM_MOUSELEAVE && owner && IsWindow(owner) && (IsChild(owner,message.hwnd) || hostFor(message.hwnd)==owner)){
            message.message=WM_NULL;message.wParam=message.lParam=0;
        }
    }
    return CallNextHookEx(nullptr,code,remove,data);
}

// Relocate owned popup sources before display, preserving their native ownership.
extern "C" __declspec(dllexport) LRESULT CALLBACK AppWindowMessage(int code,WPARAM sent,LPARAM data){
    if(code>=0){
        const auto& message=*reinterpret_cast<CWPSTRUCT*>(data);
        const auto style=GetWindowLongPtrW(message.hwnd,GWL_STYLE);
        if(message.message==WM_WINDOWPOSCHANGING && (style&WS_POPUP) && !(style&(WS_CHILD|WS_CAPTION))){
            auto* pos=reinterpret_cast<WINDOWPOS*>(message.lParam);HWND host=hostFor(message.hwnd);
            if(pos->flags&SWP_HIDEWINDOW){RemovePropW(message.hwnd,L"PicoPet.PopupHost");return CallNextHookEx(nullptr,code,sent,data);}
            if(host && host!=message.hwnd){
                SetPropW(message.hwnd,L"PicoPet.PopupHost",host);
                RECT area{};GetClientRect(host,&area);MapWindowPoints(host,nullptr,reinterpret_cast<POINT*>(&area),2);
                RECT old{};GetWindowRect(message.hwnd,&old);
                const LONG width=(pos->flags&SWP_NOSIZE)?old.right-old.left:pos->cx;
                const LONG height=(pos->flags&SWP_NOSIZE)?old.bottom-old.top:pos->cy;
                const LONG x=(pos->flags&SWP_NOMOVE)?old.left:pos->x,y=(pos->flags&SWP_NOMOVE)?old.top:pos->y;
                pos->x=std::clamp(x,area.left,std::max(area.left,area.right-width));
                pos->y=std::clamp(y,area.top,std::max(area.top,area.bottom-height));pos->flags&=~SWP_NOMOVE;
            }
        }
    }
    return CallNextHookEx(nullptr,code,sent,data);
}
