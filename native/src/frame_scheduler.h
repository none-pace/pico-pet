#pragma once
#include <thread>

// Active only during movement, with at most one frame queued for the UI.
class FrameScheduler {
    HANDLE cancel=nullptr,timer=nullptr;
    std::thread worker;
public:
    ~FrameScheduler(){stop();}
    void stop(){
        if(cancel)SetEvent(cancel);
        if(worker.joinable())worker.join();
        if(timer)CloseHandle(timer);if(cancel)CloseHandle(cancel);timer=cancel=nullptr;
    }
    bool start(HWND window,UINT_PTR id,int fps,std::atomic_bool& queued){
        stop();cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!cancel || !timer){stop();return false;}
        worker=std::thread([this,window,id,fps,&queued]{
            const double interval=1.0/fps;double deadline=preciseSeconds()+interval;
            HANDLE handles[]={cancel,timer};
            while(WaitForSingleObject(cancel,0)==WAIT_TIMEOUT){
                LARGE_INTEGER due{};due.QuadPart=-std::max<LONGLONG>(1,static_cast<LONGLONG>((deadline-preciseSeconds())*10000000));
                if(!SetWaitableTimerEx(timer,&due,0,nullptr,nullptr,nullptr,0))break;
                if(WaitForMultipleObjects(2,handles,FALSE,INFINITE)!=WAIT_OBJECT_0+1)break;
                if(!queued.exchange(true) && !PostMessageW(window,WM_TIMER,id,0))queued=false;
                deadline+=interval;const double now=preciseSeconds();
                if(deadline<now)deadline+=std::ceil((now-deadline)/interval)*interval;
            }
        });
        return true;
    }
};
