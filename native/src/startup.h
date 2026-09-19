#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace startup {
inline constexpr wchar_t RunKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t RunValue[]=L"PicoPet.Win11";
inline bool enabled(){
    DWORD bytes=0,type=0;
    return RegGetValueW(HKEY_CURRENT_USER,RunKey,RunValue,RRF_RT_REG_SZ,&type,nullptr,&bytes)==ERROR_SUCCESS && bytes>sizeof(wchar_t);
}
inline bool setEnabled(bool value){
    HKEY key=nullptr;
    if(!value){
        const auto opened=RegOpenKeyExW(HKEY_CURRENT_USER,RunKey,0,KEY_SET_VALUE,&key);
        if(opened==ERROR_FILE_NOT_FOUND)return true;if(opened!=ERROR_SUCCESS)return false;
        const auto result=RegDeleteValueW(key,RunValue);RegCloseKey(key);
        return result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND;
    }
    wchar_t executable[32768]{};const DWORD size=GetModuleFileNameW(nullptr,executable,32768);
    if(!size || size>=32768)return false;
    const std::wstring command=L"\""+std::wstring(executable)+L"\" --startup";
    if(RegCreateKeyExW(HKEY_CURRENT_USER,RunKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    const auto result=RegSetValueExW(key,RunValue,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    RegCloseKey(key);return result==ERROR_SUCCESS;
}
struct Display {std::wstring device,label;RECT work{};bool primary=false;};
inline std::vector<Display> displays(){
    std::vector<Display> result;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{
        MONITORINFOEXW info{};info.cbSize=sizeof(info);if(!GetMonitorInfoW(monitor,&info))return TRUE;
        DISPLAY_DEVICEW panel{};panel.cb=sizeof(panel);EnumDisplayDevicesW(info.szDevice,0,&panel,0);
        const std::wstring device=info.szDevice;const auto number=device.find_last_not_of(L"0123456789");
        std::wstring label=L"屏幕 "+device.substr(number==std::wstring::npos?0:number+1);
        if(info.dwFlags&MONITORINFOF_PRIMARY)label+=L" · 主屏";
        if(panel.DeviceString[0])label+=L" · "+std::wstring(panel.DeviceString);
        label+=L" · "+std::to_wstring(info.rcMonitor.right-info.rcMonitor.left)+L" × "+std::to_wstring(info.rcMonitor.bottom-info.rcMonitor.top);
        reinterpret_cast<std::vector<Display>*>(data)->push_back({device,label,info.rcWork,(info.dwFlags&MONITORINFOF_PRIMARY)!=0});return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    std::sort(result.begin(),result.end(),[](const Display& a,const Display& b){return a.device.size()!=b.device.size()?a.device.size()<b.device.size():a.device<b.device;});
    return result;
}
// Empty selection preserves the previous position. A missing selected monitor
// falls back to primary without discarding the saved selection.
inline const Display* select(const std::vector<Display>& list,const std::wstring& device){
    if(device.empty())return nullptr;
    const Display* primary=nullptr;
    for(const auto& display:list){if(display.device==device)return &display;if(display.primary)primary=&display;}
    return primary?primary:list.empty()?nullptr:&list.front();
}
inline bool contains(const RECT& area,POINT point){return point.x>=area.left && point.x<area.right && point.y>=area.top && point.y<area.bottom;}
}
