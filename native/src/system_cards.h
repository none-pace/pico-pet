#pragma once
#include "system_presentation.h"
#include "system_observations.h"
#include <deque>
#include <functional>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <cmath>

namespace systemdesk::cards {
inline std::wstring field(const Table& table,const Row& row,const wchar_t* name){
    const auto i=presentation::column(table,name);return i<row.size()?row[i]:L"";
}
inline uint64_t rate(const std::wstring& value){uint64_t result=0;presentation::byteRate(value,result);return result;}
inline Table groupProcesses(const Table& source){
    if(source.columns.size()<15)return source;
    struct Group {std::vector<const Row*> rows;};std::map<std::wstring,Group> groups;
    std::set<std::wstring> active;for(const auto& row:source.rows)if(row.size()>=15 && !ProcessObservations::recent(row))active.insert(softwareKey(row[12],row[1]));
    for(const auto& row:source.rows)if(row.size()>=15){const auto key=softwareKey(row[12],row[1]);groups[key+(ProcessObservations::recent(row) && active.contains(key)?L"|recent":L"")].rows.push_back(&row);}
    Table result{source.columns,{},source.summary+L" 按完整程序路径合并；工作集总和可能重复包含共享页。未知路径按 PID 分开。"};
    result.columns.push_back(L"子进程详情");
    for(const auto& [key,group]:groups){
        Row row=*group.rows.front();std::wstring details,pids;
        if(row[12].find(L":\\")!=std::wstring::npos)row[14]=key;
        for(const auto* item:group.rows){if(!pids.empty())pids+=L", ";pids+=(*item)[1];details+=presentation::details(source,*item,true)+L"\r\n";}
        if(group.rows.size()>1){
            row[1]=pids;row[14]=key;if(!ProcessObservations::recent(row))row[6]=std::to_wstring(group.rows.size())+L" 个进程";
            double cpu=0;bool cpuValid=true;
            for(const auto* item:group.rows){wchar_t* end=nullptr;const double value=wcstod((*item)[2].c_str(),&end);if(end==(*item)[2].c_str() || !std::isfinite(value) || value<0)cpuValid=false;else cpu+=value;}
            wchar_t value[64]{};swprintf_s(value,L"%.1f %%",cpu);row[2]=cpuValid?value:L"部分不可用";
            for(const size_t i:{3u,4u,5u,7u}){uint64_t total=0;bool valid=true;for(const auto* item:group.rows){uint64_t amount=0;if(!presentation::byteRate((*item)[i],amount) || amount>UINT64_MAX-total)valid=false;else total+=amount;}row[i]=valid?bytes(total)+(i==4 || i==5?L"/s":L""):L"部分不可用";}
            for(const size_t i:{8u,9u}){uint64_t total=0;bool valid=true;for(const auto* item:group.rows){wchar_t* end=nullptr;const auto amount=wcstoull((*item)[i].c_str(),&end,10);if(end==(*item)[i].c_str() || *end || amount>UINT64_MAX-total)valid=false;else total+=amount;}row[i]=valid?std::to_wstring(total):L"部分不可用";}
            row[10]=L"多个，见子进程详情";for(const auto* item:group.rows)row[11]=std::max(row[11],(*item)[11]);row[13]=L"同路径资源求和；启动时间为最新子进程。不可读数据不当作零。工作集可能重复计算共享内存，I/O 不等于磁盘吞吐。";
        }
        row.push_back(std::move(details));result.rows.push_back(std::move(row));
    }
    return result;
}
inline void sort(Table& table,int mode){
    if(mode==0)return;
    const auto score=[&](const Row& row){
        if(mode==2){for(const auto* name:{L"上传（TCP）",L"发送速率",L"TCP 发送速率"}){const auto value=field(table,row,name);if(!value.empty())return static_cast<double>(rate(value));}}
        if(mode==3){const auto value=field(table,row,L"CPU");wchar_t* end=nullptr;const auto n=wcstod(value.c_str(),&end);return end!=value.c_str() && std::isfinite(n)?n:-1.0;}
        if(mode==4)return static_cast<double>(rate(field(table,row,L"内存工作集")));
        if(mode==7){for(const auto* name:{L"下载（TCP）",L"接收速率",L"TCP 接收速率"}){const auto value=field(table,row,name);if(!value.empty())return static_cast<double>(rate(value));}}
        if(mode==8 || mode==9)return static_cast<double>(rate(field(table,row,mode==8?L"读取 /s":L"写入 /s")));
        if(mode==10){const auto total=rate(field(table,row,L"总容量"));return total?static_cast<double>(rate(field(table,row,L"已用")))/total:-1.0;}
        return -1.0;
    };
    const auto date=[&](const Row& row){
        if(mode==5)return field(table,row,L"启动时间");
        for(const auto* name:{L"最后可见",L"记录时间",L"时间",L"修改时间",L"现修改时间",L"键最后写入时间"}){const auto value=field(table,row,name);if(!value.empty())return value;}
        return std::wstring{};
    };
    std::stable_sort(table.rows.begin(),table.rows.end(),[&](const Row& a,const Row& b){
        if(mode==1)return _wcsicmp(a.empty()?L"":a[0].c_str(),b.empty()?L"":b[0].c_str())<0;
        if(mode==5 || mode==6)return date(a)>date(b);
        return score(a)>score(b);
    });
}
inline std::vector<std::pair<int,const wchar_t*>> sortOptions(const Table& table){
    std::vector<std::pair<int,const wchar_t*>> result={{0,L"排序：保持位置"},{1,L"排序：名称"}};
    const auto has=[&](const wchar_t* name){return presentation::column(table,name)<table.columns.size();};
    if(has(L"上传（TCP）") || has(L"发送速率") || has(L"TCP 发送速率"))result.emplace_back(2,L"排序：上传从高到低");
    if(has(L"CPU"))result.emplace_back(3,L"排序：CPU 从高到低");
    if(has(L"内存工作集"))result.emplace_back(4,L"排序：内存从高到低");
    if(has(L"启动时间"))result.emplace_back(5,L"排序：最新启动");
    if(has(L"最后可见") || has(L"记录时间") || has(L"时间") || has(L"修改时间") || has(L"现修改时间") || has(L"键最后写入时间"))result.emplace_back(6,L"排序：最新记录 / 修改");
    if(has(L"下载（TCP）") || has(L"接收速率") || has(L"TCP 接收速率"))result.emplace_back(7,L"排序：下载从高到低");
    if(has(L"读取 /s"))result.emplace_back(8,L"排序：I/O 读取从高到低");
    if(has(L"写入 /s"))result.emplace_back(9,L"排序：I/O 写入从高到低");
    if(has(L"总容量") && has(L"已用"))result.emplace_back(10,L"排序：磁盘占用率");
    return result;
}
inline std::wstring explain(const Table& table,const Row& row,int section){
    if(row.empty() || table.columns.empty())return L"未选择项目";
    if(section==2)return presentation::details(table,row,true);
    const auto& kind=table.columns[0];
    if(kind==L"软件" && row.size()>=15){
        if(section==1)return L"程序来源\r\n"+row[13]+L"\r\n\r\n当前进程 PID\r\n"+row[6]+L"\r\n\r\n通信对象（地址、端口、PTR）\r\n"+row[9]+L"\r\n\r\n最近观测\r\n"+row[11]+L"\r\n历史端点："+row[12]+L"\r\n\r\n测速覆盖\r\n"+row[7]+L"\r\n\r\n归纳与建议\r\n"+row[8]+L"\r\n\r\n证据边界\r\nPTR 不是实际请求域名或 URL；TLS 内容不可见。本机代理的公网流向需要进一步查看代理程序。";
        std::wstring result=(row.size()>15?row[15]:row[1])+L"。"+row[4]+L"\r\n已测 TCP：上传 "+row[3]+L"，下载 "+row[2]+L"。";
        if(row.size()>17 && row[17].find(L"观测时间：")==0)result+=L"\r\n曾记录持续上传，请核对是否为同步或发文件；不代表泄露。";
        return result;
    }
    if(kind==L"进程名称" && row.size()>=15){
        if(section==1)return L"程序路径\r\n"+row[12]+L"\r\n\r\n进程证据\r\n"+(row.size()>15?row[15]:presentation::details(table,row,true));
        return row[6]+L"。CPU "+row[2]+L"，内存 "+row[3]+L"。\r\nI/O：读取 "+row[4]+L"，写入 "+row[5]+L"。";
    }
    if(section==1)return presentation::details(table,row,true);
    if(kind==L"设备类别" && row.size()>=11)return L"设备状态："+row[2]+L"。\r\n驱动版本："+row[4]+(row[2]==L"运行中"?L"":L"\r\n可在设备管理器核对原因。");
    if(kind==L"网络接口" && row.size()>=7)return row[6]+L"。当前接收 "+row[2]+L"，发送 "+row[3]+L"。\r\n这是整张网卡的流量。";
    if(presentation::connections(table) && row.size()>=17){
        std::wstring result=(row.size()>=20?row[19]+L"；":L"")+row[3]+L"，"+row[13]+L"。\r\n接收 "+presentation::cell(table,row,10,false)+L"，发送 "+presentation::cell(table,row,11,false)+L"。";
        if(row.size()>=23 && row[12].find(L"ETW")==0)return row[19]+L"；"+row[3]+L"。\r\n累计捕获：接收 "+bytes(std::wcstoull(row[20].c_str(),nullptr,10))+L"，发送 "+bytes(std::wcstoull(row[21].c_str(),nullptr,10))+L"。";
        else if(row[2].find(L"UDP")==0)result+=L"\r\nUDP 流向可切换“实时捕获”查看。";
        else if(row[3]==L"本机回环" || row[3]==L"本机接口")result+=L"\r\n这一步在本机内通信，代理后续流向需另查。";
        else if(row[3]==L"公网地址")result+=L"\r\n对端属于公网，不能仅凭连接判断风险。";
        return result;
    }
    if(kind==L"盘符 / 分区" && row.size()>=7)return L"可用 "+row[5]+L"，已用 "+row[4]+L"，总容量 "+row[3]+L"。";
    if(kind==L"名称" && row.size()>=7)return row[1]+L"，"+row[2]+L"。\r\n最后修改："+row[3];
    if(kind==L"路径" && row.size()>=6)return L"快照中的"+row[1]+L"，"+row[2]+L"。\r\n记录的修改时间："+row[3];
    if(kind==L"变化" && row.size()>=7)return row[0]+L"。大小："+row[2]+L" → "+row[3]+L"。\r\n这是文件元数据比较结果。";
    if(kind==L"记录时间" && presentation::column(table,L"解读")<table.columns.size())return field(table,row,L"级别")+L"："+field(table,row,L"解读");
    if(kind==L"记录时间" && presentation::column(table,L"类别")<table.columns.size())return field(table,row,L"风险")+L"："+field(table,row,L"类别")+L" / "+field(table,row,L"事件");
    if(kind==L"记录时间" && presentation::column(table,L"完整输出")<table.columns.size())return field(table,row,L"类型")+L"："+field(table,row,L"状态")+L"。退出码 "+field(table,row,L"退出码")+L"。";
    if(kind==L"键 / 值名称")return field(table,row,L"解读");
    if(kind==L"安全项目" && row.size()>=4)return row[1]+L"（"+row[2]+L"）。"+(row[2]==L"正常"?L"":L"\r\n"+row[3]);
    if(kind==L"时间")return field(table,row,L"事件")+L"；"+field(table,row,L"通信范围")+L"，"+field(table,row,L"状态");
    if(kind==L"进程" && row.size()==9)return L"已建立 TCP："+field(table,row,L"已建立 TCP")+L"。\r\n接收 "+field(table,row,L"TCP 接收速率")+L"，发送 "+field(table,row,L"TCP 发送速率")+L"。";
    return row.size()>1?row[0]+L"："+row[1]:row[0];
}
struct Sample {ULONGLONG time=0;uint64_t rx=0,tx=0;bool valid=false;};
class Insights {
    struct State {std::deque<Sample> samples;ULONGLONG highSince=0,last=0,activeAt=0;uint64_t peak=0;std::wstring notice;};
    std::map<std::wstring,State> states;
public:
    void interrupt(){for(auto& [key,state]:states){(void)key;state.highSince=0;if(!state.samples.empty())state.samples.back().valid=false;}}
    const std::deque<Sample>* history(const std::wstring& key)const{const auto found=states.find(key);return found==states.end()?nullptr:&found->second.samples;}
    void annotate(Table& apps,ULONGLONG now,bool complete){
        if(apps.columns.empty())return;
        apps.columns.insert(apps.columns.end(),{L"活动提示",L"峰值上传（已测 TCP）",L"保留的上传提示"});
        for(auto& row:apps.rows){if(row.size()<15)continue;
            if(!states.contains(row[14]) && row[5]==L"0"){row.insert(row.end(),{L"本轮无端点",L"--",L"仅保留端点历史，没有本软件的流量样本。"});continue;}
            if(!states.contains(row[14]) && states.size()>=2048){
                auto oldest=std::min_element(states.begin(),states.end(),[](const auto& a,const auto& b){return a.second.activeAt<b.second.activeAt;});
                if(now-oldest->second.activeAt<60000){row.insert(row.end(),{L"活动记录容量已满",L"--",L"当前仍显示端点与速率；最多保存 2048 个软件的分钟曲线与上传提示。"});continue;}
                states.erase(oldest);
            }
            auto& state=states[row[14]];uint64_t rx=0,tx=0;
            const bool valid=presentation::byteRate(row[2],rx) && presentation::byteRate(row[3],tx);
            const bool continuous=state.last && now>state.last && now-state.last<=2500;
            state.last=now;
            if(row[5]!=L"0")state.activeAt=now;
            if(state.samples.empty() || state.samples.back().time!=now/1000)state.samples.push_back({now/1000,rx,tx,valid});
            else state.samples.back()={now/1000,rx,tx,valid};
            while(state.samples.size()>60 || (!state.samples.empty() && now/1000-state.samples.front().time>=60))state.samples.pop_front();
            if(valid)state.peak=std::max(state.peak,tx);
            std::wstring signal=row[5]==L"0"?L"本轮无端点":!valid?L"流量尚不可见":tx?L"观测到上传":L"已测 TCP 暂无上传";
            if(valid && complete && tx>=256*1024){
                if(!continuous || !state.highSince)state.highSince=now;
                if(now-state.highSince>=5000){
                    signal=L"持续上传，建议核对";
                    state.notice=L"观测时间："+row[11]+L"。已测 TCP 上传连续至少 5 秒不低于 256 KiB/s；峰值 "+bytes(state.peak)+L"/s。可能是同步、发送文件或正常业务，请核对该软件用途与目标；这不是泄露判定。";
                }
            }else state.highSince=0;
            row.insert(row.end(),{signal,state.peak?bytes(state.peak)+L"/s":L"--",state.notice.empty()?L"尚无达到持续上传规则的记录；不代表没有上传或泄露。":state.notice});
            row[8]+=L"\r\n\r\n上传活动记录\r\n"+row.back();
        }
    }
};
enum class Filter {All,Uploading,Public,Local,Listening,Unknown,Inactive,Recorded};
inline bool matches(const Row& row,Filter filter){
    if(row.size()<15)return filter==Filter::All;
    switch(filter){
    case Filter::Uploading:return rate(row[3])>0;
    case Filter::Public:return row[4].find(L"公网 0 ")!=0;
    case Filter::Local:return row[4].find(L"本机 0 ")==std::wstring::npos;
    case Filter::Listening:return row[8].find(L"个监听端点")!=std::wstring::npos;
    case Filter::Unknown:return row[5]!=L"0" && (row[8].find(L"未取得速率")!=std::wstring::npos || row[8].find(L"UDP/QUIC 无")!=std::wstring::npos || row[3]==L"不可用");
    case Filter::Inactive:return row[5]==L"0";
    case Filter::Recorded:return row.size()>17 && row[17].find(L"观测时间：")==0;
    default:return true;
    }
}
struct Card {std::wstring title,category,primary,secondary,note,path,glyph=L"\xE8A5";COLORREF accent=RGB(0,105,124);};
inline Card describe(const Table& table,const Row& row){
    Card card;if(row.empty() || table.columns.empty())return card;
    const auto& kind=table.columns[0];card.title=row[0];card.category=kind;
    if(kind==L"软件" && row.size()>=15){
        card.glyph=L"\xE774";card.path=row[13];card.category=row.size()>15?row[15]:row[1];
        card.primary=L"上传  "+row[3];card.secondary=L"下载  "+row[2];card.note=row[4];
        if(card.category.find(L"持续上传")==0 || (row.size()>17 && matches(row,Filter::Recorded)))card.accent=RGB(165,83,12);
        else if(row[3]==L"不可用")card.accent=RGB(106,110,119);
    }else if(kind==L"进程名称" && row.size()>=14){
        card.path=row[12];card.glyph=L"\xE7C4";card.category=L"PID "+row[1]+L"  "+row[6];
        card.primary=L"CPU  "+row[2];card.secondary=L"内存  "+row[3];card.note=L"读取 "+row[4]+L"   写入 "+row[5];
    }else if(kind==L"网络接口" && row.size()>=7){
        card.glyph=L"\xE968";card.category=row[6];card.primary=L"发送  "+row[3];card.secondary=L"接收  "+row[2];card.note=L"连接速率  "+row[1];
    }else if(presentation::connections(table) && row.size()>=17){
        card.glyph=L"\xE839";card.path=row[15];card.category=row[3]+L"  "+row[2];
        card.primary=presentation::cell(table,row,9,false);card.secondary=L"上传 "+presentation::cell(table,row,11,false)+L"   下载 "+presentation::cell(table,row,10,false);
        card.note=row.size()>=20?row[19]+L"  "+row[18]:row[13];
    }else if(kind==L"盘符 / 分区" && row.size()>=7){
        card.glyph=L"\xEDA2";card.title=row[0]+L"  "+row[1];card.category=row[2];card.primary=L"可用  "+row[5];card.secondary=L"已用 "+row[4]+L" / "+row[3];card.note=row[6];
    }else if(kind==L"设备类别" && row.size()>=11){
        card.glyph=L"\xE7F4";card.title=row[1];card.category=row[0];card.primary=row[2];card.secondary=L"驱动  "+row[4];card.note=row[3];if(row[2]!=L"运行中")card.accent=RGB(165,83,12);
    }else if(kind==L"安全项目" && row.size()>=4){
        card.glyph=L"\xEA18";card.category=row[2];card.primary=row[1];card.secondary=row[3];card.note=L"Windows 本机保护状态";card.accent=row[2]==L"正常"?RGB(27,117,87):RGB(165,83,12);
    }else if(kind==L"项目" && row.size()>=2){
        card.glyph=L"\xE9D9";card.category=L"系统状态";card.primary=row[1];card.note=L"Windows 本机读数";
    }else if(kind==L"名称" && row.size()>=7){
        card.glyph=row[1]==L"文件夹"?L"\xE8B7":L"\xE8A5";card.category=row[1];card.primary=row[2];card.secondary=L"修改  "+row[3];card.note=row[6];
    }else{
        const auto cols=presentation::columns(table,false);
        if(cols.size()>1)card.primary=presentation::cell(table,row,cols[1],false);
        if(cols.size()>2)card.secondary=presentation::cell(table,row,cols[2],false);
        if(cols.size()>3)card.note=presentation::cell(table,row,cols[3],false);
        const auto severity=field(table,row,L"级别");if(!severity.empty()){card.category=severity;if(severity==L"错误" || severity==L"严重")card.accent=RGB(175,48,62);}
    }
    for(auto* value:{&card.title,&card.category,&card.primary,&card.secondary,&card.note}){
        const auto end=value->find_first_of(L"\r\n");if(end!=std::wstring::npos)*value=value->substr(0,end)+L" …";
    }
    return card;
}

// Only visible cards own native buttons; the complete table remains the data source.
class Board {
    HWND window=nullptr;std::vector<HWND> slots;int dpi=96,columns=1,offset=0,selected=-1,paintedSelection=-1;
    HFONT font=nullptr,strong=nullptr,icons=nullptr;
    std::map<std::wstring,HICON> iconCache;
    std::function<int()> count;std::function<Card(int)> describeItem;std::function<void(int,bool)> activate;
    int px(int n)const{return MulDiv(n,dpi,96);}
    int totalHeight()const{return ((count()+columns-1)/columns)*px(166);}
    void scroll(int value){RECT r{};GetClientRect(window,&r);offset=std::clamp(value,0,std::max(0,totalHeight()-static_cast<int>(r.bottom)));refresh();}
    void moveSelection(int index){
        if(!count())return;selected=std::clamp(index,0,count()-1);RECT r{};GetClientRect(window,&r);const int top=(selected/columns)*px(166);
        if(top<offset)offset=top;else if(top+px(158)>offset+r.bottom)offset=top+px(158)-r.bottom;
        refresh();for(HWND slot:slots)if(static_cast<int>(GetWindowLongPtrW(slot,GWLP_USERDATA))==selected && IsWindowVisible(slot)){SetFocus(slot);break;}
        activate(selected,false);
    }
    static LRESULT CALLBACK buttonProc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Board*>(data);
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(h,buttonProc,1);
        if(msg==WM_MOUSEWHEEL)return SendMessageW(self.window,msg,w,l);
        if(msg==WM_GETDLGCODE)return DefSubclassProc(h,msg,w,l)|DLGC_WANTARROWS;
        if(msg==WM_KEYDOWN){const int index=static_cast<int>(GetWindowLongPtrW(h,GWLP_USERDATA));
            int delta=w==VK_LEFT?-1:w==VK_RIGHT?1:w==VK_UP?-self.columns:w==VK_DOWN?self.columns:0;
            if(delta){self.moveSelection(index+delta);return 0;}
            if(w==VK_RETURN){self.activate(index,true);return 0;}
        }
        return DefSubclassProc(h,msg,w,l);
    }
    HICON appIcon(const std::wstring& path){
        if(path.empty() || path.size()<3 || path[1]!=L':' || path[2]!=L'\\')return nullptr;
        const auto found=iconCache.find(path);if(found!=iconCache.end())return found->second;
        if(iconCache.size()>=64)return nullptr;
        SHFILEINFOW info{};SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_SMALLICON);
        iconCache.emplace(path,info.hIcon);return info.hIcon;
    }
    void draw(const DRAWITEMSTRUCT& item){
        const int index=static_cast<int>(GetWindowLongPtrW(item.hwndItem,GWLP_USERDATA));if(index<0 || index>=count())return;
        const auto card=describeItem(index);RECT r=item.rcItem;const bool chosen=index==selected;
        HDC buffer=CreateCompatibleDC(item.hDC);HBITMAP bitmap=buffer?CreateCompatibleBitmap(item.hDC,r.right,r.bottom):nullptr;
        HGDIOBJ oldBitmap=bitmap?SelectObject(buffer,bitmap):nullptr;HDC dc=bitmap?buffer:item.hDC;const int saved=SaveDC(dc);
        FillRect(dc,&r,GetSysColorBrush(COLOR_WINDOW));
        HBRUSH bg=CreateSolidBrush(chosen?RGB(234,246,247):RGB(255,255,255));HPEN border=CreatePen(PS_SOLID,px(chosen?2:1),chosen?RGB(0,105,124):RGB(220,226,230));
        SelectObject(dc,bg);SelectObject(dc,border);RoundRect(dc,1,1,r.right-1,r.bottom-1,px(12),px(12));
        SetBkMode(dc,TRANSPARENT);
        auto label=[&](const std::wstring& value,int x,int y,int right,int height,HFONT face,COLORREF color,UINT flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS){RECT box{px(x),px(y),right,px(y+height)};SelectObject(dc,face);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),-1,&box,flags|DT_NOPREFIX);};
        if(auto icon=appIcon(card.path))DrawIconEx(dc,px(15),px(17),icon,px(24),px(24),0,nullptr,DI_NORMAL);
        else label(card.glyph,15,16,px(42),28,icons,card.accent);
        label(card.title,51,13,r.right-px(14),26,strong,RGB(32,42,46));
        label(card.category,51,40,r.right-px(14),20,font,card.accent);
        label(card.primary,16,68,r.right-px(14),25,strong,RGB(32,42,46));
        label(card.secondary,16,97,r.right-px(14),22,font,RGB(69,82,91));
        label(card.note,16,129,r.right-px(14),18,font,RGB(101,111,121));
        if((item.itemState&ODS_FOCUS) && !(item.itemState&ODS_NOFOCUSRECT)){InflateRect(&r,-px(5),-px(5));DrawFocusRect(dc,&r);}
        RestoreDC(dc,saved);DeleteObject(border);DeleteObject(bg);
        if(bitmap){BitBlt(item.hDC,0,0,item.rcItem.right,item.rcItem.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,oldBitmap);DeleteObject(bitmap);}if(buffer)DeleteDC(buffer);
    }
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Board*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Board*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(h,msg,w,l);
        switch(msg){
        case WM_SIZE:self->refresh();return 0;
        case WM_MOUSEWHEEL:self->scroll(self->offset-GET_WHEEL_DELTA_WPARAM(w)*self->px(54)/WHEEL_DELTA);return 0;
        case WM_VSCROLL:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(h,SB_VERT,&info);int position=self->offset;
            RECT r{};GetClientRect(h,&r);switch(LOWORD(w)){case SB_LINEUP:position-=self->px(54);break;case SB_LINEDOWN:position+=self->px(54);break;case SB_PAGEUP:position-=r.bottom;break;case SB_PAGEDOWN:position+=r.bottom;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:position=info.nTrackPos;break;case SB_TOP:position=0;break;case SB_BOTTOM:position=self->totalHeight();break;}
            self->scroll(position);return 0;}
        case WM_DRAWITEM:self->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
        case WM_COMMAND:if(HIWORD(w)==BN_CLICKED || HIWORD(w)==BN_DOUBLECLICKED){const int index=static_cast<int>(GetWindowLongPtrW(reinterpret_cast<HWND>(l),GWLP_USERDATA));if(index>=0 && index<self->count()){self->selected=index;self->activate(index,HIWORD(w)==BN_DOUBLECLICKED);self->refresh();}return 0;}break;
        case WM_ERASEBKGND:{RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(w),&r,GetSysColorBrush(COLOR_WINDOW));return 1;}
        case WM_DESTROY:self->slots.clear();self->window=nullptr;return 0;
        }
        return DefWindowProcW(h,msg,w,l);
    }
