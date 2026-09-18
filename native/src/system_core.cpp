#include "system_core.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#include <shlobj.h>
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <tlhelp32.h>

namespace systemdesk {
static uint64_t ticks(FILETIME f){return (static_cast<uint64_t>(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
std::wstring bytes(uint64_t value){
    const wchar_t* units[]={L"B",L"KiB",L"MiB",L"GiB",L"TiB"};double v=static_cast<double>(value);int n=0;
    while(v>=1024 && n<4){v/=1024;++n;}
    std::wostringstream out;out<<std::fixed<<std::setprecision(n?2:0)<<v<<L" "<<units[n];return out.str();
}
std::wstring timestamp(uint64_t value){
    FILETIME ft{static_cast<DWORD>(value),static_cast<DWORD>(value>>32)},local{};SYSTEMTIME s{};
    if(!FileTimeToLocalFileTime(&ft,&local) || !FileTimeToSystemTime(&local,&s))return L"";
    wchar_t text[40]{};swprintf_s(text,L"%04u-%02u-%02u %02u:%02u:%02u",s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond);return text;
}
std::wstring errorText(DWORD code){
    wchar_t* text=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
    std::wstring result=text?text:L"Windows 错误";if(text)LocalFree(text);return result+L" ("+std::to_wstring(code)+L")";
}
std::filesystem::path dataDirectory(){
    PWSTR folder=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder)))throw std::runtime_error("Locate application data");
    std::filesystem::path result=std::filesystem::path(folder)/L"PicoPet";CoTaskMemFree(folder);std::filesystem::create_directories(result);return result;
}
static std::wstring registry(HKEY root,const wchar_t* path,const wchar_t* name){
    wchar_t value[512]{};DWORD size=sizeof(value);return RegGetValueW(root,path,name,RRF_RT_REG_SZ,nullptr,value,&size)==ERROR_SUCCESS?value:L"不可用";
}
Table Performance::sample(){
    Table table{{L"项目",L"当前状态 / 设备信息"},{},L"每秒采样；窗口最小化或关闭后停止。"};
    FILETIME idle{},kernel{},user{};GetSystemTimes(&idle,&kernel,&user);
    const uint64_t i=ticks(idle),k=ticks(kernel),u=ticks(user),total=k+u-previousKernel-previousUser;
    const double cpu=previousKernel && total?100.0*static_cast<double>(total-(i-previousIdle))/static_cast<double>(total):0;
    std::wostringstream percent;percent<<std::fixed<<std::setprecision(1)<<cpu<<L" %";
    table.rows.push_back({L"CPU 使用率",previousKernel?percent.str():L"采样中"});previousIdle=i;previousKernel=k;previousUser=u;
    MEMORYSTATUSEX memory{sizeof(memory)};GlobalMemoryStatusEx(&memory);
    table.rows.push_back({L"物理内存",bytes(memory.ullTotalPhys-memory.ullAvailPhys)+L" / "+bytes(memory.ullTotalPhys)+L"  ("+std::to_wstring(memory.dwMemoryLoad)+L"%)"});
    table.rows.push_back({L"可用内存",bytes(memory.ullAvailPhys)});
    PERFORMANCE_INFORMATION perf{sizeof(perf)};GetPerformanceInfo(&perf,sizeof(perf));
    table.rows.push_back({L"提交内存",bytes(perf.CommitTotal*perf.PageSize)+L" / "+bytes(perf.CommitLimit*perf.PageSize)});
    table.rows.push_back({L"进程 / 线程 / 句柄",std::to_wstring(perf.ProcessCount)+L" / "+std::to_wstring(perf.ThreadCount)+L" / "+std::to_wstring(perf.HandleCount)});
    const auto seconds=GetTickCount64()/1000;
    table.rows.push_back({L"系统运行时间",std::to_wstring(seconds/86400)+L" 天 "+std::to_wstring(seconds/3600%24)+L" 时 "+std::to_wstring(seconds/60%60)+L" 分"});
    const size_t dynamicRows=table.rows.size();
    if(devices.empty()){
    table.rows.push_back({L"处理器",registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString")});
    SYSTEM_INFO info{};GetNativeSystemInfo(&info);
    table.rows.push_back({L"逻辑处理器",std::to_wstring(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))});
    table.rows.push_back({L"系统架构",info.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64?L"x64":L"ARM64 / 其他"});
    table.rows.push_back({L"Windows 版本",L"Windows 11  "+registry(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"DisplayVersion")+L"  Build "+registry(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"CurrentBuildNumber")});
    table.rows.push_back({L"设备制造商",registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"SystemManufacturer")});
    table.rows.push_back({L"设备型号",registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"SystemProductName")});
    table.rows.push_back({L"主板",registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BaseBoardProduct")});
    table.rows.push_back({L"BIOS",registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion")});
    wchar_t computer[256]{};DWORD length=256;GetComputerNameW(computer,&length);table.rows.push_back({L"计算机名",computer});
    DISPLAY_DEVICEW device{sizeof(device)};std::set<std::wstring> displays;
    for(DWORD n=0;EnumDisplayDevicesW(nullptr,n,&device,0);++n){if(!(device.StateFlags&DISPLAY_DEVICE_MIRRORING_DRIVER) && displays.insert(device.DeviceString).second)table.rows.push_back({L"显示设备",device.DeviceString});device={sizeof(device)};}
    devices.assign(table.rows.begin()+static_cast<ptrdiff_t>(dynamicRows),table.rows.end());
    }else table.rows.insert(table.rows.end(),devices.begin(),devices.end());
    SYSTEM_POWER_STATUS power{};if(GetSystemPowerStatus(&power))table.rows.push_back({L"供电",power.ACLineStatus==1?L"外接电源":L"电池 / 未知"});
    return table;
}

Table ProcessPerformance::sample(){
    Table out{{L"进程名称",L"PID",L"CPU",L"内存工作集",L"读取 /s",L"写入 /s",L"状态",L"私有提交",L"线程数",L"句柄数",L"父 PID",L"启动时间",L"程序路径",L"读取说明",L"进程标识"},{},L"CPU 按全机逻辑处理器归一化；I/O 包含文件、设备和网络，不等同物理磁盘吞吐。受保护进程保留列表项并标明不可读取。仅打开进程视图时每秒采集。"};
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot read process snapshot");
    const auto now=GetTickCount64();const auto processors=std::max<DWORD>(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    std::map<DWORD,Counter> next;PROCESSENTRY32W entry{sizeof(entry)};
    for(BOOL ok=Process32FirstW(snapshot,&entry);ok;ok=Process32NextW(snapshot,&entry)){
        Row row{entry.szExeFile,std::to_wstring(entry.th32ProcessID),L"--",L"--",L"--",L"--",L"运行中",L"--",std::to_wstring(entry.cntThreads),L"--",std::to_wstring(entry.th32ParentProcessID),L"--",L"不可读取",L"",std::to_wstring(entry.th32ProcessID)};
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,entry.th32ProcessID);
        if(!process)process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!process){row[6]=L"部分信息不可读";row[13]=errorText(GetLastError());out.rows.push_back(std::move(row));continue;}
        Counter c;c.time=now;FILETIME created{},exit{},kernel{},user{};IO_COUNTERS io{};
        const bool times=GetProcessTimes(process,&created,&exit,&kernel,&user)!=FALSE,ioOk=GetProcessIoCounters(process,&io)!=FALSE;
        if(times){c.created=ticks(created);c.cpu=ticks(kernel)+ticks(user);row[11]=timestamp(c.created);row[14]+=L":"+std::to_wstring(c.created);}
        if(ioOk){c.read=io.ReadTransferCount;c.written=io.WriteTransferCount;c.ioValid=true;}
        const auto old=previous.find(entry.th32ProcessID);const bool baseline=times && old!=previous.end() && old->second.created==c.created && now>old->second.time;
        if(baseline)c.path=old->second.path;
        if(c.path.empty()){wchar_t path[32768]{};DWORD size=32768;if(QueryFullProcessImageNameW(process,0,path,&size))c.path=path;}
        if(!c.path.empty())row[12]=c.path;
        if(baseline && c.cpu>=old->second.cpu){std::wostringstream value;value<<std::fixed<<std::setprecision(1)<<std::clamp(100.0*(c.cpu-old->second.cpu)/((now-old->second.time)*10000.0*processors),0.0,100.0)<<L" %";row[2]=value.str();}else row[2]=times?L"采样中":L"不可用";
        if(baseline && ioOk && old->second.ioValid){uint64_t read=0,write=0;if(counterRate(c.read,old->second.read,now-old->second.time,read) && counterRate(c.written,old->second.written,now-old->second.time,write)){row[4]=bytes(read)+L"/s";row[5]=bytes(write)+L"/s";}}
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        if(GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))){row[3]=bytes(memory.WorkingSetSize);row[7]=bytes(memory.PrivateUsage);}else row[13]=L"内存读取受权限限制。";
        DWORD handles=0;if(GetProcessHandleCount(process,&handles))row[9]=std::to_wstring(handles);
        if(!times || !ioOk)row[13]+=L"部分时间或 I/O 计数不可用。";
        if(row[13].empty())row[13]=L"Windows 进程 API 实测；首次采样建立基准。";
        CloseHandle(process);if(times)next[entry.th32ProcessID]=std::move(c);out.rows.push_back(std::move(row));
    }const DWORD enumerationError=GetLastError();CloseHandle(snapshot);
    if(enumerationError!=ERROR_NO_MORE_FILES)throw std::runtime_error("Process enumeration incomplete");previous=std::move(next);
    std::sort(out.rows.begin(),out.rows.end(),[](const Row& a,const Row& b){const int name=_wcsicmp(a[0].c_str(),b[0].c_str());return name?name<0:std::stoul(a[1])<std::stoul(b[1]);});return out;
}

Table volumes(){
    Table table{{L"盘符 / 分区",L"卷标 / 类型",L"文件系统",L"总容量",L"已用",L"可用",L"磁盘布局 / 状态"},{},L"本地卷与可读取的物理布局；容量使用二进制单位。"};
    const DWORD mask=GetLogicalDrives();std::set<DWORD> disks;
    for(int n=0;n<26;++n){if(!(mask&(1u<<n)))continue;std::wstring root=L"A:\\";root[0]=static_cast<wchar_t>(L'A'+n);
        const UINT type=GetDriveTypeW(root.c_str());if(type==DRIVE_REMOTE)continue;
        wchar_t label[256]{},fs[80]{};DWORD serial=0;ULARGE_INTEGER available{},total{},free{};
        const BOOL ready=GetDiskFreeSpaceExW(root.c_str(),&available,&total,&free);
        GetVolumeInformationW(root.c_str(),label,256,&serial,nullptr,nullptr,fs,80);
        std::wstring layout=type==DRIVE_REMOVABLE?L"可移动卷":L"本地卷";
        const std::wstring device=L"\\\\.\\"+root.substr(0,2);
        HANDLE handle=CreateFileW(device.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(handle!=INVALID_HANDLE_VALUE){
            std::array<BYTE,4096> buffer{};DWORD returned=0;
            if(DeviceIoControl(handle,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,nullptr,0,buffer.data(),static_cast<DWORD>(buffer.size()),&returned,nullptr)){
                const auto* extents=reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());layout.clear();
                const size_t capacity=returned>=FIELD_OFFSET(VOLUME_DISK_EXTENTS,Extents)?(returned-FIELD_OFFSET(VOLUME_DISK_EXTENTS,Extents))/sizeof(DISK_EXTENT):0;
                for(size_t j=0;j<std::min<size_t>(extents->NumberOfDiskExtents,capacity);++j){const auto& e=extents->Extents[j];
                    disks.insert(e.DiskNumber);
                    layout+=L"磁盘 "+std::to_wstring(e.DiskNumber)+L"  偏移 "+bytes(e.StartingOffset.QuadPart)+L"  长度 "+bytes(e.ExtentLength.QuadPart)+L"  ";}
            }else layout+=L"；物理映射不可读";
            CloseHandle(handle);
        }
        if(ready && total.QuadPart){const auto remaining=static_cast<unsigned>(100.0L*available.QuadPart/total.QuadPart);layout=(remaining<10?L"空间不足":L"剩余空间")+std::wstring(L" ")+std::to_wstring(remaining)+L"%；"+layout;}
        if(!ready)layout=L"未就绪 / 无法访问";
        table.rows.push_back({root,label,fs,ready?bytes(total.QuadPart):L"--",ready?bytes(total.QuadPart-free.QuadPart):L"--",ready?bytes(available.QuadPart):L"--",layout});
    }
    for(const DWORD disk:disks){
        const auto name=L"\\\\.\\PhysicalDrive"+std::to_wstring(disk);HANDLE handle=CreateFileW(name.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(handle==INVALID_HANDLE_VALUE)continue;
        std::vector<BYTE> buffer(65536);DWORD returned=0;
        if(DeviceIoControl(handle,IOCTL_DISK_GET_DRIVE_LAYOUT_EX,nullptr,0,buffer.data(),static_cast<DWORD>(buffer.size()),&returned,nullptr)){
            const auto* layout=reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(buffer.data());
            const size_t capacity=returned>=FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry)?(returned-FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry))/sizeof(PARTITION_INFORMATION_EX):0;
            for(size_t i=0;i<std::min<size_t>(layout->PartitionCount,capacity);++i){const auto& partition=layout->PartitionEntry[i];if(partition.PartitionLength.QuadPart==0)continue;
                std::wstring label=partition.PartitionStyle==PARTITION_STYLE_GPT?std::wstring(partition.Gpt.Name,wcsnlen_s(partition.Gpt.Name,36)):L"MBR 分区";
                table.rows.push_back({L"磁盘 "+std::to_wstring(disk)+L" / 分区 "+std::to_wstring(partition.PartitionNumber),label,partition.PartitionStyle==PARTITION_STYLE_GPT?L"GPT":L"MBR",bytes(partition.PartitionLength.QuadPart),L"--",L"--",L"偏移 "+std::to_wstring(partition.StartingOffset.QuadPart)+L" B；长度 "+std::to_wstring(partition.PartitionLength.QuadPart)+L" B"});
            }
        }else table.rows.push_back({L"磁盘 "+std::to_wstring(disk),L"布局不可读取",L"--",L"--",L"--",L"--",errorText(GetLastError())});
        CloseHandle(handle);
    }return table;
}
Table browse(const std::wstring& directory){
    Table table{{L"名称",L"类型",L"大小",L"修改时间",L"创建时间",L"属性",L"完整路径"},{},L""};
    std::filesystem::path root(directory);const auto pattern=(root/L"*").wstring();WIN32_FIND_DATAW item{};
    HANDLE find=FindFirstFileExW(pattern.c_str(),FindExInfoBasic,&item,FindExSearchNameMatch,nullptr,FIND_FIRST_EX_LARGE_FETCH);
    if(find==INVALID_HANDLE_VALUE){table.summary=errorText(GetLastError());return table;}
    do{if(!wcscmp(item.cFileName,L".") || !wcscmp(item.cFileName,L".."))continue;
        const bool folder=(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        const uint64_t size=(static_cast<uint64_t>(item.nFileSizeHigh)<<32)|item.nFileSizeLow;
        std::wstring attrs;
        if(item.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)attrs+=L"链接/云占位 ";
        if(item.dwFileAttributes&FILE_ATTRIBUTE_HIDDEN)attrs+=L"隐藏 ";
        if(item.dwFileAttributes&FILE_ATTRIBUTE_READONLY)attrs+=L"只读 ";
        if(item.dwFileAttributes&FILE_ATTRIBUTE_SYSTEM)attrs+=L"系统 ";
        table.rows.push_back({item.cFileName,folder?L"文件夹":L"文件",folder?L"--":bytes(size),timestamp(ticks(item.ftLastWriteTime)),timestamp(ticks(item.ftCreationTime)),attrs,(root/item.cFileName).wstring()});
    }while(table.rows.size()<3000 && FindNextFileW(find,&item));FindClose(find);
    std::sort(table.rows.begin(),table.rows.end(),[](const Row& a,const Row& b){if(a[1]!=b[1])return a[1]==L"文件夹";return _wcsicmp(a[0].c_str(),b[0].c_str())<0;});
    table.summary=directory+L"  |  "+std::to_wstring(table.rows.size())+L" 项（每层最多显示 3000 项；双击文件夹进入，文件查看详细信息）";return table;
}

static std::wstring endpoint(const void* address,int family,DWORD port,DWORD scope=0){
    wchar_t value[INET6_ADDRSTRLEN]{};InetNtopW(family,const_cast<void*>(address),value,INET6_ADDRSTRLEN);
    std::wstring text=value;if(scope)text+=L"%"+std::to_wstring(scope);
    return (family==AF_INET6?L"["+text+L"]":text)+L":"+std::to_wstring(ntohs(static_cast<u_short>(port)));
}
static std::wstring ipText(const void* address,int family){
    if(!address)return L"";wchar_t value[INET6_ADDRSTRLEN]{};InetNtopW(family,const_cast<void*>(address),value,INET6_ADDRSTRLEN);return value;
}
static std::wstring tcpState(DWORD state){
    switch(state){case MIB_TCP_STATE_LISTEN:return L"监听";case MIB_TCP_STATE_ESTAB:return L"已连接（方向未提供）";case MIB_TCP_STATE_SYN_SENT:return L"出站握手";case MIB_TCP_STATE_SYN_RCVD:return L"入站握手";case MIB_TCP_STATE_TIME_WAIT:return L"TIME_WAIT";case MIB_TCP_STATE_CLOSE_WAIT:return L"CLOSE_WAIT";case MIB_TCP_STATE_FIN_WAIT1:return L"FIN_WAIT1";case MIB_TCP_STATE_FIN_WAIT2:return L"FIN_WAIT2";case MIB_TCP_STATE_LAST_ACK:return L"LAST_ACK";case MIB_TCP_STATE_CLOSING:return L"CLOSING";default:return L"状态 "+std::to_wstring(state);}
}
Table Network::sample(){
    Table table{{L"进程",L"PID",L"协议",L"通信范围",L"本机地址",L"本机端口",L"本机名称 / PTR",L"远端地址",L"远端端口",L"远端 PTR 域名",L"接收速率",L"发送速率",L"测速状态",L"连接状态",L"本机地址类型",L"程序路径",L"进程启动标识"},{},L""};
    MIB_IF_TABLE2* interfaces=nullptr;const ULONGLONG now=GetTickCount64();uint64_t rx=0,tx=0;bool anyRate=false;
    const double elapsed=lastTime?static_cast<double>(now-lastTime)/1000:0;
    adapters={{L"网络接口",L"连接速率",L"接收速率",L"发送速率",L"累计接收",L"累计发送",L"接口类型"},{},L"仅显示已启用接口；累计值来自系统网卡计数器，可能在禁用/重置网卡后清零。"};
    if(GetIfTable2(&interfaces)==NO_ERROR){
        std::vector<Adapter> next;
        for(ULONG i=0;i<interfaces->NumEntries;++i){const auto& row=interfaces->Table[i];
            if(row.Type==IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus!=IfOperStatusUp)continue;
            auto old=std::find_if(previous.begin(),previous.end(),[&](const Adapter& a){return a.id==row.InterfaceLuid.Value;});
            uint64_t received=0,sent=0;
            if(old!=previous.end() && row.InOctets>=old->received && row.OutOctets>=old->sent){received=row.InOctets-old->received;sent=row.OutOctets-old->sent;rx+=received;tx+=sent;}
            const bool valid=elapsed>0 && old!=previous.end() && row.InOctets>=old->received && row.OutOctets>=old->sent;
            anyRate=anyRate || valid;
            adapters.rows.push_back({row.Alias,std::to_wstring(row.ReceiveLinkSpeed/1000000)+L" Mbps",valid?bytes(static_cast<uint64_t>(static_cast<double>(received)/elapsed))+L"/s":L"建立基准",valid?bytes(static_cast<uint64_t>(static_cast<double>(sent)/elapsed))+L"/s":L"建立基准",bytes(row.InOctets),bytes(row.OutOctets),row.Type==IF_TYPE_IEEE80211?L"Wi-Fi":row.Type==IF_TYPE_ETHERNET_CSMACD?L"以太网 / 虚拟以太网":L"其他 / 隧道"});
            next.push_back({row.InterfaceLuid.Value,row.InOctets,row.OutOctets});
        }previous=std::move(next);FreeMibTable(interfaces);
        receiveRate=elapsed>0?static_cast<uint64_t>(static_cast<double>(rx)/elapsed):0;
        sendRate=elapsed>0?static_cast<uint64_t>(static_cast<double>(tx)/elapsed):0;rateAvailable=elapsed>0 && anyRate;
        rates=rateAvailable?L"接收 "+bytes(receiveRate)+L"/s    发送 "+bytes(sendRate)+L"/s":L"网络速率采样中";
    }else {rates=L"网络速率读取失败";previous.clear();receiveRate=0;sendRate=0;rateAvailable=false;}
    lastTime=now;
    std::vector<Connection> next;bool complete=true;
    for(const ULONG family:{static_cast<ULONG>(AF_INET),static_cast<ULONG>(AF_INET6)}){
        for(const bool tcp:{true,false}){
            ULONG size=0;
            auto fetch=[&](void* data){return tcp?GetExtendedTcpTable(data,&size,FALSE,family,TCP_TABLE_OWNER_PID_ALL,0):GetExtendedUdpTable(data,&size,FALSE,family,UDP_TABLE_OWNER_PID,0);};
            DWORD status=fetch(nullptr);std::vector<BYTE> buffer;
            for(int attempt=0;attempt<3 && status==ERROR_INSUFFICIENT_BUFFER;++attempt){buffer.resize(size);status=fetch(buffer.data());}
            if(status!=NO_ERROR){complete=false;continue;}
            auto append=[&](DWORD pid,const void* local,DWORD localPort,const void* remote,DWORD remotePort,DWORD state,DWORD localZone=0,DWORD remoteZone=0){
                Connection c;c.pid=pid;c.tcp=tcp;c.family=static_cast<int>(family);c.tcpStatus=state;c.localZone=localZone;c.remoteZone=remoteZone;
                c.localIp=ipText(local,c.family);c.remoteIp=ipText(remote,c.family);c.localPort=ntohs(static_cast<u_short>(localPort));c.remotePort=ntohs(static_cast<u_short>(remotePort));
                c.local=endpoint(local,c.family,localPort,localZone);c.remote=remote?endpoint(remote,c.family,remotePort,remoteZone):L"未提供";c.protocol=(tcp?L"TCP":L"UDP")+std::wstring(family==AF_INET?L"v4":L"v6");c.state=tcp?tcpState(state):L"端点（系统不提供远端）";
                c.key=c.protocol+L"|"+std::to_wstring(pid)+L"|"+c.local+L"|"+c.remote;next.push_back(std::move(c));
            };
            if(tcp && family==AF_INET){const auto* rows=reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());for(DWORD i=0;i<rows->dwNumEntries;++i){const auto& r=rows->table[i];append(r.dwOwningPid,&r.dwLocalAddr,r.dwLocalPort,&r.dwRemoteAddr,r.dwRemotePort,r.dwState);}}
            else if(tcp){const auto* rows=reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());for(DWORD i=0;i<rows->dwNumEntries;++i){const auto& r=rows->table[i];append(r.dwOwningPid,r.ucLocalAddr,r.dwLocalPort,r.ucRemoteAddr,r.dwRemotePort,r.dwState,r.dwLocalScopeId,r.dwRemoteScopeId);}}
            else if(family==AF_INET){const auto* rows=reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buffer.data());for(DWORD i=0;i<rows->dwNumEntries;++i){const auto& r=rows->table[i];append(r.dwOwningPid,&r.dwLocalAddr,r.dwLocalPort,nullptr,0,0);}}
            else {const auto* rows=reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(buffer.data());for(DWORD i=0;i<rows->dwNumEntries;++i){const auto& r=rows->table[i];append(r.dwOwningPid,r.ucLocalAddr,r.dwLocalPort,nullptr,0,0,r.dwLocalScopeId);}}
        }
    }
    std::stable_sort(next.begin(),next.end(),[](const Connection& a,const Connection& b){
        const int ar=a.tcp && a.tcpStatus==MIB_TCP_STATE_ESTAB?0:a.tcp && a.tcpStatus!=MIB_TCP_STATE_LISTEN?1:2;
        const int br=b.tcp && b.tcpStatus==MIB_TCP_STATE_ESTAB?0:b.tcp && b.tcpStatus!=MIB_TCP_STATE_LISTEN?1:2;
        return ar!=br?ar<br:a.pid!=b.pid?a.pid<b.pid:a.key<b.key;
    });
    std::map<DWORD,std::pair<std::wstring,std::wstring>> names;
    struct ProcessRates {std::wstring name,path;uint64_t received=0,sent=0;size_t endpoints=0,measured=0,established=0;};
    std::map<DWORD,ProcessRates> totals;
    for(auto& c:next){auto it=names.find(c.pid);if(it==names.end()){
            std::wstring path=L"不可读取（系统/权限限制）",started=L"未知";
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,c.pid);
            if(process){wchar_t value[32768]{};DWORD length=32768;if(QueryFullProcessImageNameW(process,0,value,&length))path=value;FILETIME created{},exited{},kernel{},user{};if(GetProcessTimes(process,&created,&exited,&kernel,&user))started=std::to_wstring(ticks(created));CloseHandle(process);}
            it=names.emplace(c.pid,std::make_pair(std::move(path),std::move(started))).first;
        }c.path=it->second.first;c.started=it->second.second;c.key+=L"|"+c.started;c.process=c.path.find(L"不可读取")==0?(c.pid==4?L"System":L"系统 / 不可读取"):std::filesystem::path(c.path).filename().wstring();
    }
    details.enrich(next,resolveDns,measureConnections);
    for(const auto& c:next){
        table.rows.push_back({c.process,std::to_wstring(c.pid),c.protocol,c.scope,c.localIp+(c.localZone?L"%"+std::to_wstring(c.localZone):L""),std::to_wstring(c.localPort),c.localDns,
            c.remoteIp.empty()?L"未提供":c.remoteIp+(c.remoteZone?L"%"+std::to_wstring(c.remoteZone):L""),c.remoteIp.empty()?L"--":std::to_wstring(c.remotePort),c.remoteDns,c.received,c.sent,c.statistics,c.state,c.localScope,c.path,c.started});
        auto& total=totals[c.pid];total.name=c.process;total.path=c.path;++total.endpoints;if(c.tcp && c.tcpStatus==MIB_TCP_STATE_ESTAB)++total.established;
        if(c.measured){++total.measured;total.received+=c.receiveRate;total.sent+=c.sendRate;}
    }
    processes={{L"进程",L"PID",L"端点数",L"已建立 TCP",L"可测速 TCP",L"TCP 接收速率",L"TCP 发送速率",L"覆盖范围",L"程序路径"},{},L"只汇总本次成功采样的 TCP 连接；不包含 UDP/QUIC、未授权连接和采样间隔内结束的连接，不等于整个进程的全部流量。"};
    for(const auto& [pid,total]:totals)processes.rows.push_back({total.name,std::to_wstring(pid),std::to_wstring(total.endpoints),std::to_wstring(total.established),std::to_wstring(total.measured),
        total.measured?bytes(total.received)+L"/s":L"--",total.measured?bytes(total.sent)+L"/s":L"--",std::to_wstring(total.measured)+L" / "+std::to_wstring(total.established)+L" 条已建立 TCP（UDP 未覆盖）",total.path});
    FILETIME ft{};GetSystemTimeAsFileTime(&ft);const auto time=timestamp(ticks(ft));
    std::map<std::wstring,const Connection*> oldMap,newMap;for(const auto& c:connections)oldMap[c.key]=&c;for(const auto& c:next)newMap[c.key]=&c;
    auto event=[&](const wchar_t* kind,const Connection& c){events.push_back({time,kind,c.process,std::to_wstring(c.pid),c.protocol,c.local,c.remote,c.state,c.scope,c.localDns,c.remoteDns,c.path,c.started});};
    if(complete){
        for(const auto& c:next){const auto found=oldMap.find(c.key);if(found==oldMap.end())event(initialized?L"出现":L"首次观测",c);else if(found->second->state!=c.state)event(L"状态改变",c);}
        if(initialized)for(const auto& c:connections)if(!newMap.count(c.key))event(L"消失",c);
        connections=std::move(next);initialized=true;
    }
    if(events.size()>20000)events.erase(events.begin(),events.end()-20000);
    sampleComplete=complete;
    table.summary=std::to_wstring(table.rows.size())+L" 个端点"+(complete?L"":L"；部分连接表读取失败")+L"。PTR 是 IP 反查名称，不代表实际请求域名或 URL；反查会向系统配置的 DNS 服务发出查询。\r\n地址范围不代表是否安全；代理/VPN 的下一跳可能是本机。接口合计可能重复包含虚拟网卡；TCP 测速受权限限制，不含 UDP/QUIC。";
    return table;
}
Table Network::log() const {return {{L"时间",L"事件",L"进程",L"PID",L"协议",L"本机端点",L"远端端点",L"状态",L"通信范围",L"本机名称 / PTR",L"远端 PTR 域名",L"程序路径",L"进程启动标识"},events,L"本次运行保留最近 20,000 条变化，可导出；达到容量后淘汰最早记录。域名为事件时的反查结果；一秒采样可能漏掉短连接。出现/消失不等于攻击或泄露。"};}

