#pragma once
#include <unordered_map>

namespace windowlayer {
struct App {DWORD pid=0;std::wstring path,title;int windows=0;};
inline bool samePath(const std::wstring& a,const std::wstring& b){return !a.empty() && _wcsicmp(a.c_str(),b.c_str())==0;}
inline std::wstring processPath(DWORD pid){
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return {};
    std::wstring path(32768,L'\0');DWORD length=static_cast<DWORD>(path.size());
    const bool found=QueryFullProcessImageNameW(process,0,path.data(),&length)!=FALSE;CloseHandle(process);
    if(!found)return {};path.resize(length);return path;
}
inline bool taskbarWindow(HWND window,DWORD excludedProcess=GetCurrentProcessId()){
    if(!IsWindow(window) || !IsWindowVisible(window) || GetAncestor(window,GA_ROOT)!=window)return false;
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(!pid || pid==excludedProcess)return false;
    const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if(style&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE))return false;
    if(GetWindow(window,GW_OWNER) && !(style&WS_EX_APPWINDOW))return false;
    DWORD cloaked=0;if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked)return false;
    wchar_t name[128]{};GetClassNameW(window,name,128);
    for(const auto* shell:{L"Progman",L"WorkerW",L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",L"Windows.UI.Core.CoreWindow",L"PicoPet.Win11.Native"})if(wcscmp(name,shell)==0)return false;
    return GetWindowTextLengthW(window)>0;
}
inline std::vector<App> applications(){
    std::vector<App> result;
    EnumWindows([](HWND window,LPARAM data)->BOOL{
        if(!taskbarWindow(window))return TRUE;
        auto& apps=*reinterpret_cast<std::vector<App>*>(data);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        auto found=std::find_if(apps.begin(),apps.end(),[&](const App& app){return app.pid==pid;});
        if(found!=apps.end()){++found->windows;return TRUE;}
        auto path=processPath(pid);if(path.empty())return TRUE;
        wchar_t title[512]{};GetWindowTextW(window,title,512);apps.push_back({pid,std::move(path),title,1});return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    std::sort(result.begin(),result.end(),[](const App& a,const App& b){const int order=_wcsicmp(a.path.c_str(),b.path.c_str());return order?order<0:a.pid<b.pid;});
    return result;
}
class Controller {
    HWND owner=nullptr,anchor=nullptr;DWORD targetPid=0;std::wstring targetPath;
    UINT notification=0;std::array<HWINEVENTHOOK,5> hooks{};bool queued=false,changing=false;
    inline static Controller* active=nullptr;
    static void CALLBACK event(HWINEVENTHOOK,DWORD type,HWND window,LONG object,LONG child,DWORD,DWORD){
        auto* self=active;if(!self || window==self->owner)return;
        if(type==EVENT_OBJECT_REORDER){if(window && object!=OBJID_WINDOW && GetAncestor(window,GA_ROOT)!=window)return;}
        else if(type>=EVENT_OBJECT_CREATE && (object!=OBJID_WINDOW || child!=CHILDID_SELF))return;
        if(type==EVENT_OBJECT_STATECHANGE || type==EVENT_OBJECT_LOCATIONCHANGE){DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(self->targetPid && pid!=self->targetPid)return;}
        if(!self->queued){self->queued=PostMessageW(self->owner,self->notification,0,0)!=FALSE;}
    }
public:
    ~Controller(){stop();}
    void stop(){for(auto& hook:hooks){if(hook)UnhookWinEvent(hook);hook=nullptr;}if(active==this)active=nullptr;queued=false;anchor=nullptr;}
    void configure(HWND window,UINT message,bool enabled,const std::wstring& path,DWORD pid){
        if(!enabled){stop();targetPath=path;targetPid=pid;return;}
        owner=window;notification=message;
        if(targetPath!=path || (pid && pid!=targetPid)){anchor=nullptr;targetPid=pid;targetPath=path;}
        if(!hooks[0]){active=this;
            constexpr DWORD flags=WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS;
            hooks[0]=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_REORDER,nullptr,event,0,0,flags);
            hooks[1]=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,event,0,0,flags);
            hooks[2]=SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND,nullptr,event,0,0,flags);
            hooks[3]=SetWinEventHook(EVENT_OBJECT_CLOAKED,EVENT_OBJECT_UNCLOAKED,nullptr,event,0,0,flags);
            hooks[4]=SetWinEventHook(EVENT_OBJECT_STATECHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,event,0,0,flags);
        }
    }
    void acknowledge(){queued=false;}
    HWND target()const{return anchor;}
    DWORD pid()const{return targetPid;}
    void resolve(){
        anchor=nullptr;if(targetPath.empty())return;
        // A PID is only a live-session hint; always verify the executable before reattaching.
        if(!samePath(processPath(targetPid),targetPath))targetPid=0;
        struct Search {Controller* self;std::unordered_map<DWORD,bool> matches;};Search search{this,{}};
        EnumWindows([](HWND window,LPARAM data)->BOOL{
            auto& search=*reinterpret_cast<Search*>(data);auto* self=search.self;
            if(!taskbarWindow(window))return TRUE;
            DWORD pid=0;GetWindowThreadProcessId(window,&pid);
            if(self->targetPid && pid!=self->targetPid)return TRUE;
            auto [entry,added]=search.matches.emplace(pid,false);if(added)entry->second=samePath(processPath(pid),self->targetPath);
            if(!entry->second)return TRUE;
            if(!self->targetPid)self->targetPid=pid;
            if(!IsIconic(window))self->anchor=window;return TRUE;
        },reinterpret_cast<LPARAM>(&search));
    }
    void apply(HWND window,bool below,bool topmost,bool refresh=false){
        if(changing)return;changing=true;
        if(below && refresh)resolve();
        HWND targetWindow=below && taskbarWindow(anchor) && !IsIconic(anchor)?anchor:nullptr;
        const bool desiredTop=targetWindow?(GetWindowLongPtrW(targetWindow,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0:!below && topmost;
        const bool actualTop=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
        constexpr UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
        if(actualTop!=desiredTop)SetWindowPos(window,desiredTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,flags);
        if(targetWindow && GetWindow(targetWindow,GW_HWNDNEXT)!=window)SetWindowPos(window,targetWindow,0,0,0,0,flags);
        changing=false;
    }
};
}
