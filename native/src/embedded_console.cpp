#include "embedded_console.h"
#include "system_core.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace systemdesk {
constexpr SHORT kTerminalColumns=80,kTerminalRows=25;
struct EmbeddedConsole::Impl {
    HWND owner=nullptr;
    mutable std::mutex mutex;
    std::thread worker,reader;
    std::atomic_bool cancel=false,shutdownReader=false,queued=false;
    HANDLE process=nullptr,job=nullptr,in=nullptr,out=nullptr;
    HANDLE tuiProcess=nullptr,tuiJob=nullptr,tuiInput=nullptr,tuiOutput=nullptr;
    std::thread tuiReader;
    std::atomic_bool tuiStop=false;
    std::vector<CHAR_INFO> cells;
    COORD tuiCursor{};
    bool tui=false;
    bool active=false,running=false;
    int mode=0,scroll=0;
    size_t caret=0,historyIndex=0;
    bool attached=false;
    std::vector<std::wstring> history;
    std::wstring draft,input,output=L"",pending,marker;
    ~Impl(){shutdown();}
    void changed(UINT message=kEmbeddedConsoleUpdated){if(owner && (message!=kEmbeddedConsoleUpdated || !queued.exchange(true)))PostMessageW(owner,message,0,0);}
    void append(const std::wstring& text){output+=text;if(output.size()>262144)output.erase(0,output.size()-262144);}
    void closeShell(){shutdownReader=true;if(job)TerminateJobObject(job,ERROR_CANCELLED);else if(process)TerminateProcess(process,ERROR_CANCELLED);if(reader.joinable())reader.join();for(HANDLE h:{in,out,process,job})if(h)CloseHandle(h);in=out=process=job=nullptr;}
    void closeTui(){
        tuiStop=true;if(tuiJob)TerminateJobObject(tuiJob,ERROR_CANCELLED);if(tuiReader.joinable())tuiReader.join();
        for(HANDLE h:{tuiInput,tuiOutput,tuiProcess,tuiJob})if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);
        // FreeConsole() only when we actually attached one; otherwise it would tear down the host's own console.
        if(attached){FreeConsole();attached=false;}tuiInput=tuiOutput=tuiProcess=tuiJob=nullptr;
        std::lock_guard lock(mutex);tui=false;cells.clear();
    }
    void tuiKey(WORD key,wchar_t c=0){
        if(!tuiInput || tuiInput==INVALID_HANDLE_VALUE)return;
        INPUT_RECORD records[2]{};records[0].EventType=KEY_EVENT;auto& event=records[0].Event.KeyEvent;
        event.bKeyDown=TRUE;event.wRepeatCount=1;event.wVirtualKeyCode=key;event.wVirtualScanCode=static_cast<WORD>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC));event.uChar.UnicodeChar=c;
        event.dwControlKeyState=((GetKeyState(VK_CONTROL)&0x8000)?LEFT_CTRL_PRESSED:0)|((GetKeyState(VK_SHIFT)&0x8000)?SHIFT_PRESSED:0);
        records[1]=records[0];records[1].Event.KeyEvent.bKeyDown=FALSE;DWORD written=0;WriteConsoleInputW(tuiInput,records,2,&written);
    }
    void startTui(){
        closeTui();wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);const std::wstring exe=std::wstring(system)+L"\\cmd.exe";
        std::wstring command=L"\""+exe+L"\" /d /q /k \"chcp 65001 >nul\"";STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
        tuiJob=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!tuiJob || !SetInformationJobObject(tuiJob,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) || !CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NEW_CONSOLE|CREATE_SUSPENDED,nullptr,nullptr,&si,&pi)){closeTui();std::lock_guard lock(mutex);append(L"OpenCode console could not start.\r\n");running=false;changed();return;}
        tuiProcess=pi.hProcess;if(!AssignProcessToJobObject(tuiJob,tuiProcess)){TerminateProcess(tuiProcess,1);CloseHandle(pi.hThread);closeTui();std::lock_guard lock(mutex);running=false;append(L"OpenCode process isolation failed.\r\n");changed();return;}ResumeThread(pi.hThread);CloseHandle(pi.hThread);
        attached=false;for(int attempt=0;attempt<50 && !attached;++attempt){attached=AttachConsole(pi.dwProcessId)!=FALSE;if(!attached)Sleep(20);}
        if(!attached){closeTui();std::lock_guard lock(mutex);append(L"OpenCode console attachment failed.\r\n");running=false;changed();return;}
        if(HWND window=GetConsoleWindow())ShowWindow(window,SW_HIDE);
        SetConsoleCP(CP_UTF8);SetConsoleOutputCP(CP_UTF8);
        tuiInput=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        tuiOutput=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(tuiInput==INVALID_HANDLE_VALUE || tuiOutput==INVALID_HANDLE_VALUE){closeTui();std::lock_guard lock(mutex);running=false;append(L"OpenCode console handles unavailable.\r\n");changed();return;}
        SMALL_RECT window{0,0,kTerminalColumns-1,kTerminalRows-1};SetConsoleWindowInfo(tuiOutput,TRUE,&window);
        if(!SetConsoleScreenBufferSize(tuiOutput,{kTerminalColumns,kTerminalRows}) || !SetConsoleWindowInfo(tuiOutput,TRUE,&window)){closeTui();std::lock_guard lock(mutex);running=false;append(L"OpenCode console sizing failed.\r\n");changed();return;}
        // Launch only after the console has its final dimensions, before the TUI queries them.
        FlushConsoleInputBuffer(tuiInput);
        for(wchar_t c:std::wstring(L"opencode"))tuiKey(0,c);
        tuiKey(VK_RETURN,L'\r');
        {std::lock_guard lock(mutex);tui=true;running=true;input.clear();caret=0;}tuiStop=false;
        tuiReader=std::thread([this]{while(!tuiStop && WaitForSingleObject(tuiProcess,50)==WAIT_TIMEOUT){
            std::vector<CHAR_INFO> next(kTerminalColumns*kTerminalRows);CONSOLE_SCREEN_BUFFER_INFO info{};if(!GetConsoleScreenBufferInfo(tuiOutput,&info))continue;
            SMALL_RECT rect{info.srWindow.Left,info.srWindow.Top,static_cast<SHORT>(info.srWindow.Left+kTerminalColumns-1),static_cast<SHORT>(info.srWindow.Top+kTerminalRows-1)};
            if(!ReadConsoleOutputW(tuiOutput,next.data(),{kTerminalColumns,kTerminalRows},{0,0},&rect))continue;
            bool dirty=false;{std::lock_guard lock(mutex);COORD cursor{static_cast<SHORT>(info.dwCursorPosition.X-info.srWindow.Left),static_cast<SHORT>(info.dwCursorPosition.Y-info.srWindow.Top)};
                dirty=cells.size()!=next.size() || memcmp(cells.data(),next.data(),next.size()*sizeof(CHAR_INFO))!=0 || cursor.X!=tuiCursor.X || cursor.Y!=tuiCursor.Y;cells=std::move(next);tuiCursor=cursor;}
            if(dirty)changed();
        }});changed();
    }
    bool write(const std::wstring& text){
        const int count=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
        std::string bytes(static_cast<size_t>(count),0);WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),bytes.data(),count,nullptr,nullptr);
        DWORD sent=0;return in && WriteFile(in,bytes.data(),static_cast<DWORD>(bytes.size()),&sent,nullptr) && sent==bytes.size();
    }
    bool startShell(){
        if(process)return true;
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE childIn=nullptr,childOut=nullptr;
        if(!CreatePipe(&childIn,&in,&sa,0) || !CreatePipe(&out,&childOut,&sa,0)){if(childIn)CloseHandle(childIn);if(childOut)CloseHandle(childOut);closeShell();return false;}
        SetHandleInformation(in,HANDLE_FLAG_INHERIT,0);SetHandleInformation(out,HANDLE_FLAG_INHERIT,0);
        wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);std::wstring exe=std::wstring(system)+L"\\cmd.exe",command=L"\""+exe+L"\" /d /q /a /k \"chcp 65001 >nul\"";
        STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=childIn;si.hStdOutput=childOut;si.hStdError=childOut;PROCESS_INFORMATION pi{};
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const bool configured=job && SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
        const BOOL created=configured && CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&si,&pi);
        CloseHandle(childIn);CloseHandle(childOut);
        if(!created){closeShell();return false;}process=pi.hProcess;
        if(!AssignProcessToJobObject(job,process)){TerminateProcess(process,1);CloseHandle(pi.hThread);closeShell();return false;}
        ResumeThread(pi.hThread);CloseHandle(pi.hThread);shutdownReader=false;
        write(L"@echo off\r\nprompt $s\r\nchcp 65001 >nul\r\n");
        reader=std::thread([this]{
            std::vector<unsigned char> bytes;
            while(!shutdownReader){
                DWORD available=0;if(!PeekNamedPipe(out,nullptr,0,nullptr,&available,nullptr))break;
                if(available){unsigned char buffer[4096];DWORD count=0;if(!ReadFile(out,buffer,std::min<DWORD>(available,sizeof(buffer)),&count,nullptr))break;bytes.insert(bytes.end(),buffer,buffer+count);
                    size_t used=bytes.size();if(used){size_t start=used-1;while(start && (bytes[start]&0xc0)==0x80)--start;const unsigned char lead=bytes[start];const size_t needed=lead<0x80?1:lead<0xe0?2:lead<0xf0?3:4;if(used-start<needed)used=start;}
                    const int length=MultiByteToWideChar(CP_UTF8,0,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(used),nullptr,0);std::wstring chunk(static_cast<size_t>(length),0);if(length)MultiByteToWideChar(CP_UTF8,0,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(used),chunk.data(),length);bytes.erase(bytes.begin(),bytes.begin()+used);
                    {std::lock_guard lock(mutex);pending+=chunk;
                        const auto found=marker.empty()?std::wstring::npos:pending.find(marker);
                        if(found!=std::wstring::npos){append(pending.substr(0,found));pending.erase(0,found+marker.size());running=false;marker.clear();}
                        size_t keep=0;if(!marker.empty())for(size_t n=1;n<=std::min(pending.size(),marker.size());++n)if(pending.compare(pending.size()-n,n,marker,0,n)==0)keep=n;
                        append(pending.substr(0,pending.size()-keep));pending.erase(0,pending.size()-keep);
                    }changed();
                }else if(WaitForSingleObject(process,30)==WAIT_OBJECT_0)break;
            }
            {std::lock_guard lock(mutex);append(pending);pending.clear();running=false;}changed();
        });return true;
    }
    void show(HWND window){{std::lock_guard lock(mutex);owner=window;active=true;}changed();}
    void hide(){stop();{std::lock_guard lock(mutex);active=false;}changed(kEmbeddedConsoleClosed);}
    void shutdown(){cancel=true;closeTui();closeShell();if(worker.joinable())worker.join();owner=nullptr;}
    EmbeddedConsoleState state()const{std::lock_guard lock(mutex);return {active,running,mode,input,output,caret,scroll,cells,{kTerminalColumns,kTerminalRows},tuiCursor,tui};}
    bool visible()const{std::lock_guard lock(mutex);return active;}
    bool busy()const{std::lock_guard lock(mutex);return running;}
    void character(wchar_t c){if(tui){if(c==22){paste();return;}tuiKey(c<32?static_cast<WORD>(c+64):0,c);return;}if(c<32 || c==127)return;{std::lock_guard lock(mutex);if(!active || input.size()>=4096)return;input.insert(caret++,1,c);}changed();}
    void backspace(){key(VK_BACK);}
    void key(UINT k){
        if(tui){tuiKey(static_cast<WORD>(k),k==VK_BACK?L'\b':k==VK_TAB?L'\t':k==VK_ESCAPE?static_cast<wchar_t>(27):0);return;}
        {std::lock_guard lock(mutex);
        if(k==VK_LEFT && caret) --caret;else if(k==VK_RIGHT && caret<input.size())++caret;
        else if(k==VK_HOME)caret=0;else if(k==VK_END)caret=input.size();
        else if(k==VK_BACK && caret)input.erase(--caret,1);else if(k==VK_DELETE && caret<input.size())input.erase(caret,1);
        else if(k==VK_PRIOR)scroll=std::min(scroll+6,static_cast<int>(output.size()/kTerminalColumns)+kTerminalRows);else if(k==VK_NEXT)scroll=std::max(0,scroll-6);
        else if(k==VK_UP && historyIndex){if(historyIndex==history.size())draft=input;input=history[--historyIndex];caret=input.size();}
        else if(k==VK_DOWN && historyIndex<history.size()){++historyIndex;input=historyIndex==history.size()?draft:history[historyIndex];caret=input.size();}}
        changed();
    }
    void paste(){if(!OpenClipboard(owner))return;HANDLE h=GetClipboardData(CF_UNICODETEXT);if(h){const auto* p=static_cast<const wchar_t*>(GlobalLock(h));if(p){for(size_t i=0;i<4096 && p[i];++i){const wchar_t c=p[i]==L'\n'||p[i]==L'\r'?L' ':p[i];
        // Feed the TUI directly: going through character() would recurse forever on a pasted Ctrl+V (0x16).
        if(tui)tuiKey(c<32?static_cast<WORD>(c+64):0,c);else character(c);}GlobalUnlock(h);}}CloseClipboard();}
    void cycleMode(int direction){ {std::lock_guard lock(mutex);if(running)return;mode=(mode+direction+3)%3;}changed();}
    void execute(){
        if(tui){tuiKey(VK_RETURN,L'\r');return;}
        if(worker.joinable()){bool busy;{std::lock_guard lock(mutex);busy=running;}if(busy)return;worker.join();}
        int selected;std::wstring command;bool wasRunning;
        {std::lock_guard lock(mutex);if(!active || input.empty())return;selected=mode;command=input;wasRunning=running;
            if(mode==0 && !running && _wcsicmp(command.c_str(),L"codex")==0){mode=1;input.clear();caret=0;changed();return;}
            history.push_back(command);if(history.size()>100)history.erase(history.begin());historyIndex=history.size();input.clear();caret=0;scroll=0;append(L"\r\n> "+command+L"\r\n");running=true;}
        if(selected==0 && !wasRunning && _wcsicmp(command.c_str(),L"opencode")==0){startTui();return;}
        if(selected==0){
            if(process && WaitForSingleObject(process,0)==WAIT_OBJECT_0)closeShell();
            if(!startShell()){std::lock_guard lock(mutex);running=false;append(L"CMD could not start.\r\n");}
            else {std::wstring suffix;if(!wasRunning){std::lock_guard lock(mutex);marker=L"__PICO_DONE_"+std::to_wstring(GetTickCount64())+L"__";suffix=L"\r\necho "+marker+L"\r\n";}if(!write(command+(wasRunning?L"\r\n":suffix))){std::lock_guard lock(mutex);running=false;append(L"CMD input failed.\r\n");}}
            changed();return;
        }
        cancel=false;changed();ConsoleRequest request{command,std::filesystem::current_path().wstring(),selected,true};
        worker=std::thread([this,request]{Table result=runConsoleCommand(request,cancel);{std::lock_guard lock(mutex);if(!result.rows.empty())append(result.rows[0][7]+L"\r\n");else append(result.summary);running=false;}changed();});
    }
    void stop(){cancel=true;closeTui();closeShell();{std::lock_guard lock(mutex);running=false;marker.clear();append(L"\r\n[Stopped; CMD session reset]\r\n");}changed();}
};
EmbeddedConsole::EmbeddedConsole():impl(std::make_unique<Impl>()){}
EmbeddedConsole::~EmbeddedConsole()=default;
void EmbeddedConsole::show(HWND h){impl->show(h);}void EmbeddedConsole::hide(){impl->hide();}void EmbeddedConsole::shutdown(){impl->shutdown();}
void EmbeddedConsole::character(wchar_t c){impl->character(c);}void EmbeddedConsole::backspace(){impl->backspace();}void EmbeddedConsole::paste(){impl->paste();}
void EmbeddedConsole::execute(){impl->execute();}void EmbeddedConsole::stop(){impl->stop();}void EmbeddedConsole::cycleMode(int d){impl->cycleMode(d);}
void EmbeddedConsole::key(UINT k){impl->key(k);}void EmbeddedConsole::acknowledge(){impl->queued=false;}
bool EmbeddedConsole::interactive()const{std::lock_guard lock(impl->mutex);return impl->tui;}
bool EmbeddedConsole::visible()const{return impl->visible();}bool EmbeddedConsole::busy()const{return impl->busy();}
bool EmbeddedConsole::outputContains(const std::wstring& s)const{const auto state=impl->state();if(state.interactive){std::wstring text;for(const auto& cell:state.cells)if(!(cell.Attributes&COMMON_LVB_TRAILING_BYTE))text+=cell.Char.UnicodeChar;return text.find(s)!=std::wstring::npos;}return state.output.find(s)!=std::wstring::npos;}
EmbeddedConsoleState EmbeddedConsole::state()const{return impl->state();}
}
