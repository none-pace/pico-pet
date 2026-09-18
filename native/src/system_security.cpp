#include "system_core.h"
#include <tlhelp32.h>
#include <wscapi.h>
#include <tbs.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace systemdesk {
namespace {
struct ProcessRecord { std::wstring name,path; };

std::wstring nowText(){
    SYSTEMTIME time{};GetLocalTime(&time);wchar_t value[32]{};
    swprintf_s(value,L"%04u-%02u-%02u %02u:%02u:%02u",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    return value;
}
std::wstring lower(std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return value;}
bool startsWith(const std::wstring& value,const std::wstring& prefix){return !prefix.empty() && value.size()>=prefix.size() && _wcsnicmp(value.c_str(),prefix.c_str(),prefix.size())==0;}
std::wstring processPath(DWORD pid){
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return L"";
    std::array<wchar_t,32768> path{};DWORD size=static_cast<DWORD>(path.size());const bool ok=QueryFullProcessImageNameW(process,0,path.data(),&size)!=FALSE;CloseHandle(process);
    return ok?std::wstring(path.data(),size):L"";
}
std::unordered_map<DWORD,ProcessRecord> processes(const std::unordered_map<DWORD,ProcessRecord>* previous=nullptr){
    std::unordered_map<DWORD,ProcessRecord> result;HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return result;
    PROCESSENTRY32W entry{sizeof(entry)};if(Process32FirstW(snapshot,&entry))do{
        if(!entry.th32ProcessID)continue;
        const auto found=previous?previous->find(entry.th32ProcessID):std::unordered_map<DWORD,ProcessRecord>::const_iterator{};
        if(previous && found!=previous->end() && _wcsicmp(found->second.name.c_str(),entry.szExeFile)==0)result.emplace(entry.th32ProcessID,found->second);
        else result.emplace(entry.th32ProcessID,ProcessRecord{entry.szExeFile,processPath(entry.th32ProcessID)});
    }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return result;
}
std::pair<std::wstring,std::wstring> processAssessment(const ProcessRecord& process){
    if(process.path.empty())return {L"关注",L"程序路径不可读；可能是受保护或已经退出的进程，需结合 PID 与其他日志核对。"};
    const auto path=lower(process.path);std::array<wchar_t,32768> temporary{},profile{};GetTempPathW(static_cast<DWORD>(temporary.size()),temporary.data());GetEnvironmentVariableW(L"USERPROFILE",profile.data(),static_cast<DWORD>(profile.size()));
    const auto temp=lower(temporary.data()),downloads=lower(std::wstring(profile.data())+L"\\Downloads\\");
    if(startsWith(path,temp) || startsWith(path,downloads) || path.find(L"\\appdata\\local\\temp\\")!=std::wstring::npos)
        return {L"关注",L"程序从临时目录或下载目录启动；这并不等同于恶意，请核对来源、数字签名和启动原因。"};
    return {L"常规",L"记录到程序生命周期变化；路径位置本身未触发当前本地规则。"};
}
bool executableLike(const std::wstring& path){
    const auto extension=lower(std::filesystem::path(path).extension().wstring());
    static constexpr std::array<const wchar_t*,10> values={L".exe",L".dll",L".sys",L".msi",L".ps1",L".bat",L".cmd",L".vbs",L".js",L".lnk"};
    return std::find_if(values.begin(),values.end(),[&](const wchar_t* value){return extension==value;})!=values.end();
}
bool registryDword(HKEY hive,const wchar_t* path,const wchar_t* name,DWORD& value){DWORD size=sizeof(value);return RegGetValueW(hive,path,name,RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS;}
bool registryKey(HKEY hive,const wchar_t* path){HKEY key=nullptr;const auto result=RegOpenKeyExW(hive,path,0,KEY_READ,&key);if(key)RegCloseKey(key);return result==ERROR_SUCCESS;}
std::wstring securityHealth(DWORD provider,std::wstring& level,std::wstring& raw){
    WSC_SECURITY_PROVIDER_HEALTH health=WSC_SECURITY_PROVIDER_HEALTH_NOTMONITORED;const HRESULT result=WscGetSecurityProviderHealth(provider,&health);raw=L"HRESULT "+std::to_wstring(static_cast<unsigned long>(result));
    if(FAILED(result)){level=L"未知";return L"无法读取";}raw+=L"；Health "+std::to_wstring(static_cast<int>(health));
    if(health==WSC_SECURITY_PROVIDER_HEALTH_GOOD){level=L"正常";return L"已开启";}
    level=L"关注";if(health==WSC_SECURITY_PROVIDER_HEALTH_SNOOZE)return L"已暂停";if(health==WSC_SECURITY_PROVIDER_HEALTH_POOR)return L"需要处理";return L"未受监测";
}
}

Table securityOverview(){
    Table table{{L"安全项目",L"当前状态",L"级别",L"建议",L"数据来源",L"原始状态"},{},L""};
    auto add=[&](std::wstring item,std::wstring state,std::wstring level,std::wstring advice,std::wstring source,std::wstring raw){table.rows.push_back({std::move(item),std::move(state),std::move(level),std::move(advice),std::move(source),std::move(raw)});};
    std::wstring level,raw;auto health=securityHealth(WSC_SECURITY_PROVIDER_ANTIVIRUS,level,raw);add(L"防病毒保护",health,level,level==L"正常"?L"Windows 安全中心报告防病毒产品处于良好状态。":L"打开 Windows 安全中心确认防病毒产品与实时保护状态。",L"Windows Security Center API",raw);
    health=securityHealth(WSC_SECURITY_PROVIDER_FIREWALL,level,raw);add(L"防火墙",health,level,level==L"正常"?L"Windows 安全中心报告防火墙处于良好状态。":L"检查当前网络配置文件的防火墙状态和近期策略变化。",L"Windows Security Center API",raw);
    DWORD value=0;if(registryDword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",L"UEFISecureBootEnabled",value))add(L"Secure Boot",value?L"已启用":L"未启用",value?L"正常":L"关注",value?L"启动链已启用固件安全启动。":L"确认设备是否支持 UEFI Secure Boot，并核对关闭原因。",L"Windows Secure Boot 状态",std::to_wstring(value));else add(L"Secure Boot",L"无法确认",L"未知",L"设备可能使用传统启动模式，或当前读取不到固件状态。",L"Windows Secure Boot 状态",errorText(GetLastError()));
    TPM_DEVICE_INFO tpm{};tpm.structVersion=1;const auto tpmResult=Tbsi_GetDeviceInfo(sizeof(tpm),&tpm);if(tpmResult==TBS_SUCCESS)add(L"TPM",tpm.tpmVersion==TPM_VERSION_20?L"TPM 2.0 可用":L"TPM 可用",L"正常",L"可信平台模块可供 Windows 安全功能使用。",L"TPM Base Services",L"接口 "+std::to_wstring(tpm.tpmInterfaceType)+L"；实现版本 "+std::to_wstring(tpm.tpmImpRevision));else add(L"TPM",L"不可用或未就绪",L"关注",L"在 Windows 安全中心或固件设置中核对安全处理器状态。",L"TPM Base Services",L"TBS "+std::to_wstring(tpmResult));
    if(registryDword(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",L"EnableLUA",value))add(L"用户账户控制 UAC",value?L"已启用":L"已关闭",value?L"正常":L"关注",value?L"需要提升权限的操作会经过 UAC。":L"关闭 UAC 会削弱权限边界，建议核对是否为预期配置。",L"Windows 系统策略",std::to_wstring(value));else add(L"用户账户控制 UAC",L"无法读取",L"未知",L"打开 Windows 安全设置核对账户控制。",L"Windows 系统策略",L"未提供");
    if(registryDword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",L"Enabled",value))add(L"内存完整性",value?L"已启用":L"未启用",value?L"正常":L"提示",value?L"基于虚拟化的代码完整性已配置。":L"可在 Windows 安全中心查看设备兼容性后决定是否启用。",L"Device Guard 配置",std::to_wstring(value));else add(L"内存完整性",L"未配置或无法读取",L"提示",L"可在 Windows 安全中心的核心隔离页面核对实际状态。",L"Device Guard 配置",L"未提供");
    const bool cbs=registryKey(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending"),update=registryKey(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired");
    add(L"系统待重启",cbs||update?L"检测到待重启标记":L"未发现待重启标记",cbs||update?L"提示":L"正常",cbs||update?L"保存工作后安排重启，使系统组件或更新完成提交。":L"当前未读取到常见的组件更新待重启标记。",L"Windows 组件与更新状态",L"CBS="+std::to_wstring(cbs)+L"；Update="+std::to_wstring(update));
    const auto attention=std::count_if(table.rows.begin(),table.rows.end(),[](const Row& row){return row.size()>2 && (row[2]==L"关注" || row[2]==L"未知");});
    table.summary=L"本次读取 "+std::to_wstring(table.rows.size())+L" 项，其中需要核对或无法确认 "+std::to_wstring(attention)+L" 项。每项独立展示来源，不生成综合安全分数；当前状态不能证明系统未被篡改。";return table;
}

struct SecurityTracker::Impl {
    mutable std::mutex mutex;
    std::deque<Row> events;
    std::thread worker;
    std::atomic_bool running=false,stopping=false;
    std::atomic<uint64_t> revision=0;
    HANDLE stopEvent=nullptr,directory=INVALID_HANDLE_VALUE;
    std::wstring target,root,fileOnly,status=L"追踪尚未启动。";
    uint64_t dropped=0,lastEventTick=0;

    void add(Row row){
        const uint64_t tick=GetTickCount64();std::lock_guard lock(mutex);
        if(!events.empty() && tick-lastEventTick<250 && row.size()>4 && events.front().size()>4 && row[2]==events.front()[2] && row[3]==events.front()[3] && row[4]==events.front()[4]){events.front()[0]=row[0];return;}
        lastEventTick=tick;events.push_front(std::move(row));if(events.size()>2000){events.pop_back();++dropped;}++revision;
    }
    void addProcess(const wchar_t* action,DWORD pid,const ProcessRecord& process){
        const auto assessment=processAssessment(process);
        add({nowText(),assessment.first,L"程序",action,process.name,std::to_wstring(pid),process.path,L"--",assessment.second,L"Windows 进程快照"});
    }
    void addFile(DWORD action,const std::wstring& relative){
        if(!fileOnly.empty() && _wcsicmp(relative.c_str(),fileOnly.c_str())!=0)return;
        const wchar_t* event=L"变化";switch(action){case FILE_ACTION_ADDED:event=L"新增";break;case FILE_ACTION_REMOVED:event=L"删除";break;case FILE_ACTION_MODIFIED:event=L"修改";break;case FILE_ACTION_RENAMED_OLD_NAME:event=L"重命名前";break;case FILE_ACTION_RENAMED_NEW_NAME:event=L"重命名后";break;}
        const auto full=(std::filesystem::path(root)/relative).lexically_normal().wstring();const bool attention=executableLike(full);
        add({nowText(),attention?L"关注":L"变更",L"文件",event,full,L"--",L"--",target,attention?L"程序、脚本、驱动或快捷方式发生变化，请核对是否符合预期。":L"文件系统报告了元数据或内容变化。",L"目录通知不提供操作进程，无法可靠归因"});
    }
    void run(){
        SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);
        auto prior=processes();ULONGLONG lastProcesses=GetTickCount64();
        HANDLE changeEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);OVERLAPPED overlapped{};overlapped.hEvent=changeEvent;
        std::array<BYTE,65536> buffer{};bool pending=false;
        const auto issue=[&]{
            if(directory==INVALID_HANDLE_VALUE || !changeEvent)return false;
            ResetEvent(changeEvent);DWORD ignored=0;
            return ReadDirectoryChangesW(directory,buffer.data(),static_cast<DWORD>(buffer.size()),TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_CREATION|FILE_NOTIFY_CHANGE_ATTRIBUTES,
                &ignored,&overlapped,nullptr)!=FALSE;
        };
        pending=issue();if(directory!=INVALID_HANDLE_VALUE && !pending)add({nowText(),L"关注",L"文件",L"监控中断",target,L"--",L"--",target,L"无法启动目录通知："+errorText(GetLastError()),L"Windows 目录通知"});
        while(!stopping){
            HANDLE waits[2]={stopEvent,changeEvent};const DWORD count=pending?2:1;const DWORD wait=WaitForMultipleObjects(count,waits,FALSE,750);
            if(wait==WAIT_OBJECT_0)break;
            if(pending && wait==WAIT_OBJECT_0+1){
                DWORD transferred=0;pending=false;const BOOL completed=GetOverlappedResult(directory,&overlapped,&transferred,FALSE);
                if(completed && transferred){
                    size_t offset=0;while(offset<transferred){
                        const auto* info=reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data()+offset);
                        addFile(info->Action,std::wstring(info->FileName,info->FileNameLength/sizeof(wchar_t)));
                        if(!info->NextEntryOffset)break;offset+=info->NextEntryOffset;
                    }
                }else if(!stopping){const DWORD error=GetLastError();add({nowText(),L"关注",L"文件",completed?L"事件过多":L"监控中断",target,L"--",L"--",target,completed?L"目录变化速度超过通知缓冲区，部分事件可能未记录；建议缩小监控目录。":L"目录通知读取失败："+errorText(error),L"Windows 目录通知"});}
                if(!stopping)pending=issue();
            }
            const ULONGLONG now=GetTickCount64();if(now-lastProcesses>=1000){
                auto current=processes(&prior);
                for(const auto& [pid,record]:current)if(!prior.contains(pid))addProcess(L"启动",pid,record);
                for(const auto& [pid,record]:prior)if(!current.contains(pid))addProcess(L"退出",pid,record);
                prior=std::move(current);lastProcesses=now;
            }
        }
        if(pending){CancelIoEx(directory,&overlapped);WaitForSingleObject(changeEvent,1000);DWORD ignored=0;GetOverlappedResult(directory,&overlapped,&ignored,FALSE);}if(changeEvent)CloseHandle(changeEvent);
        SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_END);running=false;++revision;
    }
};

SecurityTracker::SecurityTracker():impl(std::make_unique<Impl>()){}
SecurityTracker::~SecurityTracker(){stop();}
std::wstring SecurityTracker::start(const std::wstring& requested){
    stop();impl->stopping=false;impl->target=requested.empty()?L"仅程序":std::filesystem::absolute(requested).lexically_normal().wstring();impl->root.clear();impl->fileOnly.clear();
    if(!requested.empty()){
        const DWORD attributes=GetFileAttributesW(impl->target.c_str());if(attributes==INVALID_FILE_ATTRIBUTES)return L"目标不可访问："+errorText(GetLastError());
        if(attributes&FILE_ATTRIBUTE_DIRECTORY)impl->root=impl->target;else{const std::filesystem::path file=impl->target;impl->root=file.parent_path().wstring();impl->fileOnly=file.filename().wstring();}
        impl->directory=CreateFileW(impl->root.c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,nullptr);
        if(impl->directory==INVALID_HANDLE_VALUE)return L"无法监控目标："+errorText(GetLastError());
    }
    impl->stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!impl->stopEvent){if(impl->directory!=INVALID_HANDLE_VALUE){CloseHandle(impl->directory);impl->directory=INVALID_HANDLE_VALUE;}return L"无法创建追踪任务："+errorText(GetLastError());}
    {std::lock_guard lock(impl->mutex);impl->status=requested.empty()?L"正在追踪程序启动与退出。":L"正在追踪程序和目标文件变化："+impl->target;}
    impl->running=true;++impl->revision;impl->worker=std::thread([this]{impl->run();});return L"";
}
void SecurityTracker::stop(){
    if(!impl)return;impl->stopping=true;if(impl->stopEvent)SetEvent(impl->stopEvent);if(impl->directory!=INVALID_HANDLE_VALUE)CancelIoEx(impl->directory,nullptr);if(impl->worker.joinable())impl->worker.join();
    if(impl->directory!=INVALID_HANDLE_VALUE){CloseHandle(impl->directory);impl->directory=INVALID_HANDLE_VALUE;}if(impl->stopEvent){CloseHandle(impl->stopEvent);impl->stopEvent=nullptr;}
    if(impl->running.exchange(false))++impl->revision;std::lock_guard lock(impl->mutex);if(!impl->target.empty())impl->status=L"追踪已停止；现有记录保留，可筛选或导出。";
}
void SecurityTracker::clear(){std::lock_guard lock(impl->mutex);impl->events.clear();impl->dropped=0;++impl->revision;}
bool SecurityTracker::active()const{return impl->running.load();}
uint64_t SecurityTracker::generation()const{return impl->revision.load();}
Table SecurityTracker::table()const{
    Table result{{L"记录时间",L"风险",L"类别",L"事件",L"对象",L"PID",L"程序路径",L"监控目标",L"说明",L"归因可信度"},{},L""};std::lock_guard lock(impl->mutex);
    result.rows.assign(impl->events.begin(),impl->events.end());result.summary=impl->status+L" 最近记录最多保留 2000 条，已丢弃 "+std::to_wstring(impl->dropped)+L" 条。文件通知不读取内容，也不提供操作进程；程序风险标签仅是本地路径规则，不是恶意判定。";return result;
}
}