DiskActivity::~DiskActivity(){reset();}
void DiskActivity::reset(){
    if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);handle=INVALID_HANDLE_VALUE;disk=MAXDWORD;
    previousRead=0;previousWrite=0;previousTime=0;
}
ActivityReading DiskActivity::sample(const std::wstring& target){
    ActivityReading result;DWORD requested=MAXDWORD;bool multipleDisks=false;
    if(target.rfind(L"磁盘 ",0)==0){
        const wchar_t* start=target.c_str()+3;wchar_t* end=nullptr;const auto value=wcstoul(start,&end,10);
        if(end!=start && value<=MAXDWORD)requested=static_cast<DWORD>(value);
    }else if(target.size()>=2 && target[1]==L':'){
        const std::wstring volume=L"\\\\.\\"+target.substr(0,2);
        HANDLE volumeHandle=CreateFileW(volume.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(volumeHandle!=INVALID_HANDLE_VALUE){
            std::array<BYTE,4096> buffer{};DWORD returned=0;
            if(DeviceIoControl(volumeHandle,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,nullptr,0,buffer.data(),static_cast<DWORD>(buffer.size()),&returned,nullptr)){
                const auto* extents=reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
                if(extents->NumberOfDiskExtents==1)requested=extents->Extents[0].DiskNumber;
                else if(extents->NumberOfDiskExtents>1)multipleDisks=true;
            }
            CloseHandle(volumeHandle);
        }
    }
    if(requested==MAXDWORD){reset();result.status=multipleDisks?L"当前卷跨多个物理磁盘；为避免误报，未显示单盘吞吐":L"当前路径未映射到本机物理磁盘";return result;}
    result.target=L"磁盘 "+std::to_wstring(requested);
    if(requested!=disk || handle==INVALID_HANDLE_VALUE){reset();disk=requested;const auto name=L"\\\\.\\PhysicalDrive"+std::to_wstring(disk);
        handle=CreateFileW(name.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    }
    if(handle==INVALID_HANDLE_VALUE){result.status=L"无法打开 "+result.target+L" 的性能计数："+errorText(GetLastError());return result;}
    DISK_PERFORMANCE performance{};DWORD returned=0;
    if(!DeviceIoControl(handle,IOCTL_DISK_PERFORMANCE,nullptr,0,&performance,sizeof(performance),&returned,nullptr)){
        result.status=result.target+L" 未提供磁盘性能计数："+errorText(GetLastError());return result;
    }
    const uint64_t now=GetTickCount64(),read=static_cast<uint64_t>(performance.BytesRead.QuadPart),written=static_cast<uint64_t>(performance.BytesWritten.QuadPart);
    uint64_t readRate=0,writeRate=0;const bool valid=previousTime && counterRate(read,previousRead,now-previousTime,readRate) && counterRate(written,previousWrite,now-previousTime,writeRate);
    previousRead=read;previousWrite=written;previousTime=now;
    if(!valid){result.status=L"已连接 "+result.target+L"，等待下一次采样";return result;}
    result.primary=readRate;result.secondary=writeRate;result.available=true;result.status=L"物理磁盘累计字节差分，1 秒采样";return result;
}

void exportCsv(const Table& table,const std::filesystem::path& path){
    std::ofstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot create export file");file<<"\xef\xbb\xbf";
    auto write=[&](const Row& row){for(size_t i=0;i<row.size();++i){if(i)file<<',';std::wstring safe=row[i];
            if(!safe.empty() && (safe[0]==L'=' || safe[0]==L'+' || safe[0]==L'-' || safe[0]==L'@'))safe=L"'"+safe;
            std::wstring escaped=L"\"";for(wchar_t c:safe){if(c==L'\"')escaped+=L'\"';escaped+=c;}escaped+=L'\"';
            const int count=WideCharToMultiByte(CP_UTF8,0,escaped.data(),static_cast<int>(escaped.size()),nullptr,0,nullptr,nullptr);std::string utf8(static_cast<size_t>(count),'\0');
            WideCharToMultiByte(CP_UTF8,0,escaped.data(),static_cast<int>(escaped.size()),utf8.data(),count,nullptr,nullptr);file<<utf8;
        }file<<"\r\n";};
    write(table.columns);for(const auto& row:table.rows)write(row);if(!file)throw std::runtime_error("Cannot finish export file");
}
}
