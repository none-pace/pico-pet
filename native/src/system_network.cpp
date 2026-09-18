#include "system_core.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tcpestats.h>
#include <windns.h>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <limits>

namespace systemdesk {
std::wstring addressScope(const std::wstring& address){
    IN_ADDR v4{};IN6_ADDR v6{};
    if(InetPtonW(AF_INET,address.c_str(),&v4)==1){
        const auto* b=reinterpret_cast<const BYTE*>(&v4);
        if(b[0]==0 && b[1]==0 && b[2]==0 && b[3]==0)return L"通配 / 未指定";
        if(b[0]==127)return L"本机回环";
        if(b[0]==10 || (b[0]==172 && b[1]>=16 && b[1]<=31) || (b[0]==192 && b[1]==168))return L"局域网 / 私有地址";
        if(b[0]==169 && b[1]==254)return L"链路本地";
        if(b[0]==100 && b[1]>=64 && b[1]<=127)return L"运营商共享地址";
        if(b[0]>=224 && b[0]<=239)return L"组播";
        if(b[0]==255 && b[1]==255 && b[2]==255 && b[3]==255)return L"广播";
        if(b[0]==0 || b[0]>=240 || (b[0]==192 && b[1]==0 && (b[2]==0 || b[2]==2)) ||
           (b[0]==198 && (b[1]==18 || b[1]==19 || (b[1]==51 && b[2]==100))) || (b[0]==203 && b[1]==0 && b[2]==113) ||
           (b[0]==192 && b[1]==88 && b[2]==99))return L"特殊 / 保留地址";
        return L"公网地址";
    }
    if(InetPtonW(AF_INET6,address.c_str(),&v6)==1){
        const auto* b=reinterpret_cast<const BYTE*>(&v6);
        if(IN6_IS_ADDR_UNSPECIFIED(&v6))return L"通配 / 未指定";
        if(IN6_IS_ADDR_LOOPBACK(&v6))return L"本机回环";
        if(IN6_IS_ADDR_V4MAPPED(&v6)){wchar_t text[INET_ADDRSTRLEN]{};InetNtopW(AF_INET,const_cast<BYTE*>(b+12),text,INET_ADDRSTRLEN);return addressScope(text);}
        if((b[0]&0xfe)==0xfc)return L"局域网 / 私有地址";
        if(b[0]==0xfe && (b[1]&0xc0)==0x80)return L"链路本地";
        if(b[0]==0xff)return L"组播";
        if((b[0]&0xe0)!=0x20 || (b[0]==0x20 && b[1]==1 && b[2]==0x0d && b[3]==0xb8) ||
           (b[0]==0x20 && b[1]==1 && b[2]<=1) || (b[0]==0x3f && b[1]==0xff && (b[2]&0xf0)==0))return L"特殊 / 保留地址";
        return L"公网地址";
    }
    return L"未知";
}
std::wstring reverseDnsName(const std::wstring& address){
    IN_ADDR v4{};IN6_ADDR v6{};
    if(InetPtonW(AF_INET,address.c_str(),&v4)==1){const auto* b=reinterpret_cast<const BYTE*>(&v4);return std::to_wstring(b[3])+L"."+std::to_wstring(b[2])+L"."+std::to_wstring(b[1])+L"."+std::to_wstring(b[0])+L".in-addr.arpa";}
    if(InetPtonW(AF_INET6,address.c_str(),&v6)!=1)return L"";
    const auto* b=reinterpret_cast<const BYTE*>(&v6);
    if(IN6_IS_ADDR_V4MAPPED(&v6)){wchar_t text[INET_ADDRSTRLEN]{};InetNtopW(AF_INET,const_cast<BYTE*>(b+12),text,INET_ADDRSTRLEN);return reverseDnsName(text);}
    std::wstring name;constexpr wchar_t hex[]=L"0123456789abcdef";
    for(int n=15;n>=0;--n){name+=hex[b[n]&15];name+=L'.';name+=hex[b[n]>>4];name+=L'.';}
    return name+L"ip6.arpa";
}
bool counterRate(uint64_t current,uint64_t previous,uint64_t elapsedMs,uint64_t& result){
    result=0;if(!elapsedMs || current<previous)return false;
    const long double value=static_cast<long double>(current-previous)*1000/elapsedMs;
    result=value>=static_cast<long double>(UINT64_MAX)?UINT64_MAX:static_cast<uint64_t>(value);return true;
}
namespace {
std::wstring hostIp(const SOCKADDR* address){
    wchar_t text[INET6_ADDRSTRLEN]{};
    const void* value=address->sa_family==AF_INET?static_cast<const void*>(&reinterpret_cast<const SOCKADDR_IN*>(address)->sin_addr):static_cast<const void*>(&reinterpret_cast<const SOCKADDR_IN6*>(address)->sin6_addr);
    if(address->sa_family!=AF_INET && address->sa_family!=AF_INET6)return L"";
    InetNtopW(address->sa_family,const_cast<void*>(value),text,INET6_ADDRSTRLEN);return text;
}
struct DnsLookup {
    std::wstring ip,name;
    DNS_QUERY_REQUEST request{};
    DNS_QUERY_RESULT result{DNS_QUERY_RESULTS_VERSION1};
    DNS_QUERY_CANCEL cancel{};
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    ULONGLONG started=GetTickCount64();
    bool pending=false,canceled=false,timedOut=false;
    static void WINAPI finished(void* context,DNS_QUERY_RESULT*){
        // The callback retains the query even if the panel has already been destroyed.
        std::unique_ptr<std::shared_ptr<DnsLookup>> owner(static_cast<std::shared_ptr<DnsLookup>*>(context));
        SetEvent((*owner)->done);
    }
    ~DnsLookup(){if(result.pQueryRecords)DnsRecordListFree(result.pQueryRecords,DnsFreeRecordList);if(done)CloseHandle(done);}
};
struct TcpCounter {
    MIB_TCPROW v4{};MIB_TCP6ROW v6{};
    int family=AF_INET;
    bool own=false;
    DWORD error=NO_ERROR;
    uint64_t received=0,sent=0,time=0,retryAt=0;
    void setRow(const Connection& c){
        family=c.family;
        if(family==AF_INET){v4.dwState=c.tcpStatus;InetPtonW(AF_INET,c.localIp.c_str(),&v4.dwLocalAddr);InetPtonW(AF_INET,c.remoteIp.c_str(),&v4.dwRemoteAddr);v4.dwLocalPort=htons(c.localPort);v4.dwRemotePort=htons(c.remotePort);}
        else {v6.State=static_cast<MIB_TCP_STATE>(c.tcpStatus);InetPtonW(AF_INET6,c.localIp.c_str(),&v6.LocalAddr);InetPtonW(AF_INET6,c.remoteIp.c_str(),&v6.RemoteAddr);v6.dwLocalPort=htons(c.localPort);v6.dwRemotePort=htons(c.remotePort);v6.dwLocalScopeId=c.localZone;v6.dwRemoteScopeId=c.remoteZone;}
    }
    DWORD read(TCP_ESTATS_DATA_RW_v0& rw,TCP_ESTATS_DATA_ROD_v0& data){
        auto* r=reinterpret_cast<PUCHAR>(&rw);auto* d=reinterpret_cast<PUCHAR>(&data);
        return family==AF_INET?GetPerTcpConnectionEStats(&v4,TcpConnectionEstatsData,r,0,sizeof(rw),nullptr,0,0,d,0,sizeof(data)):
            GetPerTcp6ConnectionEStats(&v6,TcpConnectionEstatsData,r,0,sizeof(rw),nullptr,0,0,d,0,sizeof(data));
    }
    DWORD enable(bool value){
        TCP_ESTATS_DATA_RW_v0 rw{static_cast<BOOLEAN>(value)};
        return family==AF_INET?SetPerTcpConnectionEStats(&v4,TcpConnectionEstatsData,reinterpret_cast<PUCHAR>(&rw),0,sizeof(rw),0):
            SetPerTcp6ConnectionEStats(&v6,TcpConnectionEstatsData,reinterpret_cast<PUCHAR>(&rw),0,sizeof(rw),0);
    }
    void stop(){if(own){enable(false);own=false;}}
};
}
struct NetworkDetails::Impl {
    struct Name {std::wstring text;ULONGLONG expires,used;};
    std::map<std::wstring,Name> names;
    std::vector<std::shared_ptr<DnsLookup>> lookups;
    std::set<std::wstring> localAddresses;
    std::map<std::wstring,TcpCounter> counters;
    ULONGLONG localTime=0;
    std::wstring computer;
    Impl(){wchar_t text[256]{};DWORD size=256;GetComputerNameW(text,&size);computer=text;}
    void cancelDns(){for(auto& q:lookups)if(q->pending && !q->canceled && WaitForSingleObject(q->done,0)!=WAIT_OBJECT_0){DnsCancelQuery(&q->cancel);q->canceled=true;}}
    void drain(){
        const auto now=GetTickCount64();
        for(auto it=lookups.begin();it!=lookups.end();){auto& q=**it;
            if(WaitForSingleObject(q.done,0)!=WAIT_OBJECT_0){if(!q.canceled && now-q.started>=2500){DnsCancelQuery(&q.cancel);q.canceled=true;q.timedOut=true;}++it;continue;}
            if(q.canceled && !q.timedOut){it=lookups.erase(it);continue;}
            std::wstring name;ULONGLONG lifetime=60000;
            if(q.result.QueryStatus==ERROR_SUCCESS)for(auto* record=q.result.pQueryRecords;record;record=record->pNext){if(record->wType==DNS_TYPE_PTR && record->Data.PTR.pNameHost){name=record->Data.PTR.pNameHost;lifetime=static_cast<ULONGLONG>(std::clamp<DWORD>(record->dwTtl,1,300))*1000;break;}}
            if(name.empty())name=q.canceled?L"反查超时 / 已取消":q.result.QueryStatus==DNS_ERROR_RCODE_NAME_ERROR || q.result.QueryStatus==DNS_INFO_NO_RECORDS?L"无 PTR 记录":L"反查不可用 ("+std::to_wstring(q.result.QueryStatus)+L")";
            if(names.size()>=256){auto oldest=std::min_element(names.begin(),names.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});names.erase(oldest);}
            names[q.ip]={name,now+lifetime,now};it=lookups.erase(it);
        }
    }
    void locals(){
        const auto now=GetTickCount64();if(localTime && now-localTime<10000)return;
        ULONG size=16384;std::vector<BYTE> buffer(size);ULONG result=ERROR_BUFFER_OVERFLOW;
        for(int n=0;n<3 && result==ERROR_BUFFER_OVERFLOW;++n){buffer.resize(size);result=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);}
        if(result==NO_ERROR){localAddresses.clear();for(auto* a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());a;a=a->Next)for(auto* u=a->FirstUnicastAddress;u;u=u->Next)localAddresses.insert(hostIp(u->Address.lpSockaddr));}
        localTime=now;
    }
    std::wstring scope(const std::wstring& ip){
        const auto kind=addressScope(ip);if(kind==L"本机回环")return kind;
        IN6_ADDR mapped{};
        if(InetPtonW(AF_INET6,ip.c_str(),&mapped)==1 && IN6_IS_ADDR_V4MAPPED(&mapped)){
            wchar_t v4[INET_ADDRSTRLEN]{};InetNtopW(AF_INET,reinterpret_cast<BYTE*>(&mapped)+12,v4,INET_ADDRSTRLEN);
            if(localAddresses.count(v4))return L"本机接口";
        }
        return localAddresses.count(ip)?L"本机接口":kind;
    }
    std::wstring dns(const std::wstring& ip,bool enabled){
        if(ip.empty())return L"未提供";
        const auto kind=scope(ip);
        if(kind==L"本机回环")return L"localhost（本机）";
        if(kind==L"本机接口")return computer+L"（本机名称）";
        if(kind==L"通配 / 未指定" || kind==L"组播" || kind==L"广播" || kind==L"特殊 / 保留地址" || kind==L"未知")return L"不适用";
        if(!enabled)return L"DNS 反查已关闭";
        const auto now=GetTickCount64();auto found=names.find(ip);
        if(found!=names.end() && found->second.expires>now){found->second.used=now;return found->second.text;}
        for(const auto& q:lookups)if(q->ip==ip)return L"反查中";
        if(lookups.size()>=8)return L"等待反查";
        auto q=std::make_shared<DnsLookup>();if(!q->done)return L"反查不可用";
        q->ip=ip;q->name=reverseDnsName(ip);q->request.Version=DNS_QUERY_REQUEST_VERSION1;q->request.QueryName=q->name.c_str();q->request.QueryType=DNS_TYPE_PTR;
        auto completion=std::make_unique<std::shared_ptr<DnsLookup>>(q);
        q->request.QueryOptions=DNS_QUERY_TREAT_AS_FQDN|DNS_QUERY_NO_MULTICAST;q->request.pQueryCompletionCallback=DnsLookup::finished;q->request.pQueryContext=completion.get();
        lookups.push_back(q);
        auto* callbackOwner=completion.release();
        const auto status=DnsQueryEx(&q->request,&q->result,&q->cancel);
        q->pending=status==DNS_REQUEST_PENDING;
        if(!q->pending){delete callbackOwner;q->result.QueryStatus=status;SetEvent(q->done);}
        return L"反查中";
    }
    void measure(Connection& c,bool enabled,ULONGLONG now){
        if(!c.tcp){c.statistics=L"UDP 不提供连接字节计数";return;}
        if(c.tcpStatus!=MIB_TCP_STATE_ESTAB){c.statistics=L"非已建立 TCP 连接";return;}
        if(!enabled){c.statistics=L"逐连接测速未开启";return;}
        auto found=counters.find(c.key);
        if(found==counters.end()){
            if(counters.size()>=1024){c.statistics=L"达到 1024 条测速上限";return;}
            found=counters.try_emplace(c.key).first;found->second.setRow(c);
            TCP_ESTATS_DATA_RW_v0 rw{};TCP_ESTATS_DATA_ROD_v0 data{};
            auto status=found->second.read(rw,data);
            if(status==NO_ERROR && !rw.EnableCollection){status=found->second.enable(true);found->second.own=status==NO_ERROR;}
            found->second.error=status;found->second.retryAt=now+5000;
        }
        auto& counter=found->second;counter.setRow(c);
        if(counter.error!=NO_ERROR && now>=counter.retryAt){TCP_ESTATS_DATA_RW_v0 rw{};TCP_ESTATS_DATA_ROD_v0 data{};auto status=counter.read(rw,data);
            if(status==NO_ERROR && !rw.EnableCollection){status=counter.enable(true);counter.own=status==NO_ERROR;}counter.error=status;counter.retryAt=now+5000;}
        if(counter.error!=NO_ERROR){c.statistics=counter.error==ERROR_ACCESS_DENIED?L"需管理员权限启用 EStats":L"EStats 不可用 ("+std::to_wstring(counter.error)+L")";return;}
        TCP_ESTATS_DATA_RW_v0 rw{};TCP_ESTATS_DATA_ROD_v0 data{};const auto status=counter.read(rw,data);
        if(status!=NO_ERROR || !rw.EnableCollection){c.statistics=L"计数不可用 / 连接已变化";counter.time=0;counter.error=status!=NO_ERROR?status:ERROR_RETRY;counter.retryAt=now+1000;return;}
        if(counter.time && counterRate(data.DataBytesIn,counter.received,now-counter.time,c.receiveRate) && counterRate(data.DataBytesOut,counter.sent,now-counter.time,c.sendRate)){
            c.measured=true;c.received=bytes(c.receiveRate)+L"/s";c.sent=bytes(c.sendRate)+L"/s";c.statistics=L"TCP EStats 字节增量";
        }else c.statistics=L"建立测速基准";
        counter.received=data.DataBytesIn;counter.sent=data.DataBytesOut;counter.time=now;
    }
};
NetworkDetails::NetworkDetails():impl(std::make_unique<Impl>()){}
NetworkDetails::~NetworkDetails(){
    stop();
}

