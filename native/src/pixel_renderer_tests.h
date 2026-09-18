#pragma once
#include "pixel_renderer.h"
#include <chrono>
#include <array>

namespace pixels {
inline void referencePaint(const uint32_t* source,uint32_t* out,int size,int extent,int tilt,int bob) {
    const double angle=tilt*.5*3.141592653589793/180,cs=std::cos(angle),sn=std::sin(angle);
    const double ratio=static_cast<double>(size)/extent,center=size*.5;
    for(int y=0;y<extent;++y)for(int x=0;x<extent;++x){
        const double dx=(x+.5)*ratio-center,dy=(y+.5)*ratio-center-bob;
        const int sx=static_cast<int>(std::floor(dx*cs+dy*sn+center)),sy=static_cast<int>(std::floor(-dx*sn+dy*cs+center));
        out[y*extent+x]=sx>=0 && sx<size && sy>=0 && sy<size?source[sy*size+sx]:0;
    }
}
struct Benchmark { double referenceMs{},cachedMs{}; };
template<class Check> Benchmark testRenderer(Check check) {
    constexpr int size=192;
    std::vector<uint32_t> source(size*size);
    for(size_t i=0;i<source.size();++i)source[i]=0xff000000u|static_cast<uint32_t>(i);
    Mapping map;
    for(const int extent:{160,192,240,288,384}){
        std::vector<uint32_t> expected(extent*extent),actual(extent*extent);
        bool equal=true;
        for(int tilt=-14;tilt<=14;++tilt)for(const int bob:{-1,0,2}){
            referencePaint(source.data(),expected.data(),size,extent,tilt,bob);
            map.prepare(extent,size,tilt,bob);map.paint(source.data(),actual.data());
            equal=equal && actual==expected;
        }
        check(equal,"cached mapping matches reference pixels across all tilts and DPI sizes");
    }
    check(!map.prepare(384,size,14,2),"same transform reuses mapping without rebuilding");
    std::vector<uint32_t> identity(size*size);map.prepare(size,size,0,0);map.paint(source.data(),identity.data());
    check(identity==source,"zero tilt at native resolution is pixel exact");
    map.prepare(size,size,0,0,true);map.paint(source.data(),identity.data());
    check(identity==source,"HD identity preserves exact pixels at native resolution");
    check(!map.prepare(size,size,0,0,true),"HD transform is cached");
    check(map.prepare(size,size,0,0,false),"quality change invalidates transform cache");
    const std::array<uint32_t,4> colors={0xff000000,0xffffffff,0xff000000,0xffffffff};
    std::array<uint32_t,16> blend{};
    map.prepare(4,2,0,0,true);map.paint(colors.data(),blend.data());
    check(blend[5]==0xff404040 && blend[6]==0xffbfbfbf,"HD bilinear interpolation matches known quarter weights");
    const std::array<uint32_t,4> transparent={0,0xffff0000,0,0xffff0000};
    map.paint(transparent.data(),blend.data());
    check(blend[5]==0x40400000 && blend[6]==0xbfbf0000,"HD interpolation preserves premultiplied transparent edges");
    bool validAlpha=true;
    for(int angle=-14;angle<=14;++angle){
        map.prepare(4,2,angle,1,true);map.paint(transparent.data(),blend.data());
        for(auto color:blend)validAlpha=validAlpha && ((color>>16)&255)<=(color>>24);
    }
    check(validAlpha,"rotated HD boundary samples keep valid alpha");
    check(stableStep(5.1,10,0,1)==0 && stableStep(4.9,10,1,1)==1,"yaw hysteresis suppresses boundary chatter");
    check(stableStep(6.1,10,0,1)==1 && stableStep(3.9,10,1,1)==0,"yaw hysteresis still responds beyond margin");
    check(stableStep(-.1,10,0,1,true)==0 && stableStep(359.9,10,0,1,true)==0,"yaw hysteresis wraps across full rotation");
    std::vector<uint32_t> out(288*288);
    std::array<uint64_t,2> checksums{};
    Benchmark result;
    for(int mode=0;mode<2;++mode){
        const auto start=std::chrono::steady_clock::now();
        for(int frame=0;frame<600;++frame){
            const int tilt=(frame/8)%29-14;
            if(mode==0)referencePaint(source.data(),out.data(),size,288,tilt,0);
            else {map.prepare(288,size,tilt,0);map.paint(source.data(),out.data());}
            checksums[mode]+=out[static_cast<size_t>(frame)*131%out.size()];
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(mode==0)result.referenceMs=ms;else result.cachedMs=ms;
    }
    check(checksums[0]==checksums[1],"mapping benchmark produces identical output");
    return result;
}
}
