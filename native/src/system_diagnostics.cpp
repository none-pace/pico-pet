#include "system_core.h"
#include <winevt.h>
#include <algorithm>
#include <array>
#include <map>
#include <sstream>
#include <iomanip>
#include <tuple>

namespace systemdesk {
namespace {
std::wstring lower(std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return value;}
bool contains(const Row& row,const std::wstring& needle){if(needle.empty())return true;for(const auto& cell:row)if(lower(cell).find(needle)!=std::wstring::npos)return true;return false;}
bool appendBounded(Table& table,Row row,size_t& characters){
    size_t count=0;for(const auto& cell:row)count+=cell.size();
    if(count>4*1024*1024-characters)return false;
    characters+=count;table.rows.push_back(std::move(row));return true;
}
struct EventHandle {
    EVT_HANDLE value=nullptr;
    EventHandle()=default;explicit EventHandle(EVT_HANDLE h):value(h){}
    ~EventHandle(){if(value)EvtClose(value);}
    EventHandle(const EventHandle&)=delete;EventHandle& operator=(const EventHandle&)=delete;
};
std::wstring renderXml(EVT_HANDLE event){
    DWORD bytesNeeded=0,count=0;EvtRender(nullptr,event,EvtRenderEventXml,0,nullptr,&bytesNeeded,&count);
    if(!bytesNeeded || bytesNeeded>65536)return L"XML 未提供或超过 64 KiB 显示上限";
    std::vector<wchar_t> data(bytesNeeded/sizeof(wchar_t)+1,0);
    if(!EvtRender(nullptr,event,EvtRenderEventXml,bytesNeeded,data.data(),&bytesNeeded,&count))return errorText(GetLastError());return data.data();
}
std::wstring eventMessage(EVT_HANDLE metadata,EVT_HANDLE event){
    if(!metadata)return L"此事件的消息资源不可用，可查看原始 XML 字段。";
    DWORD needed=0;EvtFormatMessage(metadata,event,0,0,nullptr,EvtFormatMessageEvent,0,nullptr,&needed);
    if(!needed || needed>32768)return L"消息未提供或超过 32K 字符显示上限，可查看原始 XML。";
    std::vector<wchar_t> data(static_cast<size_t>(needed)+1,0);
    if(!EvtFormatMessage(metadata,event,0,0,nullptr,EvtFormatMessageEvent,needed,data.data(),&needed))return L"消息格式化失败："+errorText(GetLastError());return data.data();
}
struct RegistryPath {HKEY hive=nullptr;std::wstring root,subkey,error;};
RegistryPath parseRegistry(std::wstring path){
    while(!path.empty() && (path.back()==L'\\' || path.back()==L' '))path.pop_back();
    const auto delimiter=path.find(L'\\');const auto root=lower(path.substr(0,delimiter));
    RegistryPath result;result.subkey=delimiter==std::wstring::npos?L"":path.substr(delimiter+1);
    for(const auto& [shortName,longName,hive]:std::array<std::tuple<const wchar_t*,const wchar_t*,HKEY>,5>{{
        {L"hkcu",L"hkey_current_user",HKEY_CURRENT_USER},{L"hklm",L"hkey_local_machine",HKEY_LOCAL_MACHINE},
        {L"hkcr",L"hkey_classes_root",HKEY_CLASSES_ROOT},{L"hku",L"hkey_users",HKEY_USERS},{L"hkcc",L"hkey_current_config",HKEY_CURRENT_CONFIG}}}){
        if(root==shortName || root==longName){result.hive=hive;result.root=longName;std::transform(result.root.begin(),result.root.end(),result.root.begin(),[](wchar_t c){return static_cast<wchar_t>(towupper(c));});break;}
    }
    if(!result.hive)result.error=L"路径需以 HKCU、HKLM、HKCR、HKU 或 HKCC 开头。";return result;
}
std::wstring valueType(DWORD type){switch(type){case REG_SZ:return L"REG_SZ";case REG_EXPAND_SZ:return L"REG_EXPAND_SZ";case REG_MULTI_SZ:return L"REG_MULTI_SZ";case REG_DWORD:return L"REG_DWORD";case REG_DWORD_BIG_ENDIAN:return L"REG_DWORD_BIG_ENDIAN";case REG_QWORD:return L"REG_QWORD";case REG_BINARY:return L"REG_BINARY";case REG_NONE:return L"REG_NONE";default:return L"类型 "+std::to_wstring(type);}}
std::wstring valueText(DWORD type,const std::vector<BYTE>& data,DWORD count){
    if((type==REG_SZ || type==REG_EXPAND_SZ || type==REG_MULTI_SZ) && count%sizeof(wchar_t)==0){
        const auto* text=reinterpret_cast<const wchar_t*>(data.data());const size_t length=count/sizeof(wchar_t);
        if(type!=REG_MULTI_SZ)return std::wstring(text,wcsnlen_s(text,length));
        std::wstring result;size_t offset=0;while(offset<length && text[offset]){const auto size=wcsnlen_s(text+offset,length-offset);if(!result.empty())result+=L"\r\n";result.append(text+offset,size);offset+=size+1;}return result;
    }
    uint64_t number=0;bool numeric=false;
    if(type==REG_DWORD && count==4){DWORD value=0;memcpy(&value,data.data(),4);number=value;numeric=true;}
    if(type==REG_QWORD && count==8){memcpy(&number,data.data(),8);numeric=true;}
    if(type==REG_DWORD_BIG_ENDIAN && count==4){for(size_t i=0;i<4;++i)number=(number<<8)|data[i];numeric=true;}
    std::wostringstream out;if(numeric){out<<number<<L"  (0x"<<std::hex<<std::uppercase<<number<<L")";return out.str();}
    for(size_t i=0;i<std::min<size_t>(count,256);++i){if(i)out<<L' ';out<<std::hex<<std::uppercase<<std::setfill(L'0')<<std::setw(2)<<static_cast<unsigned>(data[i]);}
    if(count>256)out<<L" …（只显示前 256 字节，共 "<<std::dec<<count<<L" 字节）";return out.str();
}
}
Interpretation interpretEvent(const std::wstring& provider,DWORD id){
    const auto source=lower(provider);
    if(source==L"microsoft-windows-kernel-power" && id==41)return {L"上次关机未正常完成",L"这条事件通常在下次启动时记录，不等于电源硬件损坏。对照事发时间的蓝屏、温度、供电和强制关机记录，结合转储文件排查。"};
    if(source==L"eventlog" && id==6008)return {L"检测到非预期关机",L"先确认是否断电、长按电源或系统失去响应；结合相邻时间的 Kernel-Power 和崩溃事件判断。单条记录不能确定原因。"};
    if(source==L"eventlog" && id==6005)return {L"Windows 事件日志服务已启动",L"通常伴随开机或服务重新启动，可用于定位时间线。"};
    if(source==L"eventlog" && id==6006)return {L"Windows 事件日志服务正常停止",L"通常伴随正常关机，也可能是服务停止；结合上下文判断。"};
    if(source==L"application error" && id==1000)return {L"应用程序发生崩溃",L"查看原文中的故障应用、故障模块、异常代码和偏移。优先确认应用/插件版本与近期更新，避免仅凭故障模块名认定系统文件损坏。"};
    if(source==L"windows error reporting" && id==1001)return {L"Windows 生成了错误报告",L"查看报告的问题签名和事件名称；它可能对应应用崩溃或其他故障，并非所有 1001 都是蓝屏。"};
    if(source==L"service control manager" && (id==7000 || id==7001 || id==7009 || id==7011 || id==7023 || id==7031 || id==7034))return {L"服务启动、依赖或运行异常",L"原文会指出具体服务及错误。查看依赖服务、超时、账户和最近的软件更新；不要直接批量禁用服务。"};
    if(source==L"microsoft-windows-distributedcom" && id==10016)return {L"组件访问权限事件",L"Windows 中可能周期性出现；若没有同时发生的功能故障，通常无需修改注册表或 DCOM 权限。"};
    if((source==L"disk" || source==L"microsoft-windows-disk") && (id==7 || id==51 || id==153))return {L"存储设备 I/O 异常或重试",L"关注是否持续重复，并核对事件中的磁盘编号。重要数据先做备份，再检查连接、驱动及厂商诊断信息；单条事件不能直接判定硬盘损坏。"};
    if((source==L"ntfs" || source==L"microsoft-windows-ntfs") && id==55)return {L"文件系统发现一致性问题",L"核对卷名并优先备份重要文件，再使用 Windows 或设备厂商的诊断工具确认；本模块不自动执行修复。"};
    if(source==L"microsoft-windows-security-auditing"){
        if(id==4624)return {L"账户成功登录",L"结合登录类型、账户、来源地址和进程判断。服务、计划任务与本机交互登录均可能产生该事件。"};
        if(id==4625)return {L"账户登录失败",L"查看登录类型、失败原因、状态码和来源地址。重复失败值得核对，但也可能来自密码变更、服务凭据过期或正常输错密码，不能单条判定攻击。"};
        if(id==4720)return {L"创建了用户账户",L"核对执行账户与新建账户是否符合近期管理操作。"};
        if(id==4732)return {L"本地安全组增加了成员",L"检查目标组是否为管理员组，以及操作者和新增成员是否符合预期。"};
        if(id==1102)return {L"安全审计日志被清除",L"核对执行账户和时间，确认是否为已知维护操作；这条记录本身不证明入侵。"};
    }
    return {L"查看原始事件内容",L"暂无该来源与事件 ID 的专用解释。以 Windows 原文、事件发生时间、重复频率和相邻记录为依据；事件级别本身不代表故障根因。"};
}
Table eventLogs(const LogQuery& query,std::atomic_bool& cancel){
    Table result{{L"记录时间",L"级别",L"事件来源",L"事件 ID",L"解读",L"排查建议",L"Windows 原文",L"日志通道",L"记录编号",L"计算机",L"原始 XML"},{},L""};
    if(query.channel.empty() || query.hours<1 || query.hours>720 || query.level<0 || query.level>2 || query.eventId< -1 || query.eventId>65535){result.summary=L"日志查询参数无效";return result;}
    std::wstring xpath=L"*[System[TimeCreated[timediff(@SystemTime) <= "+std::to_wstring(static_cast<int64_t>(query.hours)*3600000)+L"]";
    if(query.level==1)xpath+=L" and (Level=1 or Level=2 or Level=3)";
    if(query.level==2)xpath+=L" and (Level=1 or Level=2)";
    if(query.eventId>=0)xpath+=L" and EventID="+std::to_wstring(query.eventId);xpath+=L"]]";
    EventHandle events(EvtQuery(nullptr,query.channel.c_str(),xpath.c_str(),EvtQueryChannelPath|EvtQueryReverseDirection));
    if(!events.value){result.summary=L"无法读取日志："+errorText(GetLastError());return result;}
    EventHandle context(EvtCreateRenderContext(0,nullptr,EvtRenderContextSystem));
    if(!context.value){result.summary=L"无法读取事件字段："+errorText(GetLastError());return result;}
    std::map<std::wstring,std::unique_ptr<EventHandle>> publishers;
    size_t examined=0,skipped=0,characters=0;bool memoryLimit=false;DWORD status=ERROR_SUCCESS;const auto filter=lower(query.filter);
    while(!cancel && examined<2000 && result.rows.size()<300){
        EVT_HANDLE item=nullptr;DWORD returned=0;
        if(!EvtNext(events.value,1,&item,100,0,&returned)){status=GetLastError();break;}EventHandle event(item);++examined;
        DWORD needed=0,count=0;EvtRender(context.value,event.value,EvtRenderEventValues,0,nullptr,&needed,&count);
        if(!needed || needed>65536){++skipped;continue;}
        std::vector<BYTE> data(needed);if(!EvtRender(context.value,event.value,EvtRenderEventValues,needed,data.data(),&needed,&count) || count<EvtSystemPropertyIdEND){++skipped;continue;}
        const auto* values=reinterpret_cast<const EVT_VARIANT*>(data.data());
        const auto string=[&](size_t field){return values[field].Type==EvtVarTypeString && values[field].StringVal?std::wstring(values[field].StringVal):L"--";};
        const auto provider=string(EvtSystemProviderName);const DWORD id=values[EvtSystemEventID].Type==EvtVarTypeUInt16?values[EvtSystemEventID].UInt16Val:0;
        const BYTE level=values[EvtSystemLevel].Type==EvtVarTypeByte?values[EvtSystemLevel].ByteVal:255;
        const wchar_t* levels[]={L"审计 / 未指定",L"严重",L"错误",L"警告",L"信息",L"详细"};
        auto it=publishers.find(provider);if(it==publishers.end()){
            if(publishers.size()>=16)publishers.erase(publishers.begin());
            it=publishers.emplace(provider,std::make_unique<EventHandle>(EvtOpenPublisherMetadata(nullptr,provider.c_str(),nullptr,0,0))).first;
        }
        const auto explanation=interpretEvent(provider,id);
        Row row={values[EvtSystemTimeCreated].Type==EvtVarTypeFileTime?timestamp(values[EvtSystemTimeCreated].FileTimeVal):L"--",level<6?levels[level]:L"未知",provider,std::to_wstring(id),explanation.meaning,explanation.advice,eventMessage(it->second->value,event.value),query.channel,
            values[EvtSystemEventRecordId].Type==EvtVarTypeUInt64?std::to_wstring(values[EvtSystemEventRecordId].UInt64Val):L"--",string(EvtSystemComputer),L""};
        if(!contains(row,filter))continue;row[10]=renderXml(event.value);
        if(!appendBounded(result,std::move(row),characters)){memoryLimit=true;break;}
    }
    result.summary=query.channel+L"：已检查 "+std::to_wstring(examined)+L" 条，显示 "+std::to_wstring(result.rows.size())+L" 条，未能读取 "+std::to_wstring(skipped)+L" 条。";
    if(cancel)result.summary+=L"已取消，结果不完整。";
    else if(status!=ERROR_SUCCESS && status!=ERROR_NO_MORE_ITEMS)result.summary+=L"读取中断："+errorText(status);
    else if(memoryLimit || examined>=2000 || result.rows.size()>=300)result.summary+=L"达到本次上限（300 条 / 8 MiB 文本），可缩小时间范围或指定事件 ID。";
    result.summary+=L"本地规则解释不是根因诊断；详细信息保留 Windows 原文和 XML。不会清除日志。";return result;
}
Interpretation interpretRegistry(const std::wstring& path,const std::wstring& name){
    const auto key=lower(path),value=lower(name);
    const auto ends=[&](const wchar_t* suffix){return key.ends_with(suffix);};
    if(ends(L"\\currentversion\\run") || ends(L"\\currentversion\\runonce"))return {L"用户登录时启动的程序",L"键值通常为启动命令。RunOnce 通常只执行一次；所在 HKCU/HKLM 决定作用范围。核对程序路径和发布者，单凭自启动条目不能认定恶意。"};
    if(key.find(L"\\currentcontrolset\\services\\")!=std::wstring::npos){
        if(value==L"start")return {L"服务 / 驱动启动类型",L"常见数值：0 引导、1 系统、2 自动、3 按需、4 禁用。还需结合服务类型、触发器和延迟启动设置判断实际行为。"};
        if(value==L"imagepath")return {L"服务 / 驱动映像路径",L"可能包含启动参数和环境变量；原值不会被执行或展开。路径存在与否、签名和服务用途需另行核实。"};
        return {L"Windows 服务或驱动配置",L"该位置影响服务或驱动运行，需结合服务名称、启动类型和依赖项解读；本模块只读。"};
    }
    if(key.find(L"\\internet settings")!=std::wstring::npos){
        if(value==L"proxyenable")return {L"用户代理开关",L"通常 0 为关闭、1 为开启静态代理。部分应用不使用此设置，自动配置脚本、策略和 WinHTTP 代理也可能单独生效。"};
        if(value==L"proxyserver" || value==L"autoconfigurl")return {L"代理地址 / 自动配置脚本",L"核对地址是否来自预期代理软件或组织配置；此值不代表所有程序的实际网络去向。"};
    }
    if(key.find(L"\\currentversion\\uninstall")!=std::wstring::npos)return {L"软件卸载与安装登记信息",L"常见字段包含显示名称、版本、安装位置和卸载命令。登记信息可能过期，不等同于完整软件清单；本模块不会运行卸载命令。"};
    if(key.find(L"\\currentversion\\policies")!=std::wstring::npos || key.find(L"\\software\\policies")!=std::wstring::npos)return {L"系统或应用策略配置",L"可能由组策略、组织管理或软件写入。键值存在不一定代表策略最终生效，需结合策略优先级和应用版本。"};
    if(key.find(L"\\windows nt\\currentversion")!=std::wstring::npos)return {L"Windows 版本或组件配置",L"版本信息与组件配置共存于此分支；以具体字段含义和 Windows 当前状态为准，不建议批量修改。"};
    if(key.find(L"\\hardware\\description")!=std::wstring::npos)return {L"系统枚举的硬件描述",L"主要由系统启动时生成，用于描述硬件和固件，不是驱动更新入口。"};
    return {L"注册表配置项",L"暂无该键值的专用解释。结合所属软件、值类型、路径和软件文档判断；键值本身不能证明配置已生效或存在安全问题。"};
}
std::wstring registryParent(const std::wstring& path){const auto parsed=parseRegistry(path);if(!parsed.hive)return L"HKCU";const auto pos=parsed.subkey.rfind(L'\\');return parsed.subkey.empty()?parsed.root:pos==std::wstring::npos?parsed.root:parsed.root+L"\\"+parsed.subkey.substr(0,pos);}
Table registryBrowse(const std::wstring& path,bool view32,const std::wstring& filter,const std::atomic_bool* cancel){
    Table table{{L"键 / 值名称",L"类型",L"数据",L"解读",L"说明",L"注册表路径",L"视图",L"键最后写入时间",L"数据字节数",L"项目类别"},{},L""};
    const auto parsed=parseRegistry(path);if(!parsed.hive){table.summary=parsed.error;return table;}
    HKEY key=nullptr;const auto code=RegOpenKeyExW(parsed.hive,parsed.subkey.c_str(),0,KEY_READ|(view32?KEY_WOW64_32KEY:KEY_WOW64_64KEY),&key);
    if(code!=ERROR_SUCCESS){table.summary=L"无法打开注册表："+errorText(code);return table;}
    struct Close{HKEY key;~Close(){RegCloseKey(key);}} close{key};
    FILETIME modified{};DWORD subkeys=0,values=0;
    const auto info=RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&subkeys,nullptr,nullptr,&values,nullptr,nullptr,nullptr,&modified);
    if(info!=ERROR_SUCCESS){table.summary=L"无法读取注册表元数据："+errorText(info);return table;}
    const std::wstring full=parsed.root+(parsed.subkey.empty()?L"":L"\\"+parsed.subkey),query=lower(filter),view=view32?L"32 位":L"64 位";
    const auto time=timestamp((static_cast<uint64_t>(modified.dwHighDateTime)<<32)|modified.dwLowDateTime);size_t examined=0,errors=0,characters=0;
    bool memoryLimit=false;const auto cancelled=[&]{return cancel && cancel->load();};
    bool capped=false;
    for(DWORD n=0;n<subkeys && examined<5000 && table.rows.size()<2000 && !cancelled() && !memoryLimit;++n){
        std::array<wchar_t,256> name{};DWORD size=static_cast<DWORD>(name.size());FILETIME changed{};
        const auto status=RegEnumKeyExW(key,n,name.data(),&size,nullptr,nullptr,nullptr,&changed);++examined;if(status!=ERROR_SUCCESS){++errors;continue;}
        const auto child=full+L"\\"+name.data();const auto help=interpretRegistry(child,L"");
        Row row={name.data(),L"子键",L"",help.meaning,help.advice,child,view,timestamp((static_cast<uint64_t>(changed.dwHighDateTime)<<32)|changed.dwLowDateTime),L"--",L"子键"};if(contains(row,query))memoryLimit=!appendBounded(table,std::move(row),characters);
    }
    if(examined<subkeys)capped=true;
    for(DWORD n=0;n<values && examined<5000 && table.rows.size()<2000 && !cancelled() && !memoryLimit;++n){
        std::vector<wchar_t> name(16384);DWORD nameLength=static_cast<DWORD>(name.size()),type=0,count=0;
        auto status=RegEnumValueW(key,n,name.data(),&nameLength,nullptr,&type,nullptr,&count);++examined;
        if(status!=ERROR_SUCCESS){++errors;continue;}
        std::wstring data;const DWORD declared=count;
        if(count>65536)data=L"值大于 64 KiB，未加载内容";
        else{
            std::vector<BYTE> bytesData(static_cast<size_t>(count)+2,0);
            status=RegQueryValueExW(key,name.data(),nullptr,&type,bytesData.data(),&count);
            if(status!=ERROR_SUCCESS){data=L"读取失败："+errorText(status);++errors;}
            else data=valueText(type,bytesData,count);
        }
        const auto help=interpretRegistry(full,name.data());Row row={nameLength?name.data():L"(默认)",valueType(type),data,help.meaning,help.advice,full,view,time,std::to_wstring(declared),L"值"};if(contains(row,query))memoryLimit=!appendBounded(table,std::move(row),characters);
    }
    capped=capped || examined<static_cast<size_t>(subkeys)+values;
    table.summary=full+L"（"+view+L"）：显示 "+std::to_wstring(table.rows.size())+L" 项，读取异常 "+std::to_wstring(errors)+L" 项。";
    if(cancelled())table.summary+=L"已取消，结果不完整。";
    else if(capped || memoryLimit)table.summary+=L"达到上限：最多检查当前键 5000 项、显示 2000 项 / 8 MiB 文本。";
    table.summary+=L"只读当前层，不递归扫描。时间是键的最后写入时间，不是各个值的修改时间；32/64 位视图在部分分支共享。";return table;
}
}