bool testDnsLifetime(){
    auto query=std::make_shared<DnsLookup>();if(!query->done)return false;
    std::weak_ptr<DnsLookup> lifetime=query;
    auto* completion=new std::shared_ptr<DnsLookup>(query);
    query.reset();
    if(lifetime.expired()){delete completion;return false;}
    DnsLookup::finished(completion,nullptr);
    if(!lifetime.expired())return false;
    query=std::make_shared<DnsLookup>();if(!query->done)return false;
    lifetime=query;
    DnsLookup::finished(new std::shared_ptr<DnsLookup>(query),nullptr);
    if(WaitForSingleObject(query->done,0)!=WAIT_OBJECT_0)return false;
    query.reset();
    return lifetime.expired();
}
void NetworkDetails::stop(){impl->cancelDns();for(auto& [key,counter]:impl->counters){(void)key;counter.stop();}impl->counters.clear();}
void NetworkDetails::enrich(std::vector<Connection>& rows,bool dns,bool measure){
    if(!dns)impl->cancelDns();impl->drain();impl->locals();const auto now=GetTickCount64();
    std::set<std::wstring> active;for(const auto& c:rows)if(measure && c.tcp && c.tcpStatus==MIB_TCP_STATE_ESTAB)active.insert(c.key);
    for(auto it=impl->counters.begin();it!=impl->counters.end();)if(!active.count(it->first)){it->second.stop();it=impl->counters.erase(it);}else ++it;
    for(auto& c:rows){c.localScope=impl->scope(c.localIp);c.scope=c.remoteIp.empty()?L"远端未提供":impl->scope(c.remoteIp);
        c.localDns=impl->dns(c.localIp,dns);c.remoteDns=impl->dns(c.remoteIp,dns);impl->measure(c,measure,now);}
}
}
