#include "system_core.h"
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <algorithm>
#include <set>

namespace systemdesk {
namespace {
std::wstring property(HDEVINFO devices,SP_DEVINFO_DATA& device,const DEVPROPKEY& key){
    DEVPROPTYPE type=0;DWORD size=0;
    SetupDiGetDevicePropertyW(devices,&device,&key,&type,nullptr,0,&size,0);
    if(!size || size>65536)return L"--";
    std::vector<BYTE> data(size+sizeof(wchar_t),0);
    if(!SetupDiGetDevicePropertyW(devices,&device,&key,&type,data.data(),size,nullptr,0))return L"--";
    if(type==DEVPROP_TYPE_STRING)return reinterpret_cast<const wchar_t*>(data.data());
    if(type==DEVPROP_TYPE_FILETIME && size==sizeof(FILETIME)){
        FILETIME value{};memcpy(&value,data.data(),sizeof(value));
        SYSTEMTIME date{};if(!FileTimeToSystemTime(&value,&date))return L"--";
        wchar_t text[24]{};swprintf_s(text,L"%04u-%02u-%02u",date.wYear,date.wMonth,date.wDay);return text;
    }
    if(type==DEVPROP_TYPE_STRING_LIST){
        std::wstring result;const auto* text=reinterpret_cast<const wchar_t*>(data.data());
        const auto* end=text+size/sizeof(wchar_t);
        while(text<end && *text){const size_t length=wcsnlen_s(text,static_cast<size_t>(end-text));if(!result.empty())result+=L"; ";result.append(text,length);text+=length+1;}
        return result.empty()?L"--":result;
    }
    return L"--";
}
}
Table hardwareDevices(){
    Table table{{L"设备类别",L"设备名称",L"状态",L"制造商",L"驱动版本",L"驱动提供商",L"驱动日期",L"驱动 INF",L"位置",L"设备实例 ID",L"硬件 ID"},{},L""};
    const auto devices=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    if(devices==INVALID_HANDLE_VALUE){table.summary=errorText(GetLastError());return table;}
    struct Release{HDEVINFO value;~Release(){SetupDiDestroyDeviceInfoList(value);}} release{devices};
    DWORD failure=ERROR_SUCCESS;std::set<std::wstring> categories;
    for(DWORD n=0;;++n){
        SP_DEVINFO_DATA device{sizeof(device)};
        if(!SetupDiEnumDeviceInfo(devices,n,&device)){failure=GetLastError();break;}
        wchar_t category[512]{};if(!SetupDiGetClassDescriptionW(&device.ClassGuid,category,512,nullptr))wcscpy_s(category,L"其他设备");
        categories.insert(category);
        auto name=property(devices,device,DEVPKEY_Device_FriendlyName);
        if(name==L"--")name=property(devices,device,DEVPKEY_Device_DeviceDesc);
        if(name==L"--")name=property(devices,device,DEVPKEY_Device_BusReportedDeviceDesc);
        ULONG status=0,problem=0;std::wstring state=L"状态不可读";
        if(CM_Get_DevNode_Status(&status,&problem,device.DevInst,0)==CR_SUCCESS){
            state=problem?L"问题代码 "+std::to_wstring(problem):(status&DN_STARTED)?L"运行中":L"已枚举 / 未启动";
            if(problem==CM_PROB_DISABLED)state=L"已禁用（代码 22）";
        }
        DWORD required=0;SetupDiGetDeviceInstanceIdW(devices,&device,nullptr,0,&required);
        std::vector<wchar_t> instance(static_cast<size_t>(required)+1,0);
        if(!required || !SetupDiGetDeviceInstanceIdW(devices,&device,instance.data(),static_cast<DWORD>(instance.size()),nullptr))instance={L'-',L'-',0};
        table.rows.push_back({category,name,state,property(devices,device,DEVPKEY_Device_Manufacturer),
            property(devices,device,DEVPKEY_Device_DriverVersion),property(devices,device,DEVPKEY_Device_DriverProvider),
            property(devices,device,DEVPKEY_Device_DriverDate),property(devices,device,DEVPKEY_Device_DriverInfPath),
            property(devices,device,DEVPKEY_Device_LocationInfo),instance.data(),property(devices,device,DEVPKEY_Device_HardwareIds)});
    }
    std::sort(table.rows.begin(),table.rows.end(),[](const Row& a,const Row& b){const int category=_wcsicmp(a[0].c_str(),b[0].c_str());return category?category<0:_wcsicmp(a[1].c_str(),b[1].c_str())<0;});
    table.summary=std::to_wstring(table.rows.size())+L" 个当前已枚举设备，"+std::to_wstring(categories.size())+L" 个类别；包括虚拟设备。驱动信息来自 Windows 设备属性，-- 表示未提供。";
    if(failure!=ERROR_NO_MORE_ITEMS)table.summary+=L" 枚举未完成："+errorText(failure);
    return table;
}
}
