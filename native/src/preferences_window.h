#pragma once
#include <functional>
#include <utility>
#include <commctrl.h>

namespace preferences {
struct Field {
    const wchar_t* label;
    int Settings::* number=nullptr;
    bool Settings::* flag=nullptr;
    int low=0,high=0;
    std::vector<std::pair<const wchar_t*,int>> choices;
    int unit=1;
};
class Window {
    HWND hwnd=nullptr,status=nullptr;
    HFONT font=nullptr,titleFont=nullptr;
    Settings values;
    std::function<bool(const Settings&)> apply;
    std::function<Settings()> readCurrent;
    bool loading=false;
    int dpi=96;
    std::vector<Field> fields;
    std::array<std::vector<HWND>,4> pages;
    std::vector<windowlayer::App> apps;
    int px(int value)const{return MulDiv(value,dpi,96);}
    HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){
        HWND child=CreateWindowExW(wcscmp(type,L"EDIT")==0?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,
            px(x),px(y),px(w),px(h),hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;
    }
    int get(const Field& f)const{return f.flag?values.*(f.flag):static_cast<int>(std::lround(static_cast<double>(values.*(f.number))/f.unit));}
    void set(const Field& f,int v){if(v==get(f))return;if(f.flag)values.*(f.flag)=v!=0;else values.*(f.number)=v*f.unit;}
    void showPage(){
        const int page=TabCtrl_GetCurSel(GetDlgItem(hwnd,903));
        for(size_t i=0;i<pages.size();++i)for(HWND child:pages[i])ShowWindow(child,static_cast<int>(i)==page?SW_SHOW:SW_HIDE);
    }
    void populateLayer(){
        SendMessageW(GetDlgItem(hwnd,910),CB_SETCURSEL,values.layerMode,0);
        HWND list=GetDlgItem(hwnd,911);SendMessageW(list,CB_RESETCONTENT,0,0);
        const std::wstring placeholder=values.layerPath.empty()?L"选择任务栏程序":L"已保存 · "+std::filesystem::path(values.layerPath).filename().wstring()+L"（等待窗口）";
        SendMessageW(list,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(placeholder.c_str()));int selected=0;
        for(size_t i=0;i<apps.size();++i){const auto& app=apps[i];
            const auto label=std::filesystem::path(app.path).filename().wstring()+L" · PID "+std::to_wstring(app.pid)+L" · "+app.title+L"（"+std::to_wstring(app.windows)+L" 个窗口）";
            SendMessageW(list,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(windowlayer::samePath(app.path,values.layerPath) && (!selected || app.pid==values.layerPid))selected=static_cast<int>(i)+1;
        }
        SendMessageW(list,CB_SETCURSEL,selected,0);SendMessageW(list,CB_SETDROPPEDWIDTH,px(620),0);
        SetWindowTextW(GetDlgItem(hwnd,913),values.layerPath.empty()?L"未选择目标程序":values.layerPath.c_str());
        EnableWindow(GetDlgItem(hwnd,1004),values.layerMode==0);
        EnableWindow(list,values.layerMode==1);EnableWindow(GetDlgItem(hwnd,912),values.layerMode==1);
    }
    void populate(){
        loading=true;
        for(size_t i=0;i<fields.size();++i){const auto& f=fields[i];HWND c=GetDlgItem(hwnd,1000+static_cast<int>(i));
            if(f.choices.empty())SetWindowTextW(c,std::to_wstring(get(f)).c_str());
            else {int selected=0;for(size_t j=0;j<f.choices.size();++j)if(f.choices[j].second==get(f))selected=static_cast<int>(j);SendMessageW(c,CB_SETCURSEL,selected,0);}}
        populateLayer();loading=false;
    }
    bool commit(){
        if(loading)return true;
        Settings previous=values;
        for(size_t i=0;i<fields.size();++i){const auto& f=fields[i];HWND c=GetDlgItem(hwnd,1000+static_cast<int>(i));int v=0;
            if(f.choices.empty()){
                wchar_t buffer[64]{};GetWindowTextW(c,buffer,64);wchar_t* end=nullptr;const long parsed=wcstol(buffer,&end,10);
                if(end==buffer || *end || parsed<f.low || parsed>f.high){values=previous;populate();const auto message=std::wstring(f.label)+L"：请输入 "+std::to_wstring(f.low)+L" 至 "+std::to_wstring(f.high)+L"。";SetWindowTextW(status,message.c_str());return false;}
                v=static_cast<int>(parsed);
            }else {const auto selected=SendMessageW(c,CB_GETCURSEL,0,0);if(selected<0){values=previous;populate();return false;}v=f.choices[static_cast<size_t>(selected)].second;}
            set(f,v);
        }
        values.layerMode=static_cast<int>(SendMessageW(GetDlgItem(hwnd,910),CB_GETCURSEL,0,0));
        const auto selected=SendMessageW(GetDlgItem(hwnd,911),CB_GETCURSEL,0,0);
        if(selected>0 && static_cast<size_t>(selected)<=apps.size()){
            const auto& app=apps[static_cast<size_t>(selected)-1];values.layerPath=app.path;values.layerPid=app.pid;
        }
        if(values.layerMode==1 && values.layerPath.empty()){
            values=previous;populate();
            // Keep the requested mode visible until the user chooses its required target.
            SendMessageW(GetDlgItem(hwnd,910),CB_SETCURSEL,1,0);
            SetWindowTextW(status,L"请选择任务栏中的目标程序。");EnableWindow(GetDlgItem(hwnd,1004),FALSE);
            EnableWindow(GetDlgItem(hwnd,911),TRUE);EnableWindow(GetDlgItem(hwnd,912),TRUE);return false;
        }
        const Settings requested=values;
        if(readCurrent){
            values=readCurrent();
            for(const auto& f:fields){
                if(f.flag){if(requested.*(f.flag)!=previous.*(f.flag))values.*(f.flag)=requested.*(f.flag);}
                else if(requested.*(f.number)!=previous.*(f.number))values.*(f.number)=requested.*(f.number);
            }
            if(requested.layerMode!=previous.layerMode)values.layerMode=requested.layerMode;
            if(requested.layerPath!=previous.layerPath || requested.layerPid!=previous.layerPid){values.layerPath=requested.layerPath;values.layerPid=requested.layerPid;}
        }
        const bool saved=apply(values);populate();
        SetWindowTextW(status,saved?L"已生效并自动保存。":L"当前设置已生效，但保存失败。请检查配置目录权限。后续修改会重试。");
        return saved;
    }
    void create(){
        font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        titleFont=CreateFontW(-px(17),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        auto number=[&](const wchar_t* label,int Settings::* member,int low,int high,int unit=1){fields.push_back({label,member,nullptr,low,high,{},unit});};
        auto choice=[&](const wchar_t* label,int Settings::* member,std::vector<std::pair<const wchar_t*,int>> list){fields.push_back({label,member,nullptr,0,0,std::move(list)});};
        auto toggle=[&](const wchar_t* label,bool Settings::* member){fields.push_back({label,nullptr,member,0,0,{{L"关闭",0},{L"开启",1}}});};
        number(L"窗口尺寸（逻辑像素，160–1024）",&Settings::size,160,1024);
        fields.push_back({L"渲染模式",nullptr,&Settings::hd,0,0,{{L"像素渲染",0},{L"高清渲染",1}}});
        choice(L"机身材质",&Settings::material,{{L"塑料 · 哑光",0},{L"金属 · 拉丝",1},{L"玻璃 · 有色背衬",2},{L"陶瓷纤维 · 织纹",3}});
        choice(L"默认表情",&Settings::mood,{{L"待机",0},{L"愉悦",1},{L"喜爱",2},{L"惊讶",3},{L"休眠",4},{L"闭眼",5},{L"息屏",6}});
        toggle(L"窗口置顶",&Settings::topmost);toggle(L"全屏应用期间隐藏",&Settings::autoHide);toggle(L"鼠标穿透",&Settings::clickThrough);
        number(L"水平朝向（°，−180–180）",&Settings::yaw,-180,180,1000);
        number(L"俯仰角度（°，−12–24）",&Settings::pitch,-12,24,1000);
        number(L"目标帧率（FPS，15–120）",&Settings::frameRate,15,120);
        choice(L"位移更新阈值",&Settings::pixelThreshold,{{L"1 px · 精细",1},{L"2 px · 标准",2},{L"4 px · 低频更新",4}});
        toggle(L"暂停动画",&Settings::pauseAnimation);toggle(L"惯性悬浮",&Settings::floating);
        number(L"旋转灵敏度（%，25–200）",&Settings::rotationSensitivity,25,200);
        number(L"悬浮幅度（%，0–200）",&Settings::motionAmplitude,0,200);
        number(L"投掷力度（%，0–200）",&Settings::throwGain,0,200);
        toggle(L"待机节能（降低悬浮更新频率）",&Settings::economy);
        choice(L"应用显示模式",&Settings::appMode,{{L"单应用铺满",0},{L"多窗口桌面",1}});
        number(L"应用画面上限（FPS，5–30）",&Settings::appFps,5,30);
        HWND title=control(L"STATIC",L"偏好设置",0,24,18,650,28,0);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_TAB_CLASSES};InitCommonControlsEx(&common);
        HWND tabs=control(WC_TABCONTROLW,L"",WS_TABSTOP,24,58,656,30,903);
        for(const auto* name:{L"外观与屏幕",L"窗口与层级",L"运动与性能",L"电视应用"}){TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(name);TabCtrl_InsertItem(tabs,TabCtrl_GetItemCount(tabs),&tab);}
        loading=true;
        std::array<int,4> counts{};
        for(size_t i=0;i<fields.size();++i){const auto& f=fields[i];const int page=i>=17?3:i>=9?2:i>=4 && i<=6?1:0;
            const int slot=counts[page]++,col=slot/4,row=slot%4,x=24+col*348,y=116+row*65;
            pages[page].push_back(control(L"STATIC",f.label,0,x,y,310,20,0));
            HWND c=control(f.choices.empty()?L"EDIT":L"COMBOBOX",L"",WS_TABSTOP|(f.choices.empty()?ES_AUTOHSCROLL:CBS_DROPDOWNLIST|WS_VSCROLL),x,y+21,310,f.choices.empty()?27:230,1000+static_cast<int>(i));
            pages[page].push_back(c);
            if(f.choices.empty())SendMessageW(c,EM_SETLIMITTEXT,8,0);
            else for(const auto& option:f.choices)SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(option.first));
        }
        auto layerControl=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){HWND child=control(type,text,style,x,y,w,h,id);pages[1].push_back(child);return child;};
        layerControl(L"STATIC",L"显示层级",0,372,116,310,20,0);
        HWND mode=layerControl(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,372,137,310,180,910);
        for(const auto* text:{L"常规 · 使用窗口置顶选项",L"固定在指定程序下方"})SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        layerControl(L"STATIC",L"目标程序 · 任务栏 GUI",0,372,181,310,20,0);
        layerControl(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,372,202,310,300,911);
        layerControl(L"STATIC",L"",SS_PATHELLIPSIS,372,239,310,24,913);
        layerControl(L"BUTTON",L"刷新程序列表",WS_TABSTOP|BS_PUSHBUTTON,372,278,150,28,912);
        layerControl(L"STATIC",L"目标不可用时：普通层级",0,372,321,310,22,914);
        pages[3].push_back(control(L"BUTTON",L"进入电视应用",WS_TABSTOP|BS_PUSHBUTTON,372,116,240,30,920));
        pages[3].push_back(control(L"BUTTON",L"接入已打开的窗口…",WS_TABSTOP|BS_PUSHBUTTON,372,162,240,30,921));
        pages[3].push_back(control(L"BUTTON",L"返回桌宠 · 恢复窗口",WS_TABSTOP|BS_PUSHBUTTON,372,208,240,30,922));
        pages[3].push_back(control(L"STATIC",L"屏内滚轮操作应用，Ctrl+滚轮缩放电视。\nShift+右键打开桌宠菜单。\n兼容模式：传统 Win32 应用；GPU 界面可能黑屏。\n独占全屏和部分弹窗不兼容；退出时恢复窗口。",0,24,270,650,100,923));
        status=control(L"STATIC",L"",0,24,400,650,40,900);
        control(L"BUTTON",L"关闭",BS_DEFPUSHBUTTON|WS_TABSTOP,584,450,96,28,IDCANCEL);
        pages[0].push_back(control(L"BUTTON",L"导入表情图片…",BS_PUSHBUTTON|WS_TABSTOP,24,450,180,28,901));
        pages[0].push_back(control(L"BUTTON",L"恢复内置表情",BS_PUSHBUTTON|WS_TABSTOP,218,450,160,28,902));
        apps=windowlayer::applications();loading=false;showPage();
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(h,m,w,l);
        switch(m){
        case WM_CTLCOLORSTATIC:SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);SetTextColor(reinterpret_cast<HDC>(w),RGB(35,43,48));return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_TIMER:if(w==1){KillTimer(h,1);self->commit();return 0;}break;
        case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(l)->idFrom==903 && reinterpret_cast<NMHDR*>(l)->code==TCN_SELCHANGE){self->showPage();return 0;}break;
        case WM_COMMAND:
            if(LOWORD(w)>=920 && LOWORD(w)<=922){PostMessageW(GetWindow(h,GW_OWNER),WM_COMMAND,LOWORD(w)==920?320:LOWORD(w)==921?321:323,0);return 0;}
            if(LOWORD(w)==912){const auto mode=SendMessageW(GetDlgItem(h,910),CB_GETCURSEL,0,0);self->apps=windowlayer::applications();self->loading=true;self->populateLayer();SendMessageW(GetDlgItem(h,910),CB_SETCURSEL,mode,0);EnableWindow(GetDlgItem(h,911),mode==1);EnableWindow(GetDlgItem(h,912),mode==1);self->loading=false;return 0;}
            if((LOWORD(w)==910 || LOWORD(w)==911) && HIWORD(w)==CBN_SELCHANGE && !self->loading){KillTimer(h,1);self->commit();return 0;}
            if(LOWORD(w)==901 || LOWORD(w)==902){PostMessageW(GetWindow(h,GW_OWNER),WM_COMMAND,LOWORD(w)==901?310:312,0);return 0;}
            if(LOWORD(w)==IDCANCEL){SendMessageW(h,WM_CLOSE,0,0);return 0;}
            if(LOWORD(w)>=1000 && HIWORD(w)==EN_CHANGE && !self->loading){SetTimer(h,1,500,nullptr);return 0;}
            if(LOWORD(w)>=1000 && (HIWORD(w)==CBN_SELCHANGE || HIWORD(w)==EN_KILLFOCUS)){KillTimer(h,1);self->commit();return 0;}
            break;
        case WM_CLOSE:if(!self->commit())self->populate();DestroyWindow(h);return 0;
        case WM_DESTROY:if(self->font)DeleteObject(self->font);if(self->titleFont)DeleteObject(self->titleFont);self->font=self->titleFont=nullptr;self->hwnd=nullptr;self->fields.clear();for(auto& page:self->pages)page.clear();self->apps.clear();return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }
