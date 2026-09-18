#include "system_core.h"
#include "system_network_tests.h"
#include "system_diagnostics_tests.h"
#include "system_presentation.h"
#include "system_observations.h"
#include "system_cards_tests.h"
#include <winsqlite/winsqlite3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace systemdesk {
namespace {
struct Database {
    sqlite3* handle=nullptr;
    explicit Database(const std::filesystem::path& path){
        if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());
        if(sqlite3_open16(path.c_str(),&handle)!=SQLITE_OK){if(handle)sqlite3_close(handle);handle=nullptr;throw std::runtime_error("Open snapshot database");}
        // Schema setup can throw (SQLITE_BUSY on WAL under concurrent open). A throwing constructor never
        // runs the destructor, so close explicitly here instead of leaking the connection for the process lifetime.
        try{
        sqlite3_busy_timeout(handle,3000);
        sqlite3_create_collation16(handle,L"WINPATH",SQLITE_UTF16,nullptr,[](void*,int an,const void* a,int bn,const void* b){return CompareStringOrdinal(static_cast<const wchar_t*>(a),an/2,static_cast<const wchar_t*>(b),bn/2,TRUE)-CSTR_EQUAL;});
        exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-2048;"
             "CREATE TABLE IF NOT EXISTS snapshots(id INTEGER PRIMARY KEY,root TEXT,serial INTEGER,created INTEGER,status TEXT,files INTEGER DEFAULT 0,errors INTEGER DEFAULT 0);"
             "CREATE TABLE IF NOT EXISTS entries(snapshot INTEGER,path TEXT COLLATE WINPATH,size INTEGER,modified INTEGER,created INTEGER,attrs INTEGER,PRIMARY KEY(snapshot,path));"
             "CREATE TABLE IF NOT EXISTS pending(snapshot INTEGER,path TEXT COLLATE WINPATH,PRIMARY KEY(snapshot,path));"
             "CREATE TABLE IF NOT EXISTS errors(snapshot INTEGER,path TEXT COLLATE WINPATH,code INTEGER,PRIMARY KEY(snapshot,path));");
        }catch(...){sqlite3_close(handle);handle=nullptr;throw;}
    }
    ~Database(){if(handle)sqlite3_close(handle);}
    void exec(const char* sql){if(sqlite3_exec(handle,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(handle));}
};
struct Statement {
    sqlite3_stmt* handle=nullptr;
    explicit Statement(Database& db,const char* sql){if(sqlite3_prepare_v2(db.handle,sql,-1,&handle,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db.handle));}
    ~Statement(){sqlite3_finalize(handle);}
    void bind(int i,int64_t v){if(sqlite3_bind_int64(handle,i,v)!=SQLITE_OK)throw std::runtime_error("Bind snapshot value");}
    void bind(int i,const std::wstring& v){if(sqlite3_bind_text16(handle,i,v.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error("Bind snapshot path");}
    bool next(){const int status=sqlite3_step(handle);if(status==SQLITE_ROW)return true;if(status!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(handle)));return false;}
    void reset(){sqlite3_reset(handle);sqlite3_clear_bindings(handle);}
    int64_t number(int i)const{return sqlite3_column_int64(handle,i);}
    std::wstring text(int i)const{const auto* value=static_cast<const wchar_t*>(sqlite3_column_text16(handle,i));return value?value:L"";}
};
uint64_t ticks(FILETIME time){return (static_cast<uint64_t>(time.dwHighDateTime)<<32)|time.dwLowDateTime;}
std::wstring normalized(const std::wstring& input){
    if(input.empty() || input.rfind(L"\\\\",0)==0)throw std::runtime_error("Only local folders are supported");
    std::wstring root=std::filesystem::absolute(input).lexically_normal().wstring();
    while(root.size()>3 && root.back()==L'\\')root.pop_back();
    wchar_t volume[32768]{};
    if(!GetVolumePathNameW(root.c_str(),volume,32768) || GetDriveTypeW(volume)==DRIVE_REMOTE)throw std::runtime_error("Choose an accessible local volume");
    const DWORD attr=GetFileAttributesW(root.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES || !(attr&FILE_ATTRIBUTE_DIRECTORY) || (attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Choose an accessible folder, not a junction or cloud placeholder");
    return root;
}
DWORD serial(const std::wstring& root){wchar_t volume[32768]{};DWORD value=0;
    if(!GetVolumePathNameW(root.c_str(),volume,32768) || !GetVolumeInformationW(volume,nullptr,0,&value,nullptr,nullptr,nullptr,0))throw std::runtime_error("Cannot identify selected volume");return value;
}
void updateCounts(Database& db,int64_t id,const wchar_t* status,ScanProgress& progress){
    Statement update(db,"UPDATE snapshots SET status=?1,files=(SELECT count(*) FROM entries WHERE snapshot=?2),errors=(SELECT count(*) FROM errors WHERE snapshot=?2) WHERE id=?2");
    update.bind(1,std::wstring(status));update.bind(2,id);update.next();
    Statement count(db,"SELECT files,errors FROM snapshots WHERE id=?1");count.bind(1,id);count.next();progress.files=static_cast<uint64_t>(count.number(0));progress.errors=static_cast<uint64_t>(count.number(1));
}
}

std::vector<Snapshot> Index::snapshots(){
    Database db(database);Statement rows(db,"SELECT id,root,created,status,files,errors FROM snapshots ORDER BY id DESC LIMIT 100");std::vector<Snapshot> result;
    while(rows.next())result.push_back({rows.number(0),rows.text(1),timestamp(static_cast<uint64_t>(rows.number(2))),rows.text(3),static_cast<uint64_t>(rows.number(4)),static_cast<uint64_t>(rows.number(5))});return result;
}
int64_t Index::scan(const std::wstring& input,bool resume,std::atomic_bool& cancel,ScanProgress& progress){
    const auto root=normalized(input);const DWORD volume=serial(root);Database db(database);int64_t id=0;
    if(resume){Statement find(db,"SELECT id FROM snapshots WHERE root=?1 COLLATE WINPATH AND serial=?2 AND status IN ('paused','running') ORDER BY id DESC LIMIT 1");find.bind(1,root);find.bind(2,volume);if(find.next())id=find.number(0);else throw std::runtime_error("No paused snapshot for this folder and volume");}
    else {
        db.exec("BEGIN IMMEDIATE");
        FILETIME now{};GetSystemTimeAsFileTime(&now);Statement create(db,"INSERT INTO snapshots(root,serial,created,status) VALUES(?1,?2,?3,'running')");
        create.bind(1,root);create.bind(2,volume);create.bind(3,static_cast<int64_t>(ticks(now)));create.next();id=sqlite3_last_insert_rowid(db.handle);
        Statement queue(db,"INSERT INTO pending VALUES(?1,?2)");queue.bind(1,id);queue.bind(2,root);queue.next();
        db.exec("COMMIT");
    }
    updateCounts(db,id,L"running",progress);progress.bytes=0;
    // Exclude the scanner's own database from its snapshots; compare case-insensitively and build the strings once.
    const std::wstring selfPath=database.wstring(),selfWal=selfPath+L"-wal",selfShm=selfPath+L"-shm";
    Statement item(db,"INSERT OR REPLACE INTO entries VALUES(?1,?2,?3,?4,?5,?6)");
    Statement enqueue(db,"INSERT OR IGNORE INTO pending VALUES(?1,?2)");
    Statement error(db,"INSERT OR REPLACE INTO errors VALUES(?1,?2,?3)");
    auto recordError=[&](const std::wstring& path,DWORD code){error.bind(1,id);error.bind(2,path);error.bind(3,code);error.next();error.reset();++progress.errors;};
    try {
        while(!cancel){
            std::wstring directory;
            {Statement next(db,"SELECT path FROM pending WHERE snapshot=?1 LIMIT 1");next.bind(1,id);if(!next.next())break;directory=next.text(0);}
            db.exec("BEGIN IMMEDIATE");
            WIN32_FIND_DATAW data{};const std::wstring pattern=(std::filesystem::path(directory)/L"*").wstring();
            HANDLE handle=FindFirstFileExW(pattern.c_str(),FindExInfoBasic,&data,FindExSearchNameMatch,nullptr,FIND_FIRST_EX_LARGE_FETCH);
            if(handle==INVALID_HANDLE_VALUE){const DWORD code=GetLastError();if(code!=ERROR_FILE_NOT_FOUND)recordError(directory,code);}
            else {
                try {
                    do {
                        if(cancel)break;
                        if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;
                        const auto path=(std::filesystem::path(directory)/data.cFileName).wstring();
                        // Exclude the scanner's own changing database from its snapshots.
                        if(_wcsicmp(path.c_str(),selfPath.c_str())==0 || _wcsicmp(path.c_str(),selfWal.c_str())==0 || _wcsicmp(path.c_str(),selfShm.c_str())==0)continue;
                        const uint64_t size=(static_cast<uint64_t>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
                        item.bind(1,id);item.bind(2,path);item.bind(3,static_cast<int64_t>(size));item.bind(4,static_cast<int64_t>(ticks(data.ftLastWriteTime)));item.bind(5,static_cast<int64_t>(ticks(data.ftCreationTime)));item.bind(6,data.dwFileAttributes);item.next();item.reset();
                        ++progress.files;if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))progress.bytes+=size;
                        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
                            if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)recordError(path,ERROR_CANT_ACCESS_FILE);
                            else {enqueue.bind(1,id);enqueue.bind(2,path);enqueue.next();enqueue.reset();}
                        }
                    }while(FindNextFileW(handle,&data));
                    const DWORD last=GetLastError();if(!cancel && last!=ERROR_NO_MORE_FILES)recordError(directory,last);
                }catch(...){FindClose(handle);throw;}
                FindClose(handle);
            }
            if(cancel){db.exec("ROLLBACK");break;}
            Statement done(db,"DELETE FROM pending WHERE snapshot=?1 AND path=?2");done.bind(1,id);done.bind(2,directory);done.next();
            db.exec("COMMIT");
        }
        if(serial(root)!=volume)throw std::runtime_error("Volume changed while scanning; snapshot is incomplete");
        updateCounts(db,id,cancel?L"paused":L"complete",progress);
    }catch(...){sqlite3_exec(db.handle,"ROLLBACK",nullptr,nullptr,nullptr);updateCounts(db,id,L"paused",progress);throw;}
    return id;
}

