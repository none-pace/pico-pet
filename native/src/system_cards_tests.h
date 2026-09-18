#pragma once
#include "system_cards.h"

namespace systemdesk {
template<class Check> void testCards(Check check){
    Table apps{{L"软件",L"当前活动",L"下载（TCP）",L"上传（TCP）",L"通信概况",L"端点数",L"PID 列表",L"测速覆盖",L"归纳与建议",L"远端地址与 PTR",L"远端端口",L"最后可见",L"历史端点数",L"程序路径",L"软件标识"},
        {{L"fixture.exe",L"正在传输",L"1.00 KiB/s",L"512.00 KiB/s",L"公网 1 · 本机 0 · 局域网 0",L"1",L"42",L"1 / 1 条已连接 TCP；UDP 0 条不覆盖",L"本地规则；仅凭地址不能判断泄露或攻击。",L"203.0.113.1:443",L"443",L"2026-09-18 12:00:00",L"1",L"C:\\Fixture\\fixture.exe",L"c:\\fixture\\fixture.exe"}},L"test"};
    cards::Insights insights;Table next;
    for(ULONGLONG time=1000;time<=6000;time+=1000){next=apps;insights.annotate(next,time,true);}
    check(next.rows[0][15]==L"持续上传，建议核对" && cards::matches(next.rows[0],cards::Filter::Recorded),"sustained measured upload creates retained evidence, not a malware verdict");
    check(cards::matches(next.rows[0],cards::Filter::Uploading) && cards::matches(next.rows[0],cards::Filter::Public) && !cards::matches(next.rows[0],cards::Filter::Local),"software filters distinguish measured upload and endpoint scope");
    apps.rows[0][3]=L"0 B/s";next=apps;insights.annotate(next,7000,true);
    check(!cards::matches(next.rows[0],cards::Filter::Uploading) && cards::matches(next.rows[0],cards::Filter::Recorded),"upload evidence remains after transmission stops");
    apps.rows[0][2]=apps.rows[0][3]=L"不可用";next=apps;insights.annotate(next,8000,true);
    check(next.rows[0][15]==L"流量尚不可见" && cards::matches(next.rows[0],cards::Filter::Unknown),"unavailable counters never imply zero traffic");
    check(insights.history(apps.rows[0][14])->size()==8,"software history exists before the user selects its card");
    cards::Insights interrupted;apps.rows[0][2]=L"1 KiB/s";apps.rows[0][3]=L"512 KiB/s";
    for(ULONGLONG time=1000;time<=4000;time+=1000){next=apps;interrupted.annotate(next,time,true);}
    interrupted.interrupt();next=apps;interrupted.annotate(next,6000,true);
    check(!cards::matches(next.rows[0],cards::Filter::Recorded),"paused monitoring cannot count as sustained observed upload");
    for(ULONGLONG time=7000;time<=13000;time+=1000){next=apps;interrupted.annotate(next,time,false);}
    check(!cards::matches(next.rows[0],cards::Filter::Recorded),"incomplete sampling does not create a sustained upload claim");
    for(ULONGLONG time=14000;time<=90000;time+=1000){next=apps;interrupted.annotate(next,time,true);}
    check(interrupted.history(apps.rows[0][14])->size()<=60,"software chart memory is bounded to a minute");
    const auto card=cards::describe(next,next.rows[0]);
    check(card.title==L"fixture.exe" && card.primary.find(L"上传")!=std::wstring::npos && cards::explain(next,next.rows[0],1).find(L"203.0.113.1:443")!=std::wstring::npos,"cards preserve endpoints and full evidence behind the summary");
    Table processes{{L"进程名称",L"PID",L"CPU",L"内存工作集",L"读取 /s",L"写入 /s",L"状态",L"私有提交",L"线程数",L"句柄数",L"父 PID",L"启动时间",L"程序路径",L"读取说明",L"进程标识"},
        {{L"app.exe",L"1",L"1.0 %",L"1 MiB",L"1 KiB/s",L"2 KiB/s",L"运行中",L"2 MiB",L"2",L"10",L"0",L"time",L"C:\\App\\app.exe",L"ok",L"1:time"},
         {L"app.exe",L"2",L"2.0 %",L"2 MiB",L"2 KiB/s",L"3 KiB/s",L"运行中",L"3 MiB",L"3",L"12",L"0",L"time",L"c:\\app\\APP.exe",L"ok",L"2:time"},
         {L"app.exe",L"3",L"0 %",L"1 MiB",L"0 B/s",L"0 B/s",L"运行中",L"1 MiB",L"1",L"4",L"0",L"time",L"D:\\Other\\app.exe",L"ok",L"3:time"}},L"test"};
    const auto grouped=cards::groupProcesses(processes);
    check(grouped.rows.size()==2 && grouped.rows[0][2]==L"3.0 %" && grouped.rows[0][1]==L"1, 2" && grouped.rows[0][15].find(L"2:time")!=std::wstring::npos,"process groups use case-insensitive full paths and retain constituent process evidence");
    ProcessObservations recent;recent.observe(processes,1000,L"first");
    auto live=processes;live.rows.erase(live.rows.begin()+1);const auto retained=recent.observe(live,2000,L"next");
    const auto ended=std::find_if(retained.rows.begin(),retained.rows.end(),[](const Row& row){return row[1]==L"2";});
    check(ended!=retained.rows.end() && ProcessObservations::recent(*ended) && (*ended)[2]==L"--" && (*ended)[3]==L"--" && (*ended)[13].find(L"first")!=std::wstring::npos,"disappearing process stays readable with unknown current counters and last-seen evidence");
    const auto retainedGroups=cards::groupProcesses(retained);
    check(retainedGroups.rows.size()==3,"recently missing processes do not contaminate running software resource totals");
    check(recent.observe(live,61000,L"later").rows.size()==2,"recent process retention expires after sixty seconds");
    auto newest=processes;newest.rows[0][11]=L"2026-09-18 08:00:00";newest.rows[1][11]=L"2026-09-18 10:00:00";newest.rows[2][11]=L"--";
    auto latestGroup=cards::groupProcesses(newest);cards::sort(latestGroup,5);
    check(latestGroup.rows.front()[11]==L"2026-09-18 10:00:00" && latestGroup.rows.front()[1]==L"1, 2","latest-start sorting uses the newest child and leaves unknown starts last");
    Table disks{{L"盘符 / 分区",L"总容量",L"已用"},{{L"C:",L"1 TiB",L"256 GiB"},{L"D:",L"100 GiB",L"90 GiB"}},L""};cards::sort(disks,10);
    check(disks.rows.front()[0]==L"D:","disk occupancy sorts by proportion rather than raw used bytes");
    const auto summary=cards::explain(next,next.rows[0],0);
    check(summary.size()<200 && summary.find(L"程序路径")==std::wstring::npos && cards::explain(next,next.rows[0],1).find(L"测速覆盖")!=std::wstring::npos,"brief interpretation keeps detailed measurement evidence on its own tab");
    auto single=processes;single.rows.erase(single.rows.begin()+1);
    check(cards::groupProcesses(single).rows[0][14]==grouped.rows[0][14],"software card identity survives child processes appearing or exiting");
    processes.rows[1][3]=L"--";
    check(cards::groupProcesses(processes).rows[0][3]==L"部分不可用","process aggregation does not convert unreadable memory to zero");
    cards::sort(processes,3);check(processes.rows.front()[1]==L"2","resource sorting uses numeric CPU values");
    uint64_t invalid=0;check(!presentation::byteRate(L"NaN B/s",invalid) && !presentation::byteRate(L"inf B/s",invalid),"non-finite formatted counters cannot become bogus byte totals");
}
}
