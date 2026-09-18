#include "system_core.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <vector>

namespace systemdesk {
namespace {
std::wstring consoleTime(){SYSTEMTIME time{};GetLocalTime(&time);wchar_t value[32]{};swprintf_s(value,L"%04u-%02u-%02u %02u:%02u:%02u",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);return value;}
std::wstring quote(const std::wstring& value){
    std::wstring result=L"\"";size_t slashes=0;
    for(const wchar_t c:value){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){result.append(slashes*2+1,L'\\');result+=c;slashes=0;continue;}result.append(slashes,L'\\');slashes=0;result+=c;}
    result.append(slashes*2,L'\\');return result+L"\"";
}
std::wstring decode(const std::vector<BYTE>& bytes){
    if(bytes.empty())return L"";UINT codePage=CP_UTF8;DWORD flags=MB_ERR_INVALID_CHARS;
    int size=MultiByteToWideChar(codePage,flags,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),nullptr,0);
    if(!size){codePage=CP_OEMCP;flags=0;size=MultiByteToWideChar(codePage,flags,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),nullptr,0);}
    std::wstring result(static_cast<size_t>(std::max(size,0)),L'\0');if(size)MultiByteToWideChar(codePage,flags,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),result.data(),size);return result;
}
}

Table runConsoleCommand(const ConsoleRequest& request,std::atomic_bool& cancel){
    Table table{{L"记录时间",L"类型",L"状态",L"退出码",L"耗时",L"命令 / 提示",L"工作目录",L"完整输出"},{},L""};
    if(request.input.empty()){table.summary=L"请输入命令或 Codex 提示。";return table;}
    std::filesystem::path directory=request.directory.empty()?std::filesystem::current_path():std::filesystem::path(request.directory);
    std::error_code ec;directory=std::filesystem::absolute(directory,ec).lexically_normal();if(ec || !std::filesystem::is_directory(directory,ec)){table.summary=L"工作目录不存在或不可访问。";return table;}
    std::array<wchar_t,32768> comspec{};DWORD count=GetEnvironmentVariableW(L"ComSpec",comspec.data(),static_cast<DWORD>(comspec.size()));const std::wstring shell=count?comspec.data():L"C:\\Windows\\System32\\cmd.exe";
    const wchar_t* type=request.mode==0?L"CMD":request.mode==1?L"Codex 只读":L"Codex 工作区";
    std::wstring arguments;std::filesystem::path finalOutput;
    if(request.mode==0)arguments=quote(shell)+L" /d /s /c "+request.input;
    else {
        arguments=quote(shell)+L" /d /s /c codex -a never exec --skip-git-repo-check --ephemeral --color never -s "+std::wstring(request.mode==1?L"read-only":L"workspace-write")+L" -C "+quote(directory.wstring());
        if(request.conciseOutput){finalOutput=std::filesystem::temp_directory_path()/(L"pico-codex-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".txt");arguments+=L" -o "+quote(finalOutput.wstring());}
        arguments+=L" -";
    }
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE outputRead=nullptr,outputWrite=nullptr,inputRead=nullptr,inputWrite=nullptr;
    if(!CreatePipe(&outputRead,&outputWrite,&security,0) || !SetHandleInformation(outputRead,HANDLE_FLAG_INHERIT,0) || !CreatePipe(&inputRead,&inputWrite,&security,0) || !SetHandleInformation(inputWrite,HANDLE_FLAG_INHERIT,0)){
        if(outputRead)CloseHandle(outputRead);if(outputWrite)CloseHandle(outputWrite);if(inputRead)CloseHandle(inputRead);if(inputWrite)CloseHandle(inputWrite);table.summary=L"无法创建命令管道："+errorText(GetLastError());return table;
    }
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;startup.hStdInput=inputRead;startup.hStdOutput=outputWrite;startup.hStdError=outputWrite;
    HANDLE job=CreateJobObjectW(nullptr,nullptr);if(job){JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){CloseHandle(job);job=nullptr;}}
    PROCESS_INFORMATION process{};std::vector<wchar_t> mutableCommand(arguments.begin(),arguments.end());mutableCommand.push_back(L'\0');const auto started=std::chrono::steady_clock::now();
    const BOOL created=CreateProcessW(nullptr,mutableCommand.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,directory.c_str(),&startup,&process);const DWORD createError=created?ERROR_SUCCESS:GetLastError();
    CloseHandle(inputRead);CloseHandle(outputWrite);
    if(!created){if(job)CloseHandle(job);CloseHandle(outputRead);CloseHandle(inputWrite);table.summary=L"无法启动命令："+errorText(createError);return table;}
    if(job && !AssignProcessToJobObject(job,process.hProcess)){CloseHandle(job);job=nullptr;}ResumeThread(process.hThread);
    if(request.mode>0){const int utf8Size=WideCharToMultiByte(CP_UTF8,0,request.input.c_str(),static_cast<int>(request.input.size()),nullptr,0,nullptr,nullptr);std::vector<char> prompt(static_cast<size_t>(utf8Size));WideCharToMultiByte(CP_UTF8,0,request.input.c_str(),static_cast<int>(request.input.size()),prompt.data(),utf8Size,nullptr,nullptr);DWORD written=0;if(!prompt.empty())WriteFile(inputWrite,prompt.data(),static_cast<DWORD>(prompt.size()),&written,nullptr);}
    CloseHandle(inputWrite);std::vector<BYTE> output;output.reserve(65536);bool truncated=false,cancelled=false;
    for(;;){
        DWORD available=0;if(PeekNamedPipe(outputRead,nullptr,0,nullptr,&available,nullptr) && available){std::array<BYTE,8192> chunk{};DWORD read=0;if(ReadFile(outputRead,chunk.data(),std::min<DWORD>(available,static_cast<DWORD>(chunk.size())),&read,nullptr) && read){const size_t room=512*1024-output.size();const size_t keep=std::min<size_t>(read,room);output.insert(output.end(),chunk.begin(),chunk.begin()+keep);if(keep<read)truncated=true;}}
        if(cancel.load()){if(job)TerminateJobObject(job,ERROR_CANCELLED);else TerminateProcess(process.hProcess,ERROR_CANCELLED);cancelled=true;}
        if(WaitForSingleObject(process.hProcess,50)==WAIT_OBJECT_0)break;
    }
    for(;;){DWORD available=0;if(!PeekNamedPipe(outputRead,nullptr,0,nullptr,&available,nullptr) || !available)break;std::array<BYTE,8192> chunk{};DWORD read=0;if(!ReadFile(outputRead,chunk.data(),std::min<DWORD>(available,static_cast<DWORD>(chunk.size())),&read,nullptr) || !read)break;const size_t room=512*1024-output.size(),keep=std::min<size_t>(read,room);output.insert(output.end(),chunk.begin(),chunk.begin()+keep);if(keep<read)truncated=true;}
    DWORD exitCode=0;GetExitCodeProcess(process.hProcess,&exitCode);if(job)CloseHandle(job);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(outputRead);
    auto text=decode(output);
    if(request.conciseOutput && request.mode>0 && !finalOutput.empty()){
        std::ifstream file(finalOutput,std::ios::binary);if(file){std::vector<BYTE> finalBytes((std::istreambuf_iterator<char>(file)),{});const auto finalText=decode(finalBytes);if(!finalText.empty())text=finalText;}
        std::filesystem::remove(finalOutput,ec);
    }
    if(truncated)text+=L"\r\n\r\n[输出超过 512 KiB，后续内容已截断]";if(text.empty())text=L"（命令没有输出）";
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();const std::wstring state=cancelled?L"已停止":exitCode==0?L"完成":L"失败";
    table.rows.push_back({consoleTime(),type,state,std::to_wstring(exitCode),std::to_wstring(elapsed)+L" ms",request.input,directory.wstring(),text});
    table.summary=std::wstring(type)+L"："+state+L"，退出码 "+std::to_wstring(exitCode)+L"，耗时 "+std::to_wstring(elapsed)+L" ms。命令以当前用户权限运行；输出只保存在当前窗口历史中。";return table;
}
}
