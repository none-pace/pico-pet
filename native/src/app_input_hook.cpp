#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <commctrl.h>
#pragma comment(lib,"comctl32.lib")

namespace {
HWND hostFor(HWND target);
LRESULT CALLBACK canvasProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    HWND host=static_cast<HWND>(GetPropW(window,L"PicoPet.ApplicationHost"));
    const bool root=host!=nullptr;if(!root)host=hostFor(window);
    if(!host || !IsWindow(host) || message==WM_NCDESTROY){RemovePropW(window,L"PicoPet.PopupHost");RemoveWindowSubclass(window,canvasProc,id);return DefSubclassProc(window,message,w,l);}
    if(message==WM_WINDOWPOSCHANGING && GetParent(window)==host && GetPropW(window,L"PicoPet.FillCanvas")){
        auto* pos=reinterpret_cast<WINDOWPOS*>(l);RECT area{};GetClientRect(host,&area);
        pos->x=pos->y=0;pos->cx=area.right;pos->cy=area.bottom;pos->flags&=~(SWP_NOMOVE|SWP_NOSIZE);
    }
    if(message==WM_WINDOWPOSCHANGING && !root){
        auto* pos=reinterpret_cast<WINDOWPOS*>(l);
        if(pos->flags&SWP_HIDEWINDOW)RemovePropW(window,L"PicoPet.PopupHost");
        else {
            SetPropW(window,L"PicoPet.PopupHost",host);
            RECT area{};GetClientRect(host,&area);MapWindowPoints(host,nullptr,reinterpret_cast<POINT*>(&area),2);RECT old{};GetWindowRect(window,&old);
            const LONG width=(pos->flags&SWP_NOSIZE)?old.right-old.left:pos->cx,height=(pos->flags&SWP_NOSIZE)?old.bottom-old.top:pos->cy;
            const LONG x=(pos->flags&SWP_NOMOVE)?old.left:pos->x,y=(pos->flags&SWP_NOMOVE)?old.top:pos->y;
            pos->x=std::clamp(x,area.left,std::max(area.left,area.right-width));pos->y=std::clamp(y,area.top,std::max(area.top,area.bottom-height));pos->flags&=~SWP_NOMOVE;
        }
    }
    return DefSubclassProc(window,message,w,l);
}
void watchCanvas(HWND window){
    HWND host=static_cast<HWND>(GetPropW(window,L"PicoPet.ApplicationHost"));
    DWORD_PTR existing=0;
    const auto style=GetWindowLongPtrW(window,GWL_STYLE);
    const bool root=host && GetParent(window)==host;
    const bool popup=!host && (style&WS_POPUP) && !(style&(WS_CHILD|WS_CAPTION)) && hostFor(window);
    if((root || popup) && !GetWindowSubclass(window,canvasProc,1,&existing)){
        // Subclass callbacks must remain valid even if the capture host crashes.
        // Windows releases this small compatibility module with the app process.
        HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&canvasProc),&module);
        SetWindowSubclass(window,canvasProc,1,0);
    }
}
HWND hostFor(HWND target){
    for(HWND h=target;h;h=(GetWindowLongPtrW(h,GWL_STYLE)&WS_CHILD)?GetParent(h):GetWindow(h,GW_OWNER)){
        wchar_t name[64]{};GetClassNameW(h,name,64);
        if(wcscmp(name,L"PicoPet.ApplicationHost")==0)return h;
        HWND host=static_cast<HWND>(GetPropW(h,L"PicoPet.ApplicationHost"));
        if(host && IsWindow(host) && IsChild(host,h))return host;
        // Popup HWNDs can be pooled and reused for a desktop window. Never trust
        // a cached host property without checking the current ownership chain.
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
        const auto& message=*reinterpret_cast<CWPSTRUCT*>(data);watchCanvas(message.hwnd);
    }
    return CallNextHookEx(nullptr,code,sent,data);
}