public:
    HWND handle()const{return hwnd;}
    bool isWindow(HWND h)const{return hwnd && h==hwnd;}
    void close(){if(hwnd)DestroyWindow(hwnd);}
    void open(HWND owner,const Settings& current,std::function<bool(const Settings&)> callback,std::function<Settings()> reader){
        if(hwnd){ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);return;}
        values=current;apply=std::move(callback);readCurrent=std::move(reader);
        WNDCLASSW type{};type.lpfnWndProc=proc;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);type.lpszClassName=L"PicoPet.Preferences";RegisterClassW(&type);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
        const UINT actualDpi=GetDpiForWindow(owner);
        dpi=std::max(48,std::min(static_cast<int>(actualDpi),static_cast<int>((monitor.rcWork.bottom-monitor.rcWork.top-80)*96/492)));
        RECT rect{0,0,px(704),px(492)};
        AdjustWindowRectExForDpi(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,WS_EX_CONTROLPARENT,actualDpi);
        HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,L"PICO · 偏好设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            monitor.rcWork.left+20,monitor.rcWork.top+20,rect.right-rect.left,rect.bottom-rect.top,owner,nullptr,type.hInstance,this);
        if(!h)return;create();populate();ShowWindow(h,SW_SHOW);SetForegroundWindow(h);
    }
};
}
