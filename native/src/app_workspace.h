#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

namespace appworkspace {
inline constexpr UINT Open=320,Choose=321,Launch=322,Return=323,Single=324,Desktop=325,Detach=326,Next=327,Fullscreen=328;
inline constexpr UINT FrameReady=WM_APP+20;
// The helper owns the host window and restores foreign windows if the pet exits.
int runHost(const wchar_t* mappingName);
class Workspace {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Workspace();~Workspace();
    void open(HWND owner,int mode,int fps,int resolution=3);
    void configure(int mode,int fps,int resolution=3);
    void choose();
    void launch();
    bool launchPath(const std::wstring& path);
    void fullscreen();
    bool attach(HWND window);
    void detach();
    void next();
    void close();
    void suspend(bool value);
    void tick();
    bool active()const;
    bool updated();
    void draw(HDC dc,uint32_t* pixels);
    bool mouse(UINT message,WPARAM buttons,int x,int y);
    bool key(UINT message,WPARAM key,LPARAM data);
    HWND host()const;
    int count()const;
    uint64_t frames()const;
    std::wstring status()const;
};
}