public:
    ~Board(){for(auto& [path,icon]:iconCache){(void)path;if(icon)DestroyIcon(icon);}}
    HWND handle()const{return window;}
    void create(HWND parent,int id,std::function<int()> amount,std::function<Card(int)> description,std::function<void(int,bool)> action){
        count=std::move(amount);describeItem=std::move(description);activate=std::move(action);
        WNDCLASSW type{};type.lpfnWndProc=proc;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=L"PicoPet.CardBoard";type.hbrBackground=GetSysColorBrush(COLOR_WINDOW);RegisterClassW(&type);
        CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,L"信息卡片",WS_CHILD|WS_CLIPCHILDREN|WS_VSCROLL,0,0,100,100,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),type.hInstance,this);
    }
    void appearance(int value,HFONT normal,HFONT bold,HFONT symbols){dpi=value;font=normal;strong=bold;icons=symbols;refresh();for(HWND slot:slots){SendMessageW(slot,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);InvalidateRect(slot,nullptr,FALSE);}}
    void refresh(int selection=-2,bool reset=false){
        if(!window)return;if(selection!=-2)selected=selection;if(reset)offset=0;
        RECT r{};GetClientRect(window,&r);columns=std::clamp(static_cast<int>(r.right)/std::max(1,px(290)),1,3);
        offset=std::clamp(offset,0,std::max(0,totalHeight()-static_cast<int>(r.bottom)));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,totalHeight()-1),static_cast<UINT>(r.bottom),offset,0};SetScrollInfo(window,SB_VERT,&info,TRUE);
        const int first=(offset/px(166))*columns,needed=std::min(std::max(0,count()-first),((static_cast<int>(r.bottom)/px(166))+2)*columns);
        for(int i=0;i<needed;++i){
            if(static_cast<size_t>(i)>=slots.size()){HWND button=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(32000+i)),GetModuleHandleW(nullptr),nullptr);SetWindowSubclass(button,buttonProc,1,reinterpret_cast<DWORD_PTR>(this));SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);slots.push_back(button);}
            HWND button=slots[static_cast<size_t>(i)];const int index=first+i;SetWindowLongPtrW(button,GWLP_USERDATA,index);
            const auto card=describeItem(index);const auto name=card.title+L"；"+card.category+L"；"+card.primary+L"；"+card.secondary+L"；"+card.note;
            const int length=GetWindowTextLengthW(button);std::wstring old(static_cast<size_t>(length)+1,L'\0');GetWindowTextW(button,old.data(),length+1);old.resize(static_cast<size_t>(length));if(old!=name)SetWindowTextW(button,name.c_str());
            const int width=(r.right-px(10)*(columns-1))/columns;
            RECT previous{};GetWindowRect(button,&previous);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&previous),2);
            const int x=(index%columns)*(width+px(10)),y=(index/columns)*px(166)-offset;
            const RECT next{x,y,x+std::max(width,1),y+px(156)};const bool moved=!EqualRect(&previous,&next);
            if(moved)MoveWindow(button,x,y,std::max(width,1),px(156),TRUE);ShowWindow(button,SW_SHOW);
            if(moved || old!=name || (paintedSelection!=selected && (index==selected || index==paintedSelection)))InvalidateRect(button,nullptr,FALSE);
        }
        for(size_t i=static_cast<size_t>(needed);i<slots.size();++i)ShowWindow(slots[i],SW_HIDE);
        paintedSelection=selected;InvalidateRect(window,nullptr,TRUE);
    }
};
}
