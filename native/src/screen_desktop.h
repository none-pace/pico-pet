#pragma once
#include <shobjidl.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace screendesktop {
inline constexpr int Add=1000,Previous=1001,Next=1002,Terminal=1003,Settings=1004;
inline RECT tile(int slot){const int x=34+(slot%4)*188,y=86+(slot/4)*148;return {x,y,x+168,y+136};}
inline RECT dock(int action){const int x=action==Previous?268:action==Next?476:action==Terminal?34:action==Settings?112:704;return {x,438,x+62,492};}
inline bool contains(RECT r,int x,int y){return x>=r.left && x<r.right && y>=r.top && y<r.bottom;}
inline int hit(float u,float v,int page,size_t count){
    const int x=static_cast<int>(u*800),y=static_cast<int>(v*500);
    for(int i=0;i<8;++i)if(contains(tile(i),x,y)){const int index=page*8+i;return index<4+static_cast<int>(count)?index:-2;}
    for(int action:{Add,Previous,Next,Terminal,Settings})if(contains(dock(action),x,y)){
        if((action==Previous && page==0) || (action==Next && (page+1)*8>=4+static_cast<int>(count)))return -2;return action;}
    return -2;
}
class Desktop {
    struct Shortcut {std::wstring path,name;HICON icon=nullptr;};
    std::vector<Shortcut> shortcuts;
    std::wstring storage;
    HFONT label=nullptr,symbol=nullptr,title=nullptr,dockFont=nullptr;
    void clear(){for(auto& s:shortcuts)if(s.icon)DestroyIcon(s.icon);shortcuts.clear();}
    static Shortcut make(const std::wstring& path){
        Shortcut result;result.path=path;result.name=std::filesystem::path(path).filename().wstring();
        if(result.name.empty())result.name=path;
        const auto ext=std::filesystem::path(path).extension().wstring();if(_wcsicmp(ext.c_str(),L".lnk")==0 || _wcsicmp(ext.c_str(),L".exe")==0)result.name=std::filesystem::path(path).stem().wstring();
        SHFILEINFOW info{};if(SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON))result.icon=info.hIcon;
        return result;
    }
    bool save()const{
        if(storage.empty())return false;
        std::wstring section=L"count="+std::to_wstring(shortcuts.size());section.push_back(0);
        for(size_t i=0;i<shortcuts.size();++i){section+=L"item"+std::to_wstring(i)+L"="+shortcuts[i].path;section.push_back(0);}section.push_back(0);
        return WritePrivateProfileSectionW(L"Shortcuts",section.c_str(),storage.c_str())!=FALSE;
    }
    static void block(HDC dc,RECT r,COLORREF color,int radius=0){HBRUSH brush=CreateSolidBrush(color);const auto old=SelectObject(dc,brush),pen=SelectObject(dc,GetStockObject(NULL_PEN));if(radius)RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);else FillRect(dc,&r,brush);SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(brush);}
    static void text(HDC dc,HFONT font,RECT r,const std::wstring& value,COLORREF color,UINT flags=DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){const auto old=SelectObject(dc,font);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),-1,&r,flags|DT_NOPREFIX);SelectObject(dc,old);}
