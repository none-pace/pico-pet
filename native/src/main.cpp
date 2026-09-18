#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <imm.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wtsapi32.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <powrprof.h>
#include <psapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "../assets/layout.h"
#include "../assets/hd-layout.h"
#include "../assets/screen-mesh.h"
#include "realtime_model.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"imm32.lib")
#include "interaction.h"
#include "interaction_tests.h"
#include "pixel_renderer.h"
#include "pixel_renderer_tests.h"
#include "system_core.h"
#include "embedded_console.h"
#include "screen_desktop.h"
#include "custom_expression.h"
#include "window_layer.h"
#include "app_workspace.h"

using Microsoft::WRL::ComPtr;
constexpr wchar_t kClass[] = L"PicoPet.Win11.Native";
constexpr UINT kTrayMessage = WM_APP + 1, kEnvironmentMessage = WM_APP + 2;
constexpr UINT kTestMessage = WM_APP + 3;
constexpr UINT kRenderReadyMessage = WM_APP + 6;
constexpr UINT kLayerMessage = WM_APP + 7;
constexpr UINT_PTR kLayerTimer=7;
constexpr UINT_PTR kLayerWatchTimer=8;
constexpr UINT_PTR kAnimationTimer = 1, kMotionTimer = 3, kPoseTimer = 4, kDesktopTimer=5, kAntennaTimer=6;
constexpr double kThrowSpeedLimit = 360, kMotionDamping = 1.65, kEdgeRestitution = .25;
double preciseSeconds(){LARGE_INTEGER value{},frequency{};QueryPerformanceCounter(&value);QueryPerformanceFrequency(&frequency);return static_cast<double>(value.QuadPart)/frequency.QuadPart;}
enum Command : UINT {
    Show = 100, Pause, Topmost, ClickThrough, AutoHide, Economy, ResetPosition, Exit, FloatMode,
    SizeSmall = 200, SizeNormal, SizeLarge, ViewFront = 210, ViewThree,
    MoodIdle = 220, MoodHappy, MoodLove, MoodSurprise, MoodSleep, MoodBlink, MoodOff
    ,SystemPerformance=240,SystemDisks,SystemNetwork,SystemDiagnostics,SystemConsole,SizeDesk=250,QualityPixel=260,QualityHD,
    Frame15=270,Frame30,Frame60,PixelThreshold1=280,PixelThreshold2,PixelThreshold4,
    MaterialPlastic=290,MaterialMetal,MaterialGlass,MaterialCeramic,OpenSettings=300,ScreenShortcuts=301,AddScreenFile,AddScreenFolder,RemoveScreenShortcut,
    ImportExpression=310,UseExpression,BuiltinExpression,ExpressionContain,ExpressionCover,ExpressionDark,ExpressionLight
};
enum Face { Idle, Happy, Love, Surprise, Sleepy, Blink, Off, Computer, ComputerPerformance, ComputerDisks, ComputerNetwork, ComputerDiagnostics };

struct Image {
    UINT width{}, height{};
    std::vector<uint32_t> pixels;
};

void require(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(operation);
}

Image loadImage(IWICImagingFactory* factory, int id) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) throw std::runtime_error("Embedded sprite resource is missing");
    HGLOBAL data = LoadResource(nullptr, resource);
    const DWORD size = SizeofResource(nullptr, resource);
    auto* bytes = static_cast<BYTE*>(LockResource(data));
    ComPtr<IWICStream> stream;
    require(factory->CreateStream(&stream), "Create WIC stream");
    require(stream->InitializeFromMemory(bytes, size), "Open sprite resource");
    ComPtr<IWICBitmapDecoder> decoder;
    require(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder), "Decode sprite");
    ComPtr<IWICBitmapFrameDecode> frame;
    require(decoder->GetFrame(0, &frame), "Decode sprite frame");
    ComPtr<IWICFormatConverter> converter;
    require(factory->CreateFormatConverter(&converter), "Create pixel converter");
    require(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        nullptr, 0, WICBitmapPaletteTypeCustom), "Convert premultiplied alpha");
    Image image;
    require(converter->GetSize(&image.width, &image.height), "Read sprite dimensions");
    image.pixels.resize(static_cast<size_t>(image.width)*image.height);
    require(converter->CopyPixels(nullptr, image.width*4, static_cast<UINT>(image.pixels.size()*4),
        reinterpret_cast<BYTE*>(image.pixels.data())), "Read sprite pixels");
    return image;
}

struct Settings {
    int size = 192, view = 1, mood = Idle;
    int frameRate = 30, pixelThreshold = 1, material = 0;
    int x = INT_MIN, y = INT_MIN;
    int yaw=INT_MIN,pitch=INT_MIN,motionAmplitude=100,throwGain=100,rotationSensitivity=100;
    bool pauseAnimation=false;
    bool topmost = true, clickThrough = false, autoHide = true, economy = true;
    bool floating = false;
    bool hd = false;
    int appMode=0,appFps=60,shortcutTarget=0,appResolution=3;
    int layerMode=0;
    DWORD layerPid=0;
    std::wstring layerPath;
};

#include "preferences_window.h"
#include "frame_scheduler.h"

class Pet {
public:
    HWND hwnd{};
    Settings settings;
    windowlayer::Controller layer;
    bool layerUpdatePending=false;
    bool testMode{}, paused{}, manuallyHidden{}, locked{}, displayOff{}, suspended{}, fullscreen{};
    bool powerSaving{}, dragging{}, dragMoved{}, inMenu{};
    bool hidden{}, animationArmed{}, blinking{};
    uint64_t draws{}, timerWakes{}, positionUpdates{};
    int renderedFace = -1, renderedBob = 0, renderedPose = -1, renderedRoll = 0, renderedTilt = 0;
    int composedPose=-1,composedFace=-1;
    int currentFace=Idle;
    bool screenHovered=false,assistantHovered=false,trackingMouse=false;
    int hoveredScreen=-1;
    int pressedScreen=-1;
    bool pressedAssistant=false;
    screendesktop::Desktop screenDesktop;
    expression::Image customExpression;
    int screenTextureMode=0,desktopHover=-1,desktopPressed=-1,desktopFading=-1;
    ULONGLONG desktopFadeStart=0;
    bool desktopMenu=false;
    int wheelRemainder=0;
    uint64_t compositions=0,poseDecodes=0;
    pixels::Mapping pixelMapping;
    std::wstring configPath;
    UINT dpi = 96;
    int extent = 192;
    HDC memoryDC{};
    HBITMAP bitmap{};
    HGDIOBJ oldBitmap{};
    HFONT consoleFont{};
    int consoleFontHeight{};
    HDC terminalDC{};HBITMAP terminalBitmap{};HGDIOBJ terminalPrevious{};uint32_t* terminalPixels{};
    std::vector<int> terminalMap;
    int terminalMapPose=-1,terminalMapExtent=0,terminalMapTilt=0,terminalMapBob=0;
    bool terminalDirty=true;
    bool resizingSurface=false;
    POINT resizedPosition{};
    uint64_t desktopExits=0;
    RECT terminalCaret{};
    bool terminalFocused=false;
    realtime::Model model;
    double lastYaw=10000,lastPitch=10000,lastRoll=10000;
    int lastButton=-1;
    double frameTotalMs=0,frameMaxMs=0;
    double frameSubmitMs=0,frameReadbackMs=0,frameCopyMs=0,framePresentMs=0,lastFrameSeconds=0;
    double frameGpuLatencyMs=0;
    std::array<double,512> frameGaps{};
    size_t frameGapCount=0;
    struct RenderedFrame {int face=0,bob=0,pose=0,tilt=0,button=0;double yaw=0,pitch=0,roll=0,preparationMs=0,leftBend=0,rightBend=0;};
    std::array<interaction::Spring,2> antennas{};
    double lastLeftBend=0,lastRightBend=0,antennaSeconds=0;
    FrameScheduler antennaClock;std::atomic_bool antennaQueued=false;bool antennaArmed=false;
    RenderedFrame pendingFrame;
    bool renderAgain=false;
    int requestedBob=0;
    uint32_t* dib{};
    struct CachedPose { int id=-1; uint64_t used=0; Image body, faces; };
    std::array<CachedPose, 6> poseCache;
    ComPtr<IWICImagingFactory> imageFactory;
    uint64_t cacheClock=0;
    bool poseArmed=false, rotating=false, precisionTiming=false;
    FrameScheduler poseClock;std::atomic_bool poseQueued=false;
    double baseYaw=20, basePitch=12, poseYaw=20, posePitch=12, poseRoll=0, floatRoll=0;
    double targetYaw=20, targetPitch=12, targetRoll=0, rotationOriginYaw=0, rotationOriginPitch=0;
    ULONGLONG poseTime=0;
    double poseSeconds=0,motionSeconds=0;
    std::vector<uint32_t> composition;
    POINT dragOrigin{}, windowOrigin{}, desiredPosition{};
    bool pendingPosition{};
    bool advancingMotion=false;
    interaction::Trajectory trajectory;
    interaction::Attitude attitude;
    bool gesturePose=false;
    double dragScale=1;
    ULONGLONG shakeUntil=0;
    ULONGLONG surpriseUntil=0;
    bool motionArmed{};
    FrameScheduler motionClock;bool motionContinuous=false;std::atomic_bool motionQueued{false};
    double motionX{}, motionY{}, velocityX{}, velocityY{}, floatPhase{},floatBlend=1;
    ULONGLONG motionTime{};
    ULONGLONG reactionUntil{};
    Face reactionFace = Happy;
    int reactionStep{};
    DWORD randomState = 0x12345678;
    NOTIFYICONDATAW tray{sizeof(NOTIFYICONDATAW)};
    bool trayAdded{};
    HPOWERNOTIFY displayNotification{}, saverNotification{};
    UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    systemdesk::EmbeddedConsole embeddedConsole;
    appworkspace::Workspace workspace;
    bool appPointerDown=false;POINT lastAppPoint{};
    preferences::Window preferencesWindow;

    ~Pet() { preferencesWindow.close();stopMotion();stopPose();stopAntennas();setPrecisionTiming(false);if(consoleFont)DeleteObject(consoleFont);if(terminalDC){SelectObject(terminalDC,terminalPrevious);DeleteObject(terminalBitmap);DeleteDC(terminalDC);}releaseSurface(); }

    void setPrecisionTiming(bool enabled) {
        if(enabled==precisionTiming)return;
        if(enabled) {
            if(timeBeginPeriod(1)!=TIMERR_NOERROR)return;
            precisionTiming=true;
        } else {
            timeEndPeriod(1);precisionTiming=false;
        }
        PROCESS_POWER_THROTTLING_STATE throttle{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
            PROCESS_POWER_THROTTLING_EXECUTION_SPEED,enabled?0u:PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
        SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&throttle,sizeof(throttle));
        SetPriorityClass(GetCurrentProcess(),enabled?NORMAL_PRIORITY_CLASS:BELOW_NORMAL_PRIORITY_CLASS);
    }

    void updatePrecisionTiming() {
        const double scale=static_cast<double>(dpi)/96;
        const bool fastMotion=settings.floating && canAnimate() &&
            (std::hypot(velocityX,velocityY)>3*scale || floatBlend<.995);
        setPrecisionTiming(antennaArmed || poseArmed || fastMotion || (model.ready() && settings.floating && !settings.economy && canAnimate()));
    }

