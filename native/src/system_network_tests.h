#pragma once
#include "system_core.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <array>

namespace systemdesk {
bool testDnsLifetime();
template<class Check> void testLocalNetwork(Check check){
    check(testNetworkTraceParser(),"network event decoder validates bounds, receive direction, IPv6 UDP and schema version");
    check(testDnsLifetime(),"DNS callback retains canceled queries and releases completed queries in either destruction order");
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0){check(false,"Winsock test fixture initialized");return;}
    struct Cleanup{std::vector<SOCKET> sockets;~Cleanup(){for(auto s:sockets)if(s!=INVALID_SOCKET)closesocket(s);WSACleanup();}} cleanup;
    const auto create=[&](int family,int type){const auto s=socket(family,type,0);cleanup.sockets.push_back(s);return s;};
    for(const int family:{AF_INET,AF_INET6}){
        const auto server=create(family,SOCK_STREAM),client=create(family,SOCK_STREAM),udp=create(family,SOCK_DGRAM);
        SOCKADDR_STORAGE address{};int size=family==AF_INET?sizeof(SOCKADDR_IN):sizeof(SOCKADDR_IN6);
        if(family==AF_INET){auto* v4=reinterpret_cast<SOCKADDR_IN*>(&address);v4->sin_family=AF_INET;v4->sin_addr.s_addr=htonl(INADDR_LOOPBACK);}
        else{auto* v6=reinterpret_cast<SOCKADDR_IN6*>(&address);v6->sin6_family=AF_INET6;v6->sin6_addr=in6addr_loopback;}
        const auto* endpoint=reinterpret_cast<const SOCKADDR*>(&address);
        const bool ready=server!=INVALID_SOCKET && client!=INVALID_SOCKET && udp!=INVALID_SOCKET && bind(server,endpoint,size)==0 && listen(server,1)==0 &&
            bind(udp,endpoint,size)==0 && getsockname(server,reinterpret_cast<SOCKADDR*>(&address),&size)==0 && connect(client,endpoint,size)==0;
        check(ready,"local TCP and UDP fixtures bind without external traffic");if(!ready)continue;
        const auto accepted=accept(server,nullptr,nullptr);cleanup.sockets.push_back(accepted);
        check(accepted!=INVALID_SOCKET,"local TCP fixture accepts connection");if(accepted==INVALID_SOCKET)continue;
        const auto port=std::to_wstring(family==AF_INET?ntohs(reinterpret_cast<SOCKADDR_IN*>(&address)->sin_port):ntohs(reinterpret_cast<SOCKADDR_IN6*>(&address)->sin6_port));
        Network network;network.resolveDns=false;network.measureConnections=true;
        const auto first=network.sample();
        const auto cell=[](const Table& table,const Row& row,const wchar_t* column)->const std::wstring&{return row[static_cast<size_t>(std::find(table.columns.begin(),table.columns.end(),column)-table.columns.begin())];};
        const auto find=[&](const Table& table){return std::find_if(table.rows.begin(),table.rows.end(),[&](const Row& row){return cell(table,row,L"远端端口")==port && cell(table,row,L"PID")==std::to_wstring(GetCurrentProcessId());});};
        const auto initial=find(first);check(initial!=first.rows.end(),"owned local TCP endpoint has the exact destination port");
        if(initial!=first.rows.end())check(cell(first,*initial,L"通信范围")==L"本机回环" && cell(first,*initial,L"远端 PTR 域名")==L"localhost（本机）","loopback is identified locally without a DNS query");
        std::array<char,8192> payload{};int sent=send(client,payload.data(),static_cast<int>(payload.size()),0);
        DWORD timeout=1000;setsockopt(accepted,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
        int received=0;while(received<sent){const int count=recv(accepted,payload.data(),static_cast<int>(payload.size()),0);if(count<=0)break;received+=count;}
        check(sent>0 && received==sent,"local fixture transfers real payload bytes");
        Sleep(30);
        const auto second=network.sample();const auto measured=find(second);
        if(measured!=second.rows.end()){
            const auto& status=cell(second,*measured,L"测速状态");const auto& speed=cell(second,*measured,L"发送速率");
            const bool permitted=status==L"TCP EStats 字节增量";
            check(permitted?(speed!=L"--" && speed!=L"0 B/s"):(speed==L"--" && (status.find(L"权限")!=std::wstring::npos || status.find(L"不可用")!=std::wstring::npos)),"TCP reports measured bytes or an explicit permission or availability limit");
        }else check(false,"local fixture remains visible while sampling");
        const auto udpRow=std::find_if(second.rows.begin(),second.rows.end(),[&](const Row& row){return cell(second,row,L"PID")==std::to_wstring(GetCurrentProcessId()) && cell(second,row,L"协议")== (family==AF_INET?L"UDPv4":L"UDPv6");});
        check(udpRow!=second.rows.end() && cell(second,*udpRow,L"通信范围")==L"远端未提供" && cell(second,*udpRow,L"发送速率")==L"--","UDP never invents a remote endpoint or flow rate");
        network.resetBaseline();network.measureConnections=false;
        const auto reset=network.sample();const auto resetRow=find(reset);
        check(resetRow!=reset.rows.end() && cell(reset,*resetRow,L"测速状态")==L"逐连接测速未开启","stopping TCP measurement clears its baseline");
    }
    {
        NetworkTrace trace;const auto status=trace.start();
        check(status==ERROR_SUCCESS || status==ERROR_ACCESS_DENIED,"real-time event capture starts or reports required privileges");
        if(status==ERROR_SUCCESS){
            const auto receiver=create(AF_INET,SOCK_DGRAM),sender=create(AF_INET,SOCK_DGRAM);
            SOCKADDR_IN address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            int length=sizeof(address);bool ready=bind(receiver,reinterpret_cast<SOCKADDR*>(&address),length)==0 && getsockname(receiver,reinterpret_cast<SOCKADDR*>(&address),&length)==0;
            std::array<char,1234> udpPayload{};const auto port=std::to_wstring(ntohs(address.sin_port));
            if(ready)ready=sendto(sender,udpPayload.data(),static_cast<int>(udpPayload.size()),0,reinterpret_cast<SOCKADDR*>(&address),length)==static_cast<int>(udpPayload.size());
            DWORD timeout=1000;setsockopt(receiver,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
            if(ready)ready=recv(receiver,udpPayload.data(),static_cast<int>(udpPayload.size()),0)==static_cast<int>(udpPayload.size());
            check(ready,"event fixture sends a UDP datagram locally");
            Table captured;bool found=false;
            for(int retry=0;retry<40 && !found;++retry){Sleep(100);captured=trace.snapshot();for(const auto& row:captured.rows)if(row[1]==std::to_wstring(GetCurrentProcessId()) && row[2]==L"UDPv4" && row[8]==port && std::stoull(row[21])>=udpPayload.size())found=true;}
            check(found,"real-time capture preserves UDP destination and transmitted bytes between polling samples");
            const auto listener=create(AF_INET,SOCK_STREAM);SOCKADDR_IN tcpAddress{};tcpAddress.sin_family=AF_INET;tcpAddress.sin_addr.s_addr=htonl(INADDR_LOOPBACK);int tcpLength=sizeof(tcpAddress);
            const bool tcpReady=bind(listener,reinterpret_cast<SOCKADDR*>(&tcpAddress),tcpLength)==0 && listen(listener,1)==0 && getsockname(listener,reinterpret_cast<SOCKADDR*>(&tcpAddress),&tcpLength)==0;
            const auto tcpClient=socket(AF_INET,SOCK_STREAM,0);bool transferred=false;const auto before=GetTickCount64();
            if(tcpReady && connect(tcpClient,reinterpret_cast<SOCKADDR*>(&tcpAddress),tcpLength)==0){const auto accepted=accept(listener,nullptr,nullptr);
                if(accepted!=INVALID_SOCKET){const char payload[]="short-event-test";transferred=send(tcpClient,payload,static_cast<int>(sizeof(payload)),0)==static_cast<int>(sizeof(payload));char output[sizeof(payload)]{};setsockopt(accepted,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));transferred=transferred && recv(accepted,output,static_cast<int>(sizeof(output)),MSG_WAITALL)==static_cast<int>(sizeof(output));closesocket(accepted);}}
            if(tcpClient!=INVALID_SOCKET)closesocket(tcpClient);
            check(transferred && GetTickCount64()-before<1000,"TCP fixture sends and closes before the next polling interval");
            bool shortFound=false;for(int retry=0;retry<40 && !shortFound;++retry){Sleep(100);captured=trace.snapshot();for(const auto& row:captured.rows)if(row[1]==std::to_wstring(GetCurrentProcessId()) && row[2]==L"TCPv4" && row[8]==std::to_wstring(ntohs(tcpAddress.sin_port)) && std::stoull(row[21])>=17)shortFound=true;}
            check(shortFound,"closed short TCP connection retains original destination and event byte count");
            const auto udp6Receiver=create(AF_INET6,SOCK_DGRAM),udp6Sender=create(AF_INET6,SOCK_DGRAM);SOCKADDR_IN6 v6{};v6.sin6_family=AF_INET6;v6.sin6_addr=in6addr_loopback;int v6Length=sizeof(v6);
            bool v6Ready=bind(udp6Receiver,reinterpret_cast<SOCKADDR*>(&v6),v6Length)==0 && getsockname(udp6Receiver,reinterpret_cast<SOCKADDR*>(&v6),&v6Length)==0;
            if(v6Ready)v6Ready=sendto(udp6Sender,udpPayload.data(),static_cast<int>(udpPayload.size()),0,reinterpret_cast<SOCKADDR*>(&v6),v6Length)==static_cast<int>(udpPayload.size());
            setsockopt(udp6Receiver,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));if(v6Ready)v6Ready=recv(udp6Receiver,udpPayload.data(),static_cast<int>(udpPayload.size()),0)==static_cast<int>(udpPayload.size());
            bool v6Found=false;for(int retry=0;retry<40 && !v6Found;++retry){Sleep(100);captured=trace.snapshot();for(const auto& row:captured.rows)if(row[1]==std::to_wstring(GetCurrentProcessId()) && row[2]==L"UDPv6" && row[8]==std::to_wstring(ntohs(v6.sin6_port)) && row[7]==L"::1" && std::stoull(row[21])>=udpPayload.size())v6Found=true;}
            check(v6Ready && v6Found,"IPv6 UDP event capture preserves address port direction and bytes");
            trace.stop();check(!trace.running(),"stopping capture closes only the owned trace session");
        }
    }
}
}
