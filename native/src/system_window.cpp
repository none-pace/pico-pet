#include "system_core.h"
#include "system_presentation.h"
#include "system_observations.h"
#include "system_cards.h"
#include <uxtheme.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <set>
#include <array>
#include <cmath>
#include <deque>

namespace systemdesk {
namespace {
constexpr UINT kDone=WM_APP+40;
constexpr UINT kHardwareDone=WM_APP+41;
constexpr UINT kDiagnosticsDone=WM_APP+42;
constexpr UINT kMonitorDone=WM_APP+43;
enum Control { Tabs=3000,Path,Choose,Up,Browse,Volumes,Scan,Resume,Cancel,Before,After,Compare,SnapshotView,Filter,Refresh,Export,PauseNetwork,NetworkView,ClearLog,Rows,Status,NetworkFilter,Headline,DeleteSnapshot,PerformanceView,DeviceClass,DeviceFilter,DnsNames,TcpRates,NetworkScope,FullColumns,NetworkOptions,DetailText,DetailTitle,DetailClose,DetailCopy,DetailToggle,Info,DiskMode,EmptyState,
    DiagnosticsMode,LogChannel,LogPeriod,LogLevel,LogId,DiagnosticsFilter,DiagnosticsRead,RegistryPath,RegistryView,RegistryUp,RegistryLocation,DiagnosticsCancel,ConciseMode,Activity,
    TrackerPath,TrackerChoose,TrackerToggle,TrackerClear,TrackerScope,
    ConsoleMode,ConsoleInput,ConsoleDirectory,ConsoleRun,ConsoleTerminal,ConsoleClear,
    FreezeView,FocusSoftware,AllSoftware,AnalysisText,DetailFull,ViewStyle,CardBoard,QuickFilter,DetailSections,GroupProcesses,EmptyAll,SortCards,SortLive,SortRefresh,ProcessScope,TraceCapture };
struct ActivityPoint {ULONGLONG time=0;double primary=0,secondary=0;bool valid=false;};
class Desk {
public:
    HWND hwnd=nullptr,tabs=nullptr,list=nullptr,status=nullptr,path=nullptr,filter=nullptr,before=nullptr,after=nullptr,netView=nullptr,netFilter=nullptr,headline=nullptr;
    HWND perfView=nullptr,deviceClass=nullptr,deviceFilter=nullptr,netScope=nullptr;
    HFONT font=nullptr,iconFont=nullptr,headingFont=nullptr;
    HWND tooltip=nullptr,detailText=nullptr,detailTitle=nullptr,emptyState=nullptr,diskMode=nullptr,activity=nullptr;
    HWND diagnosticsMode=nullptr,logChannel=nullptr,logPeriod=nullptr,logLevel=nullptr,logId=nullptr,diagnosticsFilter=nullptr,registryPath=nullptr,registryView=nullptr,registryLocation=nullptr;
    HWND trackerPath=nullptr,trackerScope=nullptr;
    HWND consoleMode=nullptr,consoleInput=nullptr,consoleDirectory=nullptr;
    std::vector<HWND> diagnosticControls;
    std::thread diagnosticWorker;
    std::atomic_bool diagnosticCancel=false;
    bool diagnosticBusy=false,diagnosticPending=false;
    int diagnosticTarget=0;
    Table diagnosticResult;
    Table commandHistory{{L"记录时间",L"类型",L"状态",L"退出码",L"耗时",L"命令 / 提示",L"工作目录",L"完整输出"},{},L"尚未运行命令。"};
    HIMAGELIST rowHeight=nullptr;
    std::vector<int> visibleColumns;
    std::vector<std::wstring> displayHeadings;
    bool fullColumns=false,conciseMode=true,optionsOpen=false,detailOpen=false,updating=false;
    std::wstring fullStatus,detailValue,conciseExplanation;
    int page=0,dpi=96;
    int performanceMode=0;
    std::array<std::wstring,3> performanceFilters;
    bool pausedNetwork=false,suspended=false;
    bool ready=false;
    std::vector<HWND> diskControls,networkControls,commonControls,performanceControls;
    Table hardware,hardwareResult,networkTable;
    std::thread hardwareWorker;
    bool hardwareBusy=false,hardwareLoaded=false;
    Table table,fullTable;
    bool frozen=false;Table frozenLatest;size_t frozenUpdates=0;
    std::wstring focusedSoftware,focusedName,retainedTitle,retainedDetails,retainedFullDetails;
    std::array<std::wstring,3> retainedSections;
    bool reorderView=false,sortDescending=true;int sortColumn=-1;
    Row retainedIdentity;
    NetworkObservations observations;
    ProcessObservations processObservations;
    cards::Insights insights;
    cards::Board board;
    bool cardMode=true;
    bool groupProcesses=true;
    int detailSection=0;
    Table networkHistory,softwareTable,networkLog,processTable;
    ProcessPerformance processPerformance;
    Network networkSampler;
    NetworkTrace networkTrace;Table traceTable;bool traceCapture=true;
    std::thread monitorWorker;
    bool monitorBusy=false,networkReset=true,clearNetworkLog=false;
    uint64_t monitorGeneration=0,completedGeneration=0;
    int monitorTarget=0;
    struct MonitorResult {Table connections,adapters,processes,log,performance,trace;std::wstring rates,error;uint64_t rx=0,tx=0;bool available=false,complete=false;};
    MonitorResult monitorResult;
    Performance performance;
    Network network;
    SecurityTracker tracker;
    uint64_t trackerGeneration=0;
    DiskActivity diskActivity;
    std::deque<ActivityPoint> activityPoints;
    std::wstring activityKey,activityTitle=L"活动时序",activityPrimary=L"读取",activitySecondary=L"写入",activityPrimaryValue=L"--",activitySecondaryValue=L"--",activityMessage=L"等待下一次采样";
    bool activityAvailable=false;
    uint64_t scanFiles=0,scanBytes=0;ULONGLONG scanTime=0;
    std::unique_ptr<Index> index;
    std::vector<Snapshot> snapshots;
    std::thread worker;
    std::atomic_bool busy=false,cancel=false;
    ScanProgress progress;
    std::mutex mutex;
    Table completed;
    std::wstring workError;
    bool workScan=false,scanChartHold=false;
    bool workReload=false;
    std::wstring folder=L"C:\\";
    ~Desk(){cancel=true;diagnosticCancel=true;if(monitorWorker.joinable())monitorWorker.join();if(worker.joinable())worker.join();if(hardwareWorker.joinable())hardwareWorker.join();if(diagnosticWorker.joinable())diagnosticWorker.join();if(font)DeleteObject(font);if(iconFont)DeleteObject(iconFont);if(headingFont)DeleteObject(headingFont);if(rowHeight)ImageList_Destroy(rowHeight);}
    int px(int value)const{return MulDiv(value,dpi,96);}
    HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int id){
        HWND c=CreateWindowExW(type==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,100,25,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;
    }
    HWND button(const wchar_t* text,int id){return control(L"BUTTON",text,BS_PUSHBUTTON,id);}
    static std::wstring text(HWND item){const int size=GetWindowTextLengthW(item);std::wstring result(static_cast<size_t>(size)+1,L'\0');GetWindowTextW(item,result.data(),size+1);result.resize(static_cast<size_t>(size));return result;}
    void setStatus(const std::wstring& value){
        fullStatus=value;
        std::wstring summary;
        if(page==0)summary=hardwareBusy?L"正在读取设备…":L"本机数据";
        if(page==1)summary=busy?L"后台任务进行中":L"本地磁盘";
        if(page==2)summary=pausedNetwork?L"采样已暂停":SendMessageW(netView,CB_GETCURSEL,0,0)==6?L"事件捕获 · 约 1 秒显示":L"实时 · 1 秒采样";
        if(page==3){const auto mode=SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0);summary=mode==2?(tracker.active()?L"安全追踪中":L"追踪已停止"):mode==4?(diagnosticBusy?L"命令运行中":L"内嵌命令台"):(diagnosticBusy?L"正在读取…":L"本机只读");}
        summary+=L"    "+std::to_wstring(table.rows.size());
        if(conciseMode && !fullTable.columns.empty())summary+=L" / "+std::to_wstring(fullTable.rows.size());
        summary+=L" 项";
        if(value!=table.summary){summary+=L"    "+value.substr(0,value.find_first_of(L"\r\n"));}
        else if(page==2)summary+=SendMessageW(netView,CB_GETCURSEL,0,0)==6?L"    事件原始地址":network.resolveDns?L"    DNS 已开启":L"    DNS 已关闭";
        if(page==3 && !diagnosticBusy && value==table.summary){
            summary+=L"    "+value.substr(0,value.find_first_of(L"\r\n。"));
            if(value.find(L"达到")!=std::wstring::npos)summary+=L"    已达读取上限";
        }
        if(frozen)summary=L"已定格阅读 · 后台继续采样 · "+std::to_wstring(frozenUpdates)+L" 次更新待查看";
        SetWindowTextW(status,summary.c_str());
    }
    HWND iconButton(const wchar_t* glyph,int id,const wchar_t* label){
        const HWND item=button(glyph,id);SetWindowTextW(item,glyph);SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(iconFont),TRUE);
        TOOLINFOW tip{sizeof(tip)};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=hwnd;tip.uId=reinterpret_cast<UINT_PTR>(item);tip.lpszText=const_cast<wchar_t*>(label);SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));
        SetPropW(item,L"PicoIcon",reinterpret_cast<HANDLE>(1));return item;
    }
    void fonts(){
        if(font)DeleteObject(font);if(iconFont)DeleteObject(iconFont);if(headingFont)DeleteObject(headingFont);
        font=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        headingFont=CreateFontW(-px(14),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        iconFont=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe Fluent Icons");
        EnumChildWindows(hwnd,[](HWND child,LPARAM value)->BOOL{auto* self=reinterpret_cast<Desk*>(value);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetPropW(child,L"PicoIcon")?self->iconFont:self->font),TRUE);return TRUE;},reinterpret_cast<LPARAM>(this));
        if(headline)SendMessageW(headline,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont),TRUE);
        if(detailTitle)SendMessageW(detailTitle,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont),TRUE);
        board.appearance(dpi,font,headingFont,iconFont);
        if(list){const auto old=rowHeight;rowHeight=ImageList_Create(1,px(29),ILC_COLOR32,1,1);ListView_SetImageList(list,rowHeight,LVSIL_SMALL);if(old)ImageList_Destroy(old);}
    }
    void create(){
        dpi=static_cast<int>(GetDpiForWindow(hwnd));fonts();
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        tabs=control(WC_TABCONTROLW,L"",0,Tabs);
        for(const auto* label:{L"性能与设备",L"磁盘与快照",L"网络监测",L"安全与日志"}){TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(label);TabCtrl_InsertItem(tabs,TabCtrl_GetItemCount(tabs),&tab);}
        path=control(L"EDIT",folder.c_str(),ES_AUTOHSCROLL,Path);
        diskControls={path,button(L"选择文件夹",Choose),button(L"上一级",Up),button(L"浏览",Browse),button(L"盘符 / 布局",Volumes),button(L"新建快照",Scan),button(L"继续扫描",Resume),button(L"暂停扫描",Cancel)};
        before=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,Before);after=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,After);
        diskControls.push_back(before);diskControls.push_back(after);diskControls.push_back(button(L"对比 A → B",Compare));diskControls.push_back(button(L"查看 B 快照",SnapshotView));
        diskControls.push_back(button(L"删除 B 快照",DeleteSnapshot));
        filter=control(L"EDIT",L"",ES_AUTOHSCROLL,Filter);SendMessageW(filter,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"路径包含（快照筛选）"));diskControls.push_back(filter);
        netView=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,NetworkView);
        SendMessageW(netView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"连接（含最近结束）"));SendMessageW(netView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"连接变化日志"));SendMessageW(netView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"网络接口速率"));SendMessageW(netView,CB_SETCURSEL,0,0);
        SendMessageW(netView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"进程 TCP 速率"));
        SendMessageW(netView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"软件概览"));
        SendMessageW(netView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"连接历史（保留）"));SendMessageW(netView,CB_SETCURSEL,4,0);
        SendMessageW(netView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"实时捕获 · TCP / UDP"));
        netFilter=control(L"EDIT",L"",ES_AUTOHSCROLL,NetworkFilter);SendMessageW(netFilter,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"进程、域名、IP 或端口"));
        netScope=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,NetworkScope);
        for(const auto* item:{L"所有地址范围",L"本机回环",L"本机接口",L"局域网 / 私有地址",L"公网地址",L"链路本地",L"通配 / 未指定",L"远端未提供",L"运营商共享地址",L"组播",L"广播",L"特殊 / 保留地址"})SendMessageW(netScope,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
        SendMessageW(netScope,CB_SETCURSEL,0,0);
        const auto dns=control(L"BUTTON",L"DNS 反查",BS_AUTOCHECKBOX,DnsNames);
        const auto tcp=control(L"BUTTON",L"逐连接 TCP 测速",BS_AUTOCHECKBOX,TcpRates);
        const auto trace=control(L"BUTTON",L"事件捕获（短连接 / UDP）",BS_AUTOCHECKBOX,TraceCapture);
        const auto preferences=(dataDirectory()/L"settings.ini").wstring();
        conciseMode=GetPrivateProfileIntW(L"System",L"concise",1,preferences.c_str())!=0;
        const int style=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"System",L"viewStyle",0,preferences.c_str())),0,2);
        cardMode=style==0;fullColumns=style==2;
        groupProcesses=GetPrivateProfileIntW(L"System",L"groupProcesses",1,preferences.c_str())!=0;
        network.resolveDns=GetPrivateProfileIntW(L"System",L"dns",1,preferences.c_str())!=0;
        network.measureConnections=GetPrivateProfileIntW(L"System",L"tcpRates",1,preferences.c_str())!=0;
        traceCapture=GetPrivateProfileIntW(L"System",L"traceCapture",1,preferences.c_str())!=0;SendMessageW(trace,BM_SETCHECK,traceCapture?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(dns,BM_SETCHECK,network.resolveDns?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(tcp,BM_SETCHECK,network.measureConnections?BST_CHECKED:BST_UNCHECKED,0);
        networkControls={netView,button(L"暂停采样",PauseNetwork),button(L"清空日志",ClearLog),netFilter,netScope,dns,tcp};
        perfView=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,PerformanceView);
        for(const auto* item:{L"系统总览",L"硬件与驱动",L"进程与资源"})SendMessageW(perfView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
        SendMessageW(perfView,CB_SETCURSEL,0,0);
        deviceClass=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,DeviceClass);
        SendMessageW(deviceClass,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"全部设备类别"));SendMessageW(deviceClass,CB_SETCURSEL,0,0);
        deviceFilter=control(L"EDIT",L"",ES_AUTOHSCROLL,DeviceFilter);SendMessageW(deviceFilter,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"设备名称、驱动版本或硬件 ID"));
        performanceControls={perfView,deviceClass,deviceFilter};
        diagnosticsMode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,DiagnosticsMode);
        for(const auto* label:{L"事件日志",L"注册表",L"安全追踪",L"安全总览",L"命令助手"})SendMessageW(diagnosticsMode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(diagnosticsMode,CB_SETCURSEL,3,0);
        logChannel=control(L"COMBOBOX",L"",CBS_DROPDOWN|WS_VSCROLL,LogChannel);
        for(const auto* label:{L"System",L"Application",L"Setup",L"Security",L"Microsoft-Windows-Windows Defender/Operational",L"Microsoft-Windows-PowerShell/Operational"})SendMessageW(logChannel,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(logChannel,CB_SETCURSEL,0,0);
        logPeriod=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,LogPeriod);
        for(const auto* label:{L"最近 1 小时",L"最近 24 小时",L"最近 7 天",L"最近 30 天"})SendMessageW(logPeriod,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(logPeriod,CB_SETCURSEL,1,0);
        logLevel=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,LogLevel);
        for(const auto* label:{L"所有级别",L"警告与错误",L"错误与严重"})SendMessageW(logLevel,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(logLevel,CB_SETCURSEL,0,0);
        logId=control(L"EDIT",L"",ES_AUTOHSCROLL|ES_NUMBER,LogId);SendMessageW(logId,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"事件 ID"));
        diagnosticsFilter=control(L"EDIT",L"",ES_AUTOHSCROLL,DiagnosticsFilter);SendMessageW(diagnosticsFilter,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"关键词"));
        registryPath=control(L"EDIT",L"HKCU\\Software",ES_AUTOHSCROLL,RegistryPath);
        registryView=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,RegistryView);
        for(const auto* label:{L"64 位视图",L"32 位视图"})SendMessageW(registryView,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(registryView,CB_SETCURSEL,0,0);
        registryLocation=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,RegistryLocation);
        for(const auto* label:{L"常用位置",L"当前用户软件",L"当前用户自启动",L"系统自启动",L"系统服务与驱动",L"当前用户代理",L"软件卸载信息",L"Windows 版本",L"系统策略"})SendMessageW(registryLocation,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(registryLocation,CB_SETCURSEL,0,0);
        trackerPath=control(L"EDIT",L"",ES_AUTOHSCROLL,TrackerPath);SendMessageW(trackerPath,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"文件或目录路径；留空时只追踪程序"));
        trackerScope=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,TrackerScope);
        for(const auto* label:{L"全部活动",L"仅需关注",L"仅程序",L"仅文件"})SendMessageW(trackerScope,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(trackerScope,CB_SETCURSEL,0,0);
        consoleMode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,ConsoleMode);for(const auto* label:{L"CMD",L"Codex 只读",L"Codex 工作区"})SendMessageW(consoleMode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(consoleMode,CB_SETCURSEL,1,0);
        consoleDirectory=control(L"EDIT",std::filesystem::current_path().c_str(),ES_AUTOHSCROLL,ConsoleDirectory);SendMessageW(consoleDirectory,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"工作目录"));
        consoleInput=control(L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,ConsoleInput);SendMessageW(consoleInput,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"输入 CMD 命令或 Codex 提示"));SendMessageW(consoleInput,EM_SETLIMITTEXT,32768,0);
        diagnosticControls={diagnosticsMode,logChannel,logPeriod,logLevel,logId,diagnosticsFilter,registryPath,registryView,registryLocation,button(L"读取",DiagnosticsRead),button(L"上一级",RegistryUp),button(L"取消读取",DiagnosticsCancel),trackerPath,trackerScope,button(L"选择目录",TrackerChoose),button(L"开始追踪",TrackerToggle),button(L"清空记录",TrackerClear),consoleMode,consoleInput,consoleDirectory,button(L"运行",ConsoleRun),button(L"在终端继续",ConsoleTerminal),button(L"清空历史",ConsoleClear)};
        EnableWindow(GetDlgItem(hwnd,DiagnosticsCancel),FALSE);
        commonControls={iconButton(L"\uE72C",Refresh,L"刷新"),iconButton(L"\uE74E",Export,L"导出全部字段为 CSV")};
        const auto concise=control(L"BUTTON",L"简洁模式",BS_AUTOCHECKBOX,ConciseMode);SendMessageW(concise,BM_SETCHECK,conciseMode?BST_CHECKED:BST_UNCHECKED,0);
        control(L"BUTTON",L"完整列",BS_AUTOCHECKBOX,FullColumns);
        iconButton(L"\uE713",NetworkOptions,L"网络采集设置");
        iconButton(L"\uE8A5",DetailToggle,L"显示 / 收起详情");
        iconButton(L"\uE946",Info,L"数据来源与采集说明");
        iconButton(L"\uE711",DetailClose,L"收起详情");
        iconButton(L"\uE8C8",DetailCopy,L"复制完整详情");
        diskMode=control(WC_TABCONTROLW,L"",0,DiskMode);
        for(const auto* label:{L"浏览",L"快照"}){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(label);TabCtrl_InsertItem(diskMode,TabCtrl_GetItemCount(diskMode),&item);}
        list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_OWNERDATA|LVS_SHOWSELALWAYS,Rows);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP|LVS_EX_HEADERDRAGDROP);
        SetWindowTheme(list,L"Explorer",nullptr);ListView_SetBkColor(list,RGB(255,255,255));ListView_SetTextBkColor(list,CLR_NONE);ListView_SetTextColor(list,RGB(35,43,50));
        status=control(L"STATIC",L"",SS_LEFT|SS_ENDELLIPSIS,Status);
        headline=control(L"STATIC",L"",SS_LEFT,Headline);
        detailTitle=control(L"STATIC",L"详细信息",SS_LEFT|SS_ENDELLIPSIS,DetailTitle);
        detailText=control(L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL,DetailText);
        SendMessageW(detailText,EM_SETLIMITTEXT,1024*1024,0);
        emptyState=control(L"STATIC",L"暂无匹配结果",SS_CENTER,EmptyState);
        button(L"显示全部项目",EmptyAll);
        activity=control(L"STATIC",L"",SS_OWNERDRAW,Activity);
        button(L"定格阅读",FreezeView);button(L"只看此软件",FocusSoftware);button(L"全部软件",AllSoftware);
        control(L"STATIC",L"",SS_LEFT,AnalysisText);
        control(L"BUTTON",L"全部字段",BS_AUTOCHECKBOX,DetailFull);
        const auto viewStyle=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,ViewStyle);
        for(const auto* label:{L"卡片概览",L"精简表格",L"完整表格"})SendMessageW(viewStyle,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(viewStyle,CB_SETCURSEL,style,0);
        const auto quickFilter=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,QuickFilter);
        for(const auto* label:{L"全部软件",L"正在上传",L"公网通信",L"本机通信",L"监听端点",L"测速不完整",L"本轮无端点",L"保留的上传提示"})SendMessageW(quickFilter,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(quickFilter,CB_SETCURSEL,0,0);
        SetWindowTextW(GetDlgItem(hwnd,ConciseMode),L"仅看重点");
        const auto sections=control(WC_TABCONTROLW,L"",0,DetailSections);
        for(const auto* label:{L"解读",L"证据",L"全部字段"}){TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(label);TabCtrl_InsertItem(sections,TabCtrl_GetItemCount(sections),&tab);}
        const auto grouped=control(L"BUTTON",L"合并同软件",BS_AUTOCHECKBOX,GroupProcesses);SendMessageW(grouped,BM_SETCHECK,groupProcesses?BST_CHECKED:BST_UNCHECKED,0);
        const auto sorting=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,SortCards);
        SendMessageW(sorting,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"排序：保持位置"));SendMessageW(sorting,CB_SETCURSEL,0,0);
        control(L"BUTTON",L"实时排序",BS_AUTOCHECKBOX,SortLive);iconButton(L"\uE8CB",SortRefresh,L"按当前数值重新排序");
        const auto processScope=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,ProcessScope);
        for(const auto* label:{L"当前与最近未见",L"仅当前进程",L"最近未见（60 秒）"})SendMessageW(processScope,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(processScope,CB_SETCURSEL,0,0);
        board.create(hwnd,CardBoard,[this]{return static_cast<int>(table.rows.size());},[this](int i){return cards::describe(table,table.rows[static_cast<size_t>(i)]);},[this](int i,bool open){
            ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
            updateDetail(true);if(page==2)updateNetworkActivity();if(open)activateRow();
        });
        fonts();
        index=std::make_unique<Index>(dataDirectory()/L"index.db");
        ready=true;SetTimer(hwnd,1,1000,nullptr);select(page);
    }
    void layout(){
        if(!ready)return;
        RECT r{};GetClientRect(hwnd,&r);const int w=MulDiv(r.right,96,dpi),h=MulDiv(r.bottom,96,dpi);
        auto place=[&](HWND c,int x,int y,int width,int height=30){MoveWindow(c,px(x),px(y),px(std::max(width,1)),px(std::max(height,1)),TRUE);};
        auto showControl=[&](int id,bool visible){ShowWindow(GetDlgItem(hwnd,id),visible?SW_SHOW:SW_HIDE);};
        place(tabs,16,12,w-356,32);
        place(GetDlgItem(hwnd,ConciseMode),w-328,13,84,28);
        place(GetDlgItem(hwnd,ViewStyle),w-236,13,98,140);
        showControl(FullColumns,false);
        place(GetDlgItem(hwnd,DetailToggle),w-132,12,32,30);
        place(commonControls[0],w-94,12,32,30);place(commonControls[1],w-56,12,32,30);
        int y=136;
        for(HWND item:diskControls)ShowWindow(item,page==1?SW_SHOW:SW_HIDE);
        for(HWND item:networkControls)ShowWindow(item,page==2?SW_SHOW:SW_HIDE);
        for(HWND item:performanceControls)ShowWindow(item,page==0?SW_SHOW:SW_HIDE);
        for(HWND item:diagnosticControls)ShowWindow(item,page==3?SW_SHOW:SW_HIDE);
        showControl(NetworkOptions,page==2);showControl(DiskMode,page==1);
        showControl(TraceCapture,page==2 && optionsOpen);
        showControl(QuickFilter,page==2 && SendMessageW(netView,CB_GETCURSEL,0,0)==4);
        showControl(GroupProcesses,page==0 && SendMessageW(perfView,CB_GETCURSEL,0,0)==2);
        showControl(ProcessScope,page==0 && SendMessageW(perfView,CB_GETCURSEL,0,0)==2);
        showControl(FreezeView,page==0 || page==2);showControl(FocusSoftware,page==2 || (page==0 && SendMessageW(perfView,CB_GETCURSEL,0,0)==2));showControl(AllSoftware,page==2);showControl(AnalysisText,page==2);
        if(page==1){
            const bool snapshotsMode=TabCtrl_GetCurSel(diskMode)==1;
            place(diskMode,16,56,148,30);
            place(path,180,56,w-482);place(GetDlgItem(hwnd,Choose),w-294,56,96);place(GetDlgItem(hwnd,Up),w-190,56,74);place(GetDlgItem(hwnd,Browse),w-108,56,92);
            for(const int id:{Scan,Resume,Cancel,Before,After,Compare,SnapshotView,DeleteSnapshot,Filter})showControl(id,snapshotsMode);
            showControl(Volumes,!snapshotsMode);
            if(snapshotsMode){
                place(GetDlgItem(hwnd,Scan),16,96,96);place(GetDlgItem(hwnd,Resume),120,96,96);place(GetDlgItem(hwnd,Cancel),224,96,96);place(filter,336,96,w-352);
                place(before,16,136,(w-372)/2,260);place(after,24+(w-372)/2,136,(w-372)/2,260);
                place(GetDlgItem(hwnd,Compare),w-332,136,96);place(GetDlgItem(hwnd,SnapshotView),w-228,136,100);place(GetDlgItem(hwnd,DeleteSnapshot),w-120,136,104);y=178;
            }else{place(GetDlgItem(hwnd,Volumes),16,96,124);place(headline,156,101,w-172,24);y=138;}
        }else if(page==2){
            const auto mode=SendMessageW(netView,CB_GETCURSEL,0,0);const bool scopeVisible=mode<2 || mode==5 || mode==6;
            place(netView,16,56,156,220);showControl(NetworkScope,scopeVisible);
            if(scopeVisible)place(netScope,184,56,166,340);
            if(mode==4)place(GetDlgItem(hwnd,QuickFilter),184,56,166,240);
            const int search=scopeVisible || mode==4?362:184;place(netFilter,search,56,w-search-162);
            place(GetDlgItem(hwnd,PauseNetwork),w-150,56,96);place(GetDlgItem(hwnd,NetworkOptions),w-46,56,30);
            showControl(DnsNames,optionsOpen);showControl(TcpRates,optionsOpen);showControl(ClearLog,optionsOpen);
            place(GetDlgItem(hwnd,FocusSoftware),16,96,118);place(GetDlgItem(hwnd,AllSoftware),142,96,100);place(GetDlgItem(hwnd,FreezeView),250,96,108);
            EnableWindow(GetDlgItem(hwnd,AllSoftware),!focusedSoftware.empty());
            place(headline,374,99,w-390,25);
            if(optionsOpen){place(GetDlgItem(hwnd,DnsNames),16,136,128);place(GetDlgItem(hwnd,TcpRates),160,136,176);place(GetDlgItem(hwnd,ClearLog),352,136,96);place(GetDlgItem(hwnd,TraceCapture),460,136,260);}
            const int summaryY=optionsOpen?177:137;place(GetDlgItem(hwnd,AnalysisText),16,summaryY,w-32,44);y=summaryY+54;
        }else if(page==3){
            const int mode=static_cast<int>(SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0));const bool registry=mode==1,tracking=mode==2,overview=mode==3,console=mode==4;
            place(diagnosticsMode,16,56,140,260);
            for(const int id:{LogChannel,LogPeriod,LogLevel,LogId})showControl(id,mode==0);
            for(const int id:{RegistryPath,RegistryView,RegistryUp,RegistryLocation})showControl(id,registry);
            for(const int id:{TrackerPath,TrackerScope,TrackerChoose,TrackerToggle,TrackerClear})showControl(id,tracking);
            for(const int id:{ConsoleMode,ConsoleInput,ConsoleDirectory,ConsoleRun,ConsoleTerminal,ConsoleClear})showControl(id,console);
            showControl(DiagnosticsFilter,mode<=2);showControl(DiagnosticsRead,mode==0 || registry || overview);showControl(DiagnosticsCancel,mode==0 || registry);
            if(mode==0 || registry){place(GetDlgItem(hwnd,DiagnosticsRead),w-188,56,72);place(GetDlgItem(hwnd,DiagnosticsCancel),w-108,56,92);SetWindowTextW(GetDlgItem(hwnd,DiagnosticsRead),L"读取");}
            else if(overview){place(GetDlgItem(hwnd,DiagnosticsRead),w-100,56,84);SetWindowTextW(GetDlgItem(hwnd,DiagnosticsRead),L"刷新状态");}
            if(registry){place(registryPath,168,56,w-364);place(registryLocation,16,96,200,280);place(registryView,228,96,120,160);place(GetDlgItem(hwnd,RegistryUp),360,96,80);place(diagnosticsFilter,452,96,w-468);}
            else if(tracking){place(trackerPath,168,56,w-500);place(GetDlgItem(hwnd,TrackerChoose),w-320,56,88);place(GetDlgItem(hwnd,TrackerToggle),w-224,56,96);place(GetDlgItem(hwnd,TrackerClear),w-120,56,104);place(trackerScope,16,96,140,180);place(diagnosticsFilter,168,96,w-184);}
            else if(console){place(consoleMode,168,56,144,180);place(consoleDirectory,324,56,w-476);place(GetDlgItem(hwnd,ConsoleTerminal),w-140,56,124);place(consoleInput,16,96,w-252,58);place(GetDlgItem(hwnd,ConsoleRun),w-224,96,96);place(GetDlgItem(hwnd,ConsoleClear),w-120,96,104);SetWindowTextW(GetDlgItem(hwnd,ConsoleRun),diagnosticBusy?L"停止":L"运行");}
            else if(mode==0){place(logChannel,168,56,w-280,280);place(logPeriod,16,96,148,200);place(logLevel,176,96,140,180);place(logId,328,96,96);place(diagnosticsFilter,436,96,w-452);}
            place(headline,16,console?166:140,w-32,24);y=console?202:176;
        }else{
            const auto mode=SendMessageW(perfView,CB_GETCURSEL,0,0);const bool devices=mode==1;
            place(perfView,16,56,164,180);showControl(DeviceClass,devices);showControl(DeviceFilter,devices || mode==2);
            if(devices){place(deviceClass,192,56,228,420);place(deviceFilter,432,56,w-448);}
            else if(mode==2){place(GetDlgItem(hwnd,ProcessScope),192,56,166,180);place(deviceFilter,370,56,w-386);}
            place(GetDlgItem(hwnd,FreezeView),16,96,108);if(mode==2)place(GetDlgItem(hwnd,FocusSoftware),132,96,118);
            if(mode==2)place(GetDlgItem(hwnd,GroupProcesses),266,96,112);
            place(headline,mode==2?390:140,102,w-(mode==2?406:156),24);y=138;
        }
        ShowWindow(headline,page==1 && TabCtrl_GetCurSel(diskMode)==1?SW_HIDE:SW_SHOW);
        const bool showActivity=page==1 || page==2;
        ShowWindow(activity,showActivity?SW_SHOW:SW_HIDE);
        if(showActivity){const int chartHeight=page==2?88:112;place(activity,16,y,w-32,chartHeight);y+=chartHeight+12;}
        place(GetDlgItem(hwnd,SortCards),16,y,228,300);place(GetDlgItem(hwnd,SortRefresh),252,y,30);place(GetDlgItem(hwnd,SortLive),294,y,100);y+=40;
        int listWidth=w-32,listBottom=h-42,detailX=16,detailY=0,detailWidth=w-32,detailHeight=0;
        if(detailOpen){
            if(w>=820){detailWidth=w>=1180?360:280;detailX=w-detailWidth-16;detailY=y;detailHeight=h-y-42;listWidth=detailX-32;}
            else{detailHeight=std::min(190,std::max(138,(h-y)/3));detailY=h-42-detailHeight;listBottom=detailY-12;}
        }
        place(list,16,y,listWidth,listBottom-y);ShowWindow(list,cardMode?SW_HIDE:SW_SHOW);
        place(board.handle(),16,y,listWidth,listBottom-y);ShowWindow(board.handle(),cardMode?SW_SHOW:SW_HIDE);
        place(emptyState,36,y+54,std::max(120,listWidth-40),28);ShowWindow(emptyState,table.rows.empty()?SW_SHOW:SW_HIDE);
        place(GetDlgItem(hwnd,EmptyAll),16+(listWidth-144)/2,y+94,144);showControl(EmptyAll,table.rows.empty() && conciseMode && !fullTable.rows.empty());
        if(table.rows.empty()){SetWindowPos(emptyState,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowPos(GetDlgItem(hwnd,EmptyAll),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
        for(const int id:{DetailText,DetailTitle,DetailClose,DetailCopy,DetailSections})showControl(id,detailOpen);
        showControl(DetailFull,false);
        if(detailOpen){
            place(detailTitle,detailX+12,detailY+6,detailWidth-94,24);
            place(GetDlgItem(hwnd,DetailCopy),detailX+detailWidth-76,detailY,30);
            place(GetDlgItem(hwnd,DetailClose),detailX+detailWidth-38,detailY,30);
            place(GetDlgItem(hwnd,DetailSections),detailX+6,detailY+34,detailWidth-12,26);
            place(detailText,detailX,detailY+66,detailWidth,detailHeight-66);
        }
        place(status,16,h-29,w-68,22);place(GetDlgItem(hwnd,Info),w-44,h-34,28,28);
        sizeColumns();
        if(detailOpen)updateDetail(false);
    }
    void sizeColumns(){
        if(visibleColumns.empty())return;RECT r{};GetClientRect(list,&r);
        const int count=static_cast<int>(visibleColumns.size()),available=std::max(1,MulDiv(r.right,96,dpi)-22);
        if(!fullColumns){
            std::vector<int> weights(static_cast<size_t>(count),15);
            if(presentation::connections(table))weights=count==4?std::vector<int>{34,26,20,20}:count==5?std::vector<int>{36,16,16,16,16}:table.columns.size()>=20?std::vector<int>{17,25,10,16,16,16}:std::vector<int>{18,30,13,12,12,15};
            else if(table.columns[0]==L"软件")weights={26,17,14,14,29};
            else if(table.columns[0]==L"进程名称")weights={24,14,12,20,15,15};
            else if(table.columns[0]==L"记录时间" && std::find(table.columns.begin(),table.columns.end(),L"完整输出")!=table.columns.end())weights={18,12,12,10,10,38};
            else if(table.columns[0]==L"记录时间" && std::find(table.columns.begin(),table.columns.end(),L"类别")!=table.columns.end())weights={18,10,10,12,40,10};
            else if(table.columns[0]==L"记录时间")weights={20,10,24,10,36};
            else if(table.columns[0]==L"键 / 值名称")weights={24,16,30,30};
            else if(table.columns[0]==L"设备类别")weights={38,24,22,16};
            else if(table.columns.size()==2)weights={27,73};
            else if(table.columns[0]==L"名称")weights={46,14,16,24};
            else if(table.columns[0]==L"变化")weights={12,54,17,17};
            else if(table.columns[0]==L"完整路径")weights={48,14,14,24};
            else if(table.columns[0]==L"时间")weights={21,12,19,32,16};
            else if(table.columns[0]==L"网络接口")weights={32,17,17,17,17};
            else if(table.columns[0]==L"进程")weights={26,10,18,18,28};
            if(weights.size()!=static_cast<size_t>(count))weights.assign(static_cast<size_t>(count),15);
            int total=0;for(int weight:weights)total+=weight;int used=0;
            for(int i=0;i<count;++i){const int width=i==count-1?available-used:available*weights[static_cast<size_t>(i)]/total;ListView_SetColumnWidth(list,i,px(width));used+=width;}
            return;
        }
        for(int i=0;i<count;++i){
            const auto& title=table.columns[static_cast<size_t>(visibleColumns[static_cast<size_t>(i)])];int width=150;
            if(count==2)width=i==0?215:std::max(450,MulDiv(r.right,96,dpi)-225);
            else if(title.find(L"路径")!=std::wstring::npos || title==L"设备实例 ID" || title==L"硬件 ID")width=340;
            else if(title==L"设备名称")width=270;
            else if(title.find(L"PTR")!=std::wstring::npos)width=210;
            else if(title.find(L"地址")!=std::wstring::npos)width=180;
            else if(title.find(L"端口")!=std::wstring::npos)width=85;
            else if(title==L"PID")width=65;
            else if(title==L"协议")width=75;
            else if(title.find(L"速率")!=std::wstring::npos)width=110;
            else if(title==L"通信范围")width=135;
            else if(title==L"进程")width=135;
            ListView_SetColumnWidth(list,i,px(width));
        }
    }
    int sortMode()const{const HWND combo=GetDlgItem(hwnd,SortCards);const auto selected=SendMessageW(combo,CB_GETCURSEL,0,0);return selected<0?0:static_cast<int>(SendMessageW(combo,CB_GETITEMDATA,selected,0));}
    void showView(Table next){
        auto selectedColumns=presentation::columns(next,fullColumns);
        if(!fullColumns && !focusedSoftware.empty() && page==2 && presentation::connections(next))selectedColumns=next.columns.size()>=20?std::vector<int>{9,19,17,18}:std::vector<int>{9,3,10,11,13};
        std::vector<std::wstring> headings;for(int source:selectedColumns)headings.push_back(presentation::heading(next,source,fullColumns));
        const bool schema=next.columns!=table.columns,columns=schema || selectedColumns!=visibleColumns || headings!=displayHeadings;
        if(schema){const auto combo=GetDlgItem(hwnd,SortCards);SendMessageW(combo,CB_RESETCONTENT,0,0);
            for(const auto& [id,label]:cards::sortOptions(next)){const auto option=SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(combo,CB_SETITEMDATA,option,id);}SendMessageW(combo,CB_SETCURSEL,0,0);}
        const bool liveSort=sortMode()>0 && SendMessageW(GetDlgItem(hwnd,SortLive),BM_GETCHECK,0,0)==BST_CHECKED && !frozen;
        if(liveSort)cards::sort(next,sortMode());
        updating=true;
        const int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
        const auto identity=presentation::identity;
        const int top=ListView_GetTopIndex(list);
        Row topIdentity;if(!schema && !reorderView && !liveSort && top>=0 && static_cast<size_t>(top)<table.rows.size())topIdentity=identity(table,table.rows[static_cast<size_t>(top)]);
        if(!schema && !reorderView && !liveSort && (page==0 || page==2)){
            std::map<Row,size_t> positions;for(size_t i=0;i<next.rows.size();++i)positions.try_emplace(identity(next,next.rows[i]),i);
            std::vector<Row> ordered;ordered.reserve(next.rows.size());std::vector<bool> used(next.rows.size(),false);
            for(const auto& row:table.rows){const auto found=positions.find(identity(table,row));if(found!=positions.end() && !used[found->second]){used[found->second]=true;ordered.push_back(std::move(next.rows[found->second]));}}
            for(size_t i=0;i<next.rows.size();++i)if(!used[i])ordered.push_back(std::move(next.rows[i]));next.rows=std::move(ordered);
        }
        reorderView=false;
        Row selectedRow;if(!schema && selected>=0 && static_cast<size_t>(selected)<table.rows.size())selectedRow=identity(table,table.rows[static_cast<size_t>(selected)]);
        if(!schema && selectedRow.empty() && detailOpen)selectedRow=retainedIdentity;
        table=std::move(next);
        // WM_SETREDRAW(TRUE) also sets WS_VISIBLE; never send it to the hidden backing list.
        const bool listVisible=IsWindowVisible(list)!=FALSE;if(listVisible)SendMessageW(list,WM_SETREDRAW,FALSE,0);
        visibleColumns=selectedColumns;displayHeadings=std::move(headings);
        if(page==0 && SendMessageW(perfView,CB_GETCURSEL,0,0)==0 && table.rows.size()>=2)SetWindowTextW(headline,(L"CPU "+table.rows[0][1]+L"    内存 "+table.rows[1][1]).c_str());
        else if(page==0 && SendMessageW(perfView,CB_GETCURSEL,0,0)==2)SetWindowTextW(headline,(L"进程与资源 · "+std::to_wstring(table.rows.size())+(groupProcesses?L" 组软件":L" 个进程")).c_str());
        else if(page==0)SetWindowTextW(headline,(hardwareBusy?L"正在枚举硬件与驱动…":L"硬件与驱动   "+std::to_wstring(table.rows.size())+L" / "+std::to_wstring(hardware.rows.size())+L" 个设备").c_str());
        if(page==2)SetWindowTextW(headline,(focusedSoftware.empty()?L"软件活动与通信证据":L"正在查看："+focusedName).c_str());
        if(page==1)SetWindowTextW(headline,table.columns.empty()?L"磁盘":table.columns[0]==L"盘符 / 分区"?L"盘符与分区布局":table.columns[0]==L"变化"?L"快照差异":L"文件与索引");
        if(page==3){const auto mode=SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0);const wchar_t* idle=mode==1?L"注册表 · 只读浏览与解读":mode==3?L"安全总览 · Windows 保护状态与待处理事项":mode==4?L"命令助手 · CMD 与 Codex 输出保留在本机":L"事件日志 · Windows 原文与本地解读";SetWindowTextW(headline,mode==2?(tracker.active()?L"安全追踪 · 正在记录程序与文件活动":L"安全追踪 · 按需启动，停止后不占用后台资源"):diagnosticBusy?(mode==4?L"命令助手 · 正在运行…":L"正在读取本机数据…"):idle);}
        if(columns){while(ListView_DeleteColumn(list,0)){}for(size_t i=0;i<displayHeadings.size();++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=displayHeadings[i].data();column.cx=px(150);ListView_InsertColumn(list,static_cast<int>(i),&column);}}
        ListView_SetItemCountEx(list,static_cast<int>(table.rows.size()),LVSICF_NOSCROLL);
        ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
        if(!selectedRow.empty())for(size_t i=0;i<table.rows.size();++i)if(identity(table,table.rows[i])==selectedRow){ListView_SetItemState(list,static_cast<int>(i),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);break;}
        if(!topIdentity.empty())for(size_t i=0;i<table.rows.size();++i)if(identity(table,table.rows[i])==topIdentity){RECT rowRect{};if(ListView_GetItemRect(list,static_cast<int>(i),&rowRect,LVIR_BOUNDS))ListView_Scroll(list,0,(static_cast<int>(i)-ListView_GetTopIndex(list))*(rowRect.bottom-rowRect.top));break;}
        if(columns)sizeColumns();if(listVisible){SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);}setStatus(table.summary);updating=false;
        SetWindowTextW(emptyState,(conciseMode && !fullTable.rows.empty()?L"简洁模式下暂无需优先显示的项目":L"暂无匹配结果"));
        ShowWindow(emptyState,table.rows.empty()?SW_SHOW:SW_HIDE);if(table.rows.empty())SetWindowPos(emptyState,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);if(detailOpen)updateDetail(false);
        ShowWindow(GetDlgItem(hwnd,EmptyAll),table.rows.empty() && conciseMode && !fullTable.rows.empty()?SW_SHOW:SW_HIDE);
        if(table.rows.empty())SetWindowPos(GetDlgItem(hwnd,EmptyAll),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        board.refresh(ListView_GetNextItem(list,-1,LVNI_SELECTED),schema);
    }
    void show(Table next){
        if(frozen){frozenLatest=std::move(next);++frozenUpdates;setStatus(fullStatus);return;}
        if(conciseMode){fullTable=std::move(next);showView(presentation::concise(fullTable,conciseExplanation));}
        else{fullTable={};conciseExplanation.clear();showView(std::move(next));}
    }
    void resumeView(){frozen=false;frozenUpdates=0;SetWindowTextW(GetDlgItem(hwnd,FreezeView),L"定格阅读");frozenLatest={};}
    const Table& exportTable()const{return conciseMode && !fullTable.columns.empty()?fullTable:table;}
    void setConcise(bool enabled){
        if(enabled==conciseMode)return;
        if(enabled){fullTable=table;conciseMode=true;showView(presentation::concise(fullTable,conciseExplanation));}
        else{conciseMode=false;conciseExplanation.clear();Table restored=std::move(fullTable);fullTable={};showView(std::move(restored));}
    }
    static std::wstring rateText(uint64_t value){return bytes(value)+L"/s";}
    void activitySample(const std::wstring& key,const std::wstring& title,const std::wstring& first,const std::wstring& second,bool available,uint64_t primary,uint64_t secondary,const std::wstring& message,const std::wstring& primaryValue=L"",const std::wstring& secondaryValue=L""){
        if(key!=activityKey){activityKey=key;activityPoints.clear();}
        activityTitle=title;activityPrimary=first;activitySecondary=second;activityAvailable=available;activityMessage=message+L" · 最近 60 秒";
        activityPrimaryValue=primaryValue.empty()?rateText(primary):primaryValue;activitySecondaryValue=secondaryValue.empty()?rateText(secondary):secondaryValue;
        {
            const ULONGLONG now=GetTickCount64()/1000;ActivityPoint point{now,static_cast<double>(primary),static_cast<double>(secondary),available};
            if(!activityPoints.empty() && activityPoints.back().time==now)activityPoints.back()=point;else activityPoints.push_back(point);
            while(activityPoints.size()>60 || (!activityPoints.empty() && now-activityPoints.front().time>=60))activityPoints.pop_front();
        }
        const std::wstring accessible=activityTitle+L"；"+activityPrimary+L" "+activityPrimaryValue+L"；"+activitySecondary+L" "+activitySecondaryValue+L"；"+activityMessage+L"；样本 "+std::to_wstring(activityPoints.size())+L" / 60";SetWindowTextW(activity,accessible.c_str());
        InvalidateRect(activity,nullptr,FALSE);
    }
    std::wstring selectedDiskTarget()const{
        const int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
        if(selected>=0 && static_cast<size_t>(selected)<table.rows.size() && !table.columns.empty() && table.columns[0]==L"盘符 / 分区" && !table.rows[static_cast<size_t>(selected)].empty())return table.rows[static_cast<size_t>(selected)][0];
        return folder;
    }
    void updateDiskActivity(){
        if(busy && workScan){
            const ULONGLONG now=GetTickCount64();const uint64_t files=progress.files.load(),amount=progress.bytes.load();
            if(!scanTime){scanTime=now;scanFiles=files;scanBytes=amount;activitySample(L"scan",L"索引活动 · "+text(path),L"发现项目",L"文件标称大小",false,0,0,L"正在建立采样基准",L"--",L"--");return;}
            const uint64_t elapsed=now-scanTime;const uint64_t fileRate=elapsed && files>=scanFiles?(files-scanFiles)*1000/elapsed:0;
            const uint64_t byteRate=elapsed && amount>=scanBytes?(amount-scanBytes)*1000/elapsed:0;scanTime=now;scanFiles=files;scanBytes=amount;
            activitySample(L"scan",L"索引活动 · "+text(path),L"发现项目",L"文件标称大小",elapsed>0,fileRate,byteRate,L"只统计目录项与文件标称大小，不读取文件内容，也不等同于磁盘实际吞吐",std::to_wstring(fileRate)+L" 项/s",rateText(byteRate));return;
        }
        if(scanChartHold && activityKey==L"scan"){activityAvailable=false;activityMessage=L"索引任务已结束；图中保留本次扫描的最后 60 个采样点";InvalidateRect(activity,nullptr,FALSE);return;}
        const auto target=selectedDiskTarget();const auto reading=diskActivity.sample(target);
        activitySample(L"disk:"+reading.target+L":"+target,L"磁盘活动 · "+(reading.target.empty()?target:reading.target),L"读取",L"写入",reading.available,reading.primary,reading.secondary,reading.status,reading.available?rateText(reading.primary):L"--",reading.available?rateText(reading.secondary):L"--");
    }
    void updateNetworkActivity(){
        if(frozen)return;
        const int mode=static_cast<int>(SendMessageW(netView,CB_GETCURSEL,0,0));
        if(pausedNetwork){activitySample(activityKey.empty()?L"network":activityKey,activityTitle,L"接收",L"发送",false,0,0,L"采样已暂停",activityPrimaryValue,activitySecondaryValue);return;}
        const int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
        if((!focusedSoftware.empty() && mode!=6) || mode==4){
            const Row* application=nullptr;
            if(!focusedSoftware.empty()){for(const auto& row:softwareTable.rows)if(row[14]==focusedSoftware){application=&row;break;}}
            else if(selected>=0 && static_cast<size_t>(selected)<table.rows.size() && table.columns[0]==L"软件")application=&table.rows[static_cast<size_t>(selected)];
            if(application){const auto& row=*application;uint64_t rx=0,tx=0;const bool valid=presentation::byteRate(row[2],rx) && presentation::byteRate(row[3],tx);
                activitySample(L"software:"+row[14],L"软件活动 · "+row[0],L"下载",L"上传",valid,rx,tx,L"TCP 测速覆盖："+row[7],valid?rateText(rx):L"不可用",valid?rateText(tx):L"不可用");
                if(const auto* samples=insights.history(row[14])){activityPoints.clear();for(const auto& sample:*samples)activityPoints.push_back({sample.time,static_cast<double>(sample.rx),static_cast<double>(sample.tx),sample.valid});InvalidateRect(activity,nullptr,FALSE);}return;}
            if(!focusedSoftware.empty()){activitySample(L"software:"+focusedSoftware,L"软件活动 · "+focusedName,L"下载",L"上传",false,0,0,L"本轮无可读取端点；不会用全机流量替代此软件流量");return;}
        }
        if(mode==5){activitySample(L"network:history",L"历史端点 · 非实时流量",L"下载",L"上传",false,0,0,L"历史行保留地址与最后可见时间；选择软件可查看它的实时 TCP 曲线");return;}
        if(mode!=1 && mode!=4 && selected>=0 && static_cast<size_t>(selected)<table.rows.size()){
            const auto& row=table.rows[static_cast<size_t>(selected)];const auto received=presentation::column(table,mode==2?L"接收速率":mode==3?L"TCP 接收速率":L"接收速率");
            const auto sent=presentation::column(table,mode==2?L"发送速率":mode==3?L"TCP 发送速率":L"发送速率");uint64_t rx=0,tx=0;
            const bool valid=received<row.size() && sent<row.size() && presentation::byteRate(row[received],rx) && presentation::byteRate(row[sent],tx);
            std::wstring identity=row.empty()?L"选择项":row[0],key=L"network:"+std::to_wstring(mode)+L":"+identity;
            if(mode==0 && row.size()>9){identity+=L" → "+(row[9].find(L"反查") == std::wstring::npos?row[9]:row[7]);key+=L":"+row[1]+L":"+row[4]+L":"+row[5]+L":"+row[7]+L":"+row[8];}
            else if(mode==3 && row.size()>1)key+=L":"+row[1];
            const std::wstring limit=mode==6?L"TCP / UDP 事件批次字节均值，交付可能延迟；累计字节见详情":mode==2?L"来自该网卡的累计字节差分":mode==3?L"仅统计已获准测速的已建立 TCP；不含 UDP/QUIC":L"仅统计该 TCP 连接；需在采集设置开启逐连接测速";
            activitySample(key,(mode==2?L"网卡活动 · ":mode==3?L"进程 TCP 活动 · ":L"连接活动 · ")+identity,L"接收",L"发送",valid,rx,tx,valid?limit:L"该选择项当前没有可用速率；"+limit,valid?rateText(rx):L"--",valid?rateText(tx):L"--");return;
        }
        activitySample(L"network:total",L"网络活动 · 已启用网卡合计",L"接收",L"发送",network.rateAvailable,network.receiveRate,network.sendRate,network.rateAvailable?L"来自 Windows 网卡累计字节差分；虚拟网卡可能造成重复统计":network.rates,network.rateAvailable?rateText(network.receiveRate):L"--",network.rateAvailable?rateText(network.sendRate):L"--");
    }
    void drawActivity(const DRAWITEMSTRUCT& item){
        const HDC target=item.hDC;const RECT bounds=item.rcItem;const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,std::max(width,1),std::max(height,1));const auto oldBitmap=SelectObject(dc,bitmap);
        RECT local{0,0,width,height};HBRUSH background=CreateSolidBrush(RGB(248,250,251));FillRect(dc,&local,background);DeleteObject(background);
        HPEN border=CreatePen(PS_SOLID,1,RGB(218,224,228));const auto oldPen=SelectObject(dc,border);const auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));Rectangle(dc,0,0,width,height);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(border);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(34,43,50));SelectObject(dc,headingFont);RECT title{px(12),px(7),width-px(300),px(30)};DrawTextW(dc,activityTitle.c_str(),-1,&title,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_VCENTER);
        SelectObject(dc,font);RECT legend{width-px(292),px(7),width-px(12),px(30)};
        const std::wstring values=activityPrimary+L" "+activityPrimaryValue+L"    "+activitySecondary+L" "+activitySecondaryValue;DrawTextW(dc,values.c_str(),-1,&legend,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_VCENTER);
        RECT graph{px(12),px(34),width-px(12),height-px(22)};HPEN grid=CreatePen(PS_SOLID,1,RGB(229,233,236));const auto previousPen=SelectObject(dc,grid);
        for(int n=0;n<=4;++n){const int x=graph.left+(graph.right-graph.left)*n/4;MoveToEx(dc,x,graph.top,nullptr);LineTo(dc,x,graph.bottom);}
        for(int n=0;n<=2;++n){const int y=graph.top+(graph.bottom-graph.top)*n/2;MoveToEx(dc,graph.left,y,nullptr);LineTo(dc,graph.right,y);}SelectObject(dc,previousPen);DeleteObject(grid);
        if(activityPoints.size()>1){
            double maximum=1;for(const auto& point:activityPoints)if(point.valid)maximum=std::max({maximum,point.primary,point.secondary});
            const auto line=[&](bool primary,COLORREF color){HPEN pen=CreatePen(PS_SOLID,px(2),color);const auto prior=SelectObject(dc,pen);bool started=false;const size_t count=activityPoints.size();
                for(size_t i=0;i<count;++i){const auto& point=activityPoints[i];if(!point.valid){started=false;continue;}if(i>0 && point.time-activityPoints[i-1].time>2)started=false;
                    const int x=graph.right-static_cast<int>((activityPoints.back().time-point.time)*(graph.right-graph.left)/59);const double value=primary?point.primary:point.secondary;
                    const int y=graph.bottom-static_cast<int>(value*(graph.bottom-graph.top-1)/maximum);if(started)LineTo(dc,x,y);else{MoveToEx(dc,x,y,nullptr);started=true;}}
                SelectObject(dc,prior);DeleteObject(pen);};
            line(true,RGB(25,117,93));line(false,RGB(38,99,158));
        }
        SelectObject(dc,font);SetTextColor(dc,activityAvailable?RGB(94,104,112):RGB(148,103,22));RECT note{px(12),height-px(20),width-px(12),height-px(3)};DrawTextW(dc,activityMessage.c_str(),-1,&note,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_VCENTER);
        BitBlt(target,bounds.left,bounds.top,width,height,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);
    }
    void updateDetail(bool reveal){
        const int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
        if(selected<0 || static_cast<size_t>(selected)>=table.rows.size()){
            if(!retainedDetails.empty()){
                SetWindowTextW(detailTitle,(retainedTitle+L" · 保留阅读").c_str());const auto value=L"该项已不在当前列表。下方保留最后一次选中的信息；网络历史仍可继续查询。\r\n\r\n"+retainedSections[static_cast<size_t>(std::clamp(detailSection,0,2))];
                if(value!=detailValue){detailValue=value;SetWindowTextW(detailText,value.c_str());}return;
            }
            SetWindowTextW(detailTitle,L"详细信息");if(detailValue!=L"未选择项目"){detailValue=L"未选择项目";SetWindowTextW(detailText,detailValue.c_str());}return;
        }
        const auto& row=table.rows[static_cast<size_t>(selected)];
        const auto title=table.columns[0]==L"设备类别"?row[1]:std::find(table.columns.begin(),table.columns.end(),L"完整输出")!=table.columns.end() && row.size()>5?row[5]:row[0];SetWindowTextW(detailTitle,title.c_str());
        RECT client{};GetClientRect(hwnd,&client);
        const auto value=cards::explain(table,row,detailSection);
        retainedTitle=title;retainedDetails=value;retainedFullDetails=presentation::details(table,row,true);retainedIdentity=presentation::identity(table,row);
        retainedSections={cards::explain(table,row,0),cards::explain(table,row,1),retainedFullDetails};
        if(value!=detailValue){
            const auto scroll=SendMessageW(detailText,EM_GETFIRSTVISIBLELINE,0,0);DWORD start=0,end=0;SendMessageW(detailText,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
            detailValue=value;SetWindowTextW(detailText,detailValue.c_str());
            if(!reveal){SendMessageW(detailText,EM_SETSEL,start,end);SendMessageW(detailText,EM_LINESCROLL,0,scroll);}
        }
        if(reveal && !detailOpen){detailOpen=true;layout();ListView_EnsureVisible(list,selected,FALSE);}
    }
    void reloadSnapshots(){
        snapshots=index->snapshots();SendMessageW(before,CB_RESETCONTENT,0,0);SendMessageW(after,CB_RESETCONTENT,0,0);
        for(const auto& s:snapshots){
            const auto state=s.status==L"complete"?L"完成":s.status==L"paused"?L"已暂停":L"可续扫";
            const std::wstring value=L"#"+std::to_wstring(s.id)+L" "+s.created+L" ["+state+L"] "+s.root+L"  "+std::to_wstring(s.files)+L" 项 / "+std::to_wstring(s.errors)+L" 未覆盖";
            SendMessageW(before,CB_ADDSTRING,0,reinterpret_cast<LPARAM>((L"A · "+value).c_str()));SendMessageW(after,CB_ADDSTRING,0,reinterpret_cast<LPARAM>((L"B · "+value).c_str()));
        }
        SendMessageW(before,CB_SETCURSEL,snapshots.size()>1?1:0,0);SendMessageW(after,CB_SETCURSEL,0,0);
    }
    int64_t selectedSnapshot(HWND control){const auto selected=SendMessageW(control,CB_GETCURSEL,0,0);if(selected<0 || static_cast<size_t>(selected)>=snapshots.size())throw std::runtime_error("Create or select a snapshot first");return snapshots[static_cast<size_t>(selected)].id;}
    void launch(std::function<Table()> work,bool scan=false,bool reload=false){
        if(busy){setStatus(L"已有任务运行；可暂停扫描后再切换磁盘任务。");return;}
        if(worker.joinable())worker.join();cancel=false;busy=true;workScan=scan;workReload=scan||reload;workError.clear();
        if(scan){progress.files=0;progress.bytes=0;progress.errors=0;scanFiles=0;scanBytes=0;scanTime=0;scanChartHold=false;diskActivity.reset();activityKey.clear();activitySample(L"scan",L"索引活动 · "+text(path),L"发现项目",L"文件标称大小",false,0,0,L"正在建立采样基准",L"--",L"--");}
        else{scanChartHold=false;diskActivity.reset();activityKey.clear();}
        EnableWindow(GetDlgItem(hwnd,Scan),FALSE);EnableWindow(GetDlgItem(hwnd,Resume),FALSE);
        setStatus(scan?L"正在建立元数据快照，可暂停后继续。":L"正在读取本地数据…");
        worker=std::thread([this,work=std::move(work)]{
            SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);
            Table result;std::wstring error;
            try{result=work();}catch(const std::exception& e){const std::string what=e.what();error.assign(what.begin(),what.end());}
            {std::lock_guard lock(mutex);completed=std::move(result);workError=std::move(error);}
            PostMessageW(hwnd,kDone,0,0);
        });
    }
    void select(int value){
        resumeView();retainedDetails.clear();retainedIdentity.clear();
        if(page==2 && value!=2)resetNetwork();
        if(page==1 && value!=1)diskActivity.reset();
        detailOpen=false;
        if(page==3 && value!=3){diagnosticCancel=true;diagnosticPending=false;tracker.stop();}
        page=std::clamp(value,0,3);TabCtrl_SetCurSel(tabs,page);
        for(HWND item:diskControls)ShowWindow(item,page==1?SW_SHOW:SW_HIDE);
        for(HWND item:networkControls)ShowWindow(item,page==2?SW_SHOW:SW_HIDE);
        for(HWND item:performanceControls)ShowWindow(item,page==0?SW_SHOW:SW_HIDE);
        layout();
        if(page==0)refreshPerformance();
        activityKey.clear();activityPoints.clear();
        if(page==1){reloadSnapshots();if(!busy)launch([]{return volumes();});updateDiskActivity();}
        if(page==2){resetNetwork();refreshNetwork();}
        if(page==3)loadDiagnostics();
    }
    void refreshNetwork(){
        if(!pausedNetwork && !suspended)startMonitor(2);
        showNetwork();
    }
    void resetNetwork(){networkReset=true;++monitorGeneration;observations.interrupt();insights.interrupt();if(!networkTable.columns.empty())networkHistory=observations.history(networkTable);network.rateAvailable=false;if(!monitorBusy)startMonitor(-1);}
    void startMonitor(int target){
        if(monitorBusy || (target!=-1 && (suspended || !IsWindowVisible(hwnd) || IsIconic(hwnd))))return;
        if(monitorWorker.joinable())monitorWorker.join();monitorBusy=true;monitorTarget=target;
        const bool reset=networkReset,dns=network.resolveDns,measure=network.measureConnections,clear=clearNetworkLog,capture=traceCapture;const auto generation=monitorGeneration;
        if(target==2){networkReset=false;clearNetworkLog=false;}
        monitorWorker=std::thread([this,target,reset,dns,measure,clear,capture,generation]{
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);MonitorResult result;
            try{if(target==-1){networkSampler.resetBaseline();networkTrace.stop();}else if(target==2){if(reset){networkSampler.resetBaseline();networkTrace.stop();}if(clear)networkSampler.clearLog();networkSampler.resolveDns=dns;networkSampler.measureConnections=measure;
                if(capture)networkTrace.start();else networkTrace.stop();result.trace=networkTrace.snapshot();if(!capture)result.trace.summary=L"事件捕获已关闭；可在网络采集设置开启。";
                result.connections=networkSampler.sample();result.adapters=networkSampler.adapters;result.processes=networkSampler.processes;result.log=networkSampler.log();
                result.rates=networkSampler.rates;result.rx=networkSampler.receiveRate;result.tx=networkSampler.sendRate;result.available=networkSampler.rateAvailable;result.complete=networkSampler.sampleComplete;
            }else result.performance=processPerformance.sample();}catch(const std::exception& e){const std::string message=e.what();result.error.assign(message.begin(),message.end());}
            {std::lock_guard lock(mutex);monitorResult=std::move(result);completedGeneration=generation;}PostMessageW(hwnd,kMonitorDone,0,0);
        });
    }
    void finishMonitor(){
        if(monitorWorker.joinable())monitorWorker.join();monitorBusy=false;
        if(completedGeneration!=monitorGeneration){if(networkReset)startMonitor(-1);return;}
        if(monitorTarget==-1){if(page==2 && !pausedNetwork)startMonitor(2);else if(page==0 && SendMessageW(perfView,CB_GETCURSEL,0,0)==2)startMonitor(0);return;}
        if(!monitorResult.error.empty()){setStatus(L"采样未完成："+monitorResult.error);return;}
        if(monitorTarget==2){traceTable=std::move(monitorResult.trace);networkTable=std::move(monitorResult.connections);network.adapters=std::move(monitorResult.adapters);network.processes=std::move(monitorResult.processes);networkLog=std::move(monitorResult.log);
            network.rates=monitorResult.rates;network.receiveRate=monitorResult.rx;network.sendRate=monitorResult.tx;network.rateAvailable=monitorResult.available;network.sampleComplete=monitorResult.complete;
            FILETIME time{};GetSystemTimeAsFileTime(&time);observations.observe(networkTable,network.sampleComplete,timestamp((static_cast<uint64_t>(time.dwHighDateTime)<<32)|time.dwLowDateTime));
            networkHistory=observations.history(networkTable);softwareTable=observations.applications(networkTable,network.sampleComplete);
            insights.annotate(softwareTable,GetTickCount64(),network.sampleComplete);
            if(page==2)showNetwork();
        }else{FILETIME time{};GetSystemTimeAsFileTime(&time);processTable=processObservations.observe(monitorResult.performance,GetTickCount64(),timestamp((static_cast<uint64_t>(time.dwHighDateTime)<<32)|time.dwLowDateTime));if(page==0 && SendMessageW(perfView,CB_GETCURSEL,0,0)==2)showProcesses();}
    }
    void showProcesses(){auto result=processTable;const auto scope=SendMessageW(GetDlgItem(hwnd,ProcessScope),CB_GETCURSEL,0,0);
        if(scope>0)std::erase_if(result.rows,[&](const Row& row){return ProcessObservations::recent(row)!=(scope==2);});
        if(groupProcesses)result=cards::groupProcesses(result);filterRows(result,text(deviceFilter));show(std::move(result));}
    static void filterRows(Table& result,std::wstring query){
        std::transform(query.begin(),query.end(),query.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
        if(!query.empty())std::erase_if(result.rows,[&](const Row& row){for(auto cell:row){std::transform(cell.begin(),cell.end(),cell.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});if(cell.find(query)!=std::wstring::npos)return false;}return true;});
    }
    void showTracker(){
        trackerGeneration=tracker.generation();auto result=tracker.table();const int scope=static_cast<int>(SendMessageW(trackerScope,CB_GETCURSEL,0,0));
        if(scope==1)std::erase_if(result.rows,[](const Row& row){return row.size()<3 || row[1]==L"常规";});
        else if(scope==2)std::erase_if(result.rows,[](const Row& row){return row.size()<3 || row[2]!=L"程序";});
        else if(scope==3)std::erase_if(result.rows,[](const Row& row){return row.size()<3 || row[2]!=L"文件";});
        filterRows(result,text(diagnosticsFilter));SetWindowTextW(GetDlgItem(hwnd,TrackerToggle),tracker.active()?L"停止追踪":L"开始追踪");show(std::move(result));
    }
    void showConsole(){commandHistory.summary=diagnosticBusy?L"命令正在当前用户权限下运行；可点击停止。":L"命令在当前用户权限下运行。Codex 只读模式不能修改文件，Codex 工作区模式仅允许修改所选工作目录；完整交互请使用“在终端继续”。";show(commandHistory);layout();}
    void launchConsole(){
        if(diagnosticBusy){diagnosticCancel=true;setStatus(L"正在停止命令…");return;}
        ConsoleRequest request{text(consoleInput),text(consoleDirectory),static_cast<int>(SendMessageW(consoleMode,CB_GETCURSEL,0,0))};
        if(request.input.empty()){setStatus(L"请输入 CMD 命令或 Codex 提示。");SetFocus(consoleInput);return;}
        if(diagnosticWorker.joinable())diagnosticWorker.join();diagnosticCancel=false;diagnosticBusy=true;diagnosticPending=false;diagnosticTarget=4;showConsole();
        diagnosticWorker=std::thread([this,request]{SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);Table result=runConsoleCommand(request,diagnosticCancel);{std::lock_guard lock(mutex);diagnosticResult=std::move(result);}PostMessageW(hwnd,kDiagnosticsDone,0,0);});
    }
    void showNetwork(){
        auto result=observations.history(networkTable,true);
        const auto mode=SendMessageW(netView,CB_GETCURSEL,0,0);
        if(mode==1){result=networkLog;result.summary=network.rates+L"\r\n"+result.summary;}
        if(mode==2)result=network.adapters;
        if(mode==3)result=network.processes;
        if(mode==4)result=softwareTable;
        if(mode==5)result=networkHistory;
        if(mode==6)result=traceTable;
        EnableWindow(netScope,mode<2 || mode==5 || mode==6);
        const auto scope=text(netScope);
        if((mode<2 || mode==5 || mode==6) && SendMessageW(netScope,CB_GETCURSEL,0,0)>0){
            const auto column=std::find(result.columns.begin(),result.columns.end(),L"通信范围");
            if(column!=result.columns.end()){const auto columnIndex=static_cast<size_t>(column-result.columns.begin());std::erase_if(result.rows,[&](const Row& row){return row[columnIndex]!=scope;});}
        }
        if(!focusedSoftware.empty() && mode!=2){const auto pathColumn=presentation::column(result,L"程序路径"),pidColumn=presentation::column(result,L"PID"),softwareColumn=presentation::column(result,L"软件标识");
            std::erase_if(result.rows,[&](const Row& row){if(softwareColumn<row.size())return row[softwareColumn]!=focusedSoftware;return pathColumn>=row.size() || softwareKey(row[pathColumn],pidColumn<row.size()?row[pidColumn]:L"")!=focusedSoftware;});}
        filterRows(result,text(netFilter));
        if(mode==4){const auto choice=static_cast<cards::Filter>(std::clamp(static_cast<int>(SendMessageW(GetDlgItem(hwnd,QuickFilter),CB_GETCURSEL,0,0)),0,7));std::erase_if(result.rows,[&](const Row& row){return !cards::matches(row,choice);});}
        if(pausedNetwork)result.summary=L"采样已暂停。 "+result.summary;
        show(std::move(result));
        if(!frozen){
            std::wstring summary=focusedSoftware.empty()?L"先选一个软件，再点“只看此软件”。连接结束后可在“连接历史”继续查看；“定格阅读”不停止后台采样。":L"已固定软件范围："+focusedName+L"。切换连接、历史和变化日志仍只显示该软件；同一路径的新进程自动纳入。";
            if(focusedSoftware.empty() && mode==4){size_t active=0,publicApps=0;for(const auto& app:softwareTable.rows){if(app[5]!=L"0")++active;if(app[4].find(L"公网 0 ")!=0)++publicApps;}
                size_t uploading=0,unmeasured=0,recorded=0;for(const auto& app:softwareTable.rows){if(cards::matches(app,cards::Filter::Uploading))++uploading;if(cards::matches(app,cards::Filter::Unknown))++unmeasured;if(cards::matches(app,cards::Filter::Recorded))++recorded;}
                summary=std::to_wstring(active)+L" 个活动软件     "+std::to_wstring(publicApps)+L" 个访问公网     "+std::to_wstring(uploading)+L" 个观测到上传     "+std::to_wstring(unmeasured)+L" 个测速不完整\r\n"+
                    (recorded?L"已保留 "+std::to_wstring(recorded)+L" 个软件的持续上传提示。":L"尚无达到持续上传规则的记录。")+L" 公网连接和上传活动本身不代表泄露；加密内容不可见。";}
            if(!focusedSoftware.empty())for(const auto& app:softwareTable.rows)if(app[14]==focusedSoftware){summary=app[4]+L" · 当前 "+app[5]+L" 个端点 · TCP 下载 "+app[2]+L" / 上传 "+app[3]+L"\r\n测速覆盖："+app[7]+L"。切换视图仍保留此软件范围。";break;}
            if(!network.sampleComplete)summary=L"本轮数据尚未完整读取，先保留已有记录。\r\n"+summary;
            if(mode==6)summary=traceTable.summary;
            else if(!traceTable.summary.empty()){const auto firstLine=summary.find(L"\r\n");if(firstLine!=std::wstring::npos)summary.resize(firstLine);summary+=L"\r\n"+(traceTable.summary.find(L"事件捕获已启用")==0?L"短连接与 UDP 已同时捕获，可切换“实时捕获”查看。":traceTable.summary.substr(0,traceTable.summary.find(L"。")+1));}
            if(pausedNetwork)summary=L"已暂停采样，恢复前不会采集新变化。\r\n"+summary;
            SetWindowTextW(GetDlgItem(hwnd,AnalysisText),summary.c_str());
        }
        updateNetworkActivity();
    }
    void refreshPerformance(bool reload=false){
        const auto mode=SendMessageW(perfView,CB_GETCURSEL,0,0);const bool devices=mode==1;
        EnableWindow(deviceClass,devices);EnableWindow(deviceFilter,devices || mode==2);
        SendMessageW(deviceFilter,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(mode==2?L"搜索进程名称、PID 或程序路径":L"设备名称、驱动版本或硬件 ID"));
        layout();
        if(mode==2){showProcesses();startMonitor(0);return;}
        if(!devices){show(performance.sample());return;}
        if((reload || !hardwareLoaded) && !hardwareBusy){
            if(hardwareWorker.joinable())hardwareWorker.join();hardwareBusy=true;
            hardwareWorker=std::thread([this]{
                SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);Table result;
                try{result=hardwareDevices();}catch(const std::exception& e){const std::string error=e.what();result.summary.assign(error.begin(),error.end());}
                {std::lock_guard lock(mutex);hardwareResult=std::move(result);}PostMessageW(hwnd,kHardwareDone,0,0);
            });
        }
        auto result=hardware;const auto category=text(deviceClass);
        if(SendMessageW(deviceClass,CB_GETCURSEL,0,0)>0)std::erase_if(result.rows,[&](const Row& row){return row[0]!=category;});
        filterRows(result,text(deviceFilter));show(std::move(result));
    }
    void tick(){
        if(suspended || !IsWindowVisible(hwnd) || IsIconic(hwnd))return;
        if(page==0){const auto mode=SendMessageW(perfView,CB_GETCURSEL,0,0);if(mode==0)show(performance.sample());else if(mode==2)startMonitor(0);}else if(page==2){if(!pausedNetwork)startMonitor(2);}
        if(page==1){updateDiskActivity();if(busy && workScan)setStatus(L"扫描中  "+std::to_wstring(progress.files.load())+L" 项   "+bytes(progress.bytes.load())+L"   未覆盖 "+std::to_wstring(progress.errors.load())+L" 项；仅收集元数据，不读取文件内容。");}
        if(page==3 && SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0)==2 && trackerGeneration!=tracker.generation())showTracker();
    }
    void loadDiagnostics(){
        const int mode=static_cast<int>(SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0));
        if(mode==2){showTracker();return;}
        if(mode==4){showConsole();return;}
        if(diagnosticBusy){diagnosticCancel=true;diagnosticPending=true;show({});return;}
        LogQuery query;query.channel=text(logChannel);query.filter=text(diagnosticsFilter);
        constexpr int hours[]={1,24,168,720};query.hours=hours[std::clamp(static_cast<int>(SendMessageW(logPeriod,CB_GETCURSEL,0,0)),0,3)];query.level=static_cast<int>(SendMessageW(logLevel,CB_GETCURSEL,0,0));
        const auto id=text(logId);if(mode==0 && !id.empty()){
            if(id.size()>5 || !std::all_of(id.begin(),id.end(),[](wchar_t c){return c>=L'0' && c<=L'9';}) || std::stoul(id)>65535){show({{},{},L"事件 ID 必须为 0 至 65535 的整数，留空表示不限。"});return;}
            query.eventId=std::stoi(id);
        }
        const auto pathValue=text(registryPath),filterValue=text(diagnosticsFilter);const bool view32=SendMessageW(registryView,CB_GETCURSEL,0,0)==1;
        if(diagnosticWorker.joinable())diagnosticWorker.join();diagnosticCancel=false;diagnosticBusy=true;diagnosticPending=false;diagnosticTarget=mode;
        EnableWindow(GetDlgItem(hwnd,DiagnosticsCancel),TRUE);show({});
        diagnosticWorker=std::thread([this,mode,query,pathValue,filterValue,view32]{
            SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);Table result;
            try{result=mode==0?eventLogs(query,diagnosticCancel):mode==1?registryBrowse(pathValue,view32,filterValue,&diagnosticCancel):securityOverview();}catch(const std::exception& e){const std::string error=e.what();result.summary=L"读取失败："+std::wstring(error.begin(),error.end());}
            {std::lock_guard lock(mutex);diagnosticResult=std::move(result);}PostMessageW(hwnd,kDiagnosticsDone,0,0);
        });
    }
    std::wstring chooseFolder(const wchar_t* title=L"选择本地扫描目录"){
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return L"";
        dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);dialog->SetTitle(title);
        if(FAILED(dialog->Show(hwnd)))return L"";Microsoft::WRL::ComPtr<IShellItem> item;if(FAILED(dialog->GetResult(&item)))return L"";
        PWSTR value=nullptr;if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&value)))return L"";std::wstring result=value;CoTaskMemFree(value);return result;
    }
    void browseFolder(){folder=text(path);const auto requested=folder;launch([requested]{return browse(requested);});}
    void command(int id){
        switch(id){
        case DiagnosticsMode:detailOpen=false;SetWindowTextW(diagnosticsFilter,L"");if(diagnosticBusy){diagnosticCancel=true;diagnosticPending=false;}if(SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0)!=2)tracker.stop();layout();loadDiagnostics();break;
        case DiagnosticsRead:case LogChannel:case LogPeriod:case LogLevel:case RegistryView:loadDiagnostics();break;
        case DiagnosticsCancel:diagnosticCancel=true;diagnosticPending=false;setStatus(L"正在取消读取…");break;
        case RegistryUp:{const auto parent=registryParent(text(registryPath));SetWindowTextW(registryPath,parent.c_str());loadDiagnostics();break;}
        case RegistryLocation:{
            constexpr const wchar_t* paths[]={L"",L"HKCU\\Software",L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"HKLM\\SYSTEM\\CurrentControlSet\\Services",L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",L"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"HKLM\\SOFTWARE\\Policies"};
            const int selected=static_cast<int>(SendMessageW(registryLocation,CB_GETCURSEL,0,0));if(selected>0 && selected<static_cast<int>(std::size(paths))){SetWindowTextW(registryPath,paths[selected]);loadDiagnostics();}break;}
        case TrackerChoose:{const auto chosen=chooseFolder(L"选择要安全追踪的目录");if(!chosen.empty())SetWindowTextW(trackerPath,chosen.c_str());break;}
        case TrackerToggle:{if(tracker.active())tracker.stop();else{const auto error=tracker.start(text(trackerPath));if(!error.empty())MessageBoxW(hwnd,error.c_str(),L"无法开始安全追踪",MB_OK|MB_ICONWARNING);}showTracker();layout();break;}
        case TrackerClear:tracker.clear();showTracker();break;
        case TrackerScope:showTracker();break;
        case DiagnosticsFilter:if(SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0)==2)showTracker();break;
        case ConsoleMode:SendMessageW(consoleInput,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(SendMessageW(consoleMode,CB_GETCURSEL,0,0)==0?L"输入 CMD 命令":L"输入给 Codex 的任务或问题"));break;
        case ConsoleRun:launchConsole();break;
        case ConsoleClear:if(!diagnosticBusy){commandHistory.rows.clear();showConsole();}break;
        case ConsoleTerminal:{auto directory=text(consoleDirectory);if(directory.empty())directory=std::filesystem::current_path().wstring();const std::wstring terminal=L"-d \""+directory+L"\"";if(reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",L"wt.exe",terminal.c_str(),directory.c_str(),SW_SHOWNORMAL))<=32){const std::wstring arguments=L"/k cd /d \""+directory+L"\"";ShellExecuteW(hwnd,L"open",L"cmd.exe",arguments.c_str(),directory.c_str(),SW_SHOWNORMAL);}break;}
        case ConciseMode:{const bool enabled=SendMessageW(GetDlgItem(hwnd,ConciseMode),BM_GETCHECK,0,0)==BST_CHECKED;setConcise(enabled);
            const auto preferences=(dataDirectory()/L"settings.ini").wstring();WritePrivateProfileStringW(L"System",L"concise",enabled?L"1":L"0",preferences.c_str());layout();break;}
        case ViewStyle:{const int style=std::clamp(static_cast<int>(SendMessageW(GetDlgItem(hwnd,ViewStyle),CB_GETCURSEL,0,0)),0,2);cardMode=style==0;fullColumns=style==2;SendMessageW(GetDlgItem(hwnd,FullColumns),BM_SETCHECK,fullColumns?BST_CHECKED:BST_UNCHECKED,0);showView(table);layout();WritePrivateProfileStringW(L"System",L"viewStyle",std::to_wstring(style).c_str(),(dataDirectory()/L"settings.ini").c_str());break;}
        case QuickFilter:resumeView();board.refresh(-1,true);showNetwork();break;
        case SortCards:case SortLive:case SortRefresh:{Table sorted=table;cards::sort(sorted,sortMode());reorderView=true;showView(std::move(sorted));board.refresh(ListView_GetNextItem(list,-1,LVNI_SELECTED),true);setStatus(SendMessageW(GetDlgItem(hwnd,SortLive),BM_GETCHECK,0,0)==BST_CHECKED?L"实时排序已开启；定格阅读可暂停画面更新。":L"已排序并保持位置。可点击排序旁的按钮重新排序。");break;}
        case ProcessScope:resumeView();showProcesses();break;
        case EmptyAll:SendMessageW(GetDlgItem(hwnd,ConciseMode),BM_SETCHECK,BST_UNCHECKED,0);command(ConciseMode);break;
        case GroupProcesses:groupProcesses=SendMessageW(GetDlgItem(hwnd,GroupProcesses),BM_GETCHECK,0,0)==BST_CHECKED;resumeView();showProcesses();WritePrivateProfileStringW(L"System",L"groupProcesses",groupProcesses?L"1":L"0",(dataDirectory()/L"settings.ini").c_str());break;
        case FullColumns:cardMode=false;fullColumns=SendMessageW(GetDlgItem(hwnd,FullColumns),BM_GETCHECK,0,0)==BST_CHECKED;SendMessageW(GetDlgItem(hwnd,ViewStyle),CB_SETCURSEL,fullColumns?2:1,0);showView(table);layout();break;
        case NetworkOptions:optionsOpen=!optionsOpen;layout();break;
        case DetailClose:detailOpen=false;layout();break;
        case DetailToggle:detailOpen=!detailOpen;updateDetail(false);layout();break;
        case DetailCopy:{
            if(detailValue.empty())break;
            const auto& copied=retainedDetails.empty()?detailValue:retainedFullDetails;
            const size_t size=(copied.size()+1)*sizeof(wchar_t);HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,size);
            if(!data)break;void* destination=GlobalLock(data);if(!destination){GlobalFree(data);break;}
            memcpy(destination,copied.c_str(),size);GlobalUnlock(data);
            if(OpenClipboard(hwnd)){EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,data))GlobalFree(data);CloseClipboard();}else GlobalFree(data);
            break;}
        case Info:{const std::wstring notice=fullStatus+L"\r\n\r\n可信度边界：性能与网络来自当前 Windows API 计数，系统版本与部分设备信息来自只读注册表，硬件与驱动来自 PnP 枚举。它们能说明本次读取到的状态，但不能独立证明系统文件、内核、注册表或数据源未被篡改；地址范围和事件级别也不等同于安全结论。";MessageBoxW(hwnd,notice.c_str(),L"数据来源与采集说明",MB_OK|MB_ICONINFORMATION);break;}
        case Choose:{const auto chosen=chooseFolder();if(!chosen.empty()){SetWindowTextW(path,chosen.c_str());browseFolder();}break;}
        case Browse:browseFolder();break;
        case Up:{auto parent=std::filesystem::path(text(path)).parent_path();SetWindowTextW(path,parent.c_str());browseFolder();break;}
        case Volumes:launch([]{return volumes();});break;
        case Scan:case Resume:{const auto root=text(path);const bool resume=id==Resume;
            launch([this,root,resume]{const auto snap=index->scan(root,resume,cancel,progress);auto result=index->browseSnapshot(snap);result.summary=(cancel?L"快照已暂停，可继续扫描。 ":L"快照完成。 ")+std::to_wstring(progress.files.load())+L" 项，未覆盖 "+std::to_wstring(progress.errors.load())+L" 项。 "+result.summary;return result;},true);break;}
        case Cancel:cancel=true;setStatus(L"正在保存扫描断点…");break;
        case Compare:{const auto a=selectedSnapshot(before),b=selectedSnapshot(after);const auto query=text(filter);launch([this,a,b,query]{return index->compare(a,b,query);});break;}
        case SnapshotView:{const auto idValue=selectedSnapshot(after);const auto query=text(filter);launch([this,idValue,query]{return index->browseSnapshot(idValue,query);});break;}
        case DeleteSnapshot:{if(busy){setStatus(L"请先暂停当前任务。");break;}const auto idValue=selectedSnapshot(after);
            if(MessageBoxW(hwnd,L"删除选中的 B 快照及其索引？源文件不会被删除。",L"删除快照",MB_YESNO|MB_ICONQUESTION)==IDYES)launch([this,idValue]{index->removeSnapshot(idValue);return volumes();},false,true);break;}
        case Refresh:if(page==0)refreshPerformance(true);else if(page==2)refreshNetwork();else if(page==3)loadDiagnostics();else browseFolder();break;
        case DetailFull:detailSection=SendMessageW(GetDlgItem(hwnd,DetailFull),BM_GETCHECK,0,0)==BST_CHECKED?2:0;TabCtrl_SetCurSel(GetDlgItem(hwnd,DetailSections),detailSection);updateDetail(true);break;
        case FreezeView:if(frozen){auto latest=std::move(frozenLatest);resumeView();if(!latest.columns.empty())show(std::move(latest));if(page==2)showNetwork();}else{frozen=true;frozenUpdates=0;SetWindowTextW(GetDlgItem(hwnd,FreezeView),L"恢复实时");setStatus(fullStatus);}break;
        case FocusSoftware:focusSoftware();break;
        case AllSoftware:resumeView();focusedSoftware.clear();focusedName.clear();retainedDetails.clear();detailOpen=false;activityKey.clear();activityPoints.clear();SendMessageW(netView,CB_SETCURSEL,4,0);SendMessageW(GetDlgItem(hwnd,QuickFilter),CB_SETCURSEL,0,0);showNetwork();layout();break;
        case PauseNetwork:pausedNetwork=!pausedNetwork;SetWindowTextW(GetDlgItem(hwnd,PauseNetwork),pausedNetwork?L"继续采样":L"暂停采样");resetNetwork();refreshNetwork();break;
        case ClearLog:clearNetworkLog=true;networkLog.rows.clear();refreshNetwork();break;
        case NetworkView:resumeView();retainedDetails.clear();detailOpen=false;if(focusedSoftware.empty()){activityKey.clear();activityPoints.clear();}layout();showNetwork();break;
        case NetworkScope:case NetworkFilter:resumeView();showNetwork();break;
        case PerformanceView:{
            performanceFilters[static_cast<size_t>(performanceMode)]=text(deviceFilter);
            performanceMode=std::clamp(static_cast<int>(SendMessageW(perfView,CB_GETCURSEL,0,0)),0,2);
            const bool wasReady=ready;ready=false;SetWindowTextW(deviceFilter,performanceFilters[static_cast<size_t>(performanceMode)].c_str());ready=wasReady;
            resumeView();retainedDetails.clear();retainedIdentity.clear();detailOpen=false;refreshPerformance();break;}
        case DeviceClass:case DeviceFilter:resumeView();refreshPerformance();break;
        case DnsNames:case TcpRates:case TraceCapture:{
            network.resolveDns=SendMessageW(GetDlgItem(hwnd,DnsNames),BM_GETCHECK,0,0)==BST_CHECKED;
            network.measureConnections=SendMessageW(GetDlgItem(hwnd,TcpRates),BM_GETCHECK,0,0)==BST_CHECKED;
            traceCapture=SendMessageW(GetDlgItem(hwnd,TraceCapture),BM_GETCHECK,0,0)==BST_CHECKED;
            const auto preferences=(dataDirectory()/L"settings.ini").wstring();
            WritePrivateProfileStringW(L"System",L"dns",network.resolveDns?L"1":L"0",preferences.c_str());
            WritePrivateProfileStringW(L"System",L"tcpRates",network.measureConnections?L"1":L"0",preferences.c_str());
            WritePrivateProfileStringW(L"System",L"traceCapture",traceCapture?L"1":L"0",preferences.c_str());
            resetNetwork();refreshNetwork();break;}
        case Export:{
            wchar_t file[32768]=L"PICO-report.csv";OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=hwnd;dialog.lpstrFilter=L"CSV 表格\0*.csv\0\0";dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"csv";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
            if(GetSaveFileNameW(&dialog)){const auto& source=exportTable();exportCsv(source,file);setStatus(L"已导出当前筛选范围的全部 "+std::to_wstring(source.rows.size())+L" 行："+file);}break;
        }
        }
    }
    void focusSoftware(){
        const int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(selected<0 || static_cast<size_t>(selected)>=table.rows.size()){setStatus(L"请先在列表选择一个软件、进程或连接。");return;}
        const auto row=table.rows[static_cast<size_t>(selected)];const auto pathColumn=presentation::column(table,L"程序路径"),pidColumn=presentation::column(table,L"PID"),keyColumn=presentation::column(table,L"软件标识");
        if(keyColumn<row.size())focusedSoftware=row[keyColumn];else if(pathColumn<row.size() && pidColumn<row.size())focusedSoftware=softwareKey(row[pathColumn],row[pidColumn]);else {setStatus(L"该项目不是软件或进程，请在软件概览中选择。");return;}
        const auto nameColumn=presentation::column(table,L"进程");focusedName=table.columns[0]==L"时间" && nameColumn<row.size()?row[nameColumn]:row[0];
        resumeView();retainedDetails.clear();retainedIdentity.clear();detailOpen=false;activityKey.clear();activityPoints.clear();
        const bool previousReady=ready;ready=false;SetWindowTextW(netFilter,L"");SendMessageW(netScope,CB_SETCURSEL,0,0);SendMessageW(netView,CB_SETCURSEL,5,0);ready=previousReady;
        if(page!=2)select(2);else{showNetwork();layout();}
    }
    void activateRow(){
        const int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(selected<0 || static_cast<size_t>(selected)>=table.rows.size())return;
        const auto& row=table.rows[static_cast<size_t>(selected)];
        if(page==2 && table.columns[0]==L"软件"){focusSoftware();return;}
        if(page==3 && table.columns[0]==L"键 / 值名称" && row[9]==L"子键"){
            SetWindowTextW(registryPath,row[5].c_str());SetWindowTextW(diagnosticsFilter,L"");detailOpen=false;layout();loadDiagnostics();return;
        }
        if(page==1 && !row.empty()){
            if(table.columns[0]==L"盘符 / 分区" && row[0].size()==3 && row[0][1]==L':'){SetWindowTextW(path,row[0].c_str());browseFolder();return;}
            if(table.columns[0]==L"名称" && row.size()==7 && row[1]==L"文件夹"){
                if(row[5].find(L"链接")!=std::wstring::npos){setStatus(L"不自动进入目录链接或云占位目录，避免跨卷扫描和意外读取。");return;}
                SetWindowTextW(path,row[6].c_str());browseFolder();return;
            }
        }
        updateDetail(true);SetFocus(detailText);
    }
    LRESULT message(UINT message,WPARAM wp,LPARAM lp){
        switch(message){
        case WM_CREATE:create();return 0;
        case WM_SIZE:layout();if(wp==SIZE_MINIMIZED){KillTimer(hwnd,1);resetNetwork();diskActivity.reset();diagnosticCancel=true;diagnosticPending=false;tracker.stop();}else if(!suspended)SetTimer(hwnd,1,1000,nullptr);return 0;
        case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={px(850),px(620)};return 0;}
        case WM_DPICHANGED:{dpi=HIWORD(wp);fonts();
            const auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();return 0;}
        case WM_TIMER:tick();return 0;
        case WM_DRAWITEM:{const auto* item=reinterpret_cast<const DRAWITEMSTRUCT*>(lp);if(item && item->CtlID==Activity){drawActivity(*item);return TRUE;}break;}
        case WM_CTLCOLORSTATIC:{
            const HDC dc=reinterpret_cast<HDC>(wp);const HWND child=reinterpret_cast<HWND>(lp);
            SetTextColor(dc,child==status?RGB(100,110,120):RGB(35,43,50));SetBkColor(dc,RGB(255,255,255));return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));}
        case WM_COMMAND:
            if(ready && (((LOWORD(wp)==ProcessScope || LOWORD(wp)==SortCards || LOWORD(wp)==ViewStyle || LOWORD(wp)==QuickFilter || LOWORD(wp)==NetworkView || LOWORD(wp)==NetworkScope || LOWORD(wp)==PerformanceView || LOWORD(wp)==DeviceClass || LOWORD(wp)==DiagnosticsMode || LOWORD(wp)==LogChannel || LOWORD(wp)==LogPeriod || LOWORD(wp)==LogLevel || LOWORD(wp)==RegistryView || LOWORD(wp)==RegistryLocation || LOWORD(wp)==TrackerScope || LOWORD(wp)==ConsoleMode) && HIWORD(wp)==CBN_SELCHANGE) ||
               ((LOWORD(wp)==NetworkFilter || LOWORD(wp)==DeviceFilter || LOWORD(wp)==DiagnosticsFilter) && HIWORD(wp)==EN_CHANGE) ||
               (HIWORD(wp)==BN_CLICKED && LOWORD(wp)!=Path && LOWORD(wp)!=Filter && LOWORD(wp)!=Before && LOWORD(wp)!=After && LOWORD(wp)!=NetworkFilter)))command(LOWORD(wp));return 0;
        case WM_NOTIFY:{const auto* notification=reinterpret_cast<NMHDR*>(lp);
            if(notification->idFrom==DetailSections && notification->code==TCN_SELCHANGE){detailSection=TabCtrl_GetCurSel(GetDlgItem(hwnd,DetailSections));updateDetail(false);return 0;}
            if(notification->idFrom==Rows && notification->code==LVN_COLUMNCLICK){const int clicked=reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem;if(clicked<0 || static_cast<size_t>(clicked)>=visibleColumns.size())return 0;
                const int source=visibleColumns[static_cast<size_t>(clicked)];sortDescending=sortColumn==source?!sortDescending:true;sortColumn=source;Table sorted=table;
                SendMessageW(GetDlgItem(hwnd,SortCards),CB_SETCURSEL,0,0);
                const auto number=[](const std::wstring& value){uint64_t rate=0;if(presentation::byteRate(value,rate))return static_cast<double>(rate);wchar_t* end=nullptr;const double n=wcstod(value.c_str(),&end);return end==value.c_str()?-1.0:n;};
                const auto title=table.columns[static_cast<size_t>(source)];const bool numeric=title==L"CPU" || title==L"PID" || title.find(L"内存")!=std::wstring::npos || title.find(L"/s")!=std::wstring::npos || title.find(L"速率")!=std::wstring::npos || title.find(L"（TCP）")!=std::wstring::npos || title.find(L"端点数")!=std::wstring::npos;
                std::stable_sort(sorted.rows.begin(),sorted.rows.end(),[&](const Row& a,const Row& b){if(numeric)return sortDescending?number(a[source])>number(b[source]):number(a[source])<number(b[source]);return sortDescending?_wcsicmp(a[source].c_str(),b[source].c_str())>0:_wcsicmp(a[source].c_str(),b[source].c_str())<0;});
                reorderView=true;showView(std::move(sorted));setStatus(L"已按此刻的“"+title+L"”排序；后续刷新保持行位置，再次点击可重排。");return 0;}
            if(notification->idFrom==Tabs && notification->code==TCN_SELCHANGE){select(TabCtrl_GetCurSel(tabs));return 0;}
            if(notification->idFrom==DiskMode && notification->code==TCN_SELCHANGE){layout();updateDiskActivity();return 0;}
            if(notification->idFrom==Rows && notification->code==LVN_GETDISPINFOW){auto* info=reinterpret_cast<NMLVDISPINFOW*>(lp);const auto row=static_cast<size_t>(info->item.iItem),col=static_cast<size_t>(info->item.iSubItem);
                if((info->item.mask&LVIF_TEXT) && row<table.rows.size() && col<visibleColumns.size()){
                    const auto value=presentation::cell(table,table.rows[row],visibleColumns[col],fullColumns);wcsncpy_s(info->item.pszText,static_cast<size_t>(info->item.cchTextMax),value.c_str(),_TRUNCATE);
                }return 0;}
            if(notification->idFrom==Rows && notification->code==LVN_ITEMCHANGED && !updating){const auto* change=reinterpret_cast<NMLISTVIEW*>(lp);
                if((change->uChanged&LVIF_STATE) && (change->uNewState&LVIS_SELECTED) && !(change->uOldState&LVIS_SELECTED)){
                    updateDetail(true);if(page==1){scanChartHold=false;diskActivity.reset();updateDiskActivity();}else if(page==2)updateNetworkActivity();
                }return 0;}
            if(notification->idFrom==Rows && notification->code==NM_CLICK && !updating){updateDetail(true);return 0;}
            if(notification->idFrom==Rows && notification->code==LVN_GETINFOTIPW){auto* tip=reinterpret_cast<NMLVGETINFOTIPW*>(lp);
                if(tip->iItem>=0 && static_cast<size_t>(tip->iItem)<table.rows.size()){const auto value=presentation::details(table,table.rows[static_cast<size_t>(tip->iItem)]);wcsncpy_s(tip->pszText,static_cast<size_t>(tip->cchTextMax),value.c_str(),_TRUNCATE);}return 0;}
            if(notification->idFrom==Rows && notification->code==NM_CUSTOMDRAW){auto* draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){draw->clrTextBk=draw->nmcd.dwItemSpec%2?RGB(247,249,250):RGB(255,255,255);return CDRF_NOTIFYSUBITEMDRAW;}
                if(draw->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM) && !(draw->nmcd.uItemState&CDIS_SELECTED)){
                    const auto row=static_cast<size_t>(draw->nmcd.dwItemSpec),col=static_cast<size_t>(draw->iSubItem);
                    if(row<table.rows.size() && col<visibleColumns.size()){
                        const auto& value=table.rows[row][static_cast<size_t>(visibleColumns[col])];draw->clrText=RGB(35,43,50);
                        if(!table.columns.empty() && table.columns[0]==L"盘符 / 分区" && table.rows[row].size()>6 && table.rows[row][6].find(L"空间不足")!=std::wstring::npos)draw->clrText=RGB(177,65,65);
                        else if(value==L"--" || value.find(L"未提供")!=std::wstring::npos || value.find(L"反查中")!=std::wstring::npos)draw->clrText=RGB(116,124,132);
                        else if(value==L"公网地址")draw->clrText=RGB(38,99,158);
                        else if(value==L"运行中" || value==L"本机回环" || value==L"新增" || value==L"正常" || value==L"完成")draw->clrText=RGB(25,117,93);
                        else if(value==L"警告" || value==L"关注")draw->clrText=RGB(148,103,22);
                        else if(value.find(L"问题代码")!=std::wstring::npos || value==L"删除" || value==L"错误" || value==L"严重")draw->clrText=RGB(177,65,65);
                    }
                }return CDRF_DODEFAULT;}
            if(notification->idFrom==Rows && notification->code==NM_DBLCLK){activateRow();return 0;}break;}
        case kMonitorDone:finishMonitor();return 0;
        case kDone:{if(worker.joinable())worker.join();busy=false;std::lock_guard lock(mutex);EnableWindow(GetDlgItem(hwnd,Scan),TRUE);EnableWindow(GetDlgItem(hwnd,Resume),TRUE);
            if(workScan)scanChartHold=true;
            if(workError.empty()){if(page==1)show(std::move(completed));}else if(IsWindowVisible(hwnd))MessageBoxW(hwnd,workError.c_str(),L"PICO · 操作未完成",MB_OK|MB_ICONWARNING);
            if(workReload)reloadSnapshots();return 0;}
        case kHardwareDone:{if(hardwareWorker.joinable())hardwareWorker.join();hardwareBusy=false;hardwareLoaded=true;
            {std::lock_guard lock(mutex);hardware=std::move(hardwareResult);}
            const auto selected=text(deviceClass);SendMessageW(deviceClass,CB_RESETCONTENT,0,0);SendMessageW(deviceClass,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"全部设备类别"));
            std::set<std::wstring> categories;for(const auto& row:hardware.rows)categories.insert(row[0]);
            int itemIndex=0,chosen=0;for(const auto& category:categories){++itemIndex;SendMessageW(deviceClass,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(category.c_str()));if(category==selected)chosen=itemIndex;}
            SendMessageW(deviceClass,CB_SETCURSEL,chosen,0);if(page==0)refreshPerformance();return 0;}
        case kDiagnosticsDone:{if(diagnosticWorker.joinable())diagnosticWorker.join();diagnosticBusy=false;EnableWindow(GetDlgItem(hwnd,DiagnosticsCancel),FALSE);
            Table result;{std::lock_guard lock(mutex);result=std::move(diagnosticResult);}
            const bool consoleResult=diagnosticTarget==4;
            if(consoleResult){if(!result.rows.empty()){commandHistory.rows.insert(commandHistory.rows.begin(),std::move(result.rows.front()));if(commandHistory.rows.size()>10)commandHistory.rows.resize(10);}commandHistory.summary=result.summary;}
            if(diagnosticPending && page==3 && IsWindowVisible(hwnd) && !IsIconic(hwnd) && !suspended){diagnosticPending=false;loadDiagnostics();return 0;}
            if(diagnosticCancel && !consoleResult){if(page==3 && SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0)==diagnosticTarget)show({{},{},L"已取消读取。"});return 0;}
            diagnosticCancel=false;
            if(page==3 && SendMessageW(diagnosticsMode,CB_GETCURSEL,0,0)==diagnosticTarget){if(consoleResult){showConsole();if(!commandHistory.rows.empty()){ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);updateDetail(true);}}else show(std::move(result));}return 0;}
        case WM_CLOSE:cancel=true;diagnosticCancel=true;diagnosticPending=false;ShowWindow(hwnd,SW_HIDE);resetNetwork();diskActivity.reset();tracker.stop();KillTimer(hwnd,1);return 0;
        case WM_DESTROY:cancel=true;KillTimer(hwnd,1);return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
};
std::unique_ptr<Desk> desk;
LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    Desk* self=reinterpret_cast<Desk*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<Desk*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
    try{return self->message(msg,wp,lp);}catch(const std::exception& e){const std::string value=e.what();const std::wstring text(value.begin(),value.end());MessageBoxW(hwnd,text.c_str(),L"PICO",MB_OK|MB_ICONWARNING);return 0;}
}
}
void open(int page){
    if(!desk){
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&controls);
        WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=windowProc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"PicoPet.SystemDesk";
        cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));cls.hIcon=LoadIconW(cls.hInstance,MAKEINTRESOURCEW(101));RegisterClassExW(&cls);
        desk=std::make_unique<Desk>();desk->page=page;
        const int dpi=static_cast<int>(GetDpiForSystem());POINT cursor{};GetCursorPos(&cursor);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);
        const int width=std::min(MulDiv(1120,dpi,96),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-32),height=std::min(MulDiv(760,dpi,96),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-32);
        if(!CreateWindowExW(0,cls.lpszClassName,L"PICO 系统 · 本机状态",WS_OVERLAPPEDWINDOW,monitor.rcWork.left+16,monitor.rcWork.top+16,width,height,nullptr,nullptr,cls.hInstance,desk.get())){desk.reset();throw std::runtime_error("Create system panel");}
    }else desk->select(page);
    ShowWindow(desk->hwnd,SW_RESTORE);SetForegroundWindow(desk->hwnd);if(!desk->suspended)SetTimer(desk->hwnd,1,1000,nullptr);
}
void shutdown(){
    if(!desk)return;
    // Go through WM_CLOSE so every background thread gets its cancel signal before the window dies;
    // otherwise the monitor/hardware/diagnostic threads only stop in ~Desk, after the HWND is gone.
    if(IsWindow(desk->hwnd))SendMessageW(desk->hwnd,WM_CLOSE,0,0);
    desk->cancel=true;desk->diagnosticCancel=true;desk->diagnosticPending=false;
    if(desk->worker.joinable())desk->worker.join();
    DestroyWindow(desk->hwnd);desk.reset();
}
void suspend(bool value){if(desk){desk->suspended=value;if(value){desk->cancel=true;desk->diagnosticCancel=true;desk->diagnosticPending=false;desk->resetNetwork();desk->diskActivity.reset();desk->tracker.stop();KillTimer(desk->hwnd,1);}else if(IsWindowVisible(desk->hwnd) && !IsIconic(desk->hwnd))SetTimer(desk->hwnd,1,1000,nullptr);}}
bool isWindow(HWND hwnd){return desk && desk->hwnd==hwnd;}
}
