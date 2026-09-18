#include <windows.h>

// A redirected pointer lives on the television, not over the off-screen source
// HWND. Suppress only the source's automatic leave while that exact HWND is
// under the projected pointer. All other input, including native focus, is kept.
extern "C" __declspec(dllexport) LRESULT CALLBACK AppInputMessage(int code,WPARAM remove,LPARAM data){
    if(code>=0 && remove==PM_REMOVE){
        auto& message=*reinterpret_cast<MSG*>(data);
        HWND owner=static_cast<HWND>(GetPropW(message.hwnd,L"PicoPet.ProjectedHover"));
        if(message.message==WM_MOUSELEAVE && owner && IsWindow(owner) && IsChild(owner,message.hwnd)){
            message.message=WM_NULL;message.wParam=message.lParam=0;
        }
    }
    return CallNextHookEx(nullptr,code,remove,data);
}
