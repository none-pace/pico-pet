#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace systemdesk {
inline constexpr UINT kEmbeddedConsoleClosed=WM_APP+4;
inline constexpr UINT kEmbeddedConsoleUpdated=WM_APP+5;

struct EmbeddedConsoleState {
    bool active=false,busy=false;
    int mode=0;
    std::wstring input,output;
    size_t caret=0;
    int scroll=0;
    std::vector<CHAR_INFO> cells;
    COORD grid{80,25},cursor{};
    bool interactive=false;
};

class EmbeddedConsole {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    EmbeddedConsole();
    ~EmbeddedConsole();
    EmbeddedConsole(const EmbeddedConsole&)=delete;
    EmbeddedConsole& operator=(const EmbeddedConsole&)=delete;
    void show(HWND owner);
    void hide();
    void shutdown();
    void character(wchar_t value);
    void backspace();
    void paste();
    void execute();
    void stop();
    void cycleMode(int direction=1);
    void key(UINT key);
    void acknowledge();
    bool interactive() const;
    bool visible() const;
    bool busy() const;
    bool outputContains(const std::wstring& value) const;
    EmbeddedConsoleState state() const;
};
}
