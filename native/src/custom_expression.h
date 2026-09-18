#pragma once
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace expression {
using Microsoft::WRL::ComPtr;
inline void checked(HRESULT result){if(FAILED(result))throw std::runtime_error("Windows image decoder could not read or save this image");}
struct Pixels {UINT width=0,height=0;std::vector<uint32_t> data;};
inline Pixels decode(IWICImagingFactory* factory,const std::wstring& path){
    ComPtr<IWICBitmapDecoder> decoder;checked(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));
    ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,&frame));UINT width=0,height=0;checked(frame->GetSize(&width,&height));
    if(!width || !height || static_cast<uint64_t>(width)*height>64000000)throw std::runtime_error("Image exceeds 64 million pixels");
    ComPtr<IWICBitmapSource> source=frame;
    ComPtr<IWICMetadataQueryReader> metadata;PROPVARIANT orientation{};
    if(SUCCEEDED(frame->GetMetadataQueryReader(&metadata)) && SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&orientation)) && orientation.vt==VT_UI2){
        const WICBitmapTransformOptions transforms[]={WICBitmapTransformRotate0,WICBitmapTransformRotate0,WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,
            static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
        if(orientation.uiVal>=2 && orientation.uiVal<=8){ComPtr<IWICBitmapFlipRotator> rotated;checked(factory->CreateBitmapFlipRotator(&rotated));checked(rotated->Initialize(source.Get(),transforms[orientation.uiVal]));source=rotated;checked(source->GetSize(&width,&height));}
    }
    PropVariantClear(&orientation);
    const double scale=std::min(1.0,1600.0/std::max(width,height));
    Pixels pixels;pixels.width=std::max(1u,static_cast<UINT>(width*scale));pixels.height=std::max(1u,static_cast<UINT>(height*scale));
    ComPtr<IWICBitmapScaler> scaler;checked(factory->CreateBitmapScaler(&scaler));checked(scaler->Initialize(source.Get(),pixels.width,pixels.height,WICBitmapInterpolationModeFant));
    ComPtr<IWICFormatConverter> converted;checked(factory->CreateFormatConverter(&converted));checked(converted->Initialize(scaler.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    pixels.data.resize(static_cast<size_t>(pixels.width)*pixels.height);checked(converted->CopyPixels(nullptr,pixels.width*4,static_cast<UINT>(pixels.data.size()*4),reinterpret_cast<BYTE*>(pixels.data.data())));return pixels;
}
inline void encode(IWICImagingFactory* factory,const Pixels& pixels,const std::wstring& path){
    ComPtr<IWICStream> stream;checked(factory->CreateStream(&stream));checked(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;checked(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));checked(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;checked(encoder->CreateNewFrame(&frame,nullptr));checked(frame->Initialize(nullptr));checked(frame->SetSize(pixels.width,pixels.height));
    auto format=GUID_WICPixelFormat32bppBGRA;checked(frame->SetPixelFormat(&format));if(format!=GUID_WICPixelFormat32bppBGRA)throw std::runtime_error("PNG pixel format unavailable");
    checked(frame->WritePixels(pixels.height,pixels.width*4,static_cast<UINT>(pixels.data.size()*4),reinterpret_cast<BYTE*>(const_cast<uint32_t*>(pixels.data.data()))));checked(frame->Commit());checked(encoder->Commit());
}
class Image {
    ComPtr<IWICImagingFactory> factory;
    Pixels pixels;
    std::wstring config,file;
    std::vector<uint32_t> composed;
    bool dirty=true;
    bool persist()const{
        const auto values=L"enabled="+std::to_wstring(enabled)+std::wstring(1,0)+L"cover="+std::to_wstring(cover)+std::wstring(1,0)+L"light="+std::to_wstring(light)+std::wstring(2,0);
        return WritePrivateProfileSectionW(L"Image",values.c_str(),config.c_str())!=FALSE;
    }
public:
    bool enabled=false,cover=false,light=false;
    bool available()const{return !pixels.data.empty();}
    bool active()const{return enabled && available();}
    void load(IWICImagingFactory* wic,const std::filesystem::path& directory){
        factory=wic;config=(directory/L"expression.ini").wstring();file=(directory/L"expression.png").wstring();
        enabled=GetPrivateProfileIntW(L"Image",L"enabled",0,config.c_str())!=0;cover=GetPrivateProfileIntW(L"Image",L"cover",0,config.c_str())!=0;light=GetPrivateProfileIntW(L"Image",L"light",0,config.c_str())!=0;
        pixels={};try{if(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES)pixels=decode(factory.Get(),file);}catch(const std::exception&){enabled=false;}dirty=true;
    }
    void import(const std::wstring& path){
        Pixels next=decode(factory.Get(),path);const auto temporary=file+L".new";
        try{encode(factory.Get(),next,temporary);if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot save imported image");}
        catch(...){DeleteFileW(temporary.c_str());throw;}
        pixels=std::move(next);enabled=true;dirty=true;if(!persist())throw std::runtime_error("Image imported but preference could not be saved");
    }
    void configure(bool use,bool crop,bool bright){
        if(use && !available())throw std::runtime_error("Import an image first");
        const bool wasEnabled=enabled,wasCover=cover,wasLight=light;enabled=use;cover=crop;light=bright;
        if(!persist()){enabled=wasEnabled;cover=wasCover;light=wasLight;throw std::runtime_error("Cannot save image preferences");}dirty=true;
    }
    const std::vector<uint32_t>& texture(){
        if(!dirty)return composed;if(!available())throw std::runtime_error("No imported image");
        ComPtr<IWICBitmap> bitmap;checked(factory->CreateBitmapFromMemory(pixels.width,pixels.height,GUID_WICPixelFormat32bppBGRA,pixels.width*4,static_cast<UINT>(pixels.data.size()*4),reinterpret_cast<BYTE*>(pixels.data.data()),&bitmap));
        ComPtr<IWICBitmapSource> source=bitmap;UINT width=pixels.width,height=pixels.height;
        // Crop before scaling so extreme portrait/panorama images cannot allocate giant buffers.
        constexpr double screenAspect=83.6/47.6,displayHeight=800/screenAspect;
        if(cover){WICRect area{0,0,static_cast<INT>(width),static_cast<INT>(height)};if(width>height*screenAspect){area.Width=std::max(1,static_cast<int>(height*screenAspect));area.X=(width-area.Width)/2;}else{area.Height=std::max(1,static_cast<int>(width/screenAspect));area.Y=(height-area.Height)/2;}
            ComPtr<IWICBitmapClipper> clipped;checked(factory->CreateBitmapClipper(&clipped));checked(clipped->Initialize(source.Get(),&area));source=clipped;width=area.Width;height=area.Height;}
        // Texture dimensions differ from the physical LCD aspect ratio.
        const double scale=std::min(800.0/width,displayHeight/height);const UINT targetWidth=cover?800:std::clamp(static_cast<UINT>(width*scale),1u,800u),targetHeight=cover?500:std::clamp(static_cast<UINT>(height*scale*500/displayHeight),1u,500u);
        ComPtr<IWICFormatConverter> premultiplied;checked(factory->CreateFormatConverter(&premultiplied));checked(premultiplied->Initialize(source.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        ComPtr<IWICBitmapScaler> scaler;checked(factory->CreateBitmapScaler(&scaler));checked(scaler->Initialize(premultiplied.Get(),targetWidth,targetHeight,WICBitmapInterpolationModeFant));
        std::vector<uint32_t> scaled(static_cast<size_t>(targetWidth)*targetHeight);checked(scaler->CopyPixels(nullptr,targetWidth*4,static_cast<UINT>(scaled.size()*4),reinterpret_cast<BYTE*>(scaled.data())));
        const uint32_t background=light?0xffe5eef0u:0xff182326u;composed.assign(800*500,background);
        const int left=(800-targetWidth)/2,top=(500-targetHeight)/2;
        for(UINT y=0;y<targetHeight;++y)for(UINT x=0;x<targetWidth;++x){const uint32_t color=scaled[static_cast<size_t>(y)*targetWidth+x],inverse=255-(color>>24);uint32_t output=0xff000000;
            for(int shift:{0,8,16})output|=std::min(255u,((color>>shift)&255)+(((background>>shift)&255)*inverse+127)/255)<<shift;
            composed[(y+top)*800+x+left]=output;}
        dirty=false;return composed;
    }
};
}
