#pragma once
#include "system_presentation.h"
#include <algorithm>
#include <set>
#include <cwctype>

namespace systemdesk {
inline std::wstring softwareKey(const std::wstring& path,const std::wstring& pid){
    std::wstring key=path.find(L":\\")!=std::wstring::npos?path:L"PID:"+pid;
    std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return key;
}
inline std::wstring connectionKey(const Row& row){
    std::wstring key;for(size_t i:{1u,2u,4u,5u,7u,8u,15u,16u})if(i<row.size()){key+=row[i];key+=L"|";}return key;
}
class ProcessObservations {
    struct Entry {Row row;ULONGLONG last=0;std::wstring time;};
    std::map<std::wstring,Entry> entries;
public:
    static bool recent(const Row& row){return row.size()>6 && row[6]==L"本轮未见（保留 60 秒）";}
    Table observe(const Table& current,ULONGLONG now,const std::wstring& time){
        if(current.columns.size()<15)return current;
        std::set<std::wstring> seen;
        for(const auto& row:current.rows)if(row.size()>=15){seen.insert(row[14]);entries[row[14]]={row,now,time};}
        std::erase_if(entries,[&](const auto& entry){return !seen.contains(entry.first) && now-entry.second.last>=60000;});
        // Bound retained history independently of the number of currently running processes.
        std::vector<std::pair<ULONGLONG,std::wstring>> inactive;
        for(const auto& [key,e]:entries)if(!seen.contains(key))inactive.emplace_back(e.last,key);
        if(inactive.size()>4096){std::sort(inactive.begin(),inactive.end());for(size_t i=0;i<inactive.size()-4096;++i)entries.erase(inactive[i].second);}
        Table result=current;
        for(const auto& [key,e]:entries)if(!seen.contains(key)){
            Row row=e.row;row[6]=L"本轮未见（保留 60 秒）";
            for(size_t i:{2u,3u,4u,5u,7u,8u,9u})row[i]=L"--";
            row[13]=L"最后可见："+e.time+L"。本轮未见，可能已退出；当前资源占用不可用。\r\n"+row[13];
            result.rows.push_back(std::move(row));
        }
        result.summary+=L" 最近未见的进程保留 60 秒，当前资源占用不沿用旧值。";return result;
    }
};
// Bounded session history. Missing rows are only marked ended after a complete sample.
class NetworkObservations {
    struct Entry {Row row;std::wstring first,last,state;uint64_t order=0;ULONGLONG seenAt=0;};
    std::map<std::wstring,Entry> entries;
    uint64_t serial=0,discarded=0;
    bool gap=false;
public:
    static constexpr size_t capacity=20000;
    void interrupt(){gap=true;for(auto& [key,e]:entries){(void)key;if(e.state==L"当前可见")e.state=L"采样间断，状态待确认";}}
    void observe(const Table& table,bool complete,const std::wstring& time,ULONGLONG now=GetTickCount64()){
        std::set<std::wstring> seen;
        for(const auto& row:table.rows){if(row.size()<17)continue;const auto key=connectionKey(row);seen.insert(key);
            auto [it,added]=entries.try_emplace(key);auto& e=it->second;if(added){e.first=time;e.order=++serial;}e.row=row;e.last=time;e.state=L"当前可见";e.seenAt=now;}
        if(complete){for(auto& [key,e]:entries)if(!seen.contains(key) && e.state!=L"已结束 / 不再可见"){
            if(e.state==L"当前可见" && !gap)e.state=L"已结束 / 不再可见";else e.state=L"采样间断，状态待确认";}
            gap=false;}else for(auto& [key,e]:entries)if(!seen.contains(key) && e.state==L"当前可见")e.state=L"本轮读取不完整，状态待确认";
        if(entries.size()>capacity){
            struct Candidate {decltype(entries)::iterator entry;bool current;};
            std::vector<Candidate> eviction;eviction.reserve(entries.size());
            for(auto it=entries.begin();it!=entries.end();++it)eviction.push_back({it,seen.contains(it->first)});
            std::sort(eviction.begin(),eviction.end(),[](const Candidate& a,const Candidate& b){return a.current!=b.current?!a.current:a.entry->second.order<b.entry->second.order;});
            const size_t excess=entries.size()-capacity;for(size_t i=0;i<excess;++i)entries.erase(eviction[i].entry);discarded+=excess;
        }
    }
    Table history(const Table& current,bool recent=false,ULONGLONG now=GetTickCount64())const{
        Table out{current.columns,{},L"本次运行保留最近 20,000 个观测端点；不按秒删除。重启后清空，可导出 CSV。首次/最后可见是采样时间，不是精确连接起止时间。"};
        out.columns.insert(out.columns.end(),{L"首次可见",L"最后可见",L"观测状态"});
        std::vector<const Entry*> order;for(const auto& [key,e]:entries){(void)key;order.push_back(&e);}
        std::sort(order.begin(),order.end(),[](const Entry* a,const Entry* b){return a->order<b->order;});
        for(const auto* e:order){if(recent && e->state!=L"当前可见" && now-e->seenAt>=60000)continue;auto row=e->row;if(e->state!=L"当前可见"){row[10]=row[11]=L"--";row[12]=L"历史记录：当前速率不可用";}row.insert(row.end(),{e->first,e->last,e->state});out.rows.push_back(std::move(row));}
        if(recent)out.summary=L"显示当前连接与最近 60 秒内结束或待确认的连接；更早记录保存在连接历史。";
        if(discarded)out.summary+=L" 已淘汰最早 "+std::to_wstring(discarded)+L" 项。";return out;
    }
    Table applications(const Table& current,bool complete)const{
        struct App {std::wstring name,path,last;std::set<std::wstring> pids,destinations,ports;size_t endpoints=0,publicCount=0,local=0,lan=0,udp=0,listening=0,established=0,measured=0,history=0;uint64_t rx=0,tx=0;std::set<std::wstring> limits;};
        std::map<std::wstring,App> apps;
        for(const auto& [key,e]:entries){(void)key;const auto& r=e.row;auto& a=apps[softwareKey(r[15],r[1])];a.name=r[0];a.path=r[15];a.last=std::max(a.last,e.last);++a.history;}
        for(const auto& r:current.rows){if(r.size()<17)continue;auto& a=apps[softwareKey(r[15],r[1])];a.name=r[0];a.path=r[15];a.pids.insert(r[1]);++a.endpoints;
            if(r[3]==L"公网地址")++a.publicCount;else if(r[3]==L"本机回环" || r[3]==L"本机接口")++a.local;else if(r[3]==L"局域网 / 私有地址")++a.lan;
            if(r[2].find(L"UDP")==0)++a.udp;if(r[13]==L"监听")++a.listening;if(r[13].find(L"已连接")==0)++a.established;
            if(r[12]==L"TCP EStats 字节增量"){++a.measured;a.rx+=parseRate(r[10]);a.tx+=parseRate(r[11]);}else a.limits.insert(r[12]);
            if(r[7]!=L"未提供" && r[7]!=L"0.0.0.0" && r[7]!=L"::")a.destinations.insert(r[7]+L":"+r[8]+L"  "+r[9]);
            if(r[8]!=L"--" && r[8]!=L"0")a.ports.insert(r[8]);
        }
        Table out{{L"软件",L"当前活动",L"下载（TCP）",L"上传（TCP）",L"通信概况",L"端点数",L"PID 列表",L"测速覆盖",L"归纳与建议",L"远端地址与 PTR",L"远端端口",L"最后可见",L"历史端点数",L"程序路径",L"软件标识"},{},L"按完整程序路径合并同一可执行文件的多个进程；不同路径不混合。双击软件或点击“只看此软件”持续浏览其连接、历史和日志。"};
        auto join=[](const std::set<std::wstring>& values){std::wstring result;for(const auto& v:values){if(!result.empty())result+=L"\r\n";result+=v;}return result.empty()?L"--":result;};
        for(const auto& [key,a]:apps){
            const auto scope=L"公网 "+std::to_wstring(a.publicCount)+L" · 本机 "+std::to_wstring(a.local)+L" · 局域网 "+std::to_wstring(a.lan);
            const auto coverage=std::to_wstring(a.measured)+L" / "+std::to_wstring(a.established)+L" 条已连接 TCP；UDP "+std::to_wstring(a.udp)+L" 条不覆盖";
            std::wstring summary=L"当前观测："+std::to_wstring(a.endpoints)+L" 个端点，"+std::to_wstring(a.pids.size())+L" 个进程。";
            if(a.publicCount)summary+=L"\r\n存在公网通信；仅凭地址不能判断泄露或攻击。";
            if(a.local)summary+=L"\r\n存在本机通信，可能是组件协作或代理；不能据此认定没有访问公网。";
            if(a.listening)summary+=L"\r\n有 "+std::to_wstring(a.listening)+L" 个监听端点；监听不等于外网可访问，需结合绑定地址与防火墙核对。";
            if(a.udp)summary+=L"\r\nUDP/QUIC 无远端与逐连接字节数据，不能还原完整流向。";
            if(a.measured<a.established)summary+=L"\r\n部分 TCP 未取得速率："+join(a.limits)+L"。可在采集设置检查逐连接测速。";
            if(!a.endpoints)summary+=L"\r\n本轮未看到端点；已保留历史，不代表软件已退出。";
            summary+=L"\r\n这是本地规则归纳。PTR 为 IP 反查名，不是请求 URL；加密内容不可见。";
            if(!complete)summary+=L"\r\n本轮采集不完整，数量和通信范围仅代表可读取部分。";
            const auto limits=join(a.limits);
            const auto unavailable=!a.endpoints?L"本轮无端点":limits.find(L"管理员")!=std::wstring::npos?L"需管理员权限":limits.find(L"未开启")!=std::wstring::npos?L"测速未开启":limits.find(L"基准")!=std::wstring::npos?L"采样中":!a.established?(a.udp?L"UDP 请看实时捕获":L"无传输连接"):L"连接计数不可读";
            out.rows.push_back({a.name,a.endpoints?(a.rx || a.tx?L"正在传输":L"端点可见"):L"本轮无端点",a.measured?bytes(a.rx)+L"/s":unavailable,a.measured?bytes(a.tx)+L"/s":unavailable,scope,std::to_wstring(a.endpoints),join(a.pids),coverage,summary,join(a.destinations),join(a.ports),a.last,std::to_wstring(a.history),a.path,key});
        }
        std::stable_sort(out.rows.begin(),out.rows.end(),[](const Row& a,const Row& b){const bool activeA=a[5]!=L"0",activeB=b[5]!=L"0";return activeA!=activeB?activeA:_wcsicmp(a[0].c_str(),b[0].c_str())<0;});return out;
    }
private:
    static uint64_t parseRate(const std::wstring& text){uint64_t value=0;presentation::byteRate(text,value);return value;}
};
}