    void initializeAssets() {
        require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&imageFactory)), "Create Windows image decoder");
        if(!testMode){
            const auto atlas=loadImage(imageFactory.Get(),4002),faces=loadImage(imageFactory.Get(),4003);
            HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(4001),RT_RCDATA);
            if(!resource)throw std::runtime_error("Realtime model resource missing");
            model.initialize(LockResource(LoadResource(nullptr,resource)),SizeofResource(nullptr,resource),atlas.pixels.data(),atlas.width,atlas.height,faces.pixels.data(),faces.width,faces.height);
        }
    }

    int spriteSize() const { return settings.hd?kHdSpriteSize:kSpriteSize; }
    const SpriteLayout& layoutFor(int id) const { return settings.hd?kHdLayouts[id]:kLayouts[id]; }

    void setQuality(bool hd) {
        if(settings.hd==hd)return;
        settings.hd=hd;
        for(auto& cached:poseCache)cached=CachedPose{};
        std::vector<uint32_t>().swap(composition);
        composedPose=composedFace=renderedFace=-1;
        render(currentFace,renderedBob);
    }

    CachedPose& getPose(int id) {
        for(auto& cached:poseCache)if(cached.id==id){cached.used=++cacheClock;return cached;}
        auto& cached=*std::min_element(poseCache.begin(),poseCache.begin()+(settings.hd?3:6),[](const auto& a,const auto& b){return a.used<b.used;});
        const int resource=(settings.hd?1001:201)+id*2;
        cached.body=loadImage(imageFactory.Get(),resource);
        cached.faces=loadImage(imageFactory.Get(),resource+1);
        ++poseDecodes;
        if(cached.body.width!=static_cast<UINT>(spriteSize()) || cached.body.height!=static_cast<UINT>(spriteSize()) ||
           cached.faces.width!=static_cast<UINT>(layoutFor(id).width*kFaceCount) || cached.faces.height!=static_cast<UINT>(layoutFor(id).height))
            throw std::runtime_error("Sprite layout does not match its resources");
        cached.id=id;cached.used=++cacheClock;return cached;
    }

    int poseIndex() const {
        const int yawStep=renderedPose<0?static_cast<int>(std::lround(poseYaw/10)):pixels::stableStep(poseYaw,10,renderedPose%kYawCount,1.0,true);
        const int yaw=(yawStep%kYawCount+kYawCount)%kYawCount;
        const int pitch=std::clamp(renderedPose<0?static_cast<int>(std::lround((posePitch+12)/12)):pixels::stableStep(posePitch+12,12,renderedPose/kYawCount,1.0),0,3);
        return pitch*kYawCount+yaw;
    }

    void resetOrientation() {
        gesturePose=false;attitude={};
        baseYaw=settings.view==0 ? 0 : 20;basePitch=settings.view==0 ? 0 : 12;
        if(settings.yaw!=INT_MIN){baseYaw=settings.yaw/1000.0;basePitch=settings.pitch/1000.0;}
        targetYaw=poseYaw=baseYaw;targetPitch=posePitch=basePitch;targetRoll=poseRoll=floatRoll=0;
        renderedPose=-1;
    }

    void stopPose() { KillTimer(hwnd,kPoseTimer);poseClock.stop();poseArmed=false;poseQueued=false; }
    void requestPose(double yaw,double pitch,double roll) {
        targetYaw=yaw;targetPitch=std::clamp(pitch,-12.0,24.0);targetRoll=std::clamp(roll,-7.0,7.0);
        if(shouldHide() || paused || inMenu)return;
        if(!poseArmed){
            poseTime=GetTickCount64();poseSeconds=preciseSeconds();poseArmed=poseClock.start(hwnd,kPoseTimer,settings.frameRate,poseQueued);
        }
        updatePrecisionTiming();
    }
    void poseTick() {
        ++timerWakes;poseQueued=false;
        if(!poseArmed)return;
        if(shouldHide() || paused || inMenu){stopPose();return;}
        const ULONGLONG now=GetTickCount64();
        const double seconds=preciseSeconds(),dt=std::clamp(seconds-poseSeconds,.001,.1);poseSeconds=seconds;
        poseTime=now;
        if(dragging && rotating)moveDrag(false);
        if(gesturePose){
            if(dragging && !rotating){moveDrag(false);flushDrag(false);}
            const bool held=dragging && !rotating;
            const auto k=held?trajectory.measure(static_cast<double>(now)/1000):interaction::Kinematics{};
            attitude.step(k,held,dt,basePitch);
            poseYaw=baseYaw+attitude.yaw.value;posePitch=basePitch+attitude.pitch.value;poseRoll=attitude.roll.value;
            if(held && k.shaking)shakeUntil=now+650;
            if(held && k.acceleration.length()>6500)surpriseUntil=now+180;
            const int face=held ? (now<shakeUntil ? Blink : (now<surpriseUntil ? Surprise : settings.mood)) : currentFace;
            render(face,0);
            const bool heldActive=held && (k.velocity.length()>1 || k.acceleration.length()>20 || now<shakeUntil || now<surpriseUntil);
            if(heldActive || !attitude.settled())requestPose(baseYaw,basePitch,0);
            else {gesturePose=held;attitude.yaw={};attitude.pitch={};attitude.roll={};poseYaw=baseYaw;posePitch=basePitch;poseRoll=0;render(face,0);stopPose();updatePrecisionTiming();}
            return;
        }
        const double follow=1-std::exp(-(dragging && rotating?40:14)*dt);
        poseYaw+=std::remainder(targetYaw-poseYaw,360.0)*follow;
        posePitch+=(targetPitch-posePitch)*follow;poseRoll+=(targetRoll-poseRoll)*follow;
        const double settleAngle=model.ready()?.01:.15;
        const bool settled=std::abs(std::remainder(targetYaw-poseYaw,360.0))<settleAngle && std::abs(targetPitch-posePitch)<settleAngle && std::abs(targetRoll-poseRoll)<settleAngle;
        if(settled){poseYaw=targetYaw;posePitch=targetPitch;poseRoll=targetRoll;}
        render(currentFace,renderedBob);
        if(!settled || (dragging && rotating))requestPose(targetYaw,targetPitch,targetRoll);else {stopPose();updatePrecisionTiming();}
    }

    void zoomWheel(int delta) {
        if(dragging || shouldHide() || inMenu)return;
        wheelRemainder+=delta;
        const int steps=wheelRemainder/WHEEL_DELTA;
        wheelRemainder%=WHEEL_DELTA;
        const int nextSize=std::clamp(settings.size+steps*32,160,1024);
        if(nextSize==settings.size)return;
        const int previousSize=settings.size;
        stopMotion();settings.size=nextSize;
        try{resizeSurface(true);}catch(...){settings.size=previousSize;startMotion();throw;}
        updateHover();startMotion();saveSettings();
    }

    void loadSettings() {
        if (testMode) return;
        PWSTR folder{};
        require(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder), "Locate settings folder");
        std::filesystem::path directory = std::filesystem::path(folder)/L"PicoPet";
        CoTaskMemFree(folder);
        std::filesystem::create_directories(directory);
        configPath = (directory/L"settings.ini").wstring();
        screenDesktop.load(configPath);
        customExpression.load(imageFactory.Get(),directory);
        auto get = [&](const wchar_t* key, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(L"PICO", key, fallback, configPath.c_str()));
        };
        settings.size = get(L"size", 192);
        if (settings.size<160 || settings.size>1024) settings.size=192;
        settings.view = std::clamp(get(L"view", 1), 0, 1);
        settings.mood = std::clamp(get(L"mood", Idle), 0, static_cast<int>(Off));
        settings.x = get(L"x", INT_MIN); settings.y = get(L"y", INT_MIN);
        settings.topmost = get(L"topmost", 1) != 0;
        settings.layerMode=std::clamp(get(L"layerMode",0),0,1);
        settings.appMode=std::clamp(get(L"appMode",0),0,1);settings.appFps=std::clamp(get(L"appFps",60),5,60);
        if(get(L"appFrameVersion",0)<1 && settings.appFps==15)settings.appFps=60;
        settings.shortcutTarget=std::clamp(get(L"shortcutTarget",0),0,1);
        settings.appResolution=std::clamp(get(L"appResolution",3),0,3);
        if(get(L"appCanvasVersion",0)<1 && settings.appResolution==0)settings.appResolution=3;
        wchar_t layerPath[32768]{};GetPrivateProfileStringW(L"PICO",L"layerPath",L"",layerPath,32768,configPath.c_str());settings.layerPath=layerPath;
        wchar_t layerPid[16]{};GetPrivateProfileStringW(L"PICO",L"layerPid",L"0",layerPid,16,configPath.c_str());
        wchar_t* layerPidEnd=nullptr;const auto parsedLayerPid=wcstoull(layerPid,&layerPidEnd,10);
        settings.layerPid=layerPidEnd!=layerPid && !*layerPidEnd && parsedLayerPid<=MAXDWORD?static_cast<DWORD>(parsedLayerPid):0;
        settings.material = std::clamp(get(L"material",0),0,3);
        settings.yaw=get(L"yaw",INT_MIN);settings.pitch=get(L"pitch",INT_MIN);
        if(settings.yaw!=INT_MIN){settings.yaw=std::clamp(settings.yaw,-180000,180000);settings.pitch=std::clamp(settings.pitch,-12000,24000);}
        settings.motionAmplitude=std::clamp(get(L"motionAmplitude",100),0,200);
        settings.throwGain=std::clamp(get(L"throwGain",100),0,200);
        settings.rotationSensitivity=std::clamp(get(L"rotationSensitivity",100),25,200);
        paused=settings.pauseAnimation=get(L"paused",0)!=0;
        settings.clickThrough = get(L"clickThrough", 0) != 0;
        settings.autoHide = get(L"autoHide", 1) != 0;
        settings.economy = get(L"economy", 1) != 0;
        settings.floating = get(L"floating", 0) != 0;
        settings.hd = get(L"hd", 0) == 1;
        settings.frameRate=get(L"frameRate",30);
        settings.frameRate=std::clamp(settings.frameRate,15,120);
        settings.pixelThreshold=get(L"pixelThreshold",1);
        if(settings.pixelThreshold!=1 && settings.pixelThreshold!=2 && settings.pixelThreshold!=4)settings.pixelThreshold=1;
    }

    bool saveSettings() {
        if (testMode || configPath.empty()) return true;
        RECT rect{}; GetWindowRect(hwnd, &rect);
        settings.x = rect.left; settings.y = rect.top;
        std::wstring section;
        auto put = [&](const wchar_t* key, int value) {
            section+=key;section+=L"=";section+=std::to_wstring(value);section.push_back(0);
        };
        put(L"size", settings.size); put(L"view", settings.view); put(L"mood", settings.mood);
        put(L"x", settings.x); put(L"y", settings.y); put(L"topmost", settings.topmost);
        put(L"clickThrough", settings.clickThrough); put(L"autoHide", settings.autoHide); put(L"economy", settings.economy);
        put(L"floating", settings.floating);
        put(L"hd", settings.hd);
        put(L"frameRate",settings.frameRate);put(L"pixelThreshold",settings.pixelThreshold);
        put(L"material",settings.material);put(L"appMode",settings.appMode);put(L"appFps",settings.appFps);
        put(L"shortcutTarget",settings.shortcutTarget);
        put(L"appResolution",settings.appResolution);
        put(L"appCanvasVersion",1);
        put(L"appFrameVersion",1);
        settings.yaw=static_cast<int>(std::lround(baseYaw*1000));settings.pitch=static_cast<int>(std::lround(basePitch*1000));settings.pauseAnimation=paused;
        put(L"yaw",settings.yaw);put(L"pitch",settings.pitch);put(L"paused",paused);
        put(L"motionAmplitude",settings.motionAmplitude);put(L"throwGain",settings.throwGain);put(L"rotationSensitivity",settings.rotationSensitivity);
        put(L"layerMode",settings.layerMode);section+=L"layerPid=";section+=std::to_wstring(settings.layerPid);section.push_back(0);section+=L"layerPath=";section+=settings.layerPath;section.push_back(0);
        section.push_back(0);
        const bool saved=WritePrivateProfileSectionW(L"PICO",section.c_str(),configPath.c_str())!=FALSE;
        if(!saved && trayAdded){tray.uFlags=NIF_INFO;wcscpy_s(tray.szInfoTitle,L"设置未能保存");wcscpy_s(tray.szInfo,L"无法写入本地配置文件。请检查文件权限或磁盘空间。");tray.dwInfoFlags=NIIF_ERROR;Shell_NotifyIconW(NIM_MODIFY,&tray);}
        return saved;
    }

    bool shouldHide() const {
        return manuallyHidden || locked || displayOff || suspended || (settings.autoHide && fullscreen);
    }
    bool canAnimate() const { return !shouldHide() && !paused && !dragging && !inMenu && !embeddedConsole.visible() && !workspace.active() && !customExpression.active(); }

    RECT workArea(POINT point) const {
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info);
        return info.rcWork;
    }
    RECT visibleBounds()const{
        if(model.ready())return model.visibleBounds(extent);
        if(!dib)return {0,0,extent,extent};
        RECT bounds{extent,extent,0,0};
        for(int y=0;y<extent;++y){const auto* row=dib+static_cast<size_t>(y)*extent;int first=0,last=extent-1;
            while(first<extent && (row[first]>>24)<=20)++first;if(first==extent)continue;
            while(last>first && (row[last]>>24)<=20)--last;
            bounds.left=std::min(bounds.left,static_cast<LONG>(first));bounds.right=std::max(bounds.right,static_cast<LONG>(last+1));bounds.top=std::min(bounds.top,static_cast<LONG>(y));bounds.bottom=y+1;}
        return IsRectEmpty(&bounds)?RECT{0,0,extent,extent}:bounds;
    }
    RECT movementBounds(const RECT& area)const{
        const auto visible=model.ready()?model.compressionBounds(extent,poseYaw,posePitch,poseRoll+floatRoll,requestedBob):visibleBounds();
        RECT range{area.left-visible.left,area.top-visible.top,area.right-visible.right,area.bottom-visible.bottom};
        if(range.right<range.left){const LONG middle=(range.left+range.right)/2;range.left=range.right=middle;}
        if(range.bottom<range.top){const LONG middle=(range.top+range.bottom)/2;range.top=range.bottom=middle;}
        return range;
    }

    void stopAntennas(){KillTimer(hwnd,kAntennaTimer);antennaClock.stop();antennaArmed=false;antennaQueued=false;}
    void updateAntennas(int bob,double dt=0){
        if(!model.ready())return;
        RECT window{};GetWindowRect(hwnd,&window);
        const auto area=workArea({window.left+extent/2,window.top+extent/2});
        const auto limits=movementBounds(area);
        if(window.top<limits.top){const LONG delta=limits.top-window.top;window.top=limits.top;
            SetWindowPos(hwnd,nullptr,window.left,window.top,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);motionY+=delta;++positionUpdates;}
        const auto target=model.antennaContact(extent,area.top-window.top,poseYaw,posePitch,poseRoll+floatRoll,bob);
        bool active=false;
        for(size_t i=0;i<antennas.size();++i){auto& antenna=antennas[i];
            if(dt>0)antenna.step(target[i],dt,18,.86);
            antenna.constrain(target[i],1.15);
            if(std::abs(antenna.value-target[i])<.0005 && std::abs(antenna.velocity)<.006){antenna.value=target[i];antenna.velocity=0;}else active=true;
        }
        if(active && !shouldHide() && !paused && !inMenu){
            if(!antennaArmed){antennaSeconds=preciseSeconds();antennaArmed=antennaClock.start(hwnd,kAntennaTimer,settings.frameRate,antennaQueued);updatePrecisionTiming();}
        }else if(antennaArmed){stopAntennas();updatePrecisionTiming();}
    }
    void antennaTick(){
        antennaQueued=false;if(!antennaArmed)return;++timerWakes;
        if(shouldHide() || paused || inMenu){stopAntennas();updatePrecisionTiming();return;}
        const double now=preciseSeconds(),dt=std::clamp(now-antennaSeconds,.001,.1);antennaSeconds=now;
        updateAntennas(requestedBob,dt);render(currentFace,requestedBob);
    }

    UINT frameDelay() const {return static_cast<UINT>((1000+settings.frameRate-1)/settings.frameRate);}
    int movementPixelThreshold() const {
        return settings.pixelThreshold==1 ? 1 : std::max(1,MulDiv(settings.pixelThreshold,static_cast<int>(dpi),96));
    }
    void place(POINT point, bool snap, bool usePixelThreshold=false) {
        const RECT area = workArea({point.x+extent/2, point.y+extent/2});
        const auto limits=movementBounds(area);
        point.x = std::clamp(point.x, limits.left, limits.right);
        point.y = std::clamp(point.y, limits.top, limits.bottom);
        const int threshold = MulDiv(14, static_cast<int>(dpi), 96);
        if (snap) {
            if (point.x-limits.left < threshold) point.x = limits.left;
            else if (limits.right-point.x < threshold) point.x = limits.right;
            if (point.y-limits.top < threshold) point.y = limits.top;
            else if (limits.bottom-point.y < threshold) point.y = limits.bottom;
        }
        RECT current{};GetWindowRect(hwnd,&current);
        if(usePixelThreshold && !snap){
            const int minimum=movementPixelThreshold();
            if(std::max(abs(point.x-current.left),abs(point.y-current.top))<minimum)return;
        }
        if(current.left!=point.x || current.top!=point.y) {
            SetWindowPos(hwnd, nullptr, point.x, point.y, 0, 0, SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);
            ++positionUpdates;
            if(model.ready())render(currentFace,requestedBob);
            if(!dragging && !advancingMotion)updateHover();
        }
    }

    void resetPosition() {
        velocityX=velocityY=0;
        POINT cursor{}; GetCursorPos(&cursor);
        const RECT area = workArea(cursor);
        const auto limits=movementBounds(area);
        place({limits.right-24, limits.bottom-12}, false);
    }

    void releaseSurface() {
        model.discardPending();renderAgain=false;
        if (memoryDC && oldBitmap) SelectObject(memoryDC, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (memoryDC) DeleteDC(memoryDC);
        bitmap = nullptr; memoryDC = nullptr; oldBitmap = nullptr; dib = nullptr;
    }

    void resizeSurface(bool anchorCursor=false) {
        const UINT nextDpi=GetDpiForWindow(hwnd);
        int nextExtent=MulDiv(settings.size,static_cast<int>(nextDpi),96);
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor))nextExtent=std::max(48,std::min(nextExtent,static_cast<int>(std::min(monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top)-8)));
        if(dib && nextExtent==extent && nextDpi==dpi)return;
        RECT before{};GetWindowRect(hwnd,&before);POINT cursor{};GetCursorPos(&cursor);
        const double anchorX=extent?std::clamp(static_cast<double>(cursor.x-before.left)/extent,0.0,1.0):.5;
        const double anchorY=extent?std::clamp(static_cast<double>(cursor.y-before.top)/extent,0.0,1.0):.5;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = nextExtent; info.bmiHeader.biHeight = -nextExtent;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        HDC nextDC=CreateCompatibleDC(nullptr);uint32_t* nextPixels=nullptr;
        HBITMAP nextBitmap=nextDC?CreateDIBSection(nextDC,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&nextPixels),nullptr,0):nullptr;
        if(!nextDC || !nextBitmap){if(nextBitmap)DeleteObject(nextBitmap);if(nextDC)DeleteDC(nextDC);throw std::runtime_error("Create transparent drawing surface");}
        const auto nextOld=SelectObject(nextDC,nextBitmap);
        try{model.discardPending();}catch(...){SelectObject(nextDC,nextOld);DeleteObject(nextBitmap);DeleteDC(nextDC);throw;}
        renderAgain=false;
        const auto previousDC=memoryDC;const auto previousBitmap=bitmap;const auto previousOld=oldBitmap;auto* previousPixels=dib;
        const int previousExtent=extent,previousFace=renderedFace;const UINT previousDpi=dpi;
        memoryDC=nextDC;bitmap=nextBitmap;oldBitmap=nextOld;dib=nextPixels;extent=nextExtent;dpi=nextDpi;
        resizedPosition={before.left,before.top};
        if(anchorCursor){resizedPosition.x+=static_cast<LONG>(std::lround(anchorX*(previousExtent-extent)));resizedPosition.y+=static_cast<LONG>(std::lround(anchorY*(previousExtent-extent)));}
        const auto limits=movementBounds(workArea({resizedPosition.x+extent/2,resizedPosition.y+extent/2}));
        resizedPosition.x=std::clamp(resizedPosition.x,limits.left,limits.right);resizedPosition.y=std::clamp(resizedPosition.y,limits.top,limits.bottom);
        // Keep the old layered frame visible and preserve hover until one complete replacement is ready.
        resizingSurface=true;renderedFace=-1;
        try{render(currentFace,renderedBob);}catch(...){
            resizingSurface=false;SelectObject(nextDC,nextOld);DeleteObject(nextBitmap);DeleteDC(nextDC);
            memoryDC=previousDC;bitmap=previousBitmap;oldBitmap=previousOld;dib=previousPixels;extent=previousExtent;dpi=previousDpi;renderedFace=previousFace;throw;
        }
        resizingSurface=false;
        if(previousDC && previousOld)SelectObject(previousDC,previousOld);if(previousBitmap)DeleteObject(previousBitmap);if(previousDC)DeleteDC(previousDC);
    }

    bool screenHit(POINT client,int pose) {
        if(model.ready())return model.pick(client.x,client.y,extent).kind==1;
        const int source=pixelMapping.sourceIndex(client.x,client.y);
        if(source<0 || pose<0)return false;
        const auto& layout=layoutFor(pose);
        const int x=source%spriteSize()-layout.x,y=source/spriteSize()-layout.y;
        if(x<0 || y<0 || x>=layout.width || y>=layout.height)return false;
        const auto& faces=getPose(pose).faces;
        const size_t offset=static_cast<size_t>(y)*faces.width+x;
        return faces.pixels[offset+Computer*layout.width]!=faces.pixels[offset+Off*layout.width];
    }
    bool assistantSourcePixel(int x,int y,int pose) {
        if(pose<0 || x<0 || y<0 || x>=spriteSize() || y>=spriteSize())return false;
        const auto& area=layoutFor(pose);const int unit=std::max(1,spriteSize()/kSpriteSize);
        if(x<area.x+area.width*3/4 || x>area.x+area.width+32*unit || y<area.y+area.height*4/5 || y>area.y+area.height+35*unit)return false;
        const uint32_t pixel=getPose(pose).body.pixels[static_cast<size_t>(y)*spriteSize()+x];
        const int alpha=pixel>>24,red=(pixel>>16)&255,green=(pixel>>8)&255,blue=pixel&255;
        return alpha>80 && red>125 && red>green+35 && red>blue+20;
    }
    bool assistantButtonHit(POINT client,int pose) {
        if(model.ready())return model.pick(client.x,client.y,extent).kind==2;
        const int source=pixelMapping.sourceIndex(client.x,client.y);
        return source>=0 && assistantSourcePixel(source%spriteSize(),source/spriteSize(),pose);
    }
    bool cursorAssistantButton(int pose) {
        if(testMode || dragging || paused || inMenu || settings.clickThrough || shouldHide())return false;
        POINT cursor{};GetCursorPos(&cursor);if(WindowFromPoint(cursor)!=hwnd)return false;
        ScreenToClient(hwnd,&cursor);return assistantButtonHit(cursor,pose);
    }
    int cursorScreenButton(int pose) {
        if(testMode || embeddedConsole.visible() || workspace.active() || dragging || paused || inMenu || settings.clickThrough || shouldHide())return -1;
        POINT cursor{};GetCursorPos(&cursor);
        if(WindowFromPoint(cursor)!=hwnd)return -1;
        ScreenToClient(hwnd,&cursor);
        if(model.ready()){const auto hit=model.pick(cursor.x,cursor.y,extent);return hit.kind==1?screenDesktop.pick(hit.u,hit.v):-1;}
        const int source=pixelMapping.sourceIndex(cursor.x,cursor.y);
        if(source<0 || !screenHit(cursor,pose))return -1;
        const auto& area=layoutFor(pose);
        return screenDesktop.pick(static_cast<float>(source%spriteSize()-area.x)/std::max(1,area.width),static_cast<float>(source/spriteSize()-area.y)/std::max(1,area.height));
    }
    void updateHover() {
        if(!dib)return;
        const int next=cursorScreenButton(renderedPose);
        const bool nextAssistant=cursorAssistantButton(renderedPose);
        if(next!=hoveredScreen || nextAssistant!=assistantHovered)render(currentFace,renderedBob);
    }
    void mouseMove() {
        if(!trackingMouse){
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};
            trackingMouse=TrackMouseEvent(&track)!=FALSE;
        }
        moveDrag();if(!dragging)updateHover();
    }
    RECT embeddedScreenClientRect(int pose) const {
        const auto& area=layoutFor(pose);const int source=spriteSize();const int inset=std::max(2,source/kSpriteSize*2);
        return {MulDiv(area.x+inset,extent,source),MulDiv(area.y+inset,extent,source),
            MulDiv(area.x+area.width-inset,extent,source),MulDiv(area.y+area.height-inset,extent,source)};
    }
    void drawEmbeddedConsoleContent() {
        const auto state=embeddedConsole.state();if(!state.active)return;
        terminalCaret={};
        RECT screen{40,24,760,476};const int width=720,height=452;
        if(width<80 || height<50)return;
        RECT fullTexture{0,0,800,500};HBRUSH background=CreateSolidBrush(state.interactive?RGB(0,0,0):RGB(3,24,20));FillRect(memoryDC,&fullTexture,background);DeleteObject(background);
        if(state.interactive){
            const int cellWidth=std::max(1,width/state.grid.X),cellHeight=std::max(1,height/state.grid.Y);
            HFONT font=CreateFontW(-cellHeight,cellWidth,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
            HFONT cjkFont=CreateFontW(-cellHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
            const auto previous=SelectObject(memoryDC,font);SetBkMode(memoryDC,OPAQUE);
            constexpr COLORREF colors[]={RGB(0,0,0),RGB(36,73,155),RGB(38,160,96),RGB(40,165,170),RGB(190,63,68),RGB(170,78,179),RGB(190,163,84),RGB(208,211,215),RGB(90,96,102),RGB(108,157,255),RGB(114,224,160),RGB(117,219,231),RGB(255,129,131),RGB(216,150,230),RGB(255,224,150),RGB(245,248,250)};
            for(size_t i=0;i<state.cells.size();++i){const auto& cell=state.cells[i];
                const int col=static_cast<int>(i%state.grid.X),row=static_cast<int>(i/state.grid.X);
                RECT target{screen.left+MulDiv(col,width,state.grid.X),screen.top+MulDiv(row,height,state.grid.Y),screen.left+MulDiv(col+1,width,state.grid.X),screen.top+MulDiv(row+1,height,state.grid.Y)};
                SetBkColor(memoryDC,colors[(cell.Attributes>>4)&15]);ExtTextOutW(memoryDC,0,0,ETO_OPAQUE,&target,nullptr,0,nullptr);
            }
            SetBkMode(memoryDC,TRANSPARENT);
            for(size_t i=0;i<state.cells.size();++i){const auto& cell=state.cells[i];if(cell.Attributes&COMMON_LVB_TRAILING_BYTE)continue;
                const int col=static_cast<int>(i%state.grid.X),row=static_cast<int>(i/state.grid.X),span=(cell.Attributes&COMMON_LVB_LEADING_BYTE) && col+1<state.grid.X?2:1;
                RECT target{screen.left+MulDiv(col,width,state.grid.X),screen.top+MulDiv(row,height,state.grid.Y),screen.left+MulDiv(col+span,width,state.grid.X),screen.top+MulDiv(row+1,height,state.grid.Y)};
                SelectObject(memoryDC,span==2?cjkFont:font);SetTextColor(memoryDC,colors[cell.Attributes&15]);const wchar_t c=cell.Char.UnicodeChar?cell.Char.UnicodeChar:L' ';ExtTextOutW(memoryDC,target.left,target.top,ETO_CLIPPED,&target,&c,1,nullptr);}
            if(state.cursor.X>=0 && state.cursor.X<state.grid.X && state.cursor.Y>=0 && state.cursor.Y<state.grid.Y){
                int col=state.cursor.X;const size_t index=static_cast<size_t>(state.cursor.Y)*state.grid.X+col;
                if(col>0 && index<state.cells.size() && (state.cells[index].Attributes&COMMON_LVB_TRAILING_BYTE))--col;
                terminalCaret={screen.left+MulDiv(col,width,state.grid.X),screen.top+MulDiv(state.cursor.Y,height,state.grid.Y)+1,screen.left+MulDiv(col,width,state.grid.X)+4,screen.top+MulDiv(state.cursor.Y+1,height,state.grid.Y)-1};
                drawTerminalCaret();
            }
            SelectObject(memoryDC,previous);DeleteObject(font);DeleteObject(cjkFont);
            for(int y=screen.top;y<screen.bottom;++y)for(int x=screen.left;x<screen.right;++x)dib[static_cast<size_t>(y)*extent+x]|=0xff000000u;
            return;
        }
        const int wanted=22;
        if(!consoleFont || consoleFontHeight!=wanted){if(consoleFont)DeleteObject(consoleFont);consoleFont=CreateFontW(-wanted,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Cascadia Mono");consoleFontHeight=wanted;}
        const HGDIOBJ previous=SelectObject(memoryDC,consoleFont);SetBkMode(memoryDC,TRANSPARENT);
        const int pad=std::max(3,width/80),line=std::max(wanted+3,height/9);RECT header{screen.left+pad,screen.top+pad,screen.right-pad,screen.top+line};
        SetTextColor(memoryDC,state.busy?RGB(255,198,92):RGB(89,255,191));
        const wchar_t* mode=state.mode==0?L"CMD":state.mode==1?L"CODEX-R":L"CODEX-W";
        const std::wstring title=L"PICO  "+std::wstring(mode)+(state.busy?L"  RUN":L"");DrawTextW(memoryDC,title.c_str(),-1,&header,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        HPEN divider=CreatePen(PS_SOLID,1,RGB(38,113,91));const HGDIOBJ oldPen=SelectObject(memoryDC,divider);MoveToEx(memoryDC,screen.left+pad,screen.top+line,nullptr);LineTo(memoryDC,screen.right-pad,screen.top+line);
        MoveToEx(memoryDC,screen.left+pad,screen.bottom-line,nullptr);LineTo(memoryDC,screen.right-pad,screen.bottom-line);SelectObject(memoryDC,oldPen);DeleteObject(divider);
        RECT body{screen.left+pad,screen.top+line+pad,screen.right-pad,screen.bottom-line-pad};SetTextColor(memoryDC,RGB(178,255,222));
        std::vector<std::wstring> lines(1);int used=0;const int available=static_cast<int>(body.right-body.left);
        for(wchar_t c:state.output){if(c==L'\r')continue;if(c==L'\n'){lines.emplace_back();used=0;continue;}SIZE glyph{};GetTextExtentPoint32W(memoryDC,&c,1,&glyph);if(used+glyph.cx>available && !lines.back().empty()){lines.emplace_back();used=0;}lines.back()+=c;used+=glyph.cx;}
        const int rowHeight=wanted+3,rows=std::max(1,static_cast<int>(body.bottom-body.top)/rowHeight),end=std::max(rows,static_cast<int>(lines.size())-state.scroll),start=std::max(0,end-rows);
        const int saved=SaveDC(memoryDC);IntersectClipRect(memoryDC,body.left,body.top,body.right,body.bottom);
        for(int i=start;i<end && i<static_cast<int>(lines.size());++i)TextOutW(memoryDC,body.left,body.top+(i-start)*rowHeight,lines[i].c_str(),static_cast<int>(lines[i].size()));RestoreDC(memoryDC,saved);
        RECT prompt{screen.left+pad,screen.bottom-line,screen.right-pad,screen.bottom-pad};SetTextColor(memoryDC,RGB(105,246,190));
        std::wstring entry=state.input;size_t caret=std::min(state.caret,entry.size());SIZE before{};
        while(caret){const auto prefix=L"> "+entry.substr(0,caret);GetTextExtentPoint32W(memoryDC,prefix.c_str(),static_cast<int>(prefix.size()),&before);if(before.cx<width-pad*2-wanted)break;entry.erase(0,1);--caret;}
        const auto prefix=L"> "+entry.substr(0,caret);GetTextExtentPoint32W(memoryDC,prefix.c_str(),static_cast<int>(prefix.size()),&before);
        entry=L"> "+entry;DrawTextW(memoryDC,entry.c_str(),-1,&prompt,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        const int caretTop=prompt.top+std::max(0,static_cast<int>(prompt.bottom-prompt.top-wanted)/2);
        terminalCaret={prompt.left+before.cx,caretTop,prompt.left+before.cx+4,caretTop+wanted};drawTerminalCaret();
        SelectObject(memoryDC,previous);
        for(int y=screen.top;y<screen.bottom;++y)for(int x=screen.left;x<screen.right;++x)dib[static_cast<size_t>(y)*extent+x]|=0xff000000u;
    }
    void drawTerminalCaret(){
        if(IsRectEmpty(&terminalCaret))return;
        RECT outline=terminalCaret;InflateRect(&outline,1,1);HBRUSH edge=CreateSolidBrush(RGB(5,15,12));FillRect(memoryDC,&outline,edge);DeleteObject(edge);
        // Stay visible when unfocused: output keeps scrolling while the user clicks elsewhere. Focused uses the bright caret.
        HBRUSH fill=CreateSolidBrush(terminalFocused?RGB(220,255,230):RGB(96,140,118));FillRect(memoryDC,&terminalCaret,fill);DeleteObject(fill);
    }
    void drawEmbeddedConsole(int pose,int tilt,int bob,bool portrait=false) {
        const bool application=workspace.active();
        const bool console=embeddedConsole.visible();
        const bool desktop=!application && !console && (screenHovered || desktopMenu || (pressedScreen>=0 && !dragMoved) || (testMode && currentFace>=Computer));
        if(!application && !console && !desktop && !portrait)return;
        const int mode=application?4:console?2:desktop?1:3;if(screenTextureMode!=mode){screenTextureMode=mode;terminalDirty=true;screenDesktop.dirty=true;}
        if(!terminalDC){
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=800;info.bmiHeader.biHeight=-500;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            terminalDC=CreateCompatibleDC(memoryDC);terminalBitmap=CreateDIBSection(terminalDC,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&terminalPixels),nullptr,0);
            if(!terminalDC || !terminalBitmap)throw std::runtime_error("Create terminal texture");terminalPrevious=SelectObject(terminalDC,terminalBitmap);
        }
        const HDC surface=memoryDC;auto* surfacePixels=dib;const int surfaceExtent=extent;
        const auto* imagePixels=mode==3?&customExpression.texture():nullptr;
        if(mode==1?screenDesktop.dirty:terminalDirty){
            memoryDC=terminalDC;dib=terminalPixels;extent=800;
            try{
            if(application)workspace.draw(terminalDC,terminalPixels);else if(console)drawEmbeddedConsoleContent();else if(imagePixels)std::copy(imagePixels->begin(),imagePixels->end(),terminalPixels);else{
                const int hover=testMode && currentFace>=ComputerPerformance?currentFace-ComputerPerformance:desktopHover;
                const double fade=desktopFadeStart?std::max(0.0,1-(GetTickCount64()-desktopFadeStart)/120.0):0;
                screenDesktop.draw(terminalDC,hover,desktopPressed,desktopFading,fade);
            }
            }catch(...){memoryDC=surface;dib=surfacePixels;extent=surfaceExtent;throw;}
            memoryDC=surface;dib=surfacePixels;extent=surfaceExtent;GdiFlush();for(int i=0;i<800*500;++i)terminalPixels[i]|=0xff000000u;if(model.ready())model.uploadTerminal(terminalPixels);terminalDirty=false;
        }
        if(model.ready())return;
        if(terminalMapPose!=pose || terminalMapExtent!=extent || terminalMapTilt!=tilt || terminalMapBob!=bob){
            terminalMap.assign(static_cast<size_t>(extent)*extent,-1);
            constexpr double radians=3.141592653589793/180;
            const double yaw=(pose%36)*10*radians,pitch=(-12+pose/36*12)*radians,roll=tilt*.5*radians;
            const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
            struct Projected {double x,y,u,v;};std::vector<Projected> points;
            for(const auto& vertex:kScreenVertices){
                const double x=(cy*vertex.x-sy*(vertex.z+3))/120*extent;
                const double y=-(-sp*sy*vertex.x+cp*(vertex.y-44)-sp*cy*(vertex.z+3))/120*extent;
                points.push_back({extent*.5+x*cr-y*sr,extent*.5+x*sr+y*cr+static_cast<double>(bob)*extent/kSpriteSize,vertex.u,vertex.v});
            }
            if(cy>0.12)for(const auto& triangle:kScreenTriangles){
                const auto& a=points[triangle[0]];const auto& b=points[triangle[1]];const auto& c=points[triangle[2]];
                const double denominator=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(std::abs(denominator)<1e-8)continue;
                const int left=std::max(0,static_cast<int>(std::floor(std::min({a.x,b.x,c.x})))),right=std::min(extent-1,static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))));
                const int top=std::max(0,static_cast<int>(std::floor(std::min({a.y,b.y,c.y})))),bottom=std::min(extent-1,static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))));
                for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
                    const double wa=((b.y-c.y)*(x+.5-c.x)+(c.x-b.x)*(y+.5-c.y))/denominator,wb=((c.y-a.y)*(x+.5-c.x)+(a.x-c.x)*(y+.5-c.y))/denominator,wc=1-wa-wb;
                    if(wa<0 || wb<0 || wc<0)continue;
                    const int u=std::clamp(static_cast<int>((wa*a.u+wb*b.u+wc*c.u)*800),0,799),v=std::clamp(static_cast<int>((1-wa*a.v-wb*b.v-wc*c.v)*500),0,499);
                    terminalMap[static_cast<size_t>(y)*extent+x]=v*800+u;
                }
            }
            terminalMapPose=pose;terminalMapExtent=extent;terminalMapTilt=tilt;terminalMapBob=bob;
        }
        for(size_t i=0;i<terminalMap.size();++i)if(terminalMap[i]>=0)dib[i]=terminalPixels[terminalMap[i]]|0xff000000u;
    }
    void render(int face, int bob) {
        if(!dib)return;
        currentFace=face;
        requestedBob=bob;
        updateAntennas(bob);
        if(model.pending()){renderAgain=true;return;}
        const double started=preciseSeconds();
        const int pose=poseIndex();
        const double visualRoll=poseRoll+floatRoll;
        const int tilt=renderedFace<0?static_cast<int>(std::lround(visualRoll*2)):pixels::stableStep(visualRoll,.5,renderedTilt,.08);
        const int sourceSize=spriteSize();
        if(!model.ready())pixelMapping.prepare(extent,sourceSize,tilt,bob*(sourceSize/kSpriteSize),settings.hd);
        if(!resizingSurface){hoveredScreen=cursorScreenButton(pose);screenHovered=hoveredScreen!=-1;assistantHovered=cursorAssistantButton(pose);}
        if(hoveredScreen!=desktopHover || pressedScreen!=desktopPressed){
            BOOL animate=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);
            desktopFading=desktopHover;desktopFadeStart=animate && desktopFading>=0 && !testMode && !settings.economy?GetTickCount64():0;
            desktopHover=hoveredScreen;desktopPressed=pressedScreen;screenDesktop.dirty=true;
            if(desktopFadeStart)SetTimer(hwnd,kDesktopTimer,frameDelay(),nullptr);
        }
        if(screenHovered || desktopMenu)face=hoveredScreen>=0 && hoveredScreen<4?ComputerPerformance+hoveredScreen:Computer;
        if(pressedScreen>=0 && !dragMoved && !rotating)face=pressedScreen<4?ComputerPerformance+pressedScreen:Computer;
        if(embeddedConsole.visible() || workspace.active())face=ComputerDiagnostics;
        const bool desktop=!workspace.active() && !embeddedConsole.visible() && face>=Computer;
        const bool portrait=!workspace.active() && !desktop && !embeddedConsole.visible() && face!=Off && customExpression.active();
        if(testMode && desktop && renderedFace!=face)screenDesktop.dirty=true;
        const int button=pressedAssistant?2:embeddedConsole.visible()?3:assistantHovered?1:0;
        if (!dib || (!(desktop && screenDesktop.dirty) && !((workspace.active() || embeddedConsole.visible() || portrait) && terminalDirty) && renderedFace == face && renderedBob == bob && renderedPose==pose && renderedTilt==tilt && (!model.ready() || (lastYaw==poseYaw && lastPitch==posePitch && lastRoll==visualRoll && lastButton==button && lastLeftBend==antennas[0].value && lastRightBend==antennas[1].value)))) return;
        if(!model.ready() && (composedPose!=pose || composedFace!=face)){
            const auto& cached=getPose(pose);
            const auto& layout=layoutFor(pose);
            composition.resize(static_cast<size_t>(sourceSize)*sourceSize);
            std::copy(cached.body.pixels.begin(),cached.body.pixels.end(),composition.begin());
            for(int y=0;y<layout.height;++y){
                const uint32_t* source=cached.faces.pixels.data()+y*cached.faces.width+face*layout.width;
                std::copy_n(source,layout.width,composition.data()+(layout.y+y)*sourceSize+layout.x);
            }
            composedPose=pose;composedFace=face;++compositions;
        }
        if(!model.ready())pixelMapping.paint(composition.data(),dib);
        drawEmbeddedConsole(pose,tilt,bob,portrait);
        RenderedFrame frame{face,bob,pose,tilt,button,poseYaw,posePitch,visualRoll,0,antennas[0].value,antennas[1].value};
        const bool completed=!model.ready() || model.draw(dib,extent,poseYaw,posePitch,visualRoll,bob,settings.hd,face,button,workspace.active() || embeddedConsole.visible() || desktop || portrait,settings.material,!resizingSurface && (poseArmed || motionArmed || antennaArmed),static_cast<float>(frame.leftBend),static_cast<float>(frame.rightBend));
        frame.preparationMs=(preciseSeconds()-started)*1000;
        if(!completed){pendingFrame=frame;return;}
        presentFrame(frame);
    }
    void renderReady(){
        if(!model.pending())return;
        const double started=preciseSeconds();
        if(!model.readPixels(dib,extent))return;
        pendingFrame.preparationMs+=(preciseSeconds()-started)*1000;
        presentFrame(pendingFrame);
        if(renderAgain){renderAgain=false;renderedFace=-1;render(currentFace,requestedBob);}
    }
    void desktopTick(){
        if(!screenHovered || shouldHide() || !desktopFadeStart || GetTickCount64()-desktopFadeStart>=120){KillTimer(hwnd,kDesktopTimer);desktopFadeStart=0;}
        screenDesktop.dirty=true;if(screenHovered && !shouldHide())render(currentFace,renderedBob);
    }
    void presentFrame(const RenderedFrame& frame){
        const double started=preciseSeconds();
        if(model.ready()){frameSubmitMs+=model.submitMs;frameReadbackMs+=model.readbackMs;frameCopyMs+=model.copyMs;frameGpuLatencyMs+=model.gpuLatencyMs;}
        lastYaw=frame.yaw;lastPitch=frame.pitch;lastRoll=frame.roll;lastButton=frame.button;
        lastLeftBend=frame.leftBend;lastRightBend=frame.rightBend;
        RECT rect{}; GetWindowRect(hwnd, &rect);
        POINT destination=resizingSurface?resizedPosition:POINT{rect.left,rect.top},source{};
        SIZE size{extent, extent};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const double presentStart=preciseSeconds();
        if (!UpdateLayeredWindow(hwnd, nullptr, &destination, &size, memoryDC, &source, 0, &blend, ULW_ALPHA))
            throw std::runtime_error("Update transparent window");
        const double presented=preciseSeconds();framePresentMs+=(presented-presentStart)*1000;
        if(lastFrameSeconds>0)frameGaps[frameGapCount++%frameGaps.size()]=(presented-lastFrameSeconds)*1000;
        lastFrameSeconds=presented;
        if(renderedFace>=Computer && frame.face<Computer)++desktopExits;
        renderedFace=frame.face;renderedBob=frame.bob;renderedPose=frame.pose;renderedTilt=frame.tilt;renderedRoll=static_cast<int>(std::lround(frame.tilt*.5));++draws;
        const double ms=frame.preparationMs+(preciseSeconds()-started)*1000;frameTotalMs+=ms;frameMaxMs=std::max(frameMaxMs,ms);
    }

    void stopAnimation() { KillTimer(hwnd, kAnimationTimer); animationArmed = false; }
    void arm(UINT delay) {
        stopAnimation();
        if (canAnimate()) animationArmed = SetCoalescableTimer(hwnd, kAnimationTimer, delay, nullptr, delay>1000 ? 200 : 0) != 0;
    }
    UINT blinkDelay() {
        randomState ^= randomState<<13; randomState ^= randomState>>17; randomState ^= randomState<<5;
        return (settings.economy || powerSaving ? 6500u : 4000u) + randomState%2500;
    }

    void scheduleIdle() {
        stopAnimation();
        if (settings.mood == Idle && canAnimate()) arm(blinkDelay());
    }

    void applyPolicy() {
        systemdesk::suspend(locked || displayOff || suspended);
        stopAnimation();stopMotion();stopPose();stopAntennas();
        blinking = false; reactionUntil = 0;
        hidden = shouldHide();
        workspace.suspend(hidden || paused);
        watchLayer();
        if((hidden || paused) && embeddedConsole.visible())embeddedConsole.hide();
        if ((hidden || paused) && dragging) cancelDrag();
        if(hidden || paused){gesturePose=false;attitude={};velocityX=velocityY=0;poseYaw=targetYaw=baseYaw;posePitch=targetPitch=basePitch;poseRoll=targetRoll=floatRoll=0;}
        if (!testMode) {ShowWindow(hwnd, hidden ? SW_HIDE : SW_SHOWNOACTIVATE);if(!hidden)syncTopmost();}
        if (!hidden) render(settings.mood, 0);
        scheduleIdle();
        startMotion();
        if(!hidden && !paused && (gesturePose || std::abs(poseYaw-baseYaw)>.1 || std::abs(posePitch-basePitch)>.1 || std::abs(poseRoll)>.1))requestPose(baseYaw,basePitch,0);
        updatePrecisionTiming();
    }

    void stopMotion() { KillTimer(hwnd,kMotionTimer);motionClock.stop();motionContinuous=false;motionQueued=false;motionArmed=false; }
    void startMotion() {
        stopMotion();
        if(!settings.floating || !canAnimate())return;
        RECT r{};GetWindowRect(hwnd,&r);const double scale=static_cast<double>(dpi)/96;
        const bool coasting=std::hypot(velocityX,velocityY)>3*scale;floatBlend=coasting?0:1;
        const auto drift=interaction::floatingMotion(floatPhase);
        const double amplitude=settings.motionAmplitude/100.0;
        floatRoll=!coasting && !gesturePose && !poseArmed ? drift.roll*amplitude : 0;
        motionX=r.left-drift.offset.x*scale*floatBlend*amplitude;motionY=r.top-drift.offset.y*scale*floatBlend*amplitude;
        motionTime=GetTickCount64();
        motionSeconds=preciseSeconds();
        scheduleMotion();
    }
    void scheduleMotion() {
        if(!settings.floating || !canAnimate())return;
        const double scale=static_cast<double>(dpi)/96;
        const bool moving=std::hypot(velocityX,velocityY)>3*scale || floatBlend<.995;
        const bool smooth=model.ready() && !settings.economy;
        UINT delay=frameDelay();
        if(!moving && !smooth){RECT current{};GetWindowRect(hwnd,&current);
            const double next=interaction::nextFloatingPixelChange(floatPhase,{motionX,motionY},scale,{static_cast<double>(current.left),static_cast<double>(current.top)},movementPixelThreshold());
            delay=static_cast<UINT>(std::clamp(std::ceil(next*1000)+2,static_cast<double>(frameDelay()),2000.0));}
        if(model.ready() && (moving || smooth)){
            if(!motionContinuous){KillTimer(hwnd,kMotionTimer);motionArmed=motionContinuous=motionClock.start(hwnd,kMotionTimer,settings.frameRate,motionQueued);}
        }else {if(motionContinuous)stopMotion();motionArmed=SetCoalescableTimer(hwnd,kMotionTimer,delay,nullptr,moving?0u:16u)!=0;}
        updatePrecisionTiming();
    }
    void advanceMotion(double dt) {
        const double scale=static_cast<double>(dpi)/96;
        const RECT area=workArea({static_cast<LONG>(motionX)+extent/2,static_cast<LONG>(motionY)+extent/2});
        const auto limits=movementBounds(area);
        const double left=limits.left,top=limits.top,right=limits.right,bottom=limits.bottom;
        floatPhase+=dt;const auto drift=interaction::floatingMotion(floatPhase);
        if(std::hypot(velocityX,velocityY)>3*scale) {
            // Exact exponential drag keeps the throw independent of timer jitter.
            const double decay=std::exp(-kMotionDamping*dt);
            motionX+=velocityX*(1-decay)/kMotionDamping;motionY+=velocityY*(1-decay)/kMotionDamping;
            velocityX*=decay;velocityY*=decay;
            if(motionX<left){motionX=left;velocityX=std::abs(velocityX)*kEdgeRestitution;}
            if(motionX>right){motionX=right;velocityX=-std::abs(velocityX)*kEdgeRestitution;}
            if(motionY<top){motionY=top;velocityY=std::abs(velocityY)*kEdgeRestitution;}
            if(motionY>bottom){motionY=bottom;velocityY=-std::abs(velocityY)*kEdgeRestitution;}
            const double speed=std::hypot(velocityX,velocityY)/scale;
            const double blendTarget=1-std::clamp((speed-3)/72,0.0,1.0);
            floatBlend=std::max(floatBlend,floatBlend+(blendTarget-floatBlend)*(1-std::exp(-2.4*dt)));
        } else {
            velocityX=velocityY=0;floatBlend+= (1-floatBlend)*(1-std::exp(-2.4*dt));
            if(floatBlend>.999)floatBlend=1;
        }
        const double marginX=1.36*scale*floatBlend*settings.motionAmplitude/100.0,marginY=1.93*scale*floatBlend*settings.motionAmplitude/100.0;
        if(right-left>2*marginX)motionX=std::clamp(motionX,left+marginX,right-marginX);
        if(bottom-top>2*marginY)motionY=std::clamp(motionY,top+marginY,bottom-marginY);
        const double amplitude=settings.motionAmplitude/100.0;
        const double dx=drift.offset.x*scale*floatBlend*amplitude,dy=drift.offset.y*scale*floatBlend*amplitude;
        place({static_cast<LONG>(std::lround(motionX+dx)),static_cast<LONG>(std::lround(motionY+dy))},false,true);
        const double nextFloatRoll=floatBlend>.8 && !gesturePose && !poseArmed && !dragging ? drift.roll*floatBlend*amplitude : 0;
        if(nextFloatRoll!=floatRoll){floatRoll=nextFloatRoll;render(currentFace,0);}
    }
    void motionTick() {
        ++timerWakes;motionQueued=false;if(!motionContinuous)stopMotion();
        if(!settings.floating || !canAnimate()){stopMotion();return;}
        const ULONGLONG now=GetTickCount64();
        const double maxDt=std::hypot(velocityX,velocityY)>3*static_cast<double>(dpi)/96 || floatBlend<.995?.2:2.5;
        const double seconds=preciseSeconds(),dt=std::clamp(seconds-motionSeconds,.001,maxDt);motionSeconds=seconds;
        motionTime=now;advancingMotion=true;advanceMotion(dt);advancingMotion=false;scheduleMotion();updatePrecisionTiming();
    }
    void recordDrag(POINT point,ULONGLONG now) {
        trajectory.add({point.x/dragScale,point.y/dragScale},static_cast<double>(now)/1000);
    }
    void releaseVelocity(ULONGLONG now) {
        velocityX=velocityY=0;
        if(!settings.floating)return;
        const auto v=trajectory.release(static_cast<double>(now)/1000)*(dragScale*settings.throwGain/100.0);
        velocityX=v.x;velocityY=v.y;
    }

    void tick() {
        ++timerWakes; stopAnimation();
        if (!canAnimate()) return;
        const ULONGLONG now = GetTickCount64();
        if (reactionUntil && now < reactionUntil) {
            const int bob = 0;
            render(reactionFace, bob);
            arm(settings.economy || powerSaving ? static_cast<UINT>(reactionUntil-now) : 125);
        } else if (reactionUntil) {
            reactionUntil = 0; render(settings.mood, 0); scheduleIdle();
        } else if (blinking) {
            blinking = false; render(settings.mood, 0); scheduleIdle();
        } else if (settings.mood == Idle) {
            blinking = true; render(Blink, 0); arm(140);
        }
    }

    void react(Face face) {
        if (!canAnimate()) return;
        blinking = false; reactionFace = face; reactionUntil = GetTickCount64()+2000; reactionStep = 0;
        render(face, 0); arm(settings.economy || powerSaving ? 2000 : 125);
    }

    void syncTopmost() {
        layer.apply(hwnd,settings.layerMode==1,settings.topmost);
    }
    void watchLayer(){
        KillTimer(hwnd,kLayerWatchTimer);
        if(settings.layerMode==1 && !shouldHide())SetCoalescableTimer(hwnd,kLayerWatchTimer,1000,nullptr,200);
    }
    void refreshLayer(){
        layer.acknowledge();layerUpdatePending=false;KillTimer(hwnd,kLayerTimer);
        if(settings.layerMode==1){layer.apply(hwnd,true,settings.topmost,true);const DWORD pid=layer.pid();if(pid && pid!=settings.layerPid){settings.layerPid=pid;saveSettings();}}
    }
    void updateStyles() {
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        style = settings.clickThrough ? style|WS_EX_TRANSPARENT : style&~WS_EX_TRANSPARENT;
        style = (workspace.active() || embeddedConsole.visible()) ? style&~WS_EX_NOACTIVATE : style|WS_EX_NOACTIVATE;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
        layer.configure(hwnd,kLayerMessage,settings.layerMode==1,settings.layerPath,settings.layerPid);
        watchLayer();
        SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
        layer.apply(hwnd,settings.layerMode==1,settings.topmost,true);
        if(settings.layerMode==1 && layer.pid())settings.layerPid=layer.pid();
    }
    void openWorkspace(){
        if(workspace.active())return;
        if(embeddedConsole.visible())embeddedConsole.hide();
        stopAnimation();stopMotion();stopPose();workspace.open(hwnd,settings.appMode,settings.appFps,settings.appResolution);
        SetTimer(hwnd,9,100,nullptr);terminalDirty=true;renderedFace=-1;updateStyles();render(ComputerDiagnostics,0);
    }
    void closeWorkspace(){
        workspace.close();KillTimer(hwnd,9);appPointerDown=false;if(GetCapture()==hwnd)ReleaseCapture();terminalDirty=true;renderedFace=-1;updateStyles();applyPolicy();
    }
    void workspaceTick(){
        if(hidden || paused)return;workspace.tick();if(workspace.updated() && !hidden){terminalDirty=true;renderedFace=-1;render(ComputerDiagnostics,0);}
    }
    bool workspaceMouse(UINT message,WPARAM buttons,LPARAM coordinates){
        if(!workspace.active() || dragging)return false;
        POINT point{GET_X_LPARAM(coordinates),GET_Y_LPARAM(coordinates)};
        if(message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL)ScreenToClient(hwnd,&point);
        if((GetKeyState(VK_MENU)&0x8000) && message==WM_LBUTTONDOWN)return false;
        bool hit=false;POINT mapped{};
        if(model.ready()){const auto pick=model.pick(point.x,point.y,extent);hit=pick.kind==1;mapped={static_cast<LONG>(pick.u*800),static_cast<LONG>(pick.v*500)};}
        else if(screenHit(point,renderedPose)){const int source=pixelMapping.sourceIndex(point.x,point.y);const auto& area=layoutFor(renderedPose);mapped={MulDiv(source%spriteSize()-area.x,800,area.width),MulDiv(source/spriteSize()-area.y,500,area.height)};hit=true;}
        if(!hit && !appPointerDown)return false;if(hit)lastAppPoint=mapped;else mapped=lastAppPoint;
        if(message==WM_LBUTTONDOWN)appPointerDown=true;
        workspace.mouse(message,buttons,mapped.x,mapped.y);
        if(message==WM_LBUTTONUP){appPointerDown=false;if(GetCapture()==hwnd)ReleaseCapture();}return true;
    }
    void toggleEmbeddedConsole(){
        if(workspace.active())closeWorkspace();
        if(embeddedConsole.visible()){embeddedConsole.hide();return;}
        stopAnimation();stopMotion();stopPose();velocityX=velocityY=0;floatRoll=0;
        terminalDirty=true;embeddedConsole.show(hwnd);updateStyles();SetForegroundWindow(hwnd);SetFocus(hwnd);composedFace=renderedFace=-1;render(ComputerDiagnostics,0);saveSettings();
    }
    void embeddedConsoleUpdated(){embeddedConsole.acknowledge();terminalDirty=true;renderedFace=-1;render(embeddedConsole.visible()?ComputerDiagnostics:settings.mood,0);}
    void terminalFocus(bool focused){terminalFocused=focused;if(embeddedConsole.visible()){terminalDirty=true;renderedFace=-1;render(ComputerDiagnostics,0);}}
    void embeddedConsoleClosed(){updateStyles();composedFace=renderedFace=-1;render(settings.mood,0);applyPolicy();}

    void updateEnvironment() {
        HWND foreground = GetForegroundWindow();
        bool next = false;
        if (foreground && foreground != hwnd && !IsIconic(foreground) && IsWindowVisible(foreground)) {
            wchar_t name[80]{}; GetClassNameW(foreground, name, 80);
            if (wcscmp(name,L"Progman") && wcscmp(name,L"WorkerW") && wcscmp(name,L"Shell_TrayWnd")) {
                RECT window{};
                if (FAILED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &window, sizeof(window))))
                    GetWindowRect(foreground, &window);
                const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
                MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
                const RECT& screen = info.rcMonitor;
                next = monitor == MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) &&
                    window.left<=screen.left && window.top<=screen.top && window.right>=screen.right && window.bottom>=screen.bottom;
            }
        }
        if (next != fullscreen) { fullscreen = next; applyPolicy(); }
        if(!hidden)syncTopmost();
    }

    void addTray() {
        if (testMode) return;
        tray.hWnd = hwnd; tray.uID = 1; tray.uFlags = NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;
        tray.uCallbackMessage = kTrayMessage;
        tray.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
        wcscpy_s(tray.szTip, L"PICO 像素电视宠物");
        trayAdded = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
        tray.uVersion = NOTIFYICON_VERSION_4;
        if (trayAdded) Shell_NotifyIconW(NIM_SETVERSION, &tray);
    }

    void openPreferences() {
        Settings snapshot=settings;snapshot.pauseAnimation=paused;
        snapshot.yaw=static_cast<int>(std::lround(baseYaw*1000));snapshot.pitch=static_cast<int>(std::lround(basePitch*1000));
        preferencesWindow.open(hwnd,snapshot,[this](const Settings& next){
            const bool resized=next.size!=settings.size,qualityChanged=next.hd!=settings.hd;
            stopMotion();stopPose();settings=next;workspace.configure(settings.appMode,settings.appFps,settings.appResolution);paused=settings.pauseAnimation;resetOrientation();
            if(qualityChanged){for(auto& cached:poseCache)cached=CachedPose{};composition.clear();composedPose=composedFace=-1;}
            renderedFace=-1;
            if(resized){RECT r{};GetWindowRect(hwnd,&r);resizeSurface();place({r.left,r.top},false);}
            updateStyles();applyPolicy();return saveSettings();
        },[this]{Settings current=settings;current.pauseAnimation=paused;current.yaw=static_cast<int>(std::lround(baseYaw*1000));current.pitch=static_cast<int>(std::lround(basePitch*1000));return current;});
    }

    void configureExpression(UINT action){
        inMenu=true;stopAnimation();stopMotion();stopPose();
        try{
            if(action==ImportExpression){
                ComPtr<IFileOpenDialog> dialog;require(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Open image picker");
                const COMDLG_FILTERSPEC filters[]={{L"图片 (PNG / JPEG / BMP / GIF)",L"*.png;*.jpg;*.jpeg;*.bmp;*.gif"}};
                dialog->SetTitle(L"选择自定义表情图片");dialog->SetFileTypes(1,filters);dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST);
                if(SUCCEEDED(dialog->Show(preferencesWindow.handle()?preferencesWindow.handle():hwnd))){ComPtr<IShellItem> item;require(dialog->GetResult(&item),"Read selected image");PWSTR path=nullptr;require(item->GetDisplayName(SIGDN_FILESYSPATH,&path),"Read image path");const std::wstring selected=path;CoTaskMemFree(path);customExpression.import(selected);}
            }else customExpression.configure(action==UseExpression?true:action==BuiltinExpression?false:customExpression.enabled,action==ExpressionCover?true:action==ExpressionContain?false:customExpression.cover,action==ExpressionLight?true:action==ExpressionDark?false:customExpression.light);
        }catch(const std::exception& e){const std::string error=e.what();const auto message=L"图片设置未完成。请选择有效图片（最大 6400 万像素），并确认本地配置目录可写。\r\n"+std::wstring(error.begin(),error.end());MessageBoxW(hwnd,message.c_str(),L"自定义表情",MB_OK|MB_ICONWARNING);}
        inMenu=false;terminalDirty=true;renderedFace=-1;applyPolicy();
    }
    void chooseScreenShortcut(bool folder){
        inMenu=desktopMenu=true;stopAnimation();stopMotion();stopPose();
        screenDesktop.choose(hwnd,folder);inMenu=desktopMenu=false;screenDesktop.dirty=true;renderedFace=-1;applyPolicy();
    }
    void desktopContext(POINT at,int index=-1){
        inMenu=desktopMenu=true;stopAnimation();stopMotion();stopPose();
        screenDesktop.dirty=true;renderedFace=-1;render(settings.mood,0);
        HMENU popup=createMenu();
        InsertMenuW(popup,0,MF_BYPOSITION|MF_STRING,AddScreenFile,L"添加程序或文件…");InsertMenuW(popup,1,MF_BYPOSITION|MF_STRING,AddScreenFolder,L"添加文件夹…");InsertMenuW(popup,2,MF_BYPOSITION|MF_SEPARATOR,0,nullptr);
        constexpr UINT openShortcut=4000,removeShortcut=4001,openInTV=4002,openOnDesktop=4003,removeBase=4100;
        if(index>=0 && index<4+static_cast<int>(screenDesktop.count())){InsertMenuW(popup,0,MF_BYPOSITION|MF_STRING,openShortcut,L"打开选中项目");if(index>=4){InsertMenuW(popup,1,MF_BYPOSITION|MF_STRING,openInTV,L"在电视中打开");InsertMenuW(popup,2,MF_BYPOSITION|MF_STRING,removeShortcut,L"从屏幕移除快捷方式");}}
        else if(screenDesktop.count()){
            HMENU remove=CreatePopupMenu();for(size_t i=0;i<screenDesktop.count();++i){auto name=screenDesktop.name(static_cast<int>(i)+4);size_t pos=0;while((pos=name.find(L'&',pos))!=std::wstring::npos){name.insert(pos,1,L'&');pos+=2;}AppendMenuW(remove,MF_STRING,removeBase+i,name.c_str());}InsertMenuW(popup,2,MF_BYPOSITION|MF_POPUP,reinterpret_cast<UINT_PTR>(remove),L"移除快捷方式");
        }
        if(index>=4 && index<4+static_cast<int>(screenDesktop.count()))InsertMenuW(popup,2,MF_BYPOSITION|MF_STRING,openOnDesktop,L"在桌面中打开");
        SetForegroundWindow(hwnd);const auto chosen=TrackPopupMenuEx(popup,TPM_RETURNCMD|TPM_RIGHTBUTTON,at.x,at.y,hwnd,nullptr);DestroyMenu(popup);
        PostMessageW(hwnd,WM_NULL,0,0);inMenu=desktopMenu=false;screenDesktop.dirty=true;renderedFace=-1;applyPolicy();
        if(chosen==openShortcut)activateDesktop(index);
        else if(chosen==openInTV){const auto path=screenDesktop.path(index);if(!path.empty()){openWorkspace();workspace.launchPath(path);}}
        else if(chosen==openOnDesktop)screenDesktop.launch(hwnd,index);
        else if(chosen==removeShortcut || (chosen>=removeBase && chosen<removeBase+32)){
            screenDesktop.remove(hwnd,chosen==removeShortcut?index:static_cast<int>(chosen-removeBase)+4);
            renderedFace=-1;render(settings.mood,0);
        }
        else if(chosen)command(chosen);
    }
    void activateDesktop(int index){
        if(index==screendesktop::Previous || index==screendesktop::Next){screenDesktop.page=std::clamp(screenDesktop.page+(index==screendesktop::Next?1:-1),0,screenDesktop.pages()-1);screenDesktop.dirty=true;renderedFace=-1;render(settings.mood,0);return;}
        if(index==screendesktop::Add){POINT at{};GetCursorPos(&at);desktopContext(at);return;}
        if(index==screendesktop::Terminal){toggleEmbeddedConsole();return;}
        if(index==screendesktop::Settings){openPreferences();return;}
        if(index<0 || index>=4+static_cast<int>(screenDesktop.count()))return;
        screenDesktop.selected=index;screenDesktop.dirty=true;renderedFace=-1;render(settings.mood,0);
        if(index<4)systemdesk::open(index);
        else if(settings.shortcutTarget==0){openWorkspace();workspace.launchPath(screenDesktop.path(index));}
        else screenDesktop.launch(hwnd,index);
    }
    HMENU createMenu() {
        HMENU root=CreatePopupMenu(),appearance=CreatePopupMenu(),material=CreatePopupMenu(),system=CreatePopupMenu();
        auto item=[](HMENU m,UINT id,const wchar_t* label,bool selected=false){AppendMenuW(m,MF_STRING|(selected?MF_CHECKED:0),id,label);};
        item(root,OpenSettings,L"偏好设置…");
        item(root,ScreenShortcuts,L"屏幕快捷方式…");
        HMENU applications=CreatePopupMenu();item(applications,appworkspace::Open,L"进入电视应用",workspace.active());item(applications,appworkspace::Choose,L"接入已打开的窗口…");item(applications,appworkspace::Launch,L"打开应用…");
        item(applications,appworkspace::Single,L"单应用铺满",settings.appMode==0);item(applications,appworkspace::Desktop,L"多窗口桌面",settings.appMode==1);item(applications,appworkspace::Detach,L"释放最近接入的窗口");item(applications,appworkspace::Return,L"返回桌宠 · 恢复全部窗口");AppendMenuW(root,MF_POPUP,reinterpret_cast<UINT_PTR>(applications),L"电视应用");
        HMENU expressions=CreatePopupMenu();
        item(expressions,ImportExpression,L"导入图片 / 立绘…");item(expressions,UseExpression,L"使用已导入图片",customExpression.active());
        EnableMenuItem(expressions,UseExpression,MF_BYCOMMAND|(customExpression.available()?MF_ENABLED:MF_GRAYED));
        item(expressions,BuiltinExpression,L"使用内置表情",!customExpression.active());AppendMenuW(expressions,MF_SEPARATOR,0,nullptr);
        item(expressions,ExpressionContain,L"完整显示 · 保持比例",!customExpression.cover);item(expressions,ExpressionCover,L"铺满屏幕 · 居中裁切",customExpression.cover);
        AppendMenuW(expressions,MF_SEPARATOR,0,nullptr);item(expressions,ExpressionDark,L"深色底色",!customExpression.light);item(expressions,ExpressionLight,L"浅色底色",customExpression.light);
        AppendMenuW(root,MF_POPUP,reinterpret_cast<UINT_PTR>(expressions),L"屏幕表情");
        AppendMenuW(root,MF_SEPARATOR,0,nullptr);
        item(root,SystemConsole,L"命令终端");
        item(system,SystemPerformance,L"性能与设备");item(system,SystemDisks,L"文件索引与存储");
        item(system,SystemNetwork,L"网络监测");item(system,SystemDiagnostics,L"安全诊断与日志");
        AppendMenuW(root,MF_POPUP,reinterpret_cast<UINT_PTR>(system),L"系统监测");
        item(appearance,QualityPixel,L"像素渲染",!settings.hd);item(appearance,QualityHD,L"高清渲染",settings.hd);
        AppendMenuW(appearance,MF_SEPARATOR,0,nullptr);
        item(appearance,ViewFront,L"正面视角");item(appearance,ViewThree,L"三维预设视角");
        item(material,MaterialPlastic,L"塑料 · 哑光");item(material,MaterialMetal,L"金属 · 拉丝");
        item(material,MaterialGlass,L"玻璃 · 有色背衬");item(material,MaterialCeramic,L"陶瓷纤维 · 织纹");
        CheckMenuRadioItem(material,MaterialPlastic,MaterialCeramic,MaterialPlastic+settings.material,MF_BYCOMMAND);
        AppendMenuW(appearance,MF_POPUP,reinterpret_cast<UINT_PTR>(material),L"机身材质");
        AppendMenuW(root,MF_POPUP,reinterpret_cast<UINT_PTR>(appearance),L"外观");
        AppendMenuW(root,MF_SEPARATOR,0,nullptr);
        item(root,FloatMode,L"惯性悬浮",settings.floating);item(root,Pause,L"暂停动画",paused);
        item(root,Topmost,L"窗口置顶",settings.topmost);item(root,ClickThrough,L"鼠标穿透",settings.clickThrough);
        if(settings.layerMode==1){CheckMenuItem(root,Topmost,MF_BYCOMMAND|MF_UNCHECKED);EnableMenuItem(root,Topmost,MF_BYCOMMAND|MF_GRAYED);}
        item(root,Show,manuallyHidden?L"显示桌宠":L"隐藏桌宠");item(root,ResetPosition,L"恢复可见位置");
        AppendMenuW(root,MF_SEPARATOR,0,nullptr);item(root,Exit,L"退出");
        return root;
    }
    void menu(POINT at) {
        if(dragging){flushDrag(false);cancelDrag();}
        inMenu=true;stopAnimation();stopMotion();stopPose();setPrecisionTiming(false);
        HMENU root=createMenu();
        SetForegroundWindow(hwnd);
        const UINT selected=static_cast<UINT>(TrackPopupMenuEx(root,TPM_RETURNCMD|TPM_RIGHTBUTTON,at.x,at.y,hwnd,nullptr));
        DestroyMenu(root);PostMessageW(hwnd,WM_NULL,0,0);inMenu=false;
        if(selected)command(selected);else applyPolicy();
        if(selected==OpenSettings)applyPolicy();
    }

    void command(UINT id) {
        switch (id) {
        case appworkspace::Open:openWorkspace();return;
        case appworkspace::Choose:openWorkspace();workspace.choose();return;
        case appworkspace::Launch:openWorkspace();workspace.launch();return;
        case appworkspace::Return:closeWorkspace();return;
        case appworkspace::Detach:workspace.detach();return;
        case appworkspace::Next:workspace.next();return;
        case appworkspace::Fullscreen:workspace.fullscreen();return;
        case appworkspace::Single:case appworkspace::Desktop:settings.appMode=id==appworkspace::Desktop;workspace.configure(settings.appMode,settings.appFps,settings.appResolution);saveSettings();return;
        case OpenSettings:openPreferences();return;
        case ImportExpression:case UseExpression:case BuiltinExpression:case ExpressionContain:case ExpressionCover:case ExpressionDark:case ExpressionLight:configureExpression(id);return;
        case ScreenShortcuts:{POINT at{};GetCursorPos(&at);desktopContext(at);return;}
        case AddScreenFile:case AddScreenFolder:chooseScreenShortcut(id==AddScreenFolder);return;
        case RemoveScreenShortcut:screenDesktop.remove(hwnd,screenDesktop.selected);renderedFace=-1;render(settings.mood,0);return;
        case QualityPixel:case QualityHD:setQuality(id==QualityHD);saveSettings();scheduleIdle();startMotion();
            if(gesturePose || std::abs(poseYaw-targetYaw)>.1 || std::abs(posePitch-targetPitch)>.1 || std::abs(poseRoll-targetRoll)>.1)requestPose(targetYaw,targetPitch,targetRoll);
            return;
        case Show: manuallyHidden = !manuallyHidden; break;
        case MaterialPlastic:case MaterialMetal:case MaterialGlass:case MaterialCeramic:
            settings.material=static_cast<int>(id-MaterialPlastic);renderedFace=-1;render(currentFace,renderedBob);saveSettings();return;
        case Pause: paused = !paused; break;
        case Topmost: if(settings.layerMode==1)return;settings.topmost = !settings.topmost; updateStyles(); break;
        case ClickThrough: settings.clickThrough = !settings.clickThrough; updateStyles(); break;
        case AutoHide: settings.autoHide = !settings.autoHide; break;
        case Economy: settings.economy = !settings.economy; break;
        case FloatMode: settings.floating=!settings.floating;velocityX=velocityY=0;if(!settings.floating)floatRoll=0;break;
        case Frame15:case Frame30:case Frame60:{constexpr int rates[]={15,30,60};const bool active=poseArmed;stopPose();settings.frameRate=rates[id-Frame15];if(active)requestPose(targetYaw,targetPitch,targetRoll);break;}
        case PixelThreshold1:case PixelThreshold2:case PixelThreshold4:{constexpr int thresholds[]={1,2,4};settings.pixelThreshold=thresholds[id-PixelThreshold1];break;}
        case ResetPosition: manuallyHidden = false; resetPosition(); break;
        case SystemPerformance:case SystemDisks:case SystemNetwork:case SystemDiagnostics:systemdesk::open(static_cast<int>(id-SystemPerformance));return;
        case SystemConsole:toggleEmbeddedConsole();return;
        case SizeDesk:settings.size=640;resizeSurface();{RECT r{};GetWindowRect(hwnd,&r);place({r.left,r.top},false);}break;
        case Exit: DestroyWindow(hwnd); return;
        default:
            if (id>=SizeSmall && id<=SizeLarge) {
                constexpr int sizes[] = {160,192,256}; settings.size = sizes[id-SizeSmall]; resizeSurface();
                RECT r{}; GetWindowRect(hwnd,&r); place({r.left,r.top},false);
            } else if (id==ViewFront || id==ViewThree) { settings.view = static_cast<int>(id-ViewFront);settings.yaw=settings.pitch=INT_MIN;resetOrientation();renderedFace = -1; }
            else if (id>=MoodIdle && id<=MoodOff) settings.mood = static_cast<int>(id-MoodIdle);
            break;
        }
        applyPolicy(); saveSettings();
    }

    void beginDrag(bool rotateModel=false) {
        if (settings.clickThrough || dragging || paused || shouldHide()) return;
        pressedScreen=-1;pressedAssistant=false;
        if(!rotateModel && assistantHovered){
            pressedAssistant=true;
        }else if(!rotateModel && embeddedConsole.visible()){
            POINT cursor{};GetCursorPos(&cursor);ScreenToClient(hwnd,&cursor);if(screenHit(cursor,renderedPose)){SetForegroundWindow(hwnd);SetFocus(hwnd);return;}
        }else if(!rotateModel && hoveredScreen>=0){
            pressedScreen=hoveredScreen;
        }
        dragging = true; dragMoved = false; pendingPosition = false;
        updateHover();
        if(pressedAssistant)render(currentFace,renderedBob);
        rotating=rotateModel;rotationOriginYaw=baseYaw;rotationOriginPitch=basePitch;
        stopAnimation();stopMotion();stopPose();velocityX=velocityY=0;floatRoll=0;trajectory.reset();GetCursorPos(&dragOrigin);
        dragScale=static_cast<double>(dpi)/96;shakeUntil=surpriseUntil=0;
        recordDrag(dragOrigin,GetTickCount64());
        RECT r{}; GetWindowRect(hwnd, &r); windowOrigin={r.left,r.top};
        desiredPosition=windowOrigin;
        attitude.yaw.value=std::remainder(poseYaw-baseYaw,360.0);attitude.pitch.value=posePitch-basePitch;attitude.roll.value=poseRoll;
        attitude.grab={std::clamp((dragOrigin.x-r.left-extent*.5)/(extent*.35),-1.0,1.0),std::clamp((dragOrigin.y-r.top-extent*.5)/(extent*.35),-1.0,1.0)};
        gesturePose=!rotateModel;
        SetCapture(hwnd);
        if(gesturePose)requestPose(baseYaw,basePitch,0);
    }
    void moveDrag(bool schedule=true) {
        if (!dragging) return;
        POINT cursor{}; GetCursorPos(&cursor);
        recordDrag(cursor,GetTickCount64());
        const LONG dx=cursor.x-dragOrigin.x, dy=cursor.y-dragOrigin.y;
        if (!dragMoved && abs(dx)<GetSystemMetrics(SM_CXDRAG) && abs(dy)<GetSystemMetrics(SM_CYDRAG)) return;
        dragMoved = true;
        pressedScreen=-1;pressedAssistant=false;
        if(rotating){const double sensitivity=settings.rotationSensitivity/100.0;baseYaw=std::remainder(rotationOriginYaw+dx/dragScale*.5*sensitivity,360.0);basePitch=std::clamp(rotationOriginPitch-dy/dragScale*.25*sensitivity,-12.0,24.0);requestPose(baseYaw,basePitch,0);return;}
        const POINT nextPosition{windowOrigin.x+dx,windowOrigin.y+dy};
        if(nextPosition.x==desiredPosition.x && nextPosition.y==desiredPosition.y)return;
        gesturePose=true;
        if(schedule)requestPose(baseYaw,basePitch,0);
        desiredPosition=nextPosition;
        pendingPosition = true;
    }
    void flushDrag(bool snap) {
        if (pendingPosition) { pendingPosition=false; place(desiredPosition,snap,!snap); }
        else if (snap) { RECT r{};GetWindowRect(hwnd,&r);place({r.left,r.top},true); }
    }
    void cancelDrag() {
        dragging=false;rotating=false;pendingPosition=false;
        if(GetCapture()==hwnd) ReleaseCapture();
    }
    void endDrag() {
        if (!dragging) return;
        moveDrag();
        if(rotating){cancelDrag();applyPolicy();saveSettings();return;}
        const bool clicked = !dragMoved;
        const int screenPage=pressedScreen;pressedScreen=-1;
        const bool assistant=pressedAssistant;pressedAssistant=false;
        const ULONGLONG now=GetTickCount64();
        const auto k=trajectory.measure(static_cast<double>(now)/1000);
        if(!clicked)releaseVelocity(now);
        flushDrag(!settings.floating); cancelDrag(); saveSettings();
        applyPolicy();if(clicked && assistant)toggleEmbeddedConsole();else if(clicked && screenPage>=0)activateDesktop(screenPage);else if(clicked)react(Happy);else if(now<shakeUntil || k.shaking)react(Blink);else if(k.velocity.length()>650)react(Surprise);
    }
};

