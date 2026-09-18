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
#include <mutex>
#include <atomic>


namespace appworkspace {
namespace {
constexpr int Width=800,Height=456,Bar=44;
constexpr UINT Attach=WM_APP+30,Configure=WM_APP+31,Release=WM_APP+32,Pointer=WM_APP+33,Keyboard=WM_APP+34,Paused=WM_APP+35,Cycle=WM_APP+36,Immersive=WM_APP+37;
struct Shared {
    DWORD parent=0;HWND owner=nullptr,host=nullptr;
    LONG generation=0,count=0,mode=0,fps=15,closing=0;
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
RECT contentRect(HWND window){
    RECT result{};GetWindowRect(window,&result);const int w=result.right-result.left,h=result.bottom-result.top;result={0,0,w,h};
    struct Search{HWND root;RECT rect;bool found=false;} search{window,result};
    EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL{auto& search=*reinterpret_cast<Search*>(data);wchar_t name[128]{};GetClassNameW(child,name,128);
        if(wcscmp(name,L"Chrome_RenderWidgetHostHWND")==0 && IsWindowVisible(child)){RECT r{},outer{};GetWindowRect(child,&r);GetWindowRect(search.root,&outer);OffsetRect(&r,-outer.left,-outer.top);if(r.right-r.left>100 && r.bottom-r.top>100){search.rect=r;search.found=true;return FALSE;}}return TRUE;},reinterpret_cast<LPARAM>(&search));
    return search.found?search.rect:result;
}
struct Capture {
    struct State {
        HWND window=nullptr;std::atomic_bool stop=false,paused=false;std::atomic_int fps=15;std::atomic_bool single=true,immersive=false;
        std::mutex mutex;std::array<uint32_t,Width*Height> frame{};bool fresh=false;
    };
    std::shared_ptr<State> state;
    void stop(){if(state)state->stop=true;state.reset();}
    ~Capture(){stop();}
    void pause(bool value){if(state)state->paused=value;}
    void rate(int fps){if(state)state->fps=std::clamp(fps,5,30);}
    void immersive(bool value){if(state)state->immersive=value;}
    void single(bool value){if(state)state->single=value;}
    void start(HWND window){
        stop();state=std::make_shared<State>();state->window=window;
        // A foreign window can block PrintWindow indefinitely. Only this independent
        // capture thread calls it; the host continues restoring windows and exiting.
        std::thread([shared=state]{
            HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            uint32_t* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
            if(!dc || !bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return;}
            const auto previous=SelectObject(dc,bitmap);
            HDC sourceDC=CreateCompatibleDC(dc);BITMAPINFO sourceInfo=info;sourceInfo.bmiHeader.biWidth=1024;sourceInfo.bmiHeader.biHeight=-1024;void* sourcePixels=nullptr;HBITMAP sourceBitmap=CreateDIBSection(sourceDC,&sourceInfo,DIB_RGB_COLORS,&sourcePixels,nullptr,0);const auto sourcePrevious=SelectObject(sourceDC,sourceBitmap);
            if(!sourceDC || !sourceBitmap){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);if(sourceBitmap)DeleteObject(sourceBitmap);if(sourceDC)DeleteDC(sourceDC);return;}
            while(!shared->stop){
                const auto began=GetTickCount64();
                if(!shared->paused){
                    std::fill_n(pixels,Width*Height,0xff16202au);
                    std::vector<HWND> children;for(HWND child=GetWindow(shared->window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){children.push_back(child);if(shared->single || shared->immersive)break;}
                    for(auto it=children.rbegin();it!=children.rend() && !shared->stop;++it){RECT rect{};GetWindowRect(*it,&rect);MapWindowPoints(nullptr,shared->window,reinterpret_cast<POINT*>(&rect),2);
                        if(shared->immersive){DWORD_PTR result=0;if(SendMessageTimeoutW(*it,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result)){
                            std::fill_n(static_cast<uint32_t*>(sourcePixels),1024*1024,0xff16202au);if(PrintWindow(*it,sourceDC,2)){const RECT source=contentRect(*it);SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,0,0,Width,Height,sourceDC,source.left,source.top,source.right-source.left,source.bottom-source.top,SRCCOPY);}}
                            continue;}
                        const int saved=SaveDC(dc);IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);SetViewportOrgEx(dc,rect.left,rect.top,nullptr);
                        DWORD_PTR result=0;if(SendMessageTimeoutW(*it,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result))PrintWindow(*it,dc,2);
                        RestoreDC(dc,saved);
                    }
                    GdiFlush();for(int i=0;i<Width*Height;++i)pixels[i]|=0xff000000u;
                    {std::lock_guard lock(shared->mutex);if(std::memcmp(shared->frame.data(),pixels,Width*Height*4)){std::memcpy(shared->frame.data(),pixels,Width*Height*4);shared->fresh=true;}}
                }
                const auto spent=GetTickCount64()-began;const DWORD interval=static_cast<DWORD>(1000/shared->fps.load());Sleep(spent<interval?interval-static_cast<DWORD>(spent):1);
            }
            SelectObject(sourceDC,sourcePrevious);DeleteObject(sourceBitmap);DeleteDC(sourceDC);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
        }).detach();
    }
    void tick(Link& link){
        if(!state)return;std::unique_lock guard(state->mutex,std::try_to_lock);if(!guard.owns_lock() || !state->fresh)return;
        if(link.lock()){link.data->pixels=state->frame;++link.data->generation;link.unlock();state->fresh=false;}
    }
};
struct Entry {HWND window=nullptr,parent=nullptr,owner=nullptr;DWORD pid=0;LONG_PTR style=0,exstyle=0;WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};RECT rect{};};
struct Host {
    Link link;Capture capture;HWND window=nullptr,focus=nullptr,drag=nullptr;HANDLE parent=nullptr;
    ~Host(){release();}
    std::vector<Entry> entries;bool immersive=false,paused=false;int mode=0,fps=15;POINT origin{};RECT dragRect{};ULONGLONG checked=0;
    void status(const wchar_t* text){if(link.lock()){wcscpy_s(link.data->status,text);link.data->count=static_cast<LONG>(entries.size());++link.data->generation;link.unlock();}}
    void restore(Entry& e){
        DWORD pid=0;GetWindowThreadProcessId(e.window,&pid);if(!IsWindow(e.window) || pid!=e.pid)return;
        SetParent(e.window,e.parent);SetWindowLongPtrW(e.window,GWL_STYLE,e.style);SetWindowLongPtrW(e.window,GWL_EXSTYLE,e.exstyle);
        if(!e.parent)SetWindowLongPtrW(e.window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(IsWindow(e.owner)?e.owner:nullptr));
        SetWindowPos(e.window,nullptr,e.rect.left,e.rect.top,e.rect.right-e.rect.left,e.rect.bottom-e.rect.top,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
        SetWindowPlacement(e.window,&e.placement);ShowWindow(e.window,(e.style&WS_VISIBLE)?(e.placement.showCmd==SW_SHOWMINIMIZED?SW_SHOWMINIMIZED:SW_SHOWNOACTIVATE):SW_HIDE);
    }
    void release(){capture.stop();for(auto it=entries.rbegin();it!=entries.rend();++it)restore(*it);entries.clear();focus=nullptr;}
    void layout(){
        capture.single(mode==0);
        const bool browser=immersive && std::any_of(entries.begin(),entries.end(),[](const Entry& e){return chromium(e.window);});
        SetWindowPos(window,nullptr,0,0,Width,Height+(browser?140:0),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        for(size_t i=0;i<entries.size();++i){auto& e=entries[i];if(!IsWindow(e.window))continue;
            const bool single=mode==0 || immersive;const LONG_PTR style=(e.style&~(WS_POPUP|WS_MINIMIZE|WS_MAXIMIZE))|WS_CHILD;
            SetWindowLongPtrW(e.window,GWL_STYLE,single?style&~(WS_CAPTION|WS_THICKFRAME):style);
            const int offset=static_cast<int>(i%5)*24;
            SetWindowPos(e.window,nullptr,single?0:offset,single?0:offset,single?Width:Width-100,single?Height+(immersive && chromium(e.window)?140:0):Height-100,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        }
        status(entries.empty()?L"点击“接入”选择窗口，或“打开”启动程序后接入。\n兼容模式：传统 Win32 应用优先；GPU 界面可能黑屏。":mode==0?L"单应用铺满 · 原窗口退出时恢复":L"多窗口桌面 · 最大化限制在容器内");
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
        entries.push_back(e);focus=target;layout();SetWindowPos(target,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);return true;
    }
    HWND childAt(POINT point){
        HWND target=window;POINT local=point;
        for(int i=0;i<16;++i){HWND child=ChildWindowFromPointEx(target,local,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED|CWP_SKIPTRANSPARENT);if(!child || child==target)break;MapWindowPoints(target,child,&local,1);target=child;}
        return target;
    }
    void pointer(UINT message,WPARAM buttons,POINT point){
        if(drag){
            if(message==WM_MOUSEMOVE){const int w=dragRect.right-dragRect.left,h=dragRect.bottom-dragRect.top;SetWindowPos(drag,nullptr,std::clamp<int>(dragRect.left+point.x-origin.x,0,std::max(0,Width-w)),std::clamp<int>(dragRect.top+point.y-origin.y,0,std::max(0,Height-h)),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
            if(message==WM_LBUTTONUP)drag=nullptr;return;
        }
        if(immersive){HWND top=GetWindow(window,GW_CHILD);if(top){const RECT area=contentRect(top);POINT clientOrigin{};ClientToScreen(top,&clientOrigin);RECT outer{};GetWindowRect(top,&outer);point={area.left+MulDiv(point.x,area.right-area.left,Width)+outer.left-clientOrigin.x,area.top+MulDiv(point.y,area.bottom-area.top,Height)+outer.top-clientOrigin.y};MapWindowPoints(top,window,&point,1);}}
        HWND target=childAt(point);if(target==window)return;
        POINT screen=point;ClientToScreen(window,&screen);DWORD_PTR area=HTCLIENT;
        SendMessageTimeoutW(target,WM_NCHITTEST,0,MAKELPARAM(screen.x,screen.y),SMTO_ABORTIFHUNG|SMTO_BLOCK,30,&area);
        if(message==WM_LBUTTONDOWN){focus=target;HWND root=target;while(GetParent(root)!=window && GetParent(root))root=GetParent(root);SetWindowPos(root,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            if(area==HTCAPTION && mode==1){drag=root;origin=point;GetWindowRect(root,&dragRect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&dragRect),2);return;}}
        if(area!=HTCLIENT && (message==WM_LBUTTONUP || message==WM_LBUTTONDBLCLK)){
            if(area==HTMAXBUTTON || (area==HTCAPTION && message==WM_LBUTTONDBLCLK)){SetWindowPos(target,nullptr,0,0,Width,Height,SWP_NOACTIVATE|SWP_NOZORDER);return;}
            if(area==HTCLOSE){PostMessageW(target,WM_CLOSE,0,0);return;}
        }
        POINT local=point;MapWindowPoints(window,target,&local,1);
        PostMessageW(target,message,buttons,message==WM_MOUSEWHEEL?MAKELPARAM(screen.x,screen.y):MAKELPARAM(local.x,local.y));
    }
    void tick(){
        if(link.data->closing || WaitForSingleObject(parent,0)!=WAIT_TIMEOUT){DestroyWindow(window);return;}
        const auto now=GetTickCount64();if(now-checked>=250){checked=now;
            entries.erase(std::remove_if(entries.begin(),entries.end(),[](const Entry& e){DWORD pid=0;GetWindowThreadProcessId(e.window,&pid);return !IsWindow(e.window)||pid!=e.pid;}),entries.end());
            for(auto& e:entries){RECT rect{};GetWindowRect(e.window,&rect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
                if(GetParent(e.window)!=window){SetWindowLongPtrW(e.window,GWL_STYLE,(GetWindowLongPtrW(e.window,GWL_STYLE)&~WS_POPUP)|WS_CHILD);SetParent(e.window,window);}
                const int limitHeight=Height+(immersive && chromium(e.window)?140:0);
                if(rect.left<0 || rect.top<0 || rect.right>Width || rect.bottom>limitHeight){const int w=std::clamp<int>(rect.right-rect.left,1,Width),h=std::clamp<int>(rect.bottom-rect.top,1,limitHeight);SetWindowPos(e.window,nullptr,std::clamp<int>(rect.left,0,Width-w),std::clamp<int>(rect.top,0,limitHeight-h),w,h,SWP_NOACTIVATE|SWP_NOZORDER);}
            }
            if(link.lock()){link.data->count=static_cast<LONG>(entries.size());link.unlock();}
        }
        if(!paused)try{capture.tick(link);}catch(const std::exception&){capture.stop();paused=true;status(L"窗口画面捕获失败，请返回后重新接入");}
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Host*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(h,m,w,l);
        switch(m){
        case WM_TIMER:self->tick();return 0;
        case Attach:self->attach(reinterpret_cast<HWND>(l));return 0;
        case Configure:self->mode=static_cast<int>(w);self->fps=std::clamp(static_cast<int>(l),5,30);SetTimer(h,1,1000/self->fps,nullptr);self->capture.rate(self->fps);self->layout();return 0;
        case Release:if(!self->entries.empty()){self->restore(self->entries.back());self->entries.pop_back();self->focus=self->entries.empty()?nullptr:self->entries.back().window;self->layout();}return 0;
        case Pointer:self->pointer(LOWORD(w),HIWORD(w),{GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;
        case Immersive:self->immersive=!self->immersive;self->capture.immersive(self->immersive);self->layout();return 0;
        case Cycle:if(self->entries.size()>1){std::rotate(self->entries.begin(),self->entries.begin()+1,self->entries.end());self->focus=self->entries.back().window;SetWindowPos(self->focus,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}return 0;
        case Keyboard:if(IsWindow(self->focus)){const UINT msg=LOWORD(w);if(msg==WM_KEYDOWN && HIWORD(w)==VK_TAB){HWND root=self->focus;while(GetParent(root)!=self->window && GetParent(root))root=GetParent(root);if(HWND next=GetNextDlgTabItem(root,self->focus,FALSE))self->focus=next;}else PostMessageW(self->focus,msg,HIWORD(w),l);}return 0;
        case Paused:self->paused=w!=0;self->capture.pause(self->paused);return 0;
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
        HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"PICO application workspace",WS_POPUP|WS_CLIPCHILDREN,GetSystemMetrics(SM_XVIRTUALSCREEN)-Width-32,GetSystemMetrics(SM_YVIRTUALSCREEN),Width,Height,nullptr,nullptr,type.hInstance,&host);
        if(!window){CloseHandle(host.parent);return 4;}
        const DWMNCRENDERINGPOLICY policy=DWMNCRP_DISABLED;DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&policy,sizeof(policy));
        ShowWindow(window,SW_SHOWNOACTIVATE);
        host.capture.start(window);host.mode=host.link.data->mode;host.fps=host.link.data->fps;host.capture.rate(host.fps);host.capture.single(host.mode==0);
        if(host.link.lock()){host.link.data->host=window;host.link.unlock();}
        host.status(L"点击“接入”选择窗口，或“打开”启动程序后接入。\n兼容模式：传统 Win32 应用优先；GPU 界面可能黑屏。");SetTimer(window,1,1000/std::clamp(host.fps,5,30),nullptr);
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
        CloseHandle(host.parent);DeleteObject(type.hbrBackground);
    }catch(...){result=5;}
    CoUninitialize();return result;
}

struct Workspace::Impl {
    std::unique_ptr<Link> link;HANDLE process=nullptr;HWND owner=nullptr;bool immersive=false,paused=false,dirty=true;LONG generation=-1;
    int mode=0,fps=15,windows=0;uint64_t frameCount=0;std::wstring message=L"正在启动应用容器…";
    DWORD launchedPid=0;ULONGLONG launchDeadline=0;std::vector<HWND> beforeLaunch;
    std::array<uint32_t,Width*Height> pixels{};HFONT font=nullptr;POINT pointer{};bool pointerDown=false;
    ~Impl(){if(font)DeleteObject(font);if(process)CloseHandle(process);}
};
Workspace::Workspace():impl(std::make_unique<Impl>()){}
Workspace::~Workspace(){close();}
bool Workspace::active()const{return impl->link!=nullptr;}
HWND Workspace::host()const{return active()?impl->link->data->host:nullptr;}
int Workspace::count()const{return impl->windows;}
uint64_t Workspace::frames()const{return impl->frameCount;}
std::wstring Workspace::status()const{return impl->message;}
void Workspace::open(HWND owner,int mode,int fps){
    if(active())return;impl->owner=owner;impl->mode=mode;impl->fps=fps;impl->immersive=false;impl->generation=-1;impl->windows=0;impl->paused=false;impl->dirty=true;impl->message=L"正在启动应用容器…";
    auto link=std::make_unique<Link>();const auto name=L"Local\\PicoPet.App."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
    if(!link->connect(name,true))throw std::runtime_error("Create application frame channel");
    ZeroMemory(link->data,sizeof(Shared));link->data->parent=GetCurrentProcessId();link->data->owner=owner;link->data->mode=mode;link->data->fps=fps;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);std::wstring command=L"\""+std::wstring(executable)+L"\" --app-host \""+name+L"\"";
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("Start application host");
    CloseHandle(process.hThread);impl->process=process.hProcess;impl->link=std::move(link);
}
void Workspace::configure(int mode,int fps){impl->mode=mode;impl->fps=fps;if(host())PostMessageW(host(),Configure,mode,fps);impl->dirty=true;}
bool Workspace::attach(HWND window){return host() && PostMessageW(host(),Attach,0,reinterpret_cast<LPARAM>(window));}
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
    impl->beforeLaunch.clear();EnumWindows([](HWND h,LPARAM data)->BOOL{if(IsWindowVisible(h))reinterpret_cast<std::vector<HWND>*>(data)->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&impl->beforeLaunch));
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI|SEE_MASK_NOASYNC;info.hwnd=impl->owner;info.lpVerb=L"open";info.lpFile=path.c_str();info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info)){const auto message=L"无法启动该快捷方式。Windows 错误："+std::to_wstring(GetLastError());MessageBoxW(impl->owner,message.c_str(),L"电视应用",MB_OK|MB_ICONWARNING);return false;}
    impl->launchedPid=info.hProcess?GetProcessId(info.hProcess):0;if(info.hProcess)CloseHandle(info.hProcess);
    impl->launchDeadline=GetTickCount64()+15000;impl->message=L"正在等待应用窗口；如果程序复用了现有窗口，请点“接入”选择。";impl->dirty=true;return true;
}
void Workspace::close(){
    if(!active())return;InterlockedExchange(&impl->link->data->closing,1);if(host())PostMessageW(host(),WM_CLOSE,0,0);
    // The helper also watches the owner's process; never kill it before restoration finishes.
    impl->link.reset();if(impl->process){CloseHandle(impl->process);impl->process=nullptr;}impl->windows=0;impl->pointerDown=false;impl->launchDeadline=0;
}
void Workspace::suspend(bool value){if(value==impl->paused)return;impl->paused=value;if(host())PostMessageW(host(),Paused,value,0);}
void Workspace::tick(){
    if(!active())return;
    if(WaitForSingleObject(impl->process,0)==WAIT_OBJECT_0){if(impl->message!=L"应用容器已停止，请返回后重试"){impl->message=L"应用容器已停止，请返回后重试";impl->dirty=true;}return;}
    if(impl->launchDeadline && host()){
        if(GetTickCount64()>impl->launchDeadline){impl->launchDeadline=0;impl->message=L"程序已启动，但未找到可确认的新窗口。请点“接入”选择。";impl->dirty=true;}
        else if(impl->launchedPid){struct Search{Impl* impl;HWND found=nullptr;} search{impl.get()};
            EnumWindows([](HWND h,LPARAM data)->BOOL{auto& search=*reinterpret_cast<Search*>(data);DWORD pid=0;GetWindowThreadProcessId(h,&pid);
                if(pid==search.impl->launchedPid && IsWindowVisible(h) && !GetWindow(h,GW_OWNER) && GetWindowTextLengthW(h)>0 && std::find(search.impl->beforeLaunch.begin(),search.impl->beforeLaunch.end(),h)==search.impl->beforeLaunch.end()){search.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
            if(search.found){attach(search.found);impl->launchDeadline=0;}
        }
    }
    if(impl->link->lock()){
        auto& shared=*impl->link->data;impl->windows=shared.count;
        if(shared.generation!=impl->generation){impl->generation=shared.generation;impl->pixels=shared.pixels;if(!impl->launchDeadline)impl->message=shared.status;impl->dirty=true;++impl->frameCount;}
        impl->link->unlock();
    }
}
bool Workspace::updated(){const bool dirty=impl->dirty;impl->dirty=false;return dirty;}
void Workspace::draw(HDC dc,uint32_t* pixels){
    std::fill_n(pixels,800*500,0xff16202au);std::copy(impl->pixels.begin(),impl->pixels.end(),pixels+Width*Bar);
    if(!impl->font)impl->font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    const auto previous=SelectObject(dc,impl->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(219,238,231));
    const wchar_t* labels[]={impl->mode==0?L"单应用":L"多窗口",L"接入",L"打开",L"切换",L"释放",L"返回",impl->immersive?L"还原":L"全屏"};
    for(int i=0;i<7;++i){RECT area{i?90+(i-1)*60:0,0,i?90+i*60:90,Bar};DrawTextW(dc,labels[i],-1,&area,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}
    if(!impl->windows){RECT hint{32,110,768,390};DrawTextW(dc,impl->message.c_str(),-1,&hint,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);}
    else {RECT count{540,0,785,Bar};const auto label=std::to_wstring(impl->windows)+L" 个窗口 · Ctrl+滚轮缩放";DrawTextW(dc,label.c_str(),-1,&count,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}
    SelectObject(dc,previous);
}
bool Workspace::mouse(UINT message,WPARAM buttons,int x,int y){
    if(!active())return false;
    if(y<Bar && x<450 && message==WM_LBUTTONUP){const UINT action=x<90?(impl->mode?Single:Desktop):x<150?Choose:x<210?Launch:x<270?Next:x<330?Detach:x<390?Return:Fullscreen;PostMessageW(impl->owner,WM_COMMAND,action,0);return true;}
    if(y<Bar && !impl->pointerDown)return true;
    if(message==WM_LBUTTONDOWN)impl->pointerDown=true;if(message==WM_LBUTTONUP)impl->pointerDown=false;
    impl->pointer={std::clamp(x,0,Width-1),std::clamp(y-Bar,0,Height-1)};
    if(host())PostMessageW(host(),Pointer,MAKELONG(message,static_cast<WORD>(buttons)),MAKELPARAM(impl->pointer.x,impl->pointer.y));return true;
}
bool Workspace::key(UINT message,WPARAM key,LPARAM data){
    if(!active())return false;
    if(message==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000)){
        const UINT edit=key==L'V'?WM_PASTE:key==L'C'?WM_COPY:key==L'X'?WM_CUT:key==L'Z'?WM_UNDO:0;
        if(edit){if(host())PostMessageW(host(),Keyboard,MAKELONG(edit,0),0);return true;}
    }
    if(message==WM_CHAR && (key==3 || key==22 || key==24 || key==26 || key==VK_TAB))return true;
    if(host())PostMessageW(host(),Keyboard,MAKELONG(message,static_cast<WORD>(key)),data);return true;
}
}
