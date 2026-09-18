#pragma once
#include "system_core.h"
#include <algorithm>
#include <cwchar>
#include <set>
#include <cmath>

namespace systemdesk::presentation {
inline bool connections(const Table& table){return table.columns.size()>=16 && table.columns[0]==L"进程" && table.columns[2]==L"协议";}
inline bool byteRate(const std::wstring& text,uint64_t& result){
    result=0;if(text.empty() || text==L"--")return false;
    wchar_t* end=nullptr;const double value=wcstod(text.c_str(),&end);if(end==text.c_str() || !std::isfinite(value) || value<0)return false;
    while(*end==L' ')++end;double scale=1;
    if(wcsncmp(end,L"KiB",3)==0)scale=1024;
    else if(wcsncmp(end,L"MiB",3)==0)scale=1024*1024;
    else if(wcsncmp(end,L"GiB",3)==0)scale=1024*1024*1024;
    else if(wcsncmp(end,L"TiB",3)==0)scale=1024.0*1024*1024*1024;
    else if(*end!=L'B')return false;
    const long double scaled=static_cast<long double>(value)*scale;
    result=scaled>=static_cast<long double>(UINT64_MAX)?UINT64_MAX:static_cast<uint64_t>(scaled);return true;
}
inline size_t column(const Table& table,const wchar_t* name){
    const auto found=std::find(table.columns.begin(),table.columns.end(),name);return found==table.columns.end()?table.columns.size():static_cast<size_t>(found-table.columns.begin());
}
inline Row identity(const Table& table,const Row& row){
    Row key;
    for(const auto* name:{L"软件标识",L"进程标识",L"设备实例 ID"}){const auto i=column(table,name);if(i<row.size())return {row[i]};}
    for(const auto* name:{L"PID",L"进程启动标识",L"协议",L"本机地址",L"本机端口",L"远端地址",L"远端端口",L"程序路径",L"记录时间",L"时间",L"事件",L"对象",L"本机端点",L"远端端点",L"网络接口"}){const auto i=column(table,name);if(i<row.size())key.push_back(row[i]);}
    return key.empty()?(row.empty()?Row{}:Row{row[0]}):key;
}
inline Table concise(const Table& source,std::wstring& explanation){
    Table result{source.columns,{},source.summary};explanation.clear();if(source.columns.empty())return result;
    const auto& first=source.columns[0];
    if(first==L"安全项目"){
        const auto level=column(source,L"级别");for(const auto& row:source.rows)if(level<row.size() && row[level]!=L"正常")result.rows.push_back(row);
        explanation=result.rows.empty()?L"本次读取未发现需要优先核对的保护状态":L"隐藏正常项，优先显示关注、未知和提示状态";
    }else if(first==L"项目"){
        static const std::set<std::wstring> keep={L"CPU 使用率",L"物理内存",L"可用内存",L"系统运行时间",L"Windows 版本",L"设备型号",L"BIOS",L"供电"};
        for(const auto& row:source.rows)if(!row.empty() && keep.contains(row[0]))result.rows.push_back(row);
        explanation=L"优先显示负载、系统版本、设备型号、固件与供电";
    }else if(first==L"设备类别"){
        const auto state=column(source,L"状态");
        for(const auto& row:source.rows)if(state<row.size() && row[state]!=L"运行中")result.rows.push_back(row);
        explanation=result.rows.empty()?L"当前枚举中未发现 PnP 状态异常":L"仅显示未运行、禁用、不可读或带问题代码的设备";
    }else if(connections(source)){
        result.rows=source.rows;explanation=L"只精简展示字段，保留全部端点；回环和监听也可用于排查";
    }else if(first==L"时间"){
        result.rows=source.rows;explanation=L"保留全部连接变化，不按风险猜测隐藏证据";
    }else if(first==L"进程" && source.columns.size()==9){
        const auto established=column(source,L"已建立 TCP"),measured=column(source,L"可测速 TCP"),received=column(source,L"TCP 接收速率"),sent=column(source,L"TCP 发送速率");
        for(const auto& row:source.rows)if((established<row.size() && row[established]!=L"0") || (measured<row.size() && row[measured]!=L"0"))result.rows.push_back(row);
        std::stable_sort(result.rows.begin(),result.rows.end(),[&](const Row& a,const Row& b){uint64_t ar=0,as=0,br=0,bs=0;if(received<a.size())byteRate(a[received],ar);if(sent<a.size())byteRate(a[sent],as);if(received<b.size())byteRate(b[received],br);if(sent<b.size())byteRate(b[sent],bs);return static_cast<long double>(ar)+as>static_cast<long double>(br)+bs;});
        explanation=L"显示存在已建立 TCP 或可测速连接的进程";
    }else if(first==L"网络接口"){
        result.rows=source.rows;const auto received=column(source,L"接收速率"),sent=column(source,L"发送速率");
        std::stable_sort(result.rows.begin(),result.rows.end(),[&](const Row& a,const Row& b){uint64_t ar=0,as=0,br=0,bs=0;if(received<a.size())byteRate(a[received],ar);if(sent<a.size())byteRate(a[sent],as);if(received<b.size())byteRate(b[received],br);if(sent<b.size())byteRate(b[sent],bs);return static_cast<long double>(ar)+as>static_cast<long double>(br)+bs;});
        explanation=L"保留已启用网卡，并按当前活动速率排序";
    }else if(first==L"记录时间" && column(source,L"完整输出")<source.columns.size()){
        result.rows=source.rows;explanation=L"保留最近的命令与 Codex 调用结果";
    }else if(first==L"记录时间" && column(source,L"类别")<source.columns.size()){
        const auto risk=column(source,L"风险");
        for(const auto& row:source.rows)if(risk<row.size() && row[risk]!=L"常规")result.rows.push_back(row);
        explanation=L"优先显示文件变化与需要关注的程序；完整模式保留全部程序生命周期记录";
    }else if(first==L"记录时间"){
        const auto level=column(source,L"级别"),provider=column(source,L"事件来源"),eventId=column(source,L"事件 ID");
        for(const auto& row:source.rows){
            const bool attention=level<row.size() && (row[level]==L"警告" || row[level]==L"错误" || row[level]==L"严重");
            const bool commonDcom=provider<row.size() && eventId<row.size() && row[provider]==L"Microsoft-Windows-DistributedCOM" && row[eventId]==L"10016";
            if(attention && !commonDcom)result.rows.push_back(row);
        }
        explanation=L"显示警告、错误和严重事件，并隐藏常见 DistributedCOM 10016 噪声";
    }else if(first==L"键 / 值名称"){
        const auto meaning=column(source,L"解读"),kind=column(source,L"项目类别");
        for(const auto& row:source.rows)if((kind<row.size() && row[kind]==L"子键") || (meaning<row.size() && row[meaning]!=L"注册表配置项"))result.rows.push_back(row);
        explanation=L"保留全部子键导航，并优先显示有专用解释的值";
    }else{
        result.rows=source.rows;
        explanation=first==L"名称"?L"文件浏览不隐藏项目":first==L"变化"?L"快照差异全部保留":first==L"盘符 / 分区"?L"磁盘与分区状态全部保留":L"当前视图全部保留";
    }
    result.summary=L"简洁模式：显示 "+std::to_wstring(result.rows.size())+L" / "+std::to_wstring(source.rows.size())+L" 项；"+explanation+L"。\r\n"+source.summary;
    return result;
}
inline std::vector<int> columns(const Table& table,bool full){
    if(table.columns.empty())return {};
    if(!full){
        const auto& first=table.columns[0];
        if(first==L"安全项目")return {0,1,2,3};
        if(first==L"记录时间" && column(table,L"完整输出")<table.columns.size())return {0,1,2,3,4,5};
        if(first==L"记录时间" && column(table,L"类别")<table.columns.size())return {0,1,2,3,4,5};
        if(first==L"记录时间")return {0,1,2,3,4};
        if(first==L"键 / 值名称")return {0,1,2,3};
        if(first==L"软件")return {0,1,2,3,4};
        if(first==L"进程名称")return {0,1,2,3,4,5};
        if(connections(table))return table.columns.size()>=20?std::vector<int>{0,9,3,19,17,18}:std::vector<int>{0,9,3,10,11,13};
        if(first==L"设备类别")return {1,0,4,2};
        if(first==L"网络接口")return {0,2,3,4,5};
        if(first==L"进程" && table.columns.size()==9)return {0,2,5,6,7};
        if(first==L"时间")return {0,1,2,6,8};
        if(first==L"盘符 / 分区")return {0,1,3,4,5};
        if(first==L"名称")return {0,1,2,3};
        if(first==L"变化")return {0,1,2,3};
        if(first==L"完整路径")return {0,1,2,3};
    }
    std::vector<int> result;for(size_t i=0;i<table.columns.size();++i)result.push_back(static_cast<int>(i));return result;
}
inline std::wstring endpoint(const std::wstring& ip,const std::wstring& port){
    if(ip.empty() || ip==L"未提供")return L"未提供";
    return (ip.find(L':')!=std::wstring::npos?L"["+ip+L"]":ip)+(port==L"--" || port==L"0"?L"":L":"+port);
}
inline std::wstring cell(const Table& table,const Row& row,int source,bool full){
    if(source<0 || static_cast<size_t>(source)>=row.size())return L"";
    if(full)return row[static_cast<size_t>(source)];
    if(connections(table)){
        if((source==17 || source==18) && row.size()>=20 && row[static_cast<size_t>(source)].size()>=19)return row[static_cast<size_t>(source)].substr(11,8);
        if(source==19 && row.size()>=20)return row[19]==L"已结束 / 不再可见"?L"已结束":row[19];
        if((source==10 || source==11) && row[static_cast<size_t>(source)]==L"--"){
            if(row[12].find(L"未开启")!=std::wstring::npos)return L"未开启";
            if(row[12].find(L"权限")!=std::wstring::npos)return L"需权限";
            if(row[12].find(L"UDP")!=std::wstring::npos)return L"不支持";
            if(row[12].find(L"基准")!=std::wstring::npos)return L"采样中";
            return L"--";
        }
        if(source==9){
            const auto& dns=row[9];
            const bool name=dns.find(L'.')!=std::wstring::npos && dns.find(L"反查")==std::wstring::npos;
            return name?dns+(row[8]==L"0"?L"":L":"+row[8]):endpoint(row[7],row[8]);
        }
        if(source==13 && row[13].find(L"已连接")==0)return L"已连接";
    }
    const auto& value=row[static_cast<size_t>(source)];
    if(value==L"局域网 / 私有地址")return L"局域网";
    if(value==L"通配 / 未指定")return L"监听 / 未指定";
    return value;
}
inline std::wstring heading(const Table& table,int source,bool full){
    if(!full && connections(table) && source==9)return L"通信对象";
    if(!full && connections(table) && source==3)return L"范围";
    return table.columns[static_cast<size_t>(source)];
}
inline std::wstring details(const Table& table,const Row& row,bool compact=false){
    std::wstring result;
    auto order=columns(table,true);
    if(!table.columns.empty() && table.columns[0]==L"软件")order={8,7,1,2,3,4,5,6,9,10,11,12,13};
    if(table.columns.size()==11 && table.columns[0]==L"记录时间")order={4,5,6,0,1,2,3,7,8,9,10};
    if(table.columns.size()==10 && table.columns[0]==L"键 / 值名称")order={3,4,2,0,1,5,6,7,8,9};
    for(const int source:order){const auto i=static_cast<size_t>(source);if(i<row.size())result+=table.columns[i]+(compact?L"：  ":L"\r\n")+row[i]+(compact?L"\r\n":L"\r\n\r\n");}
    return result;
}
}