Pet* gPet{};
std::atomic_bool gEnvironmentQueued{false};
void CALLBACK environmentEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD) {
    if (!gPet || !gPet->hwnd || window==gPet->hwnd) return;
    if (event==EVENT_OBJECT_LOCATIONCHANGE && (object!=OBJID_WINDOW || window!=GetForegroundWindow())) return;
    if (!gEnvironmentQueued.exchange(true)) PostMessageW(gPet->hwnd,kEnvironmentMessage,0,0);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    Pet* pet = reinterpret_cast<Pet*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        pet=static_cast<Pet*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        pet->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(pet));
    }
    if(!pet) return DefWindowProcW(hwnd,message,wp,lp);
    try {
        if(pet->taskbarCreated && message==pet->taskbarCreated) { pet->addTray(); return 0; }
        switch(message) {
        case WM_MOUSEACTIVATE: return pet->embeddedConsole.visible()?MA_ACTIVATE:MA_NOACTIVATE;
        case WM_SETCURSOR:
            if(LOWORD(lp)==HTCLIENT && pet->embeddedConsole.visible()){
                POINT point{};GetCursorPos(&point);ScreenToClient(hwnd,&point);SetCursor(LoadCursorW(nullptr,pet->screenHit(point,pet->renderedPose)?IDC_IBEAM:IDC_ARROW));return TRUE;
            }
            if(LOWORD(lp)==HTCLIENT && pet->hoveredScreen>=0){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
            break;
        case WM_SETFOCUS:pet->terminalFocus(true);return 0;
        case WM_KILLFOCUS:pet->terminalFocus(false);return 0;
        case WM_ERASEBKGND: return 1;
        case WM_WINDOWPOSCHANGED:
            if(!pet->hidden && IsWindowVisible(hwnd))pet->syncTopmost();
            return DefWindowProcW(hwnd,message,wp,lp);
        case WM_PRINTCLIENT: if(pet->memoryDC)BitBlt(reinterpret_cast<HDC>(wp),0,0,pet->extent,pet->extent,pet->memoryDC,0,0,SRCCOPY);return 0;
        case WM_PAINT: { PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);return 0; }
        case WM_NCHITTEST: {
            if(pet->settings.clickThrough) return HTTRANSPARENT;
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&point);
            if(!pet->dib || point.x<0 || point.y<0 || point.x>=pet->extent || point.y>=pet->extent) return HTTRANSPARENT;
            return (pet->dib[point.y*pet->extent+point.x]>>24)>20 ? HTCLIENT : HTTRANSPARENT;
        }
        case WM_LBUTTONDOWN: if(pet->workspaceMouse(message,wp,lp))return 0;pet->beginDrag((GetKeyState(VK_MENU)&0x8000)!=0);return 0;
        case WM_MBUTTONDOWN: pet->beginDrag(true);return 0;
        case WM_MBUTTONUP: pet->endDrag();return 0;
        case WM_MOUSEWHEEL: if(pet->workspaceMouse(message,wp,lp))return 0;pet->zoomWheel(GET_WHEEL_DELTA_WPARAM(wp));return 0;
        case WM_MOUSEHWHEEL:if(pet->workspaceMouse(message,wp,lp))return 0;break;
        case WM_MOUSEMOVE: if(pet->workspaceMouse(message,wp,lp))return 0;pet->mouseMove();return 0;
        case WM_MOUSELEAVE: pet->trackingMouse=false;pet->updateHover();return 0;
        case WM_LBUTTONUP: if(pet->workspaceMouse(message,wp,lp))return 0;pet->endDrag();return 0;
        case WM_LBUTTONDBLCLK: if(pet->workspaceMouse(message,wp,lp))return 0;pet->cancelDrag();pet->applyPolicy();if(!pet->screenHovered)pet->react(Love);return 0;
        case WM_CAPTURECHANGED: case WM_CANCELMODE:
            if(pet->appPointerDown){pet->workspace.mouse(WM_LBUTTONUP,0,pet->lastAppPoint.x,pet->lastAppPoint.y);pet->appPointerDown=false;}
            if(pet->dragging) {pet->cancelDrag();pet->applyPolicy();}
            return message==WM_CANCELMODE?DefWindowProcW(hwnd,message,wp,lp):0;
        case WM_RBUTTONDOWN:case WM_RBUTTONUP:if(!(GetKeyState(VK_SHIFT)&0x8000) && pet->workspaceMouse(message,wp,lp))return 0;break;
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(point.x==-1 && point.y==-1)GetCursorPos(&point);
            POINT client=point;ScreenToClient(hwnd,&client);
            if(!pet->workspace.active() && !pet->embeddedConsole.visible() && pet->screenHit(client,pet->renderedPose)){
                int index=pet->hoveredScreen;if(pet->model.ready()){const auto hit=pet->model.pick(client.x,client.y,pet->extent);index=pet->screenDesktop.pick(hit.u,hit.v);}
                pet->desktopContext(point,index);
            }else pet->menu(point);return 0;
        }
        case WM_KEYUP:if(pet->workspace.key(message,wp,lp))return 0;break;
        case WM_KEYDOWN:
            if(pet->workspace.key(message,wp,lp))return 0;
            if(pet->embeddedConsole.visible()){
                if(wp==VK_PROCESSKEY || wp==VK_PACKET)return DefWindowProcW(hwnd,message,wp,lp);
                if(pet->embeddedConsole.interactive()){
                    if(wp==VK_ESCAPE || wp==VK_TAB || wp==VK_LEFT || wp==VK_RIGHT || wp==VK_UP || wp==VK_DOWN || wp==VK_HOME || wp==VK_END || wp==VK_DELETE || wp==VK_PRIOR || wp==VK_NEXT)pet->embeddedConsole.key(static_cast<UINT>(wp));
                    return 0;
                }
                if(wp==L'C' && (GetKeyState(VK_CONTROL)&0x8000))pet->embeddedConsole.stop();
                else if(wp==VK_TAB)pet->embeddedConsole.cycleMode((GetKeyState(VK_SHIFT)&0x8000)?-1:1);
                else if(wp==L'V' && (GetKeyState(VK_CONTROL)&0x8000))pet->embeddedConsole.paste();
                else if(wp==VK_LEFT || wp==VK_RIGHT || wp==VK_HOME || wp==VK_END || wp==VK_UP || wp==VK_DOWN || wp==VK_DELETE || wp==VK_PRIOR || wp==VK_NEXT)pet->embeddedConsole.key(static_cast<UINT>(wp));
                else return DefWindowProcW(hwnd,message,wp,lp);
                return 0;
            }
            break;
        case WM_IME_CHAR:
            if(pet->workspace.key(WM_CHAR,wp,lp))return 0;
            if(pet->embeddedConsole.visible()){pet->embeddedConsole.character(static_cast<wchar_t>(wp));return 0;}
            break;
        case WM_IME_STARTCOMPOSITION:
            if(pet->embeddedConsole.visible()){
                HIMC context=ImmGetContext(hwnd);if(context){CANDIDATEFORM form{};form.dwStyle=CFS_CANDIDATEPOS;form.ptCurrentPos={pet->extent/3,pet->extent*2/3};ImmSetCandidateWindow(context,&form);ImmReleaseContext(hwnd,context);}
            }
            break;
        case WM_CHAR:
            if(pet->workspace.key(message,wp,lp))return 0;
            if(pet->embeddedConsole.visible()){
                if(pet->embeddedConsole.interactive() && (wp==VK_ESCAPE || wp==VK_TAB))return 0;
                if(wp==VK_RETURN)pet->embeddedConsole.execute();
                else if(wp==VK_ESCAPE)pet->embeddedConsole.hide();
                else if(wp==VK_BACK)pet->embeddedConsole.backspace();
                else pet->embeddedConsole.character(static_cast<wchar_t>(wp));
                return 0;
            }
            break;
        case kRenderReadyMessage: pet->renderReady();return 0;
        case appworkspace::FrameReady:pet->workspaceTick();return 0;
        case kLayerMessage:
            if(!pet->layerUpdatePending){pet->layerUpdatePending=SetTimer(hwnd,kLayerTimer,80,nullptr)!=0;if(!pet->layerUpdatePending)pet->refreshLayer();}return 0;
        case WM_COMMAND: pet->command(LOWORD(wp));return 0;
        case kTrayMessage:
            if(LOWORD(lp)==WM_CONTEXTMENU || LOWORD(lp)==WM_RBUTTONUP) {POINT p{};GetCursorPos(&p);pet->menu(p);}
            else if(LOWORD(lp)==NIN_SELECT || LOWORD(lp)==NIN_KEYSELECT) {pet->manuallyHidden=false;pet->applyPolicy();}
            return 0;
        case WM_TIMER:
            if(wp==9){pet->workspaceTick();return 0;}
            if(wp==kLayerWatchTimer){if(pet->settings.layerMode==1 && !pet->shouldHide())pet->syncTopmost();return 0;}
            if(wp==kLayerTimer){pet->refreshLayer();return 0;}
            if(wp==kAntennaTimer){pet->antennaTick();return 0;}
            if(wp==kDesktopTimer){pet->desktopTick();return 0;}
            if(wp==kAnimationTimer) pet->tick();
            else if(wp==kMotionTimer)pet->motionTick();
            else if(wp==kPoseTimer)pet->poseTick();
            return 0;
        case kEnvironmentMessage: gEnvironmentQueued=false;pet->updateEnvironment();return 0;
        case systemdesk::kEmbeddedConsoleClosed:pet->embeddedConsoleClosed();return 0;
        case systemdesk::kEmbeddedConsoleUpdated:pet->embeddedConsoleUpdated();return 0;
        case kTestMessage:
            if(wp==80)return reinterpret_cast<LRESULT>(pet->workspace.host());
            if(wp==81)return pet->workspace.attach(reinterpret_cast<HWND>(lp));
            if(wp==82)return pet->workspace.count();
            if(wp==83)return static_cast<LRESULT>(pet->workspace.frames());
            if(wp==0)return static_cast<LRESULT>(pet->draws);
            if(wp==1)return static_cast<LRESULT>(pet->timerWakes);
            if(wp==2)return (pet->hidden ? 1 : 0)|(pet->animationArmed ? 2 : 0)|(pet->paused ? 4 : 0)|(pet->motionArmed ? 8 : 0)|(pet->poseArmed ? 16 : 0);
            if(wp==3)return static_cast<LRESULT>(pet->positionUpdates);
            if(wp==4)return (pet->settings.floating ? 1 : 0)|(pet->settings.economy ? 2 : 0);
            if(wp==5)return pet->renderedPose;
            if(wp==6)return pet->renderedRoll;
            if(wp==7)return static_cast<LRESULT>(std::lround(pet->trajectory.measure(static_cast<double>(GetTickCount64())/1000).velocity.length()));
            if(wp==8)return pet->shakeUntil>GetTickCount64();
            if(wp==9)return pet->renderedFace;
            if(wp==10)return static_cast<LRESULT>(std::lround(pet->attitude.yaw.velocity));
            if(wp==11)return static_cast<LRESULT>(pet->compositions);
            if(wp==12)return static_cast<LRESULT>(pet->pixelMapping.builds);
            if(wp==13)return static_cast<LRESULT>(pet->poseDecodes);
            if(wp==14)return pet->screenHovered;
            if(wp==15)return pet->settings.hd;
            if(wp==16)return pet->hoveredScreen;
            if(wp==17)return pet->settings.frameRate;
            if(wp==18)return pet->settings.pixelThreshold;
            if(wp==19)return static_cast<LRESULT>(std::lround(pet->velocityX));
            if(wp==20)return static_cast<LRESULT>(std::lround(pet->velocityY));
            if(wp==21)return static_cast<LRESULT>(std::lround(pet->motionX));
            if(wp==22)return static_cast<LRESULT>(std::lround(pet->motionY));
            if(wp==23)return static_cast<LRESULT>(std::lround(pet->floatBlend*1000));
            if(wp==24)return pet->precisionTiming;
            if(wp==25)return pet->embeddedConsole.visible();
            if(wp==26)return pet->embeddedConsole.busy();
            if(wp==27)return pet->assistantHovered;
            if(wp==28)return pet->embeddedConsole.outputContains(L"PICO_TV_CMD_OK");
            if(wp==29)return pet->embeddedConsole.outputContains(L"PICO_TV_CODEX_OK");
            if(wp==30)return pet->embeddedConsole.state().mode;
            if(wp==31)return pet->embeddedConsole.outputContains(L"\r\nPICO_SESSION_42\r\n");
            if(wp==32)return pet->embeddedConsole.outputContains(L"\r\nPICO_EDIT_123\r\n");
            if(wp==33)return pet->embeddedConsole.outputContains(L"\r\nPICO_STREAM_READY\r\n");
            if(wp==34)return pet->embeddedConsole.interactive();
            if(wp==35)return pet->model.ready();
            if(wp==36)return static_cast<LRESULT>(pet->frameTotalMs*1000);
            if(wp==37)return static_cast<LRESULT>(pet->frameMaxMs*1000);
            if(wp==38)return pet->lastButton;
            if(wp==39)return static_cast<LRESULT>(pet->poseYaw*1000);
            if(wp==46)return pet->settings.material;
            if(wp==47)return static_cast<LRESULT>(pet->model.testedTriangles());
            if(wp==48)return pet->model.uniqueVertices();
            if(wp==49){pet->frameGapCount=0;pet->lastFrameSeconds=0;return 0;}
            if(wp==50)return static_cast<LRESULT>(pet->frameSubmitMs*1000);
            if(wp==51)return static_cast<LRESULT>(pet->frameReadbackMs*1000);
            if(wp==52)return static_cast<LRESULT>(pet->frameCopyMs*1000);
            if(wp==53)return static_cast<LRESULT>(pet->framePresentMs*1000);
            if(wp==54 || wp==55){auto gaps=pet->frameGaps;const size_t count=std::min(gaps.size(),pet->frameGapCount);if(!count)return 0;std::sort(gaps.begin(),gaps.begin()+count);return static_cast<LRESULT>(gaps[wp==54?static_cast<size_t>((count-1)*.95):count-1]*1000);}
            if(wp==56)return static_cast<LRESULT>(pet->frameGpuLatencyMs*1000);
            if(wp==57)return pet->model.asyncReady();
            if(wp==58)return pet->model.pending();
            if(wp==59)return static_cast<LRESULT>(pet->lastLeftBend*1000);
            if(wp==60)return static_cast<LRESULT>(pet->lastRightBend*1000);
            if(wp==61)return pet->antennaArmed;
            if(wp==62)return pet->settings.layerMode;
            if(wp==63)return reinterpret_cast<LRESULT>(pet->layer.target());
            if(wp==64)return pet->renderedFace;
            if(wp==65)return static_cast<LRESULT>(pet->desktopExits);
            if(wp==40){size_t count=0;for(int i=0;i<pet->extent*pet->extent;++i)if(pet->dib[i]>>24)++count;return static_cast<LRESULT>(count);}
            if(wp==41){wchar_t path[MAX_PATH]{};GetTempPathW(MAX_PATH,path);std::ofstream file(std::filesystem::path(path)/L"pico-render.bmp",std::ios::binary);BITMAPFILEHEADER head{};BITMAPINFOHEADER info{};head.bfType=0x4d42;head.bfOffBits=sizeof(head)+sizeof(info);head.bfSize=head.bfOffBits+pet->extent*pet->extent*4;info.biSize=sizeof(info);info.biWidth=pet->extent;info.biHeight=-pet->extent;info.biPlanes=1;info.biBitCount=32;file.write(reinterpret_cast<char*>(&head),sizeof(head));file.write(reinterpret_cast<char*>(&info),sizeof(info));file.write(reinterpret_cast<char*>(pet->dib),static_cast<std::streamsize>(pet->extent)*pet->extent*4);return file.good();}
            if(wp==42){const auto state=pet->embeddedConsole.state();wchar_t path[MAX_PATH]{};GetTempPathW(MAX_PATH,path);std::ofstream file(std::filesystem::path(path)/L"pico-cells.txt");for(size_t i=0;i<state.cells.size();++i)if(state.cells[i].Char.UnicodeChar>127)file<<i<<" "<<static_cast<unsigned>(state.cells[i].Char.UnicodeChar)<<" "<<state.cells[i].Attributes<<"\n";return 1;}
            if(wp==43)return pet->embeddedConsole.outputContains(L"你好，电视助手。中文输入测试");
            if(wp==44)return pet->terminalFocused && pet->embeddedConsole.visible() && !IsRectEmpty(&pet->terminalCaret);
            if(wp==45)return MAKELONG(pet->terminalCaret.left,pet->terminalCaret.top);
            return 0;
        case WM_WTSSESSION_CHANGE:
            if(wp==WTS_SESSION_LOCK)pet->locked=true;
            else if(wp==WTS_SESSION_UNLOCK)pet->locked=false;
            pet->applyPolicy();return 0;
        case WM_POWERBROADCAST:
            if(wp==PBT_APMSUSPEND)pet->suspended=true;
            else if(wp==PBT_APMRESUMEAUTOMATIC || wp==PBT_APMRESUMESUSPEND)pet->suspended=false;
            else if(wp==PBT_POWERSETTINGCHANGE) {
                const auto* power=reinterpret_cast<POWERBROADCAST_SETTING*>(lp);
                if(power && power->DataLength==sizeof(DWORD)) {
                    DWORD value{};memcpy(&value,power->Data,sizeof(value));
                    if(IsEqualGUID(power->PowerSetting,GUID_CONSOLE_DISPLAY_STATE))pet->displayOff=value==0;
                    else if(IsEqualGUID(power->PowerSetting,GUID_POWER_SAVING_STATUS))pet->powerSaving=value!=0;
                }
            }
            pet->applyPolicy();return TRUE;
        case WM_DPICHANGED: {
            const auto* suggested=reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            pet->resizeSurface();RECT r{};GetWindowRect(hwnd,&r);pet->place({r.left,r.top},false);return 0;
        }
        case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: {
            RECT r{};GetWindowRect(hwnd,&r);pet->place({r.left,r.top},false);pet->updateEnvironment();return 0;
        }
        case WM_QUERYENDSESSION: pet->saveSettings();return TRUE;
        case WM_CLOSE: DestroyWindow(hwnd);return 0;
        case WM_DESTROY:
            pet->layer.stop();KillTimer(hwnd,kLayerTimer);KillTimer(hwnd,kLayerWatchTimer);
            pet->workspace.close();KillTimer(hwnd,9);pet->embeddedConsole.shutdown();
            systemdesk::shutdown();
            pet->saveSettings();pet->stopAnimation();pet->stopMotion();pet->stopPose();pet->stopAntennas();pet->cancelDrag();
            if(pet->trayAdded)Shell_NotifyIconW(NIM_DELETE,&pet->tray);
            if(pet->displayNotification)UnregisterPowerSettingNotification(pet->displayNotification);
            if(pet->saverNotification)UnregisterPowerSettingNotification(pet->saverNotification);
            WTSUnRegisterSessionNotification(hwnd);PostQuitMessage(0);return 0;
        }
    } catch(const std::exception&) {
        MessageBoxW(hwnd,L"PICO 无法更新窗口，请重新启动。",L"PICO",MB_OK|MB_ICONERROR);
        DestroyWindow(hwnd);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}