public:
    int page=0,selected=-1;
    bool dirty=true;
    ~Desktop(){clear();if(label)DeleteObject(label);if(symbol)DeleteObject(symbol);if(title)DeleteObject(title);if(dockFont)DeleteObject(dockFont);}
    size_t count()const{return shortcuts.size();}
    int pages()const{return (static_cast<int>(shortcuts.size())+11)/8;}
    int pick(float u,float v)const{return hit(u,v,page,shortcuts.size());}
    void load(const std::wstring& settings){
        clear();storage=(std::filesystem::path(settings).parent_path()/L"shortcuts.ini").wstring();
        const int count=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Shortcuts",L"count",0,storage.c_str())),0,32);
        for(int i=0;i<count;++i){wchar_t path[32768]{};GetPrivateProfileStringW(L"Shortcuts",(L"item"+std::to_wstring(i)).c_str(),L"",path,32768,storage.c_str());if(*path)shortcuts.push_back(make(path));}
        page=0;selected=-1;dirty=true;
    }
    std::wstring name(int index)const{
        static const wchar_t* names[]={L"性能设备",L"磁盘文件",L"网络监测",L"安全日志"};
        if(index>=0 && index<4)return names[index];if(index>=4 && static_cast<size_t>(index-4)<shortcuts.size())return shortcuts[static_cast<size_t>(index-4)].name;
        return index==Add?L"添加快捷方式":index==Terminal?L"命令终端":index==Settings?L"偏好设置":index==Previous?L"上一页":index==Next?L"下一页":L"";
    }
    bool addPath(HWND owner,const std::wstring& path){
        for(size_t i=0;i<shortcuts.size();++i)if(_wcsicmp(shortcuts[i].path.c_str(),path.c_str())==0){page=static_cast<int>((i+4)/8);selected=static_cast<int>(i+4);dirty=true;return true;}
        if(shortcuts.size()>=32){MessageBoxW(owner,L"最多添加 32 个快捷方式。可先移除不再使用的项目。",L"屏幕快捷方式",MB_OK|MB_ICONINFORMATION);return false;}
        if(path.empty() || GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){MessageBoxW(owner,L"找不到该文件或文件夹。",L"屏幕快捷方式",MB_OK|MB_ICONWARNING);return false;}
        // A Unicode profile file preserves non-ASCII paths on every Windows locale.
        if(GetFileAttributesW(storage.c_str())==INVALID_FILE_ATTRIBUTES){HANDLE file=CreateFileW(storage.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE){const wchar_t bom=0xfeff;DWORD written=0;WriteFile(file,&bom,sizeof(bom),&written,nullptr);CloseHandle(file);}}
        shortcuts.push_back(make(path));if(!save()){if(shortcuts.back().icon)DestroyIcon(shortcuts.back().icon);shortcuts.pop_back();MessageBoxW(owner,L"快捷方式保存失败，请检查配置目录权限。",L"屏幕快捷方式",MB_OK|MB_ICONERROR);return false;}
        selected=static_cast<int>(shortcuts.size())+3;page=selected/8;dirty=true;return true;
    }
    void choose(HWND owner,bool folder){
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
        dialog->SetTitle(folder?L"添加文件夹快捷方式":L"添加程序、文件或 Windows 快捷方式");
        dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_FILEMUSTEXIST|FOS_NODEREFERENCELINKS|(folder?FOS_PICKFOLDERS:0));
        if(FAILED(dialog->Show(owner)))return;Microsoft::WRL::ComPtr<IShellItem> item;if(FAILED(dialog->GetResult(&item)))return;
        PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){const std::wstring value=path;CoTaskMemFree(path);addPath(owner,value);}
    }
    void remove(HWND owner,int index){
        if(index<4 || static_cast<size_t>(index-4)>=shortcuts.size())return;
        const auto position=shortcuts.begin()+index-4;const auto removed=*position;shortcuts.erase(position);
        if(!save()){shortcuts.insert(shortcuts.begin()+index-4,removed);MessageBoxW(owner,L"无法保存更改。",L"屏幕快捷方式",MB_OK|MB_ICONERROR);return;}
        if(removed.icon)DestroyIcon(removed.icon);page=std::min(page,pages()-1);selected=-1;dirty=true;
    }
    void launch(HWND owner,int index){
        if(index<4 || static_cast<size_t>(index-4)>=shortcuts.size())return;
        const auto& s=shortcuts[static_cast<size_t>(index-4)];SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_FLAG_NO_UI;info.hwnd=owner;info.lpVerb=L"open";info.lpFile=s.path.c_str();info.nShow=SW_SHOWNORMAL;
        if(!ShellExecuteExW(&info)){const std::wstring error=L"无法打开："+s.path+L"\r\nWindows 错误 "+std::to_wstring(GetLastError())+L"。请检查目标是否存在、是否有默认打开方式。";MessageBoxW(owner,error.c_str(),L"快捷方式",MB_OK|MB_ICONWARNING);}
    }
    void draw(HDC dc,int hovered,int pressed,int fading=-1,double fade=0){
        if(!label){label=CreateFontW(-28,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Microsoft YaHei UI");title=CreateFontW(-26,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");symbol=CreateFontW(-54,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe Fluent Icons");}
        SetBkMode(dc,TRANSPARENT);block(dc,{0,0,800,500},RGB(229,238,240));block(dc,{0,0,800,66},RGB(248,251,252));
        text(dc,title,{34,8,260,59},L"PICO",RGB(27,72,66),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        text(dc,label,{274,8,766,59},name(hovered),RGB(65,80,86),DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        constexpr wchar_t glyphs[]={0xe9d9,0xeda2,0xe968,0xea18};constexpr COLORREF colors[]={RGB(30,117,172),RGB(190,137,31),RGB(33,139,112),RGB(99,86,167)};
        for(int slot=0;slot<8;++slot){const int index=page*8+slot;if(index>=4+static_cast<int>(shortcuts.size()))break;const auto r=tile(slot);
            const bool hot=index==hovered,down=index==pressed,chosen=index==selected;double amount=hot?1:index==fading?fade:0;
            if(amount>0 || chosen){auto surface=r;surface.top-=static_cast<LONG>(amount*4);surface.bottom-=static_cast<LONG>(amount*4);auto shadow=surface;OffsetRect(&shadow,0,3);block(dc,shadow,RGB(209,219,223),8);block(dc,surface,down?RGB(208,228,234):chosen?RGB(220,237,246):RGB(248,252,253),8);}
            const int lift=down?0:static_cast<int>(6*amount),cx=(r.left+r.right)/2,top=r.top+12-lift;
            if(index<4)text(dc,symbol,{cx-40,top,cx+40,top+72},std::wstring(1,glyphs[index]),colors[index]);
            else if(shortcuts[static_cast<size_t>(index-4)].icon)DrawIconEx(dc,cx-32,top+4,shortcuts[static_cast<size_t>(index-4)].icon,64,64,0,nullptr,DI_NORMAL);
            else text(dc,symbol,{cx-40,top,cx+40,top+72},L"\xe8a5",RGB(48,112,154));
            text(dc,label,{r.left+3,r.top+84,r.right-3,r.bottom-8},name(index),RGB(34,48,55));
            if(chosen)block(dc,{cx-16,r.bottom-5,cx+16,r.bottom-2},RGB(26,120,160),3);
        }
        block(dc,{0,429,800,500},RGB(249,252,253));block(dc,{0,428,800,430},RGB(209,222,227));
        for(int action:{Previous,Next,Terminal,Settings,Add}){const RECT r=dock(action);const bool enabled=action==Previous?page>0:action==Next?page+1<pages():true;
            if(action==hovered && enabled)block(dc,r,RGB(219,234,240),8);
            const wchar_t glyph=action==Previous?0xe76b:action==Next?0xe76c:action==Terminal?0xe756:action==Settings?0xe713:0xe710;
            if(!dockFont){LOGFONTW lf{};GetObjectW(symbol,sizeof(lf),&lf);lf.lfHeight=-32;dockFont=CreateFontIndirectW(&lf);}text(dc,dockFont,r,std::wstring(1,glyph),enabled?RGB(31,89,106):RGB(185,195,200));
        }
        text(dc,label,{340,438,462,492},std::to_wstring(page+1)+L" / "+std::to_wstring(pages()),RGB(78,95,101));dirty=false;
    }
};
}
