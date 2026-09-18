#include "system_core.h"
#include <ws2tcpip.h>
#include <evntrace.h>
#include <evntcons.h>
#include <array>
#include <mutex>
#include <thread>
#include <algorithm>
#include <compare>

namespace systemdesk {
namespace {
constexpr GUID provider{0x7dd42a49,0x5329,0x4832,{0x8d,0xfd,0x43,0xd9,0x79,0x15,0x3a,0x88}};
struct Key {
    DWORD pid=0;bool v6=false,udp=false;std::array<BYTE,16> local{},remote{};USHORT localPort=0,remotePort=0;
    auto operator<=>(const Key&)const=default;
};
struct Parsed {Key key;DWORD size=0;bool receive=false,data=false,closed=false,connected=false;};
bool parse(const EVENT_RECORD& event,Parsed& out){
    if(event.EventHeader.ProviderId!=provider || event.EventHeader.EventDescriptor.Version!=0 || !event.UserData)return false;
    const auto id=event.EventHeader.EventDescriptor.Id;
    if(id!=10 && id!=11 && id!=12 && id!=13 && id!=15 && id!=26 && id!=27 && id!=28 && id!=29 && id!=31 && id!=42 && id!=43 && id!=58 && id!=59)return false;
    out.key.v6=(id>=26 && id<=31) || id>=58;out.key.udp=id>=42;
    out.receive=id==11 || id==27 || id==43 || id==59;out.closed=id==13 || id==29;out.connected=id==12 || id==15 || id==28 || id==31;
    out.data=!out.closed && !out.connected;
    const size_t width=out.key.v6?16:4;if(event.UserDataLength<8+width*2+4)return false;
    const auto* bytes=static_cast<const BYTE*>(event.UserData);memcpy(&out.key.pid,bytes,4);memcpy(&out.size,bytes+4,4);
    // Receive events describe source=remote and destination=local.
    memcpy(out.key.local.data(),bytes+8+(out.receive?0:width),width);memcpy(out.key.remote.data(),bytes+8+(out.receive?width:0),width);
    USHORT dest=0,source=0;memcpy(&dest,bytes+8+width*2,2);memcpy(&source,bytes+10+width*2,2);
    out.key.localPort=ntohs(out.receive?dest:source);out.key.remotePort=ntohs(out.receive?source:dest);return true;
}
std::wstring ip(const std::array<BYTE,16>& address,bool v6){wchar_t text[INET6_ADDRSTRLEN]{};InetNtopW(v6?AF_INET6:AF_INET,const_cast<BYTE*>(address.data()),text,INET6_ADDRSTRLEN);return text;}
}
struct NetworkTrace::Impl {
    struct Flow {uint64_t first=0,last=0,rx=0,tx=0,totalRx=0,totalTx=0,count=0;ULONGLONG seen=0;bool closed=false;std::wstring path,started;};
    std::mutex mutex;std::map<Key,Flow> flows;std::thread consumer;
    TRACEHANDLE session=0,trace=INVALID_PROCESSTRACE_HANDLE;std::wstring name;std::vector<BYTE> properties;
    std::atomic<DWORD> consumerStatus{ERROR_SUCCESS},lost{0};std::atomic<uint64_t> discarded{0};DWORD error=ERROR_SUCCESS;bool attempted=false;ULONGLONG lastSnapshot=0,captureStarted=0;
    EVENT_TRACE_PROPERTIES* props(){return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(properties.data());}
    static void WINAPI event(EVENT_RECORD* record){
        try{
        auto& self=*static_cast<Impl*>(record->UserContext);Parsed parsed;if(!parse(*record,parsed))return;
        const uint64_t when=static_cast<uint64_t>(record->EventHeader.TimeStamp.QuadPart);std::lock_guard lock(self.mutex);
        auto found=self.flows.find(parsed.key);
        if(found==self.flows.end()){if(self.flows.size()>=8192){++self.discarded;return;}found=self.flows.emplace(parsed.key,Flow{}).first;}
        auto& flow=found->second;
        if(parsed.connected && flow.closed)flow=Flow{};
        if(!flow.first)flow.first=when;flow.last=when;flow.seen=GetTickCount64();++flow.count;
        if(parsed.data){if(parsed.receive){flow.rx+=parsed.size;flow.totalRx+=parsed.size;}else{flow.tx+=parsed.size;flow.totalTx+=parsed.size;}}
        flow.closed=parsed.closed;
        }catch(...){++static_cast<Impl*>(record->UserContext)->discarded;}
    }
    static ULONG WINAPI buffer(EVENT_TRACE_LOGFILEW* logfile){auto* self=static_cast<Impl*>(logfile->Context);self->lost.store(logfile->EventsLost);return TRUE;}
    void stop(){
        if(session){ControlTraceW(session,name.c_str(),props(),EVENT_TRACE_CONTROL_STOP);session=0;}
        if(trace!=INVALID_PROCESSTRACE_HANDLE){CloseTrace(trace);trace=INVALID_PROCESSTRACE_HANDLE;}if(consumer.joinable())consumer.join();attempted=false;lastSnapshot=0;
    }
};
NetworkTrace::NetworkTrace():impl(std::make_unique<Impl>()){}
NetworkTrace::~NetworkTrace(){impl->stop();}
bool NetworkTrace::running()const{return impl->session && impl->consumerStatus.load()==ERROR_SUCCESS;}
DWORD NetworkTrace::start(){
    auto& self=*impl;if(self.attempted)return self.error;self.attempted=true;self.consumerStatus=ERROR_SUCCESS;
    self.name=L"PicoPet.Network."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
    self.properties.assign(sizeof(EVENT_TRACE_PROPERTIES)+(self.name.size()+1)*sizeof(wchar_t),0);
    auto* props=self.props();props->Wnode.BufferSize=static_cast<ULONG>(self.properties.size());props->Wnode.Flags=WNODE_FLAG_TRACED_GUID;props->Wnode.ClientContext=1;
    props->LogFileMode=EVENT_TRACE_REAL_TIME_MODE;props->BufferSize=64;props->MinimumBuffers=4;props->MaximumBuffers=16;props->FlushTimer=1;props->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);memcpy(self.properties.data()+props->LoggerNameOffset,self.name.c_str(),(self.name.size()+1)*sizeof(wchar_t));
    self.error=StartTraceW(&self.session,self.name.c_str(),props);if(self.error!=ERROR_SUCCESS){self.session=0;return self.error;}
    self.error=EnableTraceEx2(self.session,&provider,EVENT_CONTROL_CODE_ENABLE_PROVIDER,TRACE_LEVEL_INFORMATION,0x30,0,0,nullptr);
    if(self.error!=ERROR_SUCCESS){self.stop();self.attempted=true;return self.error;}
    EVENT_TRACE_LOGFILEW logfile{};logfile.LoggerName=self.name.data();logfile.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback=Impl::event;logfile.BufferCallback=Impl::buffer;logfile.Context=&self;self.trace=OpenTraceW(&logfile);
    if(self.trace==INVALID_PROCESSTRACE_HANDLE){self.error=GetLastError();self.stop();self.attempted=true;return self.error;}
    const TRACEHANDLE trace=self.trace;self.captureStarted=self.lastSnapshot=GetTickCount64();
    {std::lock_guard lock(self.mutex);for(auto& [key,flow]:self.flows){(void)key;flow.rx=flow.tx=0;}}
    try{self.consumer=std::thread([&self,trace]{SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);TRACEHANDLE input=trace;const auto status=ProcessTrace(&input,1,nullptr,nullptr);self.consumerStatus.store(status==ERROR_SUCCESS?ERROR_CANCELLED:status);});}catch(...){self.stop();throw;}
    return ERROR_SUCCESS;
}
void NetworkTrace::stop(){impl->stop();}
Table NetworkTrace::snapshot(){
    auto& self=*impl;const auto now=GetTickCount64();const auto elapsed=self.lastSnapshot?now-self.lastSnapshot:0;self.lastSnapshot=now;
    Table table{{L"进程",L"PID",L"协议",L"通信范围",L"本机地址",L"本机端口",L"本机名称 / PTR",L"远端地址",L"远端端口",L"远端 PTR 域名",L"接收速率",L"发送速率",L"测速状态",L"连接状态",L"本机地址类型",L"程序路径",L"进程启动标识",L"首次可见",L"最后可见",L"观测状态",L"捕获接收字节",L"捕获发送字节",L"事件数"},{},L""};
    const DWORD failure=self.error?self.error:self.consumerStatus.load();
    table.summary=running()?L"事件捕获已启用 · TCP / UDP / IPv4 / IPv6；Windows 通常约 1 秒批量交付，短连接无需持续到下一次轮询。":failure==ERROR_ACCESS_DENIED?L"事件捕获需要管理员权限；当前仍可使用普通连接表。":L"事件捕获未运行，Windows 错误 "+std::to_wstring(failure)+L"。";
    std::vector<std::pair<Key,Impl::Flow>> snapshot;uint64_t discarded=0;
    {std::lock_guard lock(self.mutex);std::erase_if(self.flows,[&](const auto& entry){return now-entry.second.seen>600000;});
        snapshot.reserve(self.flows.size());for(auto& [key,flow]:self.flows){snapshot.emplace_back(key,flow);flow.rx=flow.tx=0;}discarded=self.discarded.load();
    }
    std::map<DWORD,std::pair<std::wstring,uint64_t>> processes;
    for(const auto& [key,flow]:snapshot){
        std::wstring path=flow.path,started=flow.started;
        {
            auto found=processes.find(key.pid);if(found==processes.end()){
                std::wstring processPath;uint64_t creation=0;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,key.pid);
                if(process){FILETIME created{},exited{},kernel{},user{};if(GetProcessTimes(process,&created,&exited,&kernel,&user))creation=(static_cast<uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
                    wchar_t value[32768]{};DWORD length=32768;if(QueryFullProcessImageNameW(process,0,value,&length))processPath=value;CloseHandle(process);}
                found=processes.emplace(key.pid,std::make_pair(processPath,creation)).first;
            }
            if(found->second.second && found->second.second<=flow.first){path=found->second.first;started=std::to_wstring(found->second.second);}
            else if(found->second.second>flow.first && found->second.second<=flow.last){path.clear();started.clear();}
            if(!path.empty()){std::lock_guard lock(self.mutex);auto foundFlow=self.flows.find(key);if(foundFlow!=self.flows.end() && foundFlow->second.first==flow.first){foundFlow->second.path=path;foundFlow->second.started=started;}}
        }
        const auto local=ip(key.local,key.v6),remote=ip(key.remote,key.v6);uint64_t rx=0,tx=0;const bool measured=running() && elapsed && flow.seen>=self.captureStarted && now-flow.seen<2500;
        if(measured){counterRate(flow.rx,0,elapsed,rx);counterRate(flow.tx,0,elapsed,tx);}
        const auto state=flow.closed?L"已结束（事件）":!running()?L"采集已停止":flow.seen>=self.captureStarted && now-flow.seen<2500?L"近期有事件":L"保留记录";
        table.rows.push_back({path.empty()?L"PID "+std::to_wstring(key.pid):std::filesystem::path(path).filename().wstring(),std::to_wstring(key.pid),std::wstring(key.udp?L"UDP":L"TCP")+(key.v6?L"v6":L"v4"),addressScope(remote),local,std::to_wstring(key.localPort),L"事件地址",remote,std::to_wstring(key.remotePort),L"未反查（事件原始地址）",measured?bytes(rx)+L"/s":L"--",measured?bytes(tx)+L"/s":L"--",L"ETW 事件批次字节 / 交付间隔；不是瞬时线速",state,addressScope(local),path.empty()?L"进程已退出或路径不可读":path,started.empty()?L"未确认；事件首见 "+std::to_wstring(flow.first):started,timestamp(flow.first),timestamp(flow.last),state,std::to_wstring(flow.totalRx),std::to_wstring(flow.totalTx),std::to_wstring(flow.count)});
    }
    std::stable_sort(table.rows.begin(),table.rows.end(),[](const Row& a,const Row& b){return a[18]>b[18];});
    table.summary+=L" 保留最近 10 分钟，最多 8192 个通信对象；捕获地址、端口与字节数，不读取正文。";
    if(self.lost.load() || discarded)table.summary+=L" 采集不完整：系统丢失 "+std::to_wstring(self.lost.load())+L"，容量丢弃 "+std::to_wstring(discarded)+L"。";return table;
}
bool testNetworkTraceParser(){
    std::array<BYTE,44> payload{};DWORD pid=42,size=1024;memcpy(payload.data(),&pid,4);memcpy(payload.data()+4,&size,4);
    payload[8]=127;payload[11]=1;payload[12]=10;payload[15]=2;USHORT destination=htons(443),source=htons(50123);memcpy(payload.data()+16,&destination,2);memcpy(payload.data()+18,&source,2);
    EVENT_RECORD event{};event.EventHeader.ProviderId=provider;event.EventHeader.EventDescriptor.Id=11;event.UserData=payload.data();event.UserDataLength=20;Parsed receive;
    if(!parse(event,receive) || receive.key.pid!=42 || receive.size!=1024 || receive.key.localPort!=443 || receive.key.remotePort!=50123 || receive.key.local[0]!=127 || !receive.receive)return false;
    event.UserDataLength=19;Parsed shortEvent;if(parse(event,shortEvent))return false;
    event.UserDataLength=44;event.EventHeader.EventDescriptor.Id=58;Parsed udp;if(!parse(event,udp) || !udp.key.v6 || !udp.key.udp || udp.receive)return false;
    event.EventHeader.EventDescriptor.Version=1;Parsed unknown;return !parse(event,unknown);
}
}
