#pragma once
#include "system_core.h"
#include "system_presentation.h"
#include <algorithm>

namespace systemdesk {
template<class Check> void testDiagnostics(Check&& check){
    const auto unknown=interpretEvent(L"Unrelated publisher",1000);
    check(interpretEvent(L"APPLICATION ERROR",1000).meaning==L"应用程序发生崩溃","event rules match provider case insensitively");
    check(unknown.meaning==L"查看原始事件内容" && interpretEvent(L"Application Error",999).meaning==unknown.meaning,"event IDs are never interpreted without matching publisher");
    check(interpretEvent(L"Microsoft-Windows-Kernel-Power",41).advice.find(L"不等于")!=std::wstring::npos,"unexpected shutdown is not diagnosed as a hardware failure");
    check(interpretEvent(L"Microsoft-Windows-Security-Auditing",4625).advice.find(L"不能单条判定攻击")!=std::wstring::npos,"failed login advice preserves uncertainty");
    check(registryParent(L"hkcu\\Software\\Microsoft\\")==L"HKEY_CURRENT_USER\\Software","registry parent normalizes aliases and trailing separators");
    check(registryParent(L"HKLM")==L"HKEY_LOCAL_MACHINE" && registryParent(L"HKCU\\Software")==L"HKEY_CURRENT_USER","registry navigation stops at hive root");
    check(interpretRegistry(L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\EventLog",L"Start").advice.find(L"0 引导")!=std::wstring::npos,"service startup values have specific interpretation");
    check(interpretRegistry(L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",L"ProxyEnable").meaning==L"用户代理开关","proxy configuration explained by field name");
    check(interpretRegistry(L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\RunExtra",L"App").meaning==L"注册表配置项","startup rule respects exact Run key boundary");
    std::atomic_bool cancel=false;LogQuery query;query.hours=0;
    check(eventLogs(query,cancel).summary==L"日志查询参数无效","event query rejects invalid time range");
    query.hours=24;query.eventId=65536;
    check(eventLogs(query,cancel).summary==L"日志查询参数无效","event query rejects overflowing event IDs");
    query.eventId=-1;query.channel=L"PICO-No-Such-Log-7f6b91";
    check(eventLogs(query,cancel).summary.starts_with(L"无法读取日志"),"missing log channel reports an error instead of a clean result");
    query.channel=L"System";cancel=true;
    const auto cancelled=eventLogs(query,cancel);
    check(cancelled.rows.empty() && cancelled.summary.find(L"已取消")!=std::wstring::npos,"pre-cancelled event query stops without enumerating records");
    cancel=false;query.hours=720;const auto logs=eventLogs(query,cancel);
    check(logs.columns.size()==11 && logs.summary.starts_with(L"System："),"native Windows event query succeeds with complete schema");
    check(logs.rows.size()<=300 && std::all_of(logs.rows.begin(),logs.rows.end(),[](const Row& r){return r.size()==11 && r[0]!=L"--" && r[2]!=L"--" && !r[6].empty() && r[10].find(L"<Event ")!=std::wstring::npos;}),"event records preserve typed metadata, original messages and XML");
    if(!logs.rows.empty()){
        query.eventId=std::stoi(logs.rows.front()[3]);const auto matching=eventLogs(query,cancel);
        check(!matching.rows.empty() && std::all_of(matching.rows.begin(),matching.rows.end(),[&](const Row& r){return r[3]==std::to_wstring(query.eventId);}),"Windows XPath filters event ID before row limits");
        check(presentation::details(logs,logs.rows.front()).find(L"原始 XML")!=std::wstring::npos,"event inspector includes original XML evidence");
    }
    query.eventId=-1;query.level=2;const auto errors=eventLogs(query,cancel);
    check(std::all_of(errors.rows.begin(),errors.rows.end(),[](const Row& r){return r[1]==L"错误" || r[1]==L"严重";}),"severity filter includes only errors and critical events");
    check(presentation::columns(logs,false).size()==5 && presentation::columns(logs,true).size()==11,"compact event columns preserve full data projection");
    const auto invalid=registryBrowse(L"INVALID\\Software",false);
    check(invalid.rows.empty() && invalid.summary.find(L"路径需以")!=std::wstring::npos,"invalid registry hive returns actionable error");
    check(registryBrowse(L"HKCU\\PICO-No-Such-Key-7f6b91",false).summary.starts_with(L"无法打开注册表"),"missing registry key does not masquerade as empty key");
    const std::wstring path=L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const auto registry=registryBrowse(path,false,L"CurrentBuildNumber");
    check(registry.rows.size()==1 && registry.rows[0][0]==L"CurrentBuildNumber" && registry.rows[0][1]==L"REG_SZ" && !registry.rows[0][2].empty(),"registry current-key filter retains exact native string value");
    check(!registry.rows.empty() && registry.rows[0][6]==L"64 位" && registry.rows[0][7]!=L"--" && registry.rows[0][9]==L"值","registry records view, key timestamp and value category");
    const auto wow=registryBrowse(path,true,L"CurrentBuildNumber");
    check(!wow.rows.empty() && wow.rows[0][6]==L"32 位","32-bit registry view can be queried without changing 64-bit view");
    const auto root=registryBrowse(L"HKCU",false);
    check(std::any_of(root.rows.begin(),root.rows.end(),[](const Row& r){return r[0]==L"Software" && r[9]==L"子键" && r[5]==L"HKEY_CURRENT_USER\\Software";}),"registry child key rows expose navigable canonical paths");
    check(presentation::columns(registry,false).size()==4 && presentation::columns(registry,true).size()==10,"compact registry columns retain full schema");
    cancel=true;const auto stopped=registryBrowse(L"HKCU",false,L"",&cancel);
    check(stopped.rows.empty() && stopped.summary.find(L"已取消")!=std::wstring::npos,"registry enumeration observes cancellation");
}
}