Table Index::browseSnapshot(int64_t id,const std::wstring& filter){
    Database db(database);Table result{{L"路径",L"类型",L"大小",L"修改时间",L"创建时间",L"属性值"},{},L"快照目录（最多显示 3000 条，可按路径筛选；快照保存元数据，不包含文件内容）"};
    Statement rows(db,"SELECT path,size,modified,created,attrs FROM entries WHERE snapshot=?1 AND instr(lower(path),lower(?2))>0 ORDER BY path COLLATE WINPATH LIMIT 3000");rows.bind(1,id);rows.bind(2,filter);
    while(rows.next()){const auto attrs=rows.number(4);result.rows.push_back({rows.text(0),(attrs&FILE_ATTRIBUTE_DIRECTORY)?L"文件夹":L"文件",bytes(static_cast<uint64_t>(rows.number(1))),timestamp(static_cast<uint64_t>(rows.number(2))),timestamp(static_cast<uint64_t>(rows.number(3))),std::to_wstring(attrs)});}
    Statement errors(db,"SELECT path,code FROM errors WHERE snapshot=?1 LIMIT 100");errors.bind(1,id);
    while(errors.next())result.rows.push_back({errors.text(0),L"未覆盖",L"--",L"--",L"--",errorText(static_cast<DWORD>(errors.number(1)))});
    return result;
}
void Index::removeSnapshot(int64_t id){
    Database db(database);db.exec("BEGIN IMMEDIATE");
    for(const char* sql:{"DELETE FROM entries WHERE snapshot=?1","DELETE FROM pending WHERE snapshot=?1","DELETE FROM errors WHERE snapshot=?1","DELETE FROM snapshots WHERE id=?1"}){Statement remove(db,sql);remove.bind(1,id);remove.next();}
    db.exec("COMMIT");
}
Table Index::compare(int64_t before,int64_t after,const std::wstring& filter){
    Database db(database);std::wstring root;int64_t volume=0;
    for(const int64_t id:{before,after}){Statement row(db,"SELECT root,serial,status FROM snapshots WHERE id=?1");row.bind(1,id);
        if(!row.next() || row.text(2)!=L"complete")throw std::runtime_error("Only completed snapshots can be compared");
        if(root.empty()){root=row.text(0);volume=row.number(1);}else if(_wcsicmp(root.c_str(),row.text(0).c_str()) || volume!=row.number(1))throw std::runtime_error("Snapshots must refer to the same folder and volume");
    }
    Table result{{L"变化",L"完整路径",L"原大小",L"现大小",L"原修改时间",L"现修改时间",L"说明"},{},L"按路径、大小、修改/创建时间、属性比较；非内容哈希校验。未覆盖项不判定为新增/删除。最多显示 3000 条。快照为扫描期间的观测，不是 VSS 原子备份。"};
    const char* sql=R"SQL(
      WITH diff AS (
       SELECT CASE WHEN a.path IS NULL THEN 'added' ELSE 'modified' END kind,b.path,a.size oldsize,b.size newsize,a.modified oldtime,b.modified newtime,
         CASE WHEN a.path IS NULL THEN ?1 ELSE 0 END missing FROM entries b LEFT JOIN entries a ON a.snapshot=?1 AND a.path=b.path COLLATE WINPATH
         WHERE b.snapshot=?2 AND (a.path IS NULL OR a.size!=b.size OR a.modified!=b.modified OR a.created!=b.created OR a.attrs!=b.attrs)
       UNION ALL
       SELECT 'deleted',a.path,a.size,NULL,a.modified,NULL,?2 FROM entries a LEFT JOIN entries b ON b.snapshot=?2 AND b.path=a.path COLLATE WINPATH
         WHERE a.snapshot=?1 AND b.path IS NULL
      ) SELECT kind,path,oldsize,newsize,oldtime,newtime,
        EXISTS(SELECT 1 FROM errors er WHERE er.snapshot=missing AND (diff.path=er.path COLLATE WINPATH OR
          substr(diff.path,1,length(rtrim(er.path,char(92)))+1)=(rtrim(er.path,char(92))||char(92)) COLLATE WINPATH))
        FROM diff WHERE instr(lower(path),lower(?3))>0 ORDER BY path COLLATE WINPATH LIMIT 3000
    )SQL";
    Statement rows(db,sql);rows.bind(1,before);rows.bind(2,after);rows.bind(3,filter);
    while(rows.next()){
        const auto kind=rows.text(0);const bool unknown=rows.number(6)!=0;
        result.rows.push_back({unknown?L"未覆盖":kind==L"added"?L"新增":kind==L"deleted"?L"删除":L"修改",rows.text(1),kind==L"added"?L"--":bytes(static_cast<uint64_t>(rows.number(2))),kind==L"deleted"?L"--":bytes(static_cast<uint64_t>(rows.number(3))),timestamp(static_cast<uint64_t>(rows.number(4))),timestamp(static_cast<uint64_t>(rows.number(5))),unknown?L"存在无法读取或跳过的目录":L"元数据变化"});
    }return result;
}

