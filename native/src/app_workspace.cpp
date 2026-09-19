#include "app_workspace.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <vector>
#include <filesystem>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include "window_layer.h"


namespace appworkspace {
namespace {
constexpr int Width=800,Height=500;
constexpr int SourceWidth=2048,SourceHeight=1400;
SIZE canvas(int resolution,UINT dpi=96){
    const int width=resolution==3?std::clamp(MulDiv(1024,static_cast<int>(dpi?dpi:96),96),1280,1920):resolution==2?1600:resolution==1?1280:Width;
    return {width,MulDiv(width,Height,Width)};
}
constexpr UINT Attach=WM_APP+30,Configure=WM_APP+31,Release=WM_APP+32,Pointer=WM_APP+33,Keyboard=WM_APP+34,Paused=WM_APP+35,Cycle=WM_APP+36,Immersive=WM_APP+37,PopupChanged=WM_APP+38;
struct Shared {
    DWORD parent=0;HWND owner=nullptr,host=nullptr;
    LONG generation=0,count=0,mode=0,fps=60,closing=0,resolution=0,framePending=0,quitting=0;
    wchar_t status[256]{};
    std::array<uint32_t,Width*Height> pixels{};
};
struct Link {
    HANDLE mapping=nullptr,mutex=nullptr;Shared* data=nullptr;
    ~Link(){if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);if(mutex)CloseHandle(mutex);}
    bool lock(){if(!mutex)return false;const DWORD result=WaitForSingleObject(mutex,0);return result==WAIT_OBJECT_0 || result==WAIT_ABANDONED;}
    void unlock(){ReleaseMutex(mutex);}
    bool connect(const std::wstring& name,bool create){
        mapping=create?CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str()):OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
        mutex=create?CreateMutexW(nullptr,FALSE,(name+L".lock").c_str()):OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(name+L".lock").c_str());
        if(mapping)data=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
        return data && mutex;
    }
};
bool chromium(HWND window){wchar_t name[128]{};GetClassNameW(window,name,128);return wcsncmp(name,L"Chrome_WidgetWin",16)==0;}
struct LaunchTarget {std::wstring executable,arguments,directory;bool browser=false;};
LaunchTarget launchTarget(const std::wstring& path){
    LaunchTarget target;target.executable=path;
    if(_wcsicmp(std::filesystem::path(path).extension().c_str(),L".lnk")==0){
        Microsoft::WRL::ComPtr<IShellLinkW> link;Microsoft::WRL::ComPtr<IPersistFile> file;
        if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(link.GetAddressOf()))) && SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Load(path.c_str(),STGM_READ))){
            wchar_t buffer[32768]{};
            if(SUCCEEDED(link->GetPath(buffer,32768,nullptr,SLGP_RAWPATH)) && buffer[0]){wchar_t expanded[32768]{};const DWORD length=ExpandEnvironmentStringsW(buffer,expanded,32768);if(length && length<=32768)target.executable=expanded;}
            if(SUCCEEDED(link->GetArguments(buffer,32768)))target.arguments=buffer;
            if(SUCCEEDED(link->GetWorkingDirectory(buffer,32768)))target.directory=buffer;
        }
    }
    const auto name=std::filesystem::path(target.executable).filename().wstring();
    for(const auto* browser:{L"msedge.exe",L"chrome.exe",L"brave.exe"})if(_wcsicmp(name.c_str(),browser)==0)target.browser=true;
    if(target.browser)target.arguments=L"--new-window "+target.arguments;
    return target;
}
RECT contentRect(HWND window){
    RECT result{};GetWindowRect(window,&result);const int w=result.right-result.left,h=result.bottom-result.top;result={0,0,w,h};
    struct Search{HWND root;RECT rect;bool found=false;} search{window,result};
    EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL{auto& search=*reinterpret_cast<Search*>(data);wchar_t name[128]{};GetClassNameW(child,name,128);
        if(wcscmp(name,L"Chrome_RenderWidgetHostHWND")==0 && IsWindowVisible(child)){RECT r{},outer{};GetWindowRect(child,&r);GetWindowRect(search.root,&outer);OffsetRect(&r,-outer.left,-outer.top);if(r.right-r.left>100 && r.bottom-r.top>100){search.rect=r;search.found=true;return FALSE;}}return TRUE;},reinterpret_cast<LPARAM>(&search));
    return search.found?search.rect:result;
}
std::vector<HWND> popupWindows(HWND host){
    struct Search {HWND host;std::vector<HWND> windows;} search{host,{}};
    EnumWindows([](HWND h,LPARAM data)->BOOL{auto& s=*reinterpret_cast<Search*>(data);
        if(IsWindowVisible(h) && GetPropW(h,L"PicoPet.PopupHost")==s.host)s.windows.push_back(h);return TRUE;
    },reinterpret_cast<LPARAM>(&search));return search.windows;
}
RECT popupDisplayRect(HWND popup,HWND host,RECT viewport){
    RECT rect{};GetWindowRect(popup,&rect);MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&rect),2);
    const LONG w=std::max(1L,rect.right-rect.left),h=std::max(1L,rect.bottom-rect.top);
    const double scale=std::min({1.0,static_cast<double>(viewport.right-viewport.left)/w,static_cast<double>(viewport.bottom-viewport.top)/h});
    const LONG dw=std::max(1L,static_cast<LONG>(w*scale)),dh=std::max(1L,static_cast<LONG>(h*scale));
    const LONG x=std::clamp(rect.right-dw,viewport.left,viewport.right-dw),y=std::clamp(rect.top,viewport.top,viewport.bottom-dh);
    return {x,y,x+dw,y+dh};
}
struct Capture {
    struct State {
        HWND window=nullptr;std::atomic_bool stop=false,paused=false;std::atomic_int fps=60,resolution=0;std::atomic_bool single=true,immersive=false;
        std::shared_ptr<Link> link;HANDLE wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        ~State(){if(wake)CloseHandle(wake);}
    };
    std::shared_ptr<State> state;
    void stop(){if(state){state->stop=true;SetEvent(state->wake);}state.reset();}
    ~Capture(){stop();}
    void wake(){if(state)SetEvent(state->wake);}
    void pause(bool value){if(state){state->paused=value;wake();}}
    void rate(int fps){if(state){state->fps=std::clamp(fps,5,60);wake();}}
    void immersive(bool value){if(state)state->immersive=value;}
    void single(bool value){if(state)state->single=value;}
    void resolution(int value){if(state)state->resolution=value;}
    void start(HWND window,const std::wstring& mapping){
        stop();state=std::make_shared<State>();state->window=window;state->link=std::make_shared<Link>();
        if(!state->wake || !state->link->connect(mapping,false))throw std::runtime_error("Connect application capture channel");
        // A foreign window can block PrintWindow indefinitely. Only this independent
        // capture thread calls it; the host continues restoring windows and exiting.
        std::thread([shared=state]{
            HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            uint32_t* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
            if(!dc || !bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return;}
            const auto previous=SelectObject(dc,bitmap);
            HDC sourceDC=CreateCompatibleDC(dc);BITMAPINFO sourceInfo=info;sourceInfo.bmiHeader.biWidth=SourceWidth;sourceInfo.bmiHeader.biHeight=-SourceHeight;void* sourcePixels=nullptr;HBITMAP sourceBitmap=CreateDIBSection(sourceDC,&sourceInfo,DIB_RGB_COLORS,&sourcePixels,nullptr,0);const auto sourcePrevious=SelectObject(sourceDC,sourceBitmap);
            if(!sourceDC || !sourceBitmap){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);if(sourceBitmap)DeleteObject(sourceBitmap);if(sourceDC)DeleteDC(sourceDC);return;}
            HDC popupDC=nullptr;HBITMAP popupBitmap=nullptr;HGDIOBJ popupPrevious=nullptr;
            HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
            if(!timer)timer=CreateWaitableTimerW(nullptr,FALSE,nullptr);
            auto changedAt=std::chrono::steady_clock::now();
            while(!shared->stop){
                const auto began=std::chrono::steady_clock::now();
                if(!shared->paused){
                    const auto size=canvas(shared->resolution,GetDpiForWindow(shared->window));const bool immersive=shared->immersive;
                    std::fill_n(pixels,Width*Height,0xff16202au);
                    if(!immersive){RECT area{0,0,size.cx,size.cy};HBRUSH background=CreateSolidBrush(RGB(22,32,42));FillRect(sourceDC,&area,background);DeleteObject(background);}
                    std::vector<HWND> children;for(HWND child=GetWindow(shared->window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){children.push_back(child);if(shared->single || shared->immersive)break;}
                    for(auto it=children.rbegin();it!=children.rend() && !shared->stop;++it){RECT rect{};GetWindowRect(*it,&rect);MapWindowPoints(nullptr,shared->window,reinterpret_cast<POINT*>(&rect),2);
                        if(immersive){DWORD_PTR result=0;if(SendMessageTimeoutW(*it,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result)){
                            RECT area{0,0,std::min<LONG>(rect.right-rect.left,SourceWidth),std::min<LONG>(rect.bottom-rect.top,SourceHeight)};HBRUSH background=CreateSolidBrush(RGB(22,32,42));FillRect(sourceDC,&area,background);DeleteObject(background);
                            if(PrintWindow(*it,sourceDC,2)){RECT source=contentRect(*it);RECT limit{0,0,SourceWidth,SourceHeight};if(IntersectRect(&source,&source,&limit)){SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,0,0,Width,Height,sourceDC,source.left,source.top,source.right-source.left,source.bottom-source.top,SRCCOPY);}}}
                            continue;}
                        const int saved=SaveDC(sourceDC);IntersectClipRect(sourceDC,rect.left,rect.top,rect.right,rect.bottom);SetViewportOrgEx(sourceDC,rect.left,rect.top,nullptr);
                        DWORD_PTR result=0;if(SendMessageTimeoutW(*it,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result))PrintWindow(*it,sourceDC,2);
                        RestoreDC(sourceDC,saved);
                    }
                    RECT viewport{0,0,size.cx,size.cy};
                    if(immersive && !children.empty())viewport=contentRect(children.front());
                    const auto popups=popupWindows(shared->window);
                    if(!popups.empty() && !popupDC){popupDC=CreateCompatibleDC(dc);popupBitmap=CreateCompatibleBitmap(dc,SourceWidth,SourceHeight);if(popupDC && popupBitmap)popupPrevious=SelectObject(popupDC,popupBitmap);}
                    for(auto it=popups.rbegin();it!=popups.rend() && !shared->stop;++it){
                        RECT source{};GetWindowRect(*it,&source);const int pw=std::min<LONG>(source.right-source.left,SourceWidth),ph=std::min<LONG>(source.bottom-source.top,SourceHeight);
                        const RECT destination=popupDisplayRect(*it,shared->window,viewport);DWORD_PTR result=0;
                        if(popupDC && popupBitmap && pw>0 && ph>0 && SendMessageTimeoutW(*it,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result) && PrintWindow(*it,popupDC,2)){
                            HDC output=immersive?dc:sourceDC;RECT d=destination;
                            if(immersive)d={MulDiv(d.left-viewport.left,Width,viewport.right-viewport.left),MulDiv(d.top-viewport.top,Height,viewport.bottom-viewport.top),MulDiv(d.right-viewport.left,Width,viewport.right-viewport.left),MulDiv(d.bottom-viewport.top,Height,viewport.bottom-viewport.top)};
                            SetStretchBltMode(output,HALFTONE);StretchBlt(output,d.left,d.top,d.right-d.left,d.bottom-d.top,popupDC,0,0,pw,ph,SRCCOPY);
                        }
                    }
                    if(!immersive){SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,0,0,Width,Height,sourceDC,0,0,size.cx,size.cy,SRCCOPY);}
                    GdiFlush();for(int i=0;i<Width*Height;++i)pixels[i]|=0xff000000u;
                    auto& link=*shared->link;
                    if(link.lock()){
                        if(std::memcmp(link.data->pixels.data(),pixels,Width*Height*4)){
                            std::memcpy(link.data->pixels.data(),pixels,Width*Height*4);++link.data->generation;changedAt=began;
                            if(!InterlockedExchange(&link.data->framePending,1) && !PostMessageW(link.data->owner,FrameReady,0,0))InterlockedExchange(&link.data->framePending,0);
                        }
                        link.unlock();
                    }
                }
                const int rate=began-changedAt>std::chrono::milliseconds(500)?std::min(10,shared->fps.load()):shared->fps.load();
                const auto remaining=std::chrono::nanoseconds(1000000000/rate)-(std::chrono::steady_clock::now()-began);
                if(shared->paused){WaitForSingleObject(shared->wake,250);changedAt=std::chrono::steady_clock::now();}
                else if(timer && remaining.count()>0){LARGE_INTEGER due{};due.QuadPart=-std::max<LONGLONG>(1,remaining.count()/100);SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE);HANDLE waits[]={shared->wake,timer};if(WaitForMultipleObjects(2,waits,FALSE,250)==WAIT_OBJECT_0)changedAt=std::chrono::steady_clock::now();}
                else {if(WaitForSingleObject(shared->wake,1)==WAIT_OBJECT_0)changedAt=std::chrono::steady_clock::now();}
            }
            if(timer)CloseHandle(timer);if(popupPrevious)SelectObject(popupDC,popupPrevious);if(popupBitmap)DeleteObject(popupBitmap);if(popupDC)DeleteDC(popupDC);
            SelectObject(sourceDC,sourcePrevious);DeleteObject(sourceBitmap);DeleteDC(sourceDC);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
        }).detach();
    }
};
struct Entry {HWND window=nullptr,parent=nullptr,owner=nullptr;DWORD pid=0;LONG_PTR style=0,exstyle=0;WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};RECT rect{};};
// Closing is independent of the host UI and its foreign-window paint/input queues.
// Hold process handles throughout: a recycled PID must never become a kill target.
struct Shutdown {
    struct Target {HWND window;DWORD pid;HANDLE process;bool browser;};
    std::vector<Target> targets;std::atomic_bool cancel=false;std::atomic_int result=0;
    ~Shutdown(){for(const auto& t:targets)if(t.process)CloseHandle(t.process);}
    static bool alive(const Target& t){DWORD pid=0;GetWindowThreadProcessId(t.window,&pid);return IsWindow(t.window) && pid==t.pid && (!t.process || WaitForSingleObject(t.process,0)==WAIT_TIMEOUT);}
    bool externalWindow(const Target& target)const{
        struct Search {const Shutdown* self;DWORD pid;bool found=false;} search{this,target.pid};
        EnumWindows([](HWND h,LPARAM data)->BOOL{auto& s=*reinterpret_cast<Search*>(data);DWORD pid=0;GetWindowThreadProcessId(h,&pid);
            if(pid==s.pid && IsWindowVisible(h) && std::none_of(s.self->targets.begin(),s.self->targets.end(),[h](const Target& t){return t.window==h;})){s.found=true;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
        return search.found;
    }
    void run(){
        for(const auto& t:targets)if(!cancel && alive(t))PostMessageW(t.window,WM_CLOSE,0,0);
        const auto began=GetTickCount64();bool forced=false;
        while(!cancel){
            bool pending=false;
            for(const auto& t:targets){
                const bool live=alive(t),running=t.process && WaitForSingleObject(t.process,0)==WAIT_TIMEOUT;
                const bool shared=t.browser || externalWindow(t);
                if(!live && (!running || shared))continue;
                pending=true;
                if(GetTickCount64()-began>=2500 && running && !shared){
                    DWORD_PTR ignored=0;
                    // Responsive applications can be waiting for a save confirmation.
                    const bool hung=live && !SendMessageTimeoutW(t.window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,100,&ignored);
                    if(!cancel && (!live || hung)){if(TerminateProcess(t.process,1))forced=true;}
                }
            }
            if(!pending){result=1;return;}
            if(GetTickCount64()-began>= (forced?5500u:3500u))break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if(!cancel){
            // Owned modal dialogs are top-level windows; bring any confirmation back
            // into view instead of leaving it beside the off-screen capture host.
            EnumWindows([](HWND h,LPARAM data)->BOOL{auto& self=*reinterpret_cast<Shutdown*>(data);const HWND owner=GetWindow(h,GW_OWNER);
                if(IsWindowVisible(h) && std::any_of(self.targets.begin(),self.targets.end(),[owner](const Target& t){return owner==t.window && alive(t);})){RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);SetWindowPos(h,HWND_TOP,area.left+80,area.top+80,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);}return TRUE;},reinterpret_cast<LPARAM>(this));
            result=2;
        }
    }
};
struct Host {
    struct InputHook {DWORD thread;HHOOK hook,windowHook;};
    HMODULE inputModule=nullptr;HOOKPROC inputProc=nullptr,windowProc=nullptr;std::vector<InputHook> inputHooks;std::vector<HWND> hoverWindows;
    inline static Host* eventHost=nullptr;HWINEVENTHOOK popupEvents=nullptr;
    Link link;Capture capture;HWND window=nullptr,focus=nullptr,drag=nullptr,pointerCapture=nullptr;HANDLE parent=nullptr;
    ~Host(){if(popupEvents)UnhookWinEvent(popupEvents);if(eventHost==this)eventHost=nullptr;release();if(inputModule)FreeLibrary(inputModule);}
    std::vector<Entry> entries;bool immersive=false,paused=false;int mode=0,fps=60,resolution=0,width=Width,height=Height;POINT origin{};RECT dragRect{};ULONGLONG checked=0;
    void status(const wchar_t* text){if(link.lock()){wcscpy_s(link.data->status,text);link.data->count=static_cast<LONG>(entries.size());++link.data->generation;link.unlock();}}
    void restore(Entry& e){
        DWORD pid=0;GetWindowThreadProcessId(e.window,&pid);if(!IsWindow(e.window) || pid!=e.pid)return;
        for(HWND popup:popupWindows(window)){RemovePropW(popup,L"PicoPet.PopupHost");PostMessageW(popup,WM_CLOSE,0,0);}
        RemovePropW(e.window,L"PicoPet.ApplicationHost");PostMessageW(e.window,WM_CANCELMODE,0,0);
        SetParent(e.window,e.parent);SetWindowLongPtrW(e.window,GWL_STYLE,e.style);SetWindowLongPtrW(e.window,GWL_EXSTYLE,e.exstyle);
        if(!e.parent)SetWindowLongPtrW(e.window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(IsWindow(e.owner)?e.owner:nullptr));
        SetWindowPos(e.window,nullptr,e.rect.left,e.rect.top,e.rect.right-e.rect.left,e.rect.bottom-e.rect.top,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
        SetWindowPlacement(e.window,&e.placement);ShowWindow(e.window,(e.style&WS_VISIBLE)?(e.placement.showCmd==SW_SHOWMINIMIZED?SW_SHOWMINIMIZED:SW_SHOWNOACTIVATE):SW_HIDE);
    }
    void clearHover(){for(HWND h:hoverWindows){if(GetPropW(h,L"PicoPet.ProjectedHover")==window){RemovePropW(h,L"PicoPet.ProjectedHover");PostMessageW(h,WM_MOUSELEAVE,0,0);}}hoverWindows.clear();}
    bool hookThread(HWND target){
        const DWORD thread=GetWindowThreadProcessId(target,nullptr);
        if(std::none_of(inputHooks.begin(),inputHooks.end(),[thread](const InputHook& hook){return hook.thread==thread;})){
            if(!inputModule){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);const auto library=std::filesystem::path(path).parent_path()/L"PicoPet.Input.dll";inputModule=LoadLibraryW(library.c_str());if(inputModule){inputProc=reinterpret_cast<HOOKPROC>(GetProcAddress(inputModule,"AppInputMessage"));windowProc=reinterpret_cast<HOOKPROC>(GetProcAddress(inputModule,"AppWindowMessage"));}}
            HHOOK hook=inputProc?SetWindowsHookExW(WH_GETMESSAGE,inputProc,inputModule,thread):nullptr;
            if(!hook)return false;HHOOK windowHook=windowProc?SetWindowsHookExW(WH_CALLWNDPROC,windowProc,inputModule,thread):nullptr;inputHooks.push_back({thread,hook,windowHook});
        }
        return true;
    }
    bool trackHover(HWND target){
        if(!hookThread(target))return false;
        std::vector<HWND> next;for(HWND h=target;h && h!=window;h=GetParent(h))next.push_back(h);
        if(next==hoverWindows)return true;
        for(HWND old:hoverWindows)if(std::find(next.begin(),next.end(),old)==next.end() && GetPropW(old,L"PicoPet.ProjectedHover")==window){RemovePropW(old,L"PicoPet.ProjectedHover");PostMessageW(old,WM_MOUSELEAVE,0,0);}
        for(HWND h:next)SetPropW(h,L"PicoPet.ProjectedHover",window);hoverWindows=std::move(next);return true;
    }
    void release(){capture.stop();clearHover();for(HWND popup:popupWindows(window)){RemovePropW(popup,L"PicoPet.PopupHost");PostMessageW(popup,WM_CLOSE,0,0);}for(auto& hook:inputHooks){UnhookWindowsHookEx(hook.hook);if(hook.windowHook)UnhookWindowsHookEx(hook.windowHook);}inputHooks.clear();pointerCapture=nullptr;for(auto it=entries.rbegin();it!=entries.rend();++it)restore(*it);entries.clear();focus=nullptr;}
    bool ownedPopup(HWND popup)const{
        const auto style=GetWindowLongPtrW(popup,GWL_STYLE);
        if(!IsWindow(popup) || !(style&WS_POPUP) || (style&(WS_CHILD|WS_CAPTION)))return false;
        DWORD popupPid=0;GetWindowThreadProcessId(popup,&popupPid);
        for(HWND owner=GetWindow(popup,GW_OWNER);owner;owner=GetWindow(owner,GW_OWNER)){
            if(owner==window && std::any_of(entries.begin(),entries.end(),[popupPid](const Entry& e){return e.pid==popupPid;}))return true;
            if(std::any_of(entries.begin(),entries.end(),[owner](const Entry& e){return e.window==owner || IsChild(e.window,owner);}))return true;
        }
        return false;
    }
    void updatePopup(HWND popup){
        if(!IsWindowVisible(popup) || !ownedPopup(popup))return;
        SetPropW(popup,L"PicoPet.PopupHost",window);hookThread(popup);
        RECT hostRect{},rect{};GetWindowRect(window,&hostRect);GetWindowRect(popup,&rect);
        const int x=std::clamp<LONG>(rect.left,hostRect.left,std::max(hostRect.left,hostRect.left+width-(rect.right-rect.left)));
        const int y=std::clamp<LONG>(rect.top,hostRect.top,std::max(hostRect.top,hostRect.top+height-(rect.bottom-rect.top)));
        if(x!=rect.left || y!=rect.top)SetWindowPos(popup,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS);
        capture.wake();
    }
    static void CALLBACK popupEvent(HWINEVENTHOOK,DWORD event,HWND target,LONG object,LONG child,DWORD,DWORD){
        if(eventHost && object==OBJID_WINDOW && child==CHILDID_SELF && (event==EVENT_OBJECT_SHOW || event==EVENT_OBJECT_LOCATIONCHANGE) && eventHost->ownedPopup(target))PostMessageW(eventHost->window,PopupChanged,0,reinterpret_cast<LPARAM>(target));
    }
    HWND application(HWND child)const{while(IsWindow(child) && GetParent(child)!=window)child=GetParent(child);return IsWindow(child) && GetParent(child)==window?child:nullptr;}
    HWND keyboardTarget()const{
        HWND root=application(focus);if(!root)return nullptr;
        GUITHREADINFO info{sizeof(info)};const DWORD thread=GetWindowThreadProcessId(root,nullptr);
        if(GetGUIThreadInfo(thread,&info) && (info.hwndFocus==root || IsChild(root,info.hwndFocus)))return info.hwndFocus;
        return focus;
    }
    void activate(HWND target){
        if(!application(target))return;focus=target;
        const DWORD current=GetCurrentThreadId(),thread=GetWindowThreadProcessId(target,nullptr);
        const bool joined=current!=thread && AttachThreadInput(current,thread,TRUE);
        SetForegroundWindow(window);SetActiveWindow(window);SetFocus(target);
        if(joined)AttachThreadInput(current,thread,FALSE);
        capture.wake();
    }
    SIZE windowSize(HWND target)const{
        SIZE size{width,height};
        if(immersive && chromium(target)){
            RECT outer{};GetWindowRect(target,&outer);const RECT content=contentRect(target);
            size.cx+=std::clamp<LONG>(outer.right-outer.left-(content.right-content.left),0,SourceWidth-width);
            size.cy+=std::clamp<LONG>(outer.bottom-outer.top-(content.bottom-content.top),0,SourceHeight-height);
        }
        return size;
    }
    void layout(){
        capture.single(mode==0);
        const auto size=canvas(resolution,GetDpiForWindow(window));width=size.cx;height=size.cy;capture.resolution(resolution);
        const bool browser=immersive && std::any_of(entries.begin(),entries.end(),[](const Entry& e){return chromium(e.window);});
        const int hostWidth=browser?SourceWidth:width,hostHeight=browser?SourceHeight:height;
        SetWindowPos(window,nullptr,GetSystemMetrics(SM_XVIRTUALSCREEN)-hostWidth-32,GetSystemMetrics(SM_YVIRTUALSCREEN),hostWidth,hostHeight,SWP_NOZORDER|SWP_NOACTIVATE);
        for(size_t i=0;i<entries.size();++i){auto& e=entries[i];if(!IsWindow(e.window))continue;
            const bool single=mode==0 || immersive;const LONG_PTR style=(e.style&~(WS_POPUP|WS_MINIMIZE|WS_MAXIMIZE))|WS_CHILD;
            SetWindowLongPtrW(e.window,GWL_STYLE,single?style&~(WS_CAPTION|WS_THICKFRAME):style);
            const int offset=static_cast<int>(i%5)*24;
            const auto desired=windowSize(e.window);
            SetWindowPos(e.window,nullptr,single?0:offset,single?0:offset,single?desired.cx:width-100,single?desired.cy:height-100,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        }
        status(entries.empty()?L"从屏幕快捷方式启动应用，或在机身右键菜单中接入窗口。\n兼容模式：传统 Win32 应用优先；GPU 界面可能黑屏。":mode==0?L"单应用铺满 · 原窗口退出时恢复":L"多窗口桌面 · 最大化限制在容器内");
    }
    bool attach(HWND target){
        DWORD pid=0;GetWindowThreadProcessId(target,&pid);
        if(!IsWindow(target) || !pid || pid==GetCurrentProcessId() || pid==link.data->parent || GetAncestor(target,GA_ROOT)!=target || entries.size()>=8)return false;
        if(std::any_of(entries.begin(),entries.end(),[&](const Entry& e){return e.window==target;}))return false;
        Entry e;e.window=target;e.pid=pid;e.parent=GetParent(target);e.owner=GetWindow(target,GW_OWNER);if(!(GetWindowLongPtrW(target,GWL_STYLE)&WS_CHILD))e.parent=nullptr;
        e.style=GetWindowLongPtrW(target,GWL_STYLE);e.exstyle=GetWindowLongPtrW(target,GWL_EXSTYLE);GetWindowRect(target,&e.rect);GetWindowPlacement(target,&e.placement);
        ShowWindow(target,SW_RESTORE);SetLastError(0);
        SetWindowLongPtrW(target,GWL_STYLE,(e.style&~(WS_POPUP|WS_MINIMIZE|WS_MAXIMIZE))|WS_CHILD);
        SetLastError(0);SetParent(target,window);
        if(GetParent(target)!=window){restore(e);status(L"该窗口不接受嵌入，或权限级别不同；已恢复原窗口");return false;}
        SetWindowLongPtrW(target,GWL_EXSTYLE,e.exstyle&~(WS_EX_APPWINDOW|WS_EX_TOPMOST));
        SetPropW(target,L"PicoPet.ApplicationHost",window);hookThread(target);
        entries.push_back(e);focus=target;layout();SetWindowPos(target,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);return true;
    }
    HWND childAt(POINT point){
        HWND target=window;POINT local=point;

        for(int i=0;i<16;++i){HWND child=ChildWindowFromPointEx(target,local,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED|CWP_SKIPTRANSPARENT);if(!child || child==target)break;MapWindowPoints(target,child,&local,1);target=child;}
        return target;
    }
    void pointer(UINT message,WPARAM buttons,POINT point){
        if(message==WM_MOUSELEAVE){clearHover();return;}
        if(!immersive)point={MulDiv(point.x,width,Width),MulDiv(point.y,height,Height)};
        if(drag){
            if(message==WM_MOUSEMOVE){const int w=dragRect.right-dragRect.left,h=dragRect.bottom-dragRect.top;SetWindowPos(drag,nullptr,std::clamp<int>(dragRect.left+point.x-origin.x,0,std::max(0,width-w)),std::clamp<int>(dragRect.top+point.y-origin.y,0,std::max(0,height-h)),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
            if(message==WM_LBUTTONUP){drag=nullptr;pointerCapture=nullptr;}return;
        }
        if(immersive){HWND top=GetWindow(window,GW_CHILD);if(top){const RECT area=contentRect(top);POINT clientOrigin{};ClientToScreen(top,&clientOrigin);RECT outer{};GetWindowRect(top,&outer);point={area.left+MulDiv(point.x,area.right-area.left,Width)+outer.left-clientOrigin.x,area.top+MulDiv(point.y,area.bottom-area.top,Height)+outer.top-clientOrigin.y};MapWindowPoints(top,window,&point,1);}}
        RECT viewport{0,0,width,height};if(immersive){HWND top=GetWindow(window,GW_CHILD);if(top)viewport=contentRect(top);}
        for(HWND popup:popupWindows(window)){
            const RECT display=popupDisplayRect(popup,window,viewport);
            if(PtInRect(&display,point)){
                RECT rect{};GetWindowRect(popup,&rect);
                POINT screen{rect.left+MulDiv(point.x-display.left,rect.right-rect.left,display.right-display.left),rect.top+MulDiv(point.y-display.top,rect.bottom-rect.top,display.bottom-display.top)};
                POINT local=screen;ScreenToClient(popup,&local);trackHover(popup);
                PostMessageW(popup,message,buttons,(message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL)?MAKELPARAM(screen.x,screen.y):MAKELPARAM(local.x,local.y));capture.wake();return;
            }
        }
        HWND target=IsWindow(pointerCapture) && (message==WM_MOUSEMOVE || message==WM_LBUTTONUP)?pointerCapture:childAt(point);if(target==window)return;
        if(message==WM_MOUSEMOVE || message==WM_LBUTTONDOWN)trackHover(target);
        POINT screen=point;ClientToScreen(window,&screen);DWORD_PTR area=HTCLIENT;
        if(message!=WM_MOUSEMOVE && message!=WM_MOUSEWHEEL && message!=WM_MOUSEHWHEEL)SendMessageTimeoutW(target,WM_NCHITTEST,0,MAKELPARAM(screen.x,screen.y),SMTO_ABORTIFHUNG|SMTO_BLOCK,30,&area);
        if(message==WM_LBUTTONDOWN){pointerCapture=target;const bool popup=GetPropW(target,L"PicoPet.PopupHost")==window;if(!popup)activate(target);HWND root=application(target);if(root)SetWindowPos(root,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            if(area==HTCAPTION && mode==1 && !immersive){drag=root;origin=point;GetWindowRect(root,&dragRect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&dragRect),2);return;}}
        if(area!=HTCLIENT && (message==WM_LBUTTONUP || message==WM_LBUTTONDBLCLK)){
            if(area==HTMAXBUTTON || (area==HTCAPTION && message==WM_LBUTTONDBLCLK)){SetWindowPos(target,nullptr,0,0,width,height,SWP_NOACTIVATE|SWP_NOZORDER);return;}
            if(area==HTCLOSE){PostMessageW(target,WM_CLOSE,0,0);return;}
        }
        POINT local=point;MapWindowPoints(window,target,&local,1);
        PostMessageW(target,message,buttons,(message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL)?MAKELPARAM(screen.x,screen.y):MAKELPARAM(local.x,local.y));
        if(message==WM_LBUTTONUP)pointerCapture=nullptr;
        if(message!=WM_MOUSEMOVE)capture.wake();
    }
    void tick(){
        if(link.data->closing || WaitForSingleObject(parent,0)!=WAIT_TIMEOUT){DestroyWindow(window);return;}
        if(link.data->quitting)return;
        const auto now=GetTickCount64();if(now-checked>=250){checked=now;
            entries.erase(std::remove_if(entries.begin(),entries.end(),[](const Entry& e){DWORD pid=0;GetWindowThreadProcessId(e.window,&pid);return !IsWindow(e.window)||pid!=e.pid;}),entries.end());
            for(auto& e:entries){RECT rect{};GetWindowRect(e.window,&rect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
                if(GetParent(e.window)!=window){SetWindowLongPtrW(e.window,GWL_STYLE,(GetWindowLongPtrW(e.window,GWL_STYLE)&~WS_POPUP)|WS_CHILD);SetParent(e.window,window);}
                const auto desired=windowSize(e.window);const int limitWidth=desired.cx,limitHeight=desired.cy;
                if((mode==0 || immersive) && (rect.left!=0 || rect.top!=0 || rect.right!=limitWidth || rect.bottom!=limitHeight))SetWindowPos(e.window,nullptr,0,0,limitWidth,limitHeight,SWP_NOACTIVATE|SWP_NOZORDER);
                else if(rect.left<0 || rect.top<0 || rect.right>limitWidth || rect.bottom>limitHeight){const int w=std::clamp<int>(rect.right-rect.left,1,limitWidth),h=std::clamp<int>(rect.bottom-rect.top,1,limitHeight);SetWindowPos(e.window,nullptr,std::clamp<int>(rect.left,0,limitWidth-w),std::clamp<int>(rect.top,0,limitHeight-h),w,h,SWP_NOACTIVATE|SWP_NOZORDER);}
            }
            if(link.lock()){link.data->count=static_cast<LONG>(entries.size());link.unlock();}
        }
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Host*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(h,m,w,l);
        if(self->link.data->quitting && m>=Attach && m<=Immersive && m!=Paused)return 0;
        switch(m){
        case WM_TIMER:self->tick();return 0;
        case PopupChanged:self->updatePopup(reinterpret_cast<HWND>(l));return 0;
        case Attach:self->attach(reinterpret_cast<HWND>(l));return 0;
        case Configure:self->mode=LOWORD(w);self->resolution=std::clamp<int>(HIWORD(w),0,3);self->fps=std::clamp(static_cast<int>(l),5,60);self->capture.rate(self->fps);self->layout();return 0;
        case Release:if(!self->entries.empty()){self->clearHover();self->restore(self->entries.back());self->entries.pop_back();self->focus=self->entries.empty()?nullptr:self->entries.back().window;self->layout();}return 0;
        case Pointer:self->pointer(LOWORD(w),w>>16,{GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;
        case Immersive:self->immersive=!self->immersive;self->capture.immersive(self->immersive);self->layout();return 0;
        case Cycle:if(self->entries.size()>1){std::rotate(self->entries.begin(),self->entries.begin()+1,self->entries.end());self->focus=self->entries.back().window;SetWindowPos(self->focus,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}return 0;
        case Keyboard:if(HWND target=self->keyboardTarget()){PostMessageW(target,LOWORD(w),HIWORD(w),l);self->capture.wake();}return 0;
        case Paused:self->paused=w!=0;if(self->paused)self->clearHover();self->capture.pause(self->paused);return 0;
        case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
        case WM_CLOSE:DestroyWindow(h);return 0;
        case WM_DESTROY:self->release();PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }
};
std::wstring windowTitle(HWND window){wchar_t text[256]{};GetWindowTextW(window,text,256);return text;}
}

int runHost(const wchar_t* mappingName){
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;int result=0;
    try{
        Host host;if(!host.link.connect(mappingName,false))return 2;
        host.parent=OpenProcess(SYNCHRONIZE,FALSE,host.link.data->parent);if(!host.parent)return 3;
        WNDCLASSW type{};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=Host::proc;type.lpszClassName=L"PicoPet.ApplicationHost";type.hbrBackground=CreateSolidBrush(RGB(22,32,42));type.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&type);
        // Keep source windows outside the visible desktop; request bounded native paint snapshots.
        HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"PICO application workspace",WS_POPUP|WS_CLIPCHILDREN,GetSystemMetrics(SM_XVIRTUALSCREEN)-Width-32,GetSystemMetrics(SM_YVIRTUALSCREEN),Width,Height,nullptr,nullptr,type.hInstance,&host);
        if(!window){CloseHandle(host.parent);return 4;}
        const DWMNCRENDERINGPOLICY policy=DWMNCRP_DISABLED;DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&policy,sizeof(policy));
        Host::eventHost=&host;host.popupEvents=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_LOCATIONCHANGE,nullptr,Host::popupEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        ShowWindow(window,SW_SHOWNOACTIVATE);
        host.capture.start(window,mappingName);host.mode=host.link.data->mode;host.fps=host.link.data->fps;host.capture.rate(host.fps);host.capture.single(host.mode==0);
        host.resolution=host.link.data->resolution;host.layout();
        if(host.link.lock()){host.link.data->host=window;host.link.unlock();}
        host.status(L"从屏幕快捷方式启动应用，或在机身右键菜单中接入窗口。\n兼容模式：传统 Win32 应用优先；GPU 界面可能黑屏。");SetTimer(window,1,250,nullptr);
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
        CloseHandle(host.parent);DeleteObject(type.hbrBackground);
    }catch(...){result=5;}
    CoUninitialize();return result;
}

struct Workspace::Impl {
    std::unique_ptr<Link> link;HANDLE process=nullptr;HWND owner=nullptr;bool immersive=false,paused=false,dirty=true;LONG generation=-1;
    int mode=0,fps=60,windows=0;uint64_t frameCount=0;std::wstring message=L"正在启动应用容器…";
    DWORD launchedPid=0;ULONGLONG launchDeadline=0,launchChecked=0;std::vector<HWND> beforeLaunch;std::wstring browserPath;
    std::array<uint32_t,Width*Height> pixels{};HFONT font=nullptr;POINT pointer{};bool pointerDown=false;
    std::shared_ptr<Shutdown> shutdown;ULONGLONG exitBegan=0,hostExitBegan=0,noticeUntil=0;
    ~Impl(){if(font)DeleteObject(font);if(process)CloseHandle(process);}
};
Workspace::Workspace():impl(std::make_unique<Impl>()){}
Workspace::~Workspace(){close();}
bool Workspace::active()const{return impl->link!=nullptr;}
bool Workspace::exiting()const{return impl->shutdown!=nullptr;}
HWND Workspace::host()const{return active()?impl->link->data->host:nullptr;}
int Workspace::count()const{return impl->windows;}
uint64_t Workspace::frames()const{return impl->frameCount;}
std::wstring Workspace::status()const{return impl->message;}
void Workspace::open(HWND owner,int mode,int fps,int resolution){
    if(active())return;impl->owner=owner;impl->mode=mode;impl->fps=fps;impl->immersive=false;impl->generation=-1;impl->windows=0;impl->paused=false;impl->dirty=true;impl->message=L"正在启动应用容器…";
    auto link=std::make_unique<Link>();const auto name=L"Local\\PicoPet.App."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
    if(!link->connect(name,true))throw std::runtime_error("Create application frame channel");
    ZeroMemory(link->data,sizeof(Shared));link->data->parent=GetCurrentProcessId();link->data->owner=owner;link->data->mode=mode;link->data->fps=fps;
    link->data->resolution=std::clamp(resolution,0,3);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);std::wstring command=L"\""+std::wstring(executable)+L"\" --app-host \""+name+L"\"";
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("Start application host");
    CloseHandle(process.hThread);impl->process=process.hProcess;impl->link=std::move(link);
}
void Workspace::configure(int mode,int fps,int resolution){impl->mode=mode;impl->fps=fps;if(host())PostMessageW(host(),Configure,MAKELONG(mode,std::clamp(resolution,0,3)),fps);impl->dirty=true;}
bool Workspace::attach(HWND window){return !exiting() && host() && PostMessageW(host(),Attach,0,reinterpret_cast<LPARAM>(window));}
void Workspace::fullscreen(){impl->immersive=!impl->immersive;if(host())PostMessageW(host(),Immersive,0,0);impl->dirty=true;}
void Workspace::next(){if(host())PostMessageW(host(),Cycle,0,0);}
void Workspace::detach(){if(host())PostMessageW(host(),Release,0,0);}
void Workspace::choose(){
    if(!host()){MessageBoxW(impl->owner,L"应用容器尚未就绪，请稍后重试。",L"电视应用",MB_OK);return;}
    std::vector<HWND> windows;
    EnumWindows([](HWND h,LPARAM data)->BOOL{DWORD pid=0;GetWindowThreadProcessId(h,&pid);wchar_t name[128]{};GetClassNameW(h,name,128);DWORD cloaked=0;DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        if(pid!=GetCurrentProcessId() && IsWindowVisible(h) && !cloaked && !GetWindow(h,GW_OWNER) && !(GetWindowLongPtrW(h,GWL_EXSTYLE)&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE)) && GetWindowTextLengthW(h)>0 && wcscmp(name,L"Progman") && wcscmp(name,L"WorkerW"))static_cast<std::vector<HWND>*>(reinterpret_cast<void*>(data))->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&windows));
    HMENU menu=CreatePopupMenu();for(size_t i=0;i<windows.size() && i<100;++i){auto title=windowTitle(windows[i]);std::replace(title.begin(),title.end(),L'&',L' ');AppendMenuW(menu,MF_STRING,i+1,title.c_str());}
    if(windows.empty())AppendMenuW(menu,MF_GRAYED,0,L"没有可接入的任务栏窗口");POINT p{};GetCursorPos(&p);SetForegroundWindow(impl->owner);
    const UINT chosen=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,impl->owner,nullptr);DestroyMenu(menu);if(chosen && chosen<=windows.size())attach(windows[chosen-1]);
}
void Workspace::launch(){
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.GetAddressOf()))))return;
    dialog->SetTitle(L"选择程序或桌面快捷方式 · 启动后自动接入");
    dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_FILEMUSTEXIST|FOS_NODEREFERENCELINKS);
    const COMDLG_FILTERSPEC filters[]={{L"程序与快捷方式",L"*.exe;*.lnk;*.url"},{L"所有文件",L"*.*"}};dialog->SetFileTypes(2,filters);
    if(SUCCEEDED(dialog->Show(impl->owner))){Microsoft::WRL::ComPtr<IShellItem> item;if(SUCCEEDED(dialog->GetResult(item.GetAddressOf()))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){const std::wstring selected=path;CoTaskMemFree(path);launchPath(selected);}}}
}
bool Workspace::launchPath(const std::wstring& path){
    if(exiting())return false;
    const auto target=launchTarget(path);impl->browserPath=target.browser?target.executable:L"";
    impl->launchDeadline=0;impl->launchChecked=0;impl->launchedPid=0;
    impl->beforeLaunch.clear();EnumWindows([](HWND h,LPARAM data)->BOOL{reinterpret_cast<std::vector<HWND>*>(data)->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&impl->beforeLaunch));
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI|SEE_MASK_NOASYNC;info.hwnd=impl->owner;info.lpVerb=L"open";info.lpFile=path.c_str();info.nShow=SW_SHOWNORMAL;
    if(target.browser){info.lpFile=target.executable.c_str();info.lpParameters=target.arguments.c_str();if(!target.directory.empty())info.lpDirectory=target.directory.c_str();}
    if(!ShellExecuteExW(&info)){const auto message=L"无法启动该快捷方式。Windows 错误："+std::to_wstring(GetLastError());MessageBoxW(impl->owner,message.c_str(),L"电视应用",MB_OK|MB_ICONWARNING);return false;}
    impl->launchedPid=info.hProcess?GetProcessId(info.hProcess):0;if(info.hProcess)CloseHandle(info.hProcess);
    impl->launchDeadline=GetTickCount64()+15000;impl->message=L"正在启动应用，窗口就绪后会自动显示在电视内…";impl->dirty=true;return true;
}
void Workspace::close(){
    if(impl->shutdown){impl->shutdown->cancel=true;impl->shutdown.reset();}impl->hostExitBegan=0;impl->noticeUntil=0;
    if(!active())return;InterlockedExchange(&impl->link->data->closing,1);if(host())PostMessageW(host(),WM_CLOSE,0,0);
    // The helper also watches the owner's process; never kill it before restoration finishes.
    impl->link.reset();if(impl->process){CloseHandle(impl->process);impl->process=nullptr;}impl->windows=0;impl->pointerDown=false;impl->launchDeadline=0;
}
void Workspace::exitApplications(){
    if(!active() || exiting())return;
    // Finish automatic attachment before accepting close, so a just-launched app
    // cannot escape the request while its window is still being created.
    if(impl->launchDeadline){impl->message=L"应用正在启动，请窗口显示后再关闭。";impl->dirty=true;impl->noticeUntil=GetTickCount64()+3500;return;}
    auto shutdown=std::make_shared<Shutdown>();
    const HWND container=host();
    for(HWND child=container?GetWindow(container,GW_CHILD):nullptr;child;child=GetWindow(child,GW_HWNDNEXT)){
        DWORD pid=0;GetWindowThreadProcessId(child,&pid);if(!pid || pid==GetCurrentProcessId() || pid==GetProcessId(impl->process))continue;
        HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,pid);
        if(!process)process=OpenProcess(SYNCHRONIZE,FALSE,pid);
        shutdown->targets.push_back({child,pid,process,chromium(child)});
    }
    InterlockedExchange(&impl->link->data->quitting,1);
    if(container)PostMessageW(container,Paused,TRUE,0);
    impl->pointerDown=false;impl->exitBegan=GetTickCount64();impl->hostExitBegan=0;impl->noticeUntil=0;
    impl->shutdown=shutdown;impl->message=L"正在关闭电视内的应用…";impl->dirty=true;
    std::thread([shutdown]{shutdown->run();}).detach();
}
void Workspace::suspend(bool value){if(value==impl->paused)return;impl->paused=value;if(host())PostMessageW(host(),Paused,value,0);}
void Workspace::tick(){
    if(!active())return;
    InterlockedExchange(&impl->link->data->framePending,0);
    if(exiting()){
        impl->dirty=true;const auto now=GetTickCount64();const int result=impl->shutdown->result;
        if(result==1 && now-impl->exitBegan>=450){
            if(!impl->hostExitBegan){impl->hostExitBegan=now;InterlockedExchange(&impl->link->data->closing,1);if(host())PostMessageW(host(),WM_CLOSE,0,0);}
            impl->message=L"应用已退出，正在返回桌面…";
            if(WaitForSingleObject(impl->process,0)==WAIT_OBJECT_0){close();return;}
            // All attached windows have gone. A host stuck in foreign teardown can
            // now be ended without orphaning any application window.
            if(now-impl->hostExitBegan>=2000)TerminateProcess(impl->process,0);
        }else if(result==2){
            impl->shutdown.reset();InterlockedExchange(&impl->link->data->quitting,0);if(host())PostMessageW(host(),Paused,impl->paused,0);
            impl->message=L"应用尚未退出，请处理保存提示或关闭确认后重试。";impl->noticeUntil=now+5000;
        }
        return;
    }
    if(WaitForSingleObject(impl->process,0)==WAIT_OBJECT_0){if(impl->message!=L"应用容器已停止，请返回后重试"){impl->message=L"应用容器已停止，请返回后重试";impl->dirty=true;}return;}
    if(impl->launchDeadline && host() && GetTickCount64()-impl->launchChecked>=100){
        impl->launchChecked=GetTickCount64();
        if(GetTickCount64()>impl->launchDeadline){impl->launchDeadline=0;impl->message=L"程序已启动，但未找到可确认的新窗口。请从机身右键菜单接入。";impl->dirty=true;}
        else if(impl->launchedPid || !impl->browserPath.empty()){struct Search{Impl* impl;HWND found=nullptr;} search{impl.get()};
            EnumWindows([](HWND h,LPARAM data)->BOOL{auto& search=*reinterpret_cast<Search*>(data);DWORD pid=0;GetWindowThreadProcessId(h,&pid);
                if(!windowlayer::taskbarWindow(h) || std::find(search.impl->beforeLaunch.begin(),search.impl->beforeLaunch.end(),h)!=search.impl->beforeLaunch.end())return TRUE;
                const bool matches=search.impl->browserPath.empty()?pid==search.impl->launchedPid:chromium(h) && windowlayer::samePath(windowlayer::processPath(pid),search.impl->browserPath);
                if(matches){search.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
            if(search.found){attach(search.found);impl->launchDeadline=0;}
        }
    }
    if(impl->link->lock()){
        auto& shared=*impl->link->data;impl->windows=shared.count;
        if(shared.generation!=impl->generation){impl->generation=shared.generation;impl->pixels=shared.pixels;if(!impl->launchDeadline && !impl->noticeUntil)impl->message=shared.status;impl->dirty=true;++impl->frameCount;}
        impl->link->unlock();
    }
    if(impl->noticeUntil && GetTickCount64()>=impl->noticeUntil){impl->noticeUntil=0;impl->dirty=true;}
}
bool Workspace::updated(){const bool dirty=impl->dirty;impl->dirty=false;return dirty;}
void Workspace::draw(HDC dc,uint32_t* pixels){
    std::copy(impl->pixels.begin(),impl->pixels.end(),pixels);
    if(exiting())for(int i=0;i<Width*Height;++i){const auto c=pixels[i];pixels[i]=0xff000000u|(((c>>16&255)/4+12)<<16)|(((c>>8&255)/4+20)<<8)|((c&255)/4+28);}
    if(!impl->font)impl->font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    const auto previous=SelectObject(dc,impl->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(219,238,231));
    if(exiting() || !impl->windows){RECT hint{32,218,768,300};DrawTextW(dc,impl->message.c_str(),-1,&hint,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);}
    else if(impl->noticeUntil){RECT area{0,0,Width,60};HBRUSH background=CreateSolidBrush(RGB(22,32,42));FillRect(dc,&area,background);DeleteObject(background);RECT hint{20,16,780,56};DrawTextW(dc,impl->message.c_str(),-1,&hint,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);}
    SelectObject(dc,previous);
}
bool Workspace::mouse(UINT message,WPARAM buttons,int x,int y){
    if(!active())return false;
    if(exiting())return true;
    if(message==WM_LBUTTONDOWN)impl->pointerDown=true;if(message==WM_LBUTTONUP)impl->pointerDown=false;
    impl->pointer={std::clamp(x,0,Width-1),std::clamp(y,0,Height-1)};
    if(message==WM_LBUTTONDOWN && impl->process)AllowSetForegroundWindow(GetProcessId(impl->process));
    if(host())PostMessageW(host(),Pointer,static_cast<WPARAM>(message)|(static_cast<WPARAM>(static_cast<uint32_t>(buttons))<<16),MAKELPARAM(impl->pointer.x,impl->pointer.y));return true;
}
bool Workspace::key(UINT message,WPARAM key,LPARAM data){
    if(!active())return false;
    if(exiting())return true;
    if(host())PostMessageW(host(),Keyboard,MAKELONG(message,static_cast<WORD>(key)),data);return true;
}
}
