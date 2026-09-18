#pragma once
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace systemdesk {
using Row=std::vector<std::wstring>;
struct Table { std::vector<std::wstring> columns;std::vector<Row> rows;std::wstring summary; };
std::wstring bytes(uint64_t value);
std::wstring timestamp(uint64_t value);
std::wstring errorText(DWORD code);
std::filesystem::path dataDirectory();

class Performance {
    uint64_t previousIdle=0,previousKernel=0,previousUser=0;
    std::vector<Row> devices;
public:
    Table sample();
};
class ProcessPerformance {
    struct Counter {uint64_t created=0,cpu=0,read=0,written=0,time=0;std::wstring path;bool ioValid=false;};
    std::map<DWORD,Counter> previous;
public:
    Table sample();
};
Table hardwareDevices();
std::wstring addressScope(const std::wstring& address);
std::wstring reverseDnsName(const std::wstring& address);
bool counterRate(uint64_t current,uint64_t previous,uint64_t elapsedMs,uint64_t& result);
struct Connection {
    std::wstring key,protocol,local,remote,state,process,path,started;
    std::wstring localIp,remoteIp,localDns,remoteDns,scope,localScope,received=L"--",sent=L"--",statistics;
    DWORD pid=0,tcpStatus=0,localZone=0,remoteZone=0;
    uint16_t localPort=0,remotePort=0;
    int family=0;
    bool tcp=false,measured=false;
    uint64_t receiveRate=0,sendRate=0;
};
class NetworkDetails {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    NetworkDetails();
    ~NetworkDetails();
    void enrich(std::vector<Connection>& rows,bool dns,bool measure);
    void stop();
};
class NetworkTrace {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    NetworkTrace();~NetworkTrace();
    DWORD start();void stop();Table snapshot();bool running()const;
};
bool testNetworkTraceParser();
class Network {
    struct Adapter { uint64_t id,received,sent; };
    std::vector<Adapter> previous;
    std::vector<Connection> connections;
    ULONGLONG lastTime=0;
    bool initialized=false;
    NetworkDetails details;
public:
    bool resolveDns=true;
    bool measureConnections=false;
    std::vector<Row> events;
    std::wstring rates;
    uint64_t receiveRate=0,sendRate=0;
    bool rateAvailable=false;
    bool sampleComplete=false;
    Table adapters;
    Table processes;
    Table sample();
    Table log() const;
    void clearLog(){events.clear();}
    void resetBaseline(){previous.clear();connections.clear();lastTime=0;initialized=false;receiveRate=0;sendRate=0;rateAvailable=false;details.stop();}
};

struct ActivityReading {
    uint64_t primary=0,secondary=0;
    bool available=false;
    std::wstring target,status;
};
class DiskActivity {
    HANDLE handle=INVALID_HANDLE_VALUE;
    DWORD disk=MAXDWORD;
    uint64_t previousRead=0,previousWrite=0,previousTime=0;
public:
    ~DiskActivity();
    ActivityReading sample(const std::wstring& target);
    void reset();
};

struct Snapshot { int64_t id=0;std::wstring root,created,status;uint64_t files=0,errors=0; };
struct ScanProgress { std::atomic<uint64_t> files{0},bytes{0},errors{0}; };
class Index {
    std::filesystem::path database;
public:
    explicit Index(std::filesystem::path path):database(std::move(path)){}
    std::vector<Snapshot> snapshots();
    int64_t scan(const std::wstring& root,bool resume,std::atomic_bool& cancel,ScanProgress& progress);
    Table compare(int64_t before,int64_t after,const std::wstring& filter=L"");
    Table browseSnapshot(int64_t id,const std::wstring& filter=L"");
    void removeSnapshot(int64_t id);
};
Table volumes();
Table browse(const std::wstring& directory);
struct LogQuery {std::wstring channel=L"System",filter;int hours=24,level=0,eventId=-1;};
struct Interpretation {std::wstring meaning,advice;};
Interpretation interpretEvent(const std::wstring& provider,DWORD id);
Interpretation interpretRegistry(const std::wstring& path,const std::wstring& name);
Table eventLogs(const LogQuery& query,std::atomic_bool& cancel);
Table registryBrowse(const std::wstring& path,bool view32,const std::wstring& filter=L"",const std::atomic_bool* cancel=nullptr);
std::wstring registryParent(const std::wstring& path);
Table securityOverview();
struct ConsoleRequest {std::wstring input,directory;int mode=0;bool conciseOutput=false;};
Table runConsoleCommand(const ConsoleRequest& request,std::atomic_bool& cancel);
class SecurityTracker {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    SecurityTracker();
    ~SecurityTracker();
    std::wstring start(const std::wstring& target);
    void stop();
    void clear();
    bool active() const;
    uint64_t generation() const;
    Table table() const;
};
void exportCsv(const Table& table,const std::filesystem::path& path);
void open(int page=0);
void shutdown();
void suspend(bool value);
bool isWindow(HWND hwnd);
int selfTest(const std::filesystem::path& report);
}