int selfTest(const std::filesystem::path& report){
    const auto parent=std::filesystem::absolute(report.parent_path()).lexically_normal();std::filesystem::create_directories(parent);
    const auto dir=parent/(L"system-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if(!std::filesystem::create_directory(dir))return 1;
    std::filesystem::create_directory(dir/L"files");
    std::vector<std::string> failures;int checks=0;auto check=[&](bool ok,const char* name){++checks;if(!ok)failures.emplace_back(name);};
    try {
        const auto files=dir/L"files";
        {std::ofstream(files/L"changed.txt")<<"old";std::ofstream(files/L"removed.txt")<<"remove";std::ofstream(files/L"same.txt")<<"same";}
        Index index(dir/L"index.db");std::atomic_bool cancel=false;ScanProgress progress;
        const auto first=index.scan(files.wstring(),false,cancel,progress);
        {std::ofstream(files/L"changed.txt",std::ios::app)<<" changed";std::ofstream(files/L"added.txt")<<"new";}
        std::filesystem::remove(files/L"removed.txt");
        const auto second=index.scan(files.wstring(),false,cancel,progress);const auto difference=index.compare(first,second);
        check(difference.rows.size()==3,"snapshot diff contains exactly three changes");
        for(const auto* kind:{L"新增",L"修改",L"删除"})check(std::count_if(difference.rows.begin(),difference.rows.end(),[&](const Row& r){return r[0]==kind;})==1,"snapshot classifies each change");
        check(index.compare(first,second,L"changed.txt").rows.size()==1,"path filter narrows snapshot diff");
        check(index.browseSnapshot(first).rows.size()==3,"prior snapshot remains persistent and immutable");
        cancel=true;const auto paused=index.scan(files.wstring(),false,cancel,progress);check(index.snapshots()[0].status==L"paused","scan cancellation saves a resumable checkpoint");
        bool rejected=false;try{index.compare(first,paused);}catch(...){rejected=true;}check(rejected,"incomplete snapshots cannot report deleted files");
        cancel=false;check(index.scan(files.wstring(),true,cancel,progress)==paused,"scan resumes the same snapshot");
        check(index.compare(second,paused).rows.empty(),"resumed scan matches complete directory");
        {
            Database db(dir/L"index.db");Statement inaccessible(db,"INSERT INTO errors VALUES(?1,?2,5)");inaccessible.bind(1,second);inaccessible.bind(2,normalized(files.wstring()));inaccessible.next();
        }
        const auto uncertain=index.compare(first,second);
        check(std::count_if(uncertain.rows.begin(),uncertain.rows.end(),[](const Row& row){return row[0]==L"未覆盖";})==1,"inaccessible directory is not falsely reported as deleted");
        std::filesystem::create_directories(dir/L"other");const auto other=index.scan((dir/L"other").wstring(),false,cancel,progress);
        rejected=false;try{index.compare(first,other);}catch(...){rejected=true;}check(rejected,"different roots cannot be compared");
        const auto volumeTable=volumes();check(!volumeTable.rows.empty(),"local volume metadata available");
        check(std::any_of(volumeTable.rows.begin(),volumeTable.rows.end(),[](const Row& row){return row.size()>6 && row[0].size()==3 && row[6].find(L"%")!=std::wstring::npos;}),"ready volumes expose a remaining-space percentage");
        Performance performance;check(performance.sample().rows.size()>10,"device and performance information available");
        ProcessPerformance processes;const auto processFirst=processes.sample();Sleep(20);const auto processNext=processes.sample();
        const auto own=std::find_if(processNext.rows.begin(),processNext.rows.end(),[](const Row& row){return row[1]==std::to_wstring(GetCurrentProcessId());});
        check(processFirst.rows.size()>10 && processNext.columns.size()==15 && own!=processNext.rows.end(),"process performance enumerates real processes including this executable");
        check(own!=processNext.rows.end() && (*own)[2]!=L"采样中" && (*own)[3]!=L"--" && (*own)[12].find(L"PicoPet.exe")!=std::wstring::npos && (*own)[14].find(L":")!=std::wstring::npos,"process CPU memory path and creation identity come from Windows counters");
        Network network;network.resolveDns=false;const auto net=network.sample();check(net.columns.size()==17 && !net.summary.empty(),"network endpoint details and connection tables sampled");
        for(const auto& pair:std::vector<std::pair<std::wstring,std::wstring>>{
            {L"127.1.2.3",L"本机回环"},{L"::1",L"本机回环"},{L"::ffff:127.0.0.1",L"本机回环"},
            {L"192.168.1.1",L"局域网 / 私有地址"},{L"172.31.0.1",L"局域网 / 私有地址"},{L"172.32.0.1",L"公网地址"},
            {L"100.64.0.1",L"运营商共享地址"},{L"100.128.0.1",L"公网地址"},{L"169.254.1.1",L"链路本地"},
            {L"fe80::1",L"链路本地"},{L"fd12::1",L"局域网 / 私有地址"},{L"2001:db8::1",L"特殊 / 保留地址"},
            {L"2001:4860:4860::8888",L"公网地址"},{L"0.0.0.0",L"通配 / 未指定"},{L"::",L"通配 / 未指定"},
            {L"224.0.0.1",L"组播"},{L"ff02::1",L"组播"},{L"255.255.255.255",L"广播"},{L"not-an-ip",L"未知"}})
            check(addressScope(pair.first)==pair.second,"IPv4 and IPv6 address boundaries classified");
        check(reverseDnsName(L"1.2.3.4")==L"4.3.2.1.in-addr.arpa","IPv4 PTR query name uses reversed octets");
        check(reverseDnsName(L"::1")==L"1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa","IPv6 PTR query name uses all reversed nibbles");
        check(reverseDnsName(L"::ffff:1.2.3.4")==reverseDnsName(L"1.2.3.4") && reverseDnsName(L"bad").empty(),"mapped IPv4 and invalid DNS inputs handled");
        uint64_t rate=0;check(counterRate(3048,1000,2000,rate) && rate==1024,"counter rate respects actual elapsed time");
        check(!counterRate(1,1000,1000,rate) && rate==0 && !counterRate(1000,0,0,rate),"counter resets and zero duration cannot produce bogus rate");
        const auto devices=hardwareDevices();check(devices.columns.size()==11 && !devices.rows.empty(),"current hardware enumerated with driver property columns");
        check(std::all_of(devices.rows.begin(),devices.rows.end(),[&](const Row& row){return row.size()==devices.columns.size() && !row[0].empty() && row[9]!=L"--";}),"hardware rows have categories and unique instance identifiers");
        check(std::any_of(devices.rows.begin(),devices.rows.end(),[](const Row& row){return row[4]!=L"--" && row[7]!=L"--";}),"installed driver versions and INF files available");
        check(std::all_of(net.rows.begin(),net.rows.end(),[&](const Row& row){return row.size()==net.columns.size();}),"network detail rows match export columns");
        check(network.log().columns.size()==13 && network.processes.columns.size()==9,"logs and process rate views expose expanded metadata");
        testLocalNetwork(check);
        testCards(check);
        testDiagnostics(check);
        const auto security=securityOverview();check(security.columns.size()==6 && security.rows.size()>=7,"security overview reports independent sourced checks");
        check(std::any_of(security.rows.begin(),security.rows.end(),[](const Row& row){return !row.empty() && row[0]==L"防病毒保护";}) && std::any_of(security.rows.begin(),security.rows.end(),[](const Row& row){return !row.empty() && row[0]==L"Secure Boot";}),"security overview includes protection and boot trust states");
        std::atomic_bool consoleCancel=false;const auto console=runConsoleCommand({L"echo PICO_CONSOLE_TEST",files.wstring(),0},consoleCancel);
        check(console.rows.size()==1 && console.rows[0].size()==8 && console.rows[0][2]==L"完成" && console.rows[0][7].find(L"PICO_CONSOLE_TEST")!=std::wstring::npos,"embedded CMD captures output exit status and working directory");
        SecurityTracker tracker;
        check(!tracker.active(),"security tracker is inactive until explicitly started");
        const auto trackingError=tracker.start(files.wstring());check(trackingError.empty() && tracker.active(),"security tracker starts for an explicit directory");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));{std::ofstream(files/L"tracked.ps1")<<"Write-Output test";}
        Table tracking;
        for(int attempt=0;attempt<30;++attempt){std::this_thread::sleep_for(std::chrono::milliseconds(100));tracking=tracker.table();if(std::any_of(tracking.rows.begin(),tracking.rows.end(),[](const Row& row){return row.size()>4 && row[2]==L"文件" && row[4].find(L"tracked.ps1")!=std::wstring::npos;}))break;}
        const auto tracked=std::find_if(tracking.rows.begin(),tracking.rows.end(),[](const Row& row){return row.size()>4 && row[2]==L"文件" && row[4].find(L"tracked.ps1")!=std::wstring::npos;});
        check(tracked!=tracking.rows.end(),"security tracker receives file change notifications");
        check(tracked!=tracking.rows.end() && (*tracked)[1]==L"关注" && (*tracked)[9].find(L"无法可靠归因")!=std::wstring::npos,"executable-like file changes are flagged without false process attribution");
        tracker.stop();check(!tracker.active(),"security tracker releases its worker when stopped");
        const auto compactHardware=presentation::columns(devices,false);
        check(compactHardware==std::vector<int>({1,0,4,2}) && presentation::columns(devices,true).size()==11,"compact hardware retains full schema and driver fields");
        const auto compactNetwork=presentation::columns(net,false);
        check(compactNetwork==std::vector<int>({0,9,3,10,11,13}) && presentation::columns(net,true).size()==17,"compact network limits visual columns without dropping source fields");
        Row fixture(16,L"--");fixture[7]=L"2001:db8::1";fixture[8]=L"443";fixture[9]=L"反查中";fixture[12]=L"逐连接测速未开启";
        check(presentation::cell(net,fixture,9,false)==L"[2001:db8::1]:443","compact endpoint preserves IPv6 address and port when PTR is pending");
        fixture[9]=L"example.test";
        check(presentation::cell(net,fixture,9,false)==L"example.test:443" && presentation::cell(net,fixture,10,false)==L"未开启","compact endpoint and unavailable rate retain meaningful states");
        check(presentation::cell(net,fixture,9,true)==fixture[9] && presentation::details(net,fixture).find(L"2001:db8::1")!=std::wstring::npos,"full view and details retain exact source fields");
        uint64_t parsedRate=0;check(presentation::byteRate(L"1.50 MiB/s",parsedRate) && parsedRate==1572864 && !presentation::byteRate(L"--",parsedRate),"formatted byte rates round-trip into activity samples");
        std::wstring conciseReason;
        {
            NetworkObservations observations;Table sample{net.columns,{},L"fixture"};
            Row connection(17,L"--");connection[0]=L"browser.exe";connection[1]=L"10";connection[2]=L"TCPv4";connection[3]=L"公网地址";connection[4]=L"192.168.1.2";connection[5]=L"10000";connection[7]=L"1.1.1.1";connection[8]=L"443";connection[9]=L"example.test";connection[10]=L"1 KiB/s";connection[11]=L"2 KiB/s";connection[12]=L"TCP EStats 字节增量";connection[13]=L"已连接（方向未提供）";connection[15]=L"C:\\Apps\\browser.exe";connection[16]=L"100";
            sample.rows.push_back(connection);auto sibling=connection;sibling[1]=L"11";sibling[16]=L"101";sample.rows.push_back(sibling);auto differentPath=connection;differentPath[15]=L"D:\\Other\\browser.exe";sample.rows.push_back(differentPath);
            observations.observe(sample,true,L"2026-01-01 10:00:00");const auto apps=observations.applications(sample,true);
            check(apps.rows.size()==2 && apps.rows[0][2]==L"2.00 KiB/s" && apps.rows[0][6]==L"10\r\n11","software grouping merges matching executable paths and sums only measured TCP rates");
            check(apps.rows[0][8].find(L"不能判断泄露")!=std::wstring::npos && apps.rows[0][8].find(L"本地规则")!=std::wstring::npos,"software interpretation explains evidence and does not label public communication as an attack");
            Table none{net.columns,{},L""};observations.observe(none,true,L"2026-01-01 10:00:01");const auto ended=observations.history(none);
            check(ended.rows.size()==3 && ended.rows[0][19]==L"已结束 / 不再可见" && ended.rows[0][18]==L"2026-01-01 10:00:00" && ended.rows[0][10]==L"--","ended connections retain evidence and timestamps without showing stale rates as live traffic");
            check(observations.applications(none,true).rows.size()==2,"software remains available after its final endpoint disappears");
            NetworkObservations recent;recent.observe(sample,true,L"first",1000);recent.observe(none,true,L"next",2000);
            check(recent.history(none,true,60999).rows.size()==3 && recent.history(none,true,61000).rows.empty() && recent.history(none).rows.size()==3,"recent connections remain for sixty seconds without deleting full history");
            observations.observe(sample,true,L"2026-01-01 10:00:02");observations.observe(none,false,L"2026-01-01 10:00:03");
            check(observations.history(none).rows[0][19].find(L"不完整")!=std::wstring::npos,"partial capture never labels a missing connection as ended");
            observations.observe(sample,true,L"2026-01-01 10:00:04");observations.interrupt();observations.observe(none,true,L"2026-01-01 10:00:05");
            check(observations.history(none).rows[0][19].find(L"间断")!=std::wstring::npos,"pause and resume preserve uncertainty instead of inventing disconnect times");
            auto changed=connection;changed[16]=L"200";sample.rows={changed};observations.observe(sample,true,L"2026-01-01 10:00:06");
            check(observations.history(sample).rows.size()==4,"PID reuse with a new process creation time produces a distinct history identity");
            auto rateChanged=connection;rateChanged[10]=L"4 KiB/s";check(presentation::identity(net,connection)==presentation::identity(net,rateChanged) && presentation::identity(net,connection)!=presentation::identity(net,changed),"selection identity survives rate updates and distinguishes process instances");
            check(presentation::concise(ended,conciseReason).rows.size()==3 && presentation::columns(ended,false).size()==6 && ended.columns.size()==20,"history keeps every observation and all export fields in concise mode");
        }
        Table performanceFixture{{L"项目",L"当前状态 / 设备信息"},{{L"CPU 使用率",L"12.0 %"},{L"提交内存",L"1 / 2"},{L"Windows 版本",L"Windows 11"}},L"source"};
        const auto concisePerformance=presentation::concise(performanceFixture,conciseReason);
        check(concisePerformance.rows.size()==2 && concisePerformance.summary.find(L"2 / 3")!=std::wstring::npos,"concise performance keeps priority rows and reports the source count");
        Table hardwareFixture{{L"设备类别",L"设备名称",L"状态"},{{L"显示",L"正常设备",L"运行中"},{L"网络",L"关注设备",L"问题代码 10"}},L"source"};
        const auto conciseHardware=presentation::concise(hardwareFixture,conciseReason);
        check(conciseHardware.rows.size()==1 && conciseHardware.rows[0][1]==L"关注设备","concise hardware hides normal PnP noise without changing its source table");
        Table logFixture{{L"记录时间",L"级别",L"事件来源",L"事件 ID"},{{L"t",L"信息",L"source",L"1"},{L"t",L"警告",L"source",L"2"},{L"t",L"警告",L"Microsoft-Windows-DistributedCOM",L"10016"}},L"source"};
        const auto conciseLog=presentation::concise(logFixture,conciseReason);
        check(conciseLog.rows.size()==1 && conciseLog.rows[0][3]==L"2","concise logs retain attention levels and suppress known common DCOM noise");
        Table trackingFixture{{L"记录时间",L"风险",L"类别",L"事件",L"对象",L"PID"},{{L"t",L"常规",L"程序",L"启动",L"normal.exe",L"1"},{L"t",L"关注",L"程序",L"启动",L"download.exe",L"2"},{L"t",L"变更",L"文件",L"修改",L"note.txt",L"--"}},L"source"};
        const auto conciseTracking=presentation::concise(trackingFixture,conciseReason);
        check(conciseTracking.rows.size()==2 && presentation::columns(trackingFixture,false)==std::vector<int>({0,1,2,3,4,5}),"concise security tracking retains actionable activity and readable columns");
        Table securityFixture{{L"安全项目",L"当前状态",L"级别",L"建议"},{{L"防火墙",L"已开启",L"正常",L"--"},{L"UAC",L"已关闭",L"关注",L"启用"}},L"source"};
        check(presentation::concise(securityFixture,conciseReason).rows.size()==1 && presentation::columns(securityFixture,false)==std::vector<int>({0,1,2,3}),"concise security overview prioritizes non-normal states");
        Table consoleFixture{{L"记录时间",L"类型",L"状态",L"退出码",L"耗时",L"命令 / 提示",L"工作目录",L"完整输出"},{{L"t",L"CMD",L"已完成",L"0",L"1 ms",L"echo ok",L"C:\\",L"ok"}},L"source"};
        check(presentation::concise(consoleFixture,conciseReason).rows.size()==1 && presentation::columns(consoleFixture,false)==std::vector<int>({0,1,2,3,4,5}),"command assistant retains history while hiding large output from the list");
        DiskActivity diskActivity;const auto diskReading=diskActivity.sample(L"C:\\");
        check(!diskReading.status.empty(),"disk activity returns measured data or an explicit availability reason");
        exportCsv(difference,dir/L"changes.csv");check(std::filesystem::file_size(dir/L"changes.csv")>20,"snapshot report exports structured CSV");
        index.removeSnapshot(other);check(std::filesystem::exists(files/L"same.txt") && index.snapshots().size()==3,"deleting a snapshot leaves source files untouched");
    }catch(const std::exception& error){failures.emplace_back(error.what());}
    std::ofstream output(report);output<<"{\"checks\":"<<checks<<",\"failures\":[";for(size_t i=0;i<failures.size();++i)output<<(i?",":"")<<'\"'<<failures[i]<<'\"';output<<"]}\n";
    // This directory is uniquely created by this test and contains only its fixtures.
    std::error_code ec;std::filesystem::remove_all(dir,ec);return failures.empty()?0:1;
}
}