bool windows11() {
    struct Version { ULONG size,major,minor,build,platform; WCHAR servicePack[128]; } version{};
    version.size=sizeof(version);
    using Query=LONG(WINAPI*)(Version*);
    auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
    return query && query(&version)==0 && version.major>=10 && version.build>=22000;
}

int selfTest(Pet& pet, const std::filesystem::path& output) {
    std::vector<std::string> failures;
    int checks=0;
    auto check=[&](bool value,const char* label){++checks;if(!value)failures.emplace_back(label);};
    testInteractions(check);
    const auto rendererBenchmark=pixels::testRenderer(check);
    check(pet.extent>=160 && pet.dib,"surface created");
    auto hash=[&](){uint64_t h=1469598103934665603ULL;for(int i=0;i<pet.extent*pet.extent;++i){h^=pet.dib[i];h*=1099511628211ULL;}return h;};
    for(int view=0;view<2;++view) {
        pet.command(ViewFront+view);
        std::vector<uint64_t> hashes;
        for(int face=0;face<kFaceCount;++face){pet.render(face,0);hashes.push_back(hash());}
        std::sort(hashes.begin(),hashes.end());
        check(std::adjacent_find(hashes.begin(),hashes.end())==hashes.end(),"all expression frames distinct");
    }
    pet.command(MoodIdle);
    const auto count=pet.draws;
    pet.render(Idle,0);pet.render(Idle,0);
    check(pet.draws==count,"unchanged frames skip redraw");
    pet.command(Pause);check(!pet.animationArmed,"paused timer stopped");
    pet.command(Pause);check(pet.animationArmed,"resume schedules blink");
    pet.command(Show);check(pet.hidden && !pet.animationArmed,"hidden stops timer");
    pet.command(Show);check(!pet.hidden && pet.animationArmed,"show resumes");
    SendMessageW(pet.hwnd,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
    check(pet.hidden && !pet.animationArmed,"session lock suspends");
    SendMessageW(pet.hwnd,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
    check(!pet.hidden && pet.animationArmed,"session unlock resumes");
    SendMessageW(pet.hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    check(pet.hidden && !pet.animationArmed,"suspend stops timers");
    SendMessageW(pet.hwnd,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
    pet.fullscreen=true;pet.applyPolicy();check(pet.hidden && !pet.animationArmed,"fullscreen hides");
    pet.command(AutoHide);check(!pet.hidden,"fullscreen preference respected");
    pet.command(AutoHide);pet.fullscreen=false;pet.applyPolicy();
    pet.command(ClickThrough);check((GetWindowLongPtrW(pet.hwnd,GWL_EXSTYLE)&WS_EX_TRANSPARENT)!=0,"click through enabled");
    pet.command(ClickThrough);check((GetWindowLongPtrW(pet.hwnd,GWL_EXSTYLE)&WS_EX_TRANSPARENT)==0,"click through recovered");
    pet.command(Frame15);check(pet.settings.frameRate==15 && pet.frameDelay()==67,"15 FPS uses a bounded frame interval");
    pet.command(Frame30);check(pet.settings.frameRate==30 && pet.frameDelay()==34,"30 FPS uses a bounded frame interval");
    pet.command(Frame60);check(pet.settings.frameRate==60 && pet.frameDelay()==17,"60 FPS uses a bounded frame interval");
    pet.command(Frame30);
    pet.command(PixelThreshold4);check(pet.settings.pixelThreshold==4,"pixel movement threshold can be increased");
    RECT thresholdWork=pet.workArea({0,0});
    pet.place({(thresholdWork.left+thresholdWork.right-pet.extent)/2,(thresholdWork.top+thresholdWork.bottom-pet.extent)/2},false);
    RECT thresholdRect{};GetWindowRect(pet.hwnd,&thresholdRect);
    const int threshold=std::max(1,MulDiv(4,static_cast<int>(pet.dpi),96));
    pet.place({thresholdRect.left+threshold-1,thresholdRect.top},false,true);RECT thresholdAfter{};GetWindowRect(pet.hwnd,&thresholdAfter);
    check(thresholdAfter.left==thresholdRect.left,"sub-threshold window movement is coalesced");
    pet.place({thresholdRect.left+threshold,thresholdRect.top},false,true);GetWindowRect(pet.hwnd,&thresholdAfter);
    check(thresholdAfter.left==thresholdRect.left+threshold,"threshold-sized movement is committed");
    pet.command(PixelThreshold1);
    const UINT thresholdDpi=pet.dpi;pet.dpi=144;
    check(pet.movementPixelThreshold()==1,"fine movement remains one physical pixel at high DPI");
    pet.dpi=thresholdDpi;
    for(UINT id=SizeSmall;id<=SizeLarge;++id){pet.command(id);check(pet.extent==MulDiv(pet.settings.size,static_cast<int>(pet.dpi),96),"DPI dimensions");}
    pet.place({-100000,-100000},true);
    RECT rect{};GetWindowRect(pet.hwnd,&rect);RECT work=pet.workArea({rect.left,rect.top});
    const auto recovered=pet.visibleBounds();
    check(rect.left+recovered.left>=work.left && rect.top+recovered.top>=work.top,"off-screen recovery keeps visible model inside work area");
    const RECT tightWork{0,0,pet.extent+8,pet.extent+8};const auto tightRange=pet.movementBounds(tightWork);
    check(tightRange.bottom-tightRange.top>8 && tightRange.right-tightRange.left>8,"transparent padding no longer consumes movement range when window fills display");
    const RECT negativeWork{-1920,-1080,0,0};const auto negativeRange=pet.movementBounds(negativeWork);
    check(negativeRange.left+recovered.left==-1920 && negativeRange.top+recovered.top==-1080 && negativeRange.right+recovered.right==0 && negativeRange.bottom+recovered.bottom==0,"visible bounds preserve negative monitor coordinates");
    pet.settings.mood=Idle;pet.paused=false;pet.applyPolicy();
    pet.tick();check(pet.renderedFace==Blink && pet.animationArmed,"blink closes");
    pet.tick();check(pet.renderedFace==Idle && pet.animationArmed,"blink opens");
    const DWORD handlesBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<60;++i)pet.command(SizeSmall+static_cast<UINT>(i%3));
    check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==handlesBefore,"resize does not leak GDI objects");
    pet.command(FloatMode);check(pet.motionArmed,"float mode starts motion timer");
    check(!pet.poseArmed && !pet.precisionTiming,"idle floating does not keep posture or precision timers active");
    pet.floatRoll=.7;pet.poseRoll=pet.targetRoll=0;pet.gesturePose=false;pet.applyPolicy();
    check(!pet.poseArmed,"idle float roll stays separate from interaction posture recovery");
    pet.trajectory.reset();pet.recordDrag({100,100},1000);pet.recordDrag({150,75},1050);pet.recordDrag({200,50},1100);
    pet.releaseVelocity(1100);
    check(pet.velocityX>250 && pet.velocityX<350 && pet.velocityY< -100,"throw follows drag with gentle gain");
    pet.stopMotion();pet.scheduleMotion();
    check(pet.precisionTiming,"coasting temporarily enables precise frame timing");
    pet.trajectory.reset();pet.recordDrag({0,0},1000);pet.recordDrag({10000,0},1010);pet.releaseVelocity(1010);
    check(std::hypot(pet.velocityX,pet.velocityY)<=kThrowSpeedLimit*pet.dpi/96+.01,"fast throws have a safe speed cap");
    pet.releaseVelocity(1300);check(pet.velocityX==0 && pet.velocityY==0,"held drag releases without stale velocity");
    pet.resetPosition();GetWindowRect(pet.hwnd,&rect);work=pet.workArea({rect.left,rect.top});
    pet.motionX=(work.left+work.right-pet.extent)/2.0;pet.motionY=(work.top+work.bottom-pet.extent)/2.0;
    const double startX=pet.motionX;pet.velocityX=300;pet.velocityY=0;pet.advanceMotion(.1);
    check(pet.motionX>startX+20 && pet.velocityX<300,"inertial motion advances and damps");
    const auto floatLimits=pet.movementBounds(work);
    pet.motionX=floatLimits.right-1;pet.velocityX=600;pet.advanceMotion(.1);
    check(pet.velocityX<0 && pet.motionX<=floatLimits.right,"edge collision rebounds at visible model bounds");
    pet.motionX=(work.left+work.right-pet.extent)/2.0;pet.motionY=(work.top+work.bottom-pet.extent)/2.0;pet.floatBlend=0;pet.velocityX=30.0*pet.dpi/96;pet.velocityY=0;
    pet.advanceMotion(.1);check(pet.floatBlend>0 && pet.floatBlend<1,"coasting gradually blends into organic idle drift");
    pet.motionX=floatLimits.right;pet.motionY=floatLimits.bottom;pet.floatBlend=1;pet.velocityX=pet.velocityY=0;pet.advanceMotion(.1);
    check(pet.motionX<floatLimits.right && pet.motionY<floatLimits.bottom,"idle drift keeps a small motion margin at visible desktop edges");
    pet.paused=true;pet.applyPolicy();check(!pet.motionArmed && !pet.animationArmed && !pet.precisionTiming,"pause stops float, blink, and precise timing");
    pet.paused=false;pet.applyPolicy();pet.manuallyHidden=true;pet.applyPolicy();
    check(!pet.motionArmed && !pet.animationArmed,"hidden float has no timers");
    pet.manuallyHidden=false;pet.applyPolicy();pet.command(FloatMode);check(!pet.motionArmed,"exit float stops motion timer");
    pet.stopPose();pet.resetOrientation();pet.render(Idle,0);const auto straight=hash();
    const auto& screenLayout=kLayouts[pet.renderedPose];
    const POINT screenPoint{MulDiv(screenLayout.x+screenLayout.width/2,pet.extent,kSpriteSize),MulDiv(screenLayout.y+screenLayout.height/2,pet.extent,kSpriteSize)};
    check(pet.screenHit(screenPoint,pet.renderedPose),"visible CRT glass responds to screen hover");
    check(!pet.screenHit({0,0},pet.renderedPose) && !pet.screenHit({pet.extent-1,pet.extent/2},pet.renderedPose),"transparent margin and outer casing do not trigger desktop");
    POINT assistantPoint{};bool assistantFound=false;
    for(int y=0;y<pet.spriteSize() && !assistantFound;++y)for(int x=0;x<pet.spriteSize();++x)if(pet.assistantSourcePixel(x,y,pet.renderedPose)){assistantPoint={MulDiv(x,pet.extent,pet.spriteSize()),MulDiv(y,pet.extent,pet.spriteSize())};assistantFound=true;break;}
    check(assistantFound && pet.assistantButtonHit(assistantPoint,pet.renderedPose),"red casing button opens the embedded command assistant");
    pet.render(Computer,0);check(straight!=hash(),"computer desktop has a distinct rendered screen");
    std::vector<uint64_t> desktopHashes;
    for(int face=ComputerPerformance;face<=ComputerDiagnostics;++face){pet.render(face,0);desktopHashes.push_back(hash());}
    std::sort(desktopHashes.begin(),desktopHashes.end());
    check(kFaceCount==12 && std::adjacent_find(desktopHashes.begin(),desktopHashes.end())==desktopHashes.end(),"four system buttons have distinct selected frames");
    for(int slot=0;slot<8;++slot){const auto r=screendesktop::tile(slot);check(screendesktop::hit((r.left+r.right)/1600.f,(r.top+r.bottom)/1000.f,0,4)==slot,"desktop icon bounds and input coordinates agree");}
    check(screendesktop::hit(.5f,.1f,0,0)==-2 && screendesktop::hit(.5f,.82f,0,0)==-2,"desktop title and empty background never launch modules");
    check(screendesktop::hit(.63f,.93f,0,0)==-2 && screendesktop::hit(.63f,.93f,0,5)==screendesktop::Next,"desktop next page only responds when another page exists");
    check(screendesktop::hit(143.f/800,465.f/500,0,0)==screendesktop::Settings,"settings dock uses its own hit target without changing shortcut indices");
    {
        const HMENU menu=pet.createMenu();
        std::function<bool(HMENU,UINT)> contains=[&](HMENU current,UINT id){for(int i=0;i<GetMenuItemCount(current);++i){if(GetMenuItemID(current,i)==id)return true;if(HMENU sub=GetSubMenu(current,i);sub && contains(sub,id))return true;}return false;};
        check(contains(menu,OpenSettings) && contains(menu,QualityPixel) && contains(menu,QualityHD) && contains(menu,MaterialGlass) && contains(menu,ImportExpression) && contains(menu,SystemNetwork),"shared screen and casing menu includes appearance settings materials expressions and modules");DestroyMenu(menu);
        const int originalSize=pet.settings.size;pet.render(Computer,0);const auto beforeDraws=pet.draws;pet.settings.size=originalSize==256?192:256;pet.resizeSurface();
        check(pet.draws==beforeDraws+1 && pet.renderedFace>=Computer && !pet.model.pending(),"resize presents one complete desktop frame without an intermediate expression or async blank");
        pet.settings.size=originalSize;pet.resizeSurface();
    }
    {
        const auto directory=std::filesystem::temp_directory_path()/(L"PicoDesktopTests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(directory);
        const auto file=directory/L"中文 & desktop.txt";{std::ofstream fixture(file);fixture<<"desktop shortcut fixture";}
        screendesktop::Desktop first;first.load((directory/L"settings.ini").wstring());
        check(first.addPath(pet.hwnd,file.wstring()) && first.count()==1,"shortcut saves a Unicode path with spaces and ampersand");
        check(first.addPath(pet.hwnd,file.wstring()) && first.count()==1,"adding the same shortcut selects it without duplication");
        screendesktop::Desktop second;second.load((directory/L"settings.ini").wstring());check(second.count()==1 && second.name(4)==file.filename().wstring(),"shortcut name and path survive reload");
        second.remove(pet.hwnd,4);first.load((directory/L"settings.ini").wstring());check(first.count()==0 && std::filesystem::exists(file),"removing a shortcut persists and never deletes its target");
        std::filesystem::remove(directory/L"shortcuts.ini");std::filesystem::remove(file);std::filesystem::remove(directory);
    }
    pet.render(Idle,0);check(straight==hash(),"leaving desktop restores original face pixels");
    {
        const auto directory=std::filesystem::temp_directory_path()/(L"PicoImageTests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(directory);
        const auto input=directory/L"透明立绘.png";
        expression::Pixels sample{100,200,std::vector<uint32_t>(100*200,0xffff0000u)};sample.data[0]=0;expression::encode(pet.imageFactory.Get(),sample,input.wstring());
        pet.customExpression.load(pet.imageFactory.Get(),directory);pet.customExpression.import(input.wstring());
        check(pet.customExpression.active(),"import enables a custom expression");
        auto contained=pet.customExpression.texture();check(contained.size()==800*500 && contained[250*800+400]==0xffff0000 && contained[250*800+10]==0xff182326,"portrait containment preserves aspect ratio and background");
        check(contained[275]!=0xff000000,"transparent pixels composite over the LCD instead of punching a hole");
        pet.customExpression.configure(true,true,false);const auto covered=pet.customExpression.texture();check(covered[250*800+10]==0xffff0000,"cover crops a portrait before scaling to fill the LCD");
        pet.customExpression.configure(true,false,true);check(pet.customExpression.texture()[0]==0xffe5eef0,"transparent and letterboxed regions use the selected background");
        pet.terminalDirty=true;pet.renderedFace=-1;pet.render(Idle,0);const auto customHash=hash();check(customHash!=straight,"custom image is rendered in the model screen");
        pet.render(Computer,0);check(hash()!=customHash,"desktop takes precedence over a custom expression");pet.render(Idle,0);check(hash()==customHash,"leaving desktop restores the custom image texture");
        std::filesystem::remove(input);pet.customExpression.load(pet.imageFactory.Get(),directory);check(pet.customExpression.active() && pet.customExpression.light,"import survives reload after its original file is removed");
        bool rejected=false;try{pet.customExpression.import(input.wstring());}catch(const std::exception&){rejected=true;}check(rejected && pet.customExpression.active(),"failed import keeps the current expression");
        pet.customExpression.configure(false,false,true);pet.terminalDirty=true;pet.renderedFace=-1;pet.render(Idle,0);check(hash()==straight,"restoring built-in expressions recovers original pixels");
        pet.customExpression.load(pet.imageFactory.Get(),directory);check(!pet.customExpression.active() && pet.customExpression.available(),"built-in choice persists while retaining the imported image");
        std::filesystem::remove(directory/L"expression.png");std::filesystem::remove(directory/L"expression.ini");std::filesystem::remove(directory);
    }
    pet.poseRoll=6;pet.render(Idle,0);check(straight!=hash(),"drag tilt changes silhouette");
    const auto composedBefore=pet.compositions;
    pet.poseRoll=6.5;pet.render(Idle,0);
    check(pet.compositions==composedBefore,"roll-only change reuses composed face and body");
    const auto mapsBefore=pet.pixelMapping.builds;
    pet.render(Happy,0);
    check(pet.pixelMapping.builds==mapsBefore,"expression change reuses transform mapping");
    pet.poseRoll=0;pet.poseYaw=180;pet.render(Idle,0);check(straight!=hash(),"rear rotation reveals model back");
    check(!pet.screenHit({pet.extent/2,pet.extent/2},pet.renderedPose),"rear view cannot trigger screen hover");
    for(int i=0;i<kPoseCount;++i){pet.poseYaw=(i%kYawCount)*10;pet.posePitch=-12+(i/kYawCount)*12;pet.render(Idle,0);}
    check(std::count_if(pet.poseCache.begin(),pet.poseCache.end(),[](const auto& p){return p.id>=0;})==6,"pose cache stays bounded across full rotation");
    pet.resetOrientation();pet.attitude.step({{250,-100},{},0,false},true,.1,pet.basePitch);
    check(pet.attitude.yaw.value>0 && pet.attitude.pitch.value>0 && pet.attitude.roll.value<0,"posture follows throw direction");
    pet.paused=true;pet.applyPolicy();check(!pet.poseArmed && !pet.animationArmed && !pet.motionArmed,"pause stops all posture and movement timers");
    pet.paused=false;pet.manuallyHidden=true;pet.applyPolicy();check(!pet.poseArmed,"hidden stops pose recovery timer");
    pet.manuallyHidden=false;pet.applyPolicy();
    pet.attitude.yaw.velocity=180;pet.poseYaw=pet.baseYaw+6;
    pet.beginDrag();
    check(pet.dragging && pet.gesturePose && pet.poseArmed,"regrab keeps sampling while held still");
    pet.pendingPosition=true;
    pet.paused=true;pet.applyPolicy();
    check(!pet.dragging && !pet.pendingPosition && !pet.poseArmed,"pause during drag cancels capture and drag timer");
    pet.paused=false;pet.resetOrientation();pet.applyPolicy();
    pet.beginDrag();pet.poseTime=GetTickCount64()-16;pet.poseTick();
    check(!pet.poseArmed && pet.dragging,"stationary settled grab stops continuous timer");
    pet.cancelDrag();pet.applyPolicy();
    pet.command(SizeNormal);
    const double wheelYaw=pet.poseYaw,wheelPitch=pet.posePitch;
    pet.zoomWheel(WHEEL_DELTA/2);check(pet.settings.size==192,"partial wheel input accumulates without premature resize");
    pet.zoomWheel(WHEEL_DELTA/2);check(pet.settings.size==224 && pet.extent==MulDiv(224,static_cast<int>(pet.dpi),96),"wheel up enlarges pet with DPI scaling");
    check(pet.poseYaw==wheelYaw && pet.posePitch==wheelPitch,"wheel zoom preserves model orientation");
    pet.zoomWheel(-WHEEL_DELTA);check(pet.settings.size==192,"wheel down shrinks pet");
    pet.zoomWheel(40*WHEEL_DELTA);check(pet.settings.size==1024,"wheel enlargement has a bounded maximum");
    pet.zoomWheel(-40*WHEEL_DELTA);check(pet.settings.size==160,"wheel reduction has a bounded minimum");
    pet.resetOrientation();pet.render(Happy,1);
    const auto pixelHash=hash();
    const double qualityYaw=pet.poseYaw,qualityPitch=pet.posePitch;
    const int qualityExtent=pet.extent;
    RECT qualityBefore{};GetWindowRect(pet.hwnd,&qualityBefore);
    pet.command(QualityHD);
    RECT qualityAfter{};GetWindowRect(pet.hwnd,&qualityAfter);
    check(pet.settings.hd && pet.spriteSize()==576 && pixelHash!=hash(),"HD selects higher detail assets and changes rendered pixels");
    check(pet.extent==qualityExtent && EqualRect(&qualityBefore,&qualityAfter) && pet.poseYaw==qualityYaw && pet.posePitch==qualityPitch && pet.currentFace==Happy && pet.renderedBob==1,"quality switch preserves size position pose expression and bob");
    std::vector<uint64_t> hdDesktopHashes;
    for(int face=ComputerPerformance;face<=ComputerDiagnostics;++face){pet.render(face,0);hdDesktopHashes.push_back(hash());}
    std::sort(hdDesktopHashes.begin(),hdDesktopHashes.end());
    check(std::adjacent_find(hdDesktopHashes.begin(),hdDesktopHashes.end())==hdDesktopHashes.end(),"HD has four distinct system button selections");
    pet.render(Happy,1);
    pet.command(QualityPixel);
    check(hash()==pixelHash && pet.composition.size()==192*192,"returning to pixel mode restores exact output and releases HD composition");
    pet.command(QualityHD);pet.render(Idle,0);
    const auto hdDraws=pet.draws;pet.render(Idle,0);
    check(pet.draws==hdDraws,"unchanged HD frames skip redraw");
    const auto& hdGlass=pet.layoutFor(pet.renderedPose);
    check(pet.screenHit({MulDiv(hdGlass.x+hdGlass.width/2,pet.extent,pet.spriteSize()),MulDiv(hdGlass.y+hdGlass.height/2,pet.extent,pet.spriteSize())},pet.renderedPose),"HD screen hit follows higher resolution layout");
    for(int i=0;i<kPoseCount;++i){pet.poseYaw=(i%kYawCount)*10;pet.posePitch=-12+(i/kYawCount)*12;pet.render(Idle,0);}
    check(std::count_if(pet.poseCache.begin(),pet.poseCache.end(),[](const auto& p){return p.id>=0;})==3,"HD cache stays bounded to three poses across full rotation");
    pet.command(QualityPixel);
    check(std::count_if(pet.poseCache.begin(),pet.poseCache.end(),[](const auto& p){return p.id>=0;})==1,"quality toggle discards previous asset cache");
    {
        realtime::Model gpu;const auto atlas=loadImage(pet.imageFactory.Get(),4002),faces=loadImage(pet.imageFactory.Get(),4003);
        check(realtime::screenButton(15.f/84,26.f/48)==0 && realtime::screenButton(69.f/84,26.f/48)==3,"screen icon hit areas match texture bounds");
        check(realtime::screenButton(.5f,.1f)==-2 && realtime::screenButton(24.f/84,26.f/48)==-2,"screen header and icon gutters do not activate modules");
        const HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(4001),RT_RCDATA);
        gpu.initialize(LockResource(LoadResource(nullptr,resource)),SizeofResource(nullptr,resource),atlas.pixels.data(),atlas.width,atlas.height,faces.pixels.data(),faces.width,faces.height);
        std::vector<uint32_t> a(384*384),b(a.size());
        gpu.draw(a.data(),384,0,0,0,0,true,Idle,0,false);
        check(std::count_if(a.begin(),a.end(),[](uint32_t c){return (c>>24)>0;})>10000,"D3D model produces visible pixels");
        gpu.draw(b.data(),384,.4,0,0,0,true,Idle,0,false);check(a!=b,"sub-degree yaw produces a new model frame");
        gpu.draw(b.data(),384,0,.4,0,0,true,Idle,0,false);check(a!=b,"sub-degree pitch produces a new model frame");
        gpu.draw(b.data(),384,0,0,0,0,true,Idle,1,false);check(a!=b,"physical button hover changes model pixels");
        const auto hover=b;gpu.draw(b.data(),384,0,0,0,0,true,Idle,2,false);check(hover!=b,"physical button depression changes geometry and color");
        gpu.draw(b.data(),384,0,0,0,0,true,Idle,3,false);check(a!=b,"active button remains visibly lit");
        check(gpu.pick(170,200,384).kind==1,"realtime hit testing finds front glass");
        gpu.draw(b.data(),384,180,0,0,0,true,Idle,0,false);check(gpu.pick(170,200,384).kind!=1,"rear housing occludes screen hit testing");
        std::vector<uint32_t> screenTexture(800*500,0xff143a29u);
        gpu.uploadTerminal(screenTexture.data());gpu.draw(a.data(),384,180,0,0,0,true,Idle,0,true);
        std::fill(screenTexture.begin(),screenTexture.end(),0xffe5ffeeu);gpu.uploadTerminal(screenTexture.data());gpu.draw(b.data(),384,180,0,0,0,true,Idle,0,true);
        check(a==b,"terminal texture cannot bleed through the rear cabinet");
        bool clippedToGlass=true;int samples=0;
        for(double angle:{-65.,-20.,0.,20.,65.}){
            gpu.draw(a.data(),384,angle,12,0,0,true,Idle,0,false);gpu.draw(b.data(),384,angle,12,0,0,true,Idle,0,true);
            for(int y=16;y<368;y+=16)for(int x=16;x<368;x+=16){
                if(a[y*384+x]==b[y*384+x])continue;
                ++samples;const auto hit=gpu.pick(x, y,384);
                if(hit.kind!=1 && gpu.pick(x+1,y,384).kind!=1 && gpu.pick(x,y+1,384).kind!=1)clippedToGlass=false;
            }
        }
        check(samples>50 && clippedToGlass,"terminal changes only visible glass pixels across oblique views");
        for(bool hd:{false,true})for(double angle:{-130.,-65.,0.,20.,130.,180.}){
            gpu.draw(a.data(),384,angle,24,7,1,hd,Idle,0,false);const auto bounds=gpu.visibleBounds(384);bool containsPixels=true;
            for(int y=0;y<384;++y)for(int x=0;x<384;++x)if((a[y*384+x]>>24)>20 && (x<bounds.left || x>=bounds.right || y<bounds.top || y>=bounds.bottom))containsPixels=false;
            check(containsPixels && !IsRectEmpty(&bounds),"projected movement bounds contain opaque pixels in HD and pixel rotated views");
        }
        bool matchingPicks=true;const uint64_t testsBefore=gpu.testedTriangles();int queries=0;
        for(double angle:{-130.,-65.,-20.,0.,20.,65.,130.,180.}){
            gpu.draw(a.data(),384,angle,12,3,1,true,Idle,0,false);
            for(int y=11;y<384;y+=19)for(int x=7;x<384;x+=17){++queries;const auto fast=gpu.pick(x,y,384),reference=gpu.pickReference(x,y,384);
                if(fast.kind!=reference.kind || (fast.kind==1 && (std::abs(fast.u-reference.u)>.001f || std::abs(fast.v-reference.v)>.001f)))matchingPicks=false;}}
        check(matchingPicks,"BVH picking matches full triangle projection across rotated views");
        check(gpu.testedTriangles()-testsBefore<static_cast<uint64_t>(queries)*200,"BVH bounds avoid testing the complete model on pointer movement");
        check(gpu.uniqueVertices()<60000,"indexed model reuses shared vertices without reducing triangle detail");
        gpu.draw(a.data(),384,0,0,0,0,true,Idle,0,false);
        const auto uprightBounds=gpu.visibleBounds(384);
        gpu.draw(b.data(),384,0,0,0,0,true,Idle,0,false,0,false,1.1f,.65f);
        int antennaPixels=0;bool bodyUnchanged=true;
        for(int y=0;y<384;++y)for(int x=0;x<384;++x)if(a[y*384+x]!=b[y*384+x]){++antennaPixels;if(y>=120)bodyUnchanged=false;}
        check(antennaPixels>50 && bodyUnchanged,"antenna hinges deform rods and tips without changing the cabinet or LCD");
        check(gpu.visibleBounds(384).top>uprightBounds.top,"folding antennas lowers the model silhouette");
        const auto compressed=gpu.compressionBounds(384,0,0,0,0);
        const auto noContact=gpu.antennaContact(384,0,0,0,0,0);
        bool progressive=noContact[0]==0 && noContact[1]==0;float previous=0;
        for(LONG top=uprightBounds.top;top<=compressed.top;++top){const auto contact=gpu.antennaContact(384,top,0,0,0,0);
            progressive=progressive && contact[0]>=previous && std::abs(contact[0]-contact[1])<.002f;previous=contact[0];
            gpu.draw(a.data(),384,0,0,0,0,true,Idle,0,false,0,false,contact[0],contact[1]);
            progressive=progressive && gpu.visibleBounds(384).top>=top;}
        check(progressive && previous>.4f && compressed.top>uprightBounds.top+10,"upward travel progressively folds antennas and contains the visible model");
        const auto tiltedContact=gpu.antennaContact(384,compressed.top-12,20,12,7,0);
        check(std::abs(tiltedContact[0]-tiltedContact[1])>.05f,"tilted model solves left and right antenna contact independently");
        bool bentPicks=true,bentBounds=true;
        for(bool hd:{false,true})for(double angle:{-130.,0.,65.,180.}){
            gpu.draw(a.data(),384,angle,24,-7,1,hd,Idle,0,false,1,false,.35f,1.15f);
            const auto bounds=gpu.visibleBounds(384);
            for(int y=0;y<384;++y)for(int x=0;x<384;++x)if((a[y*384+x]>>24)>20 && (x<bounds.left || x>=bounds.right || y<bounds.top || y>=bounds.bottom))bentBounds=false;
            for(int y=35;y<150;y+=5)for(int x=40;x<344;x+=7){const auto fast=gpu.pick(x,y,384),reference=gpu.pickReference(x,y,384);if(fast.kind!=reference.kind || (fast.kind==1 && (std::abs(fast.u-reference.u)>.001f || std::abs(fast.v-reference.v)>.001f)))bentPicks=false;}
        }
        check(bentBounds,"bent antenna bounds contain GPU pixels across quality modes and rotated views");
        check(bentPicks,"inverse hinge ray picking matches deformed triangle projection");
        std::vector<uint32_t> largePixels(1520*1520);bool largeContained=true;
        for(float fold:{0.f,.6f,1.15f}){
            gpu.draw(largePixels.data(),1520,20,12,-7,0,false,Idle,0,false,0,false,fold,fold);
            const auto bounds=gpu.visibleBounds(1520);
            for(int y=0;y<1520;++y)for(int x=0;x<1520;++x)if((largePixels[y*1520+x]>>24)>20 && (x<bounds.left || x>=bounds.right || y<bounds.top || y>=bounds.bottom))largeContained=false;
        }
        check(largeContained,"enlarged pixel-mode antenna bounds include upscaled edge samples");
        if(gpu.enableAsync(pet.hwnd,kRenderReadyMessage)){
            gpu.draw(a.data(),384,20,12,0,0,true,Computer,0,false,1);
            gpu.draw(b.data(),384,180,0,0,0,true,Idle,0,false);
            const auto rear=b;
            check(!gpu.draw(b.data(),384,20,12,0,0,true,Computer,0,false,1,true) && gpu.pending() && b==rear,"asynchronous submission leaves displayed pixels intact until GPU completion");
            check(gpu.pick(170,200,384).kind!=1,"pending GPU frame preserves the displayed pose for picking");
            const double deadline=preciseSeconds()+3;MSG readyMessage{};bool notified=false;
            while(!notified && preciseSeconds()<deadline){notified=PeekMessageW(&readyMessage,pet.hwnd,kRenderReadyMessage,kRenderReadyMessage,PM_REMOVE)!=FALSE;if(!notified)Sleep(1);}
            check(notified,"GPU completion posts a window message without blocking the UI thread");
            gpu.readPixels(b.data(),384);
            check(!gpu.pending() && a==b,"asynchronous readback exactly matches synchronous pixels including the final frame");
            check(gpu.pick(170,200,384).kind==1,"completed GPU frame updates picking with its displayed pose");
            gpu.draw(b.data(),384,30,0,0,0,true,Idle,0,false,0,true);gpu.discardPending();
            check(!gpu.pending(),"pending readback can be drained before resizing or shutting down");
            std::vector<uint32_t> resized(256*256);gpu.draw(resized.data(),256,0,0,0,0,true,Idle,0,false);
            check(std::count_if(resized.begin(),resized.end(),[](uint32_t p){return (p>>24)>0;})>4000,"render target resizing succeeds after cancelling an asynchronous frame");
            gpu.draw(a.data(),384,0,0,0,0,true,Idle,0,false);const auto beforeBend=gpu.visibleBounds(384);
            gpu.draw(b.data(),384,0,0,0,0,true,Idle,0,false,0,true,1.1f,.7f);
            check(gpu.visibleBounds(384).top==beforeBend.top,"pending antenna deformation preserves displayed collision bounds");
            const double bendDeadline=preciseSeconds()+3;
            while(gpu.pending() && preciseSeconds()<bendDeadline){if(!gpu.readPixels(b.data(),384))Sleep(1);}
            check(!gpu.pending() && a!=b && gpu.visibleBounds(384).top>beforeBend.top,"completed antenna frame publishes deformation and bounds together");
            gpu.disableAsync();
        }
        gpu.draw(a.data(),384,20,12,0,0,true,Computer,0,false,0);
        for(int material=1;material<4;++material){
            gpu.draw(b.data(),384,20,12,0,0,true,Computer,0,false,material);
            bool sameSilhouette=true,sameDisplay=true;int changed=0;
            for(int y=0;y<384;++y)for(int x=0;x<384;++x){const size_t i=static_cast<size_t>(y)*384+x;
                if((a[i]>>24)!=(b[i]>>24))sameSilhouette=false;
                if(a[i]!=b[i]){++changed;if(x%8==0 && y%8==0 && gpu.pick(x,y,384).kind==1)sameDisplay=false;}}
            check(changed>1000 && sameSilhouette && sameDisplay,"material changes casing while preserving LCD and geometry");
        }
    }
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    if(output.has_parent_path())std::filesystem::create_directories(output.parent_path());
    std::ofstream file(output);
    file<<"{\n  \"checks\": "<<checks<<",\n  \"workingSetBytes\": "<<memory.WorkingSetSize<<",\n  \"privateBytes\": "<<memory.PrivateUsage
        <<",\n  \"rendererReferenceMs\": "<<rendererBenchmark.referenceMs<<",\n  \"rendererCachedMs\": "<<rendererBenchmark.cachedMs<<",\n  \"failures\": [";
    for(size_t i=0;i<failures.size();++i)file<<(i ? ", " : "")<<'"'<<failures[i]<<'"';
    file<<"]\n}\n";
    return failures.empty()?0:1;
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    if(!windows11()){MessageBoxW(nullptr,L"PICO 仅支持 Windows 11 x64。",L"PICO",MB_OK|MB_ICONINFORMATION);return 1;}
    int argc{};LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>=3 && wcscmp(argv[1],L"--app-host")==0){const std::wstring mapping=argv[2];LocalFree(argv);return appworkspace::runHost(mapping.c_str());}
    const bool testing=argc>=2 && wcscmp(argv[1],L"--self-test")==0;
    const bool systemTesting=argc>=2 && wcscmp(argv[1],L"--system-test")==0;
    std::filesystem::path report=argc>=3 ? argv[2] : L"self-test.json";
    LocalFree(argv);
    if(systemTesting)return systemdesk::selfTest(report);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,testing ? L"Local\\PicoPet.Win11.Tests" : L"Local\\PicoPet.Win11.Instance");
    if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        if(HWND previous=FindWindowW(kClass,nullptr)){PostMessageW(previous,WM_COMMAND,ResetPosition,0);}
        CloseHandle(mutex);return 0;
    }
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com)){CloseHandle(mutex);return 1;}
    SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
    PROCESS_POWER_THROTTLING_STATE throttle{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED,PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
    SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&throttle,sizeof(throttle));
    int result=0;
    HWINEVENTHOOK foregroundHook{},locationHook{};
    try {
        Pet pet;gPet=&pet;pet.testMode=testing;
        pet.initializeAssets();pet.loadSettings();pet.resetOrientation();
        WNDCLASSEXW type{sizeof(type)};type.style=CS_DBLCLKS;type.lpfnWndProc=windowProc;
        type.hInstance=instance;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        type.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));type.lpszClassName=kClass;
        if(!RegisterClassExW(&type))throw std::runtime_error("Register window class");
        HWND hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,kClass,L"PICO",WS_POPUP,
            0,0,192,192,nullptr,nullptr,instance,&pet);
        if(!hwnd)throw std::runtime_error("Create desktop window");
        if(pet.model.ready())pet.model.enableAsync(hwnd,kRenderReadyMessage);
        pet.resizeSurface();pet.updateStyles();
        if(pet.settings.x==INT_MIN || pet.settings.y==INT_MIN)pet.resetPosition();
        else pet.place({pet.settings.x,pet.settings.y},false);
        pet.addTray();
        if(!testing) {
            WTSRegisterSessionNotification(hwnd,NOTIFY_FOR_THIS_SESSION);
            pet.displayNotification=RegisterPowerSettingNotification(hwnd,&GUID_CONSOLE_DISPLAY_STATE,DEVICE_NOTIFY_WINDOW_HANDLE);
            pet.saverNotification=RegisterPowerSettingNotification(hwnd,&GUID_POWER_SAVING_STATUS,DEVICE_NOTIFY_WINDOW_HANDLE);
            foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,environmentEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
            locationHook=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,environmentEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        }
        pet.updateEnvironment();pet.applyPolicy();
        if(testing){result=selfTest(pet,report);DestroyWindow(hwnd);}
        else {
            MSG message{};BOOL status{};
            while((status=GetMessageW(&message,nullptr,0,0))>0){
                const HWND root=GetAncestor(message.hwnd,GA_ROOT);
                if((systemdesk::isWindow(root) || pet.preferencesWindow.isWindow(root)) && IsDialogMessageW(root,&message))continue;
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            if(status==-1)result=1;
        }
        if(foregroundHook)UnhookWinEvent(foregroundHook);
        if(locationHook)UnhookWinEvent(locationHook);
        gPet=nullptr;
    } catch(const std::exception& error) {
        if(testing){std::ofstream file(report);file<<"{\"error\":\""<<error.what()<<"\"}\n";}
        else MessageBoxW(nullptr,L"PICO 启动失败，请重新解压程序后再试。",L"PICO",MB_OK|MB_ICONERROR);
        if(foregroundHook)UnhookWinEvent(foregroundHook);
        if(locationHook)UnhookWinEvent(locationHook);
        gPet=nullptr;result=1;
    }
    CoUninitialize();CloseHandle(mutex);return result;
}
