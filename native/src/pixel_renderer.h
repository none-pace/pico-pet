#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pixels {
inline int stableStep(double value,double step,int previous,double margin,bool circular=false) {
    const double delta=circular?std::remainder(value-previous*step,360.0):value-previous*step;
    if(std::abs(delta)<=step*.5+margin)return previous;
    return static_cast<int>(std::lround(value/step));
}

// One bounded map is reused for expression/pose changes at the same tilt and DPI.
class Mapping {
    std::vector<int> indices;
    struct Sample { int16_t x,y; uint16_t fx,fy; };
    std::vector<Sample> samples;
    bool smooth=false;
    int size=-1,sourceSize=-1,tilt=0,offset=0;
public:
    uint64_t builds=0;
    bool prepare(int extent,int source,int halfDegrees,int bob,bool filtered=false) {
        if(extent==size && source==sourceSize && tilt==halfDegrees && offset==bob && smooth==filtered)return false;
        smooth=filtered;
        size=extent;sourceSize=source;tilt=halfDegrees;offset=bob;
        indices.resize(static_cast<size_t>(extent)*extent);
        if(smooth)samples.resize(indices.size());else std::vector<Sample>().swap(samples);
        const double angle=halfDegrees*.5*3.141592653589793/180,cs=std::cos(angle),sn=std::sin(angle);
        const double ratio=static_cast<double>(source)/extent,center=source*.5;
        for(int y=0;y<extent;++y){
            const double dy=(y+.5)*ratio-center-bob;
            for(int x=0;x<extent;++x){
                const double dx=(x+.5)*ratio-center;
                const double px=dx*cs+dy*sn+center,py=-dx*sn+dy*cs+center;
                const int sx=static_cast<int>(std::floor(px));
                const int sy=static_cast<int>(std::floor(py));
                indices[static_cast<size_t>(y)*extent+x]=(sx>=0 && sx<source && sy>=0 && sy<source)?sy*source+sx:-1;
                if(smooth){
                    const double left=std::floor(px-.5),top=std::floor(py-.5);
                    samples[static_cast<size_t>(y)*extent+x]={static_cast<int16_t>(left),static_cast<int16_t>(top),
                        static_cast<uint16_t>(std::lround((px-.5-left)*256)),static_cast<uint16_t>(std::lround((py-.5-top)*256))};
                }
            }
        }
        ++builds;return true;
    }
    void paint(const uint32_t* source,uint32_t* destination) const {
        if(smooth){
            const auto read=[&](int x,int y){return x>=0 && y>=0 && x<sourceSize && y<sourceSize?source[y*sourceSize+x]:0u;};
            // Interpolate premultiplied color and alpha together to avoid dark fringes.
            for(size_t i=0;i<samples.size();++i){
                const auto& s=samples[i];
                const uint32_t a=read(s.x,s.y),b=read(s.x+1,s.y),c=read(s.x,s.y+1),d=read(s.x+1,s.y+1);
                uint32_t color=0;
                for(int shift=0;shift<32;shift+=8){
                    const uint32_t top=((a>>shift)&255)*(256-s.fx)+((b>>shift)&255)*s.fx;
                    const uint32_t bottom=((c>>shift)&255)*(256-s.fx)+((d>>shift)&255)*s.fx;
                    color|=((top*(256-s.fy)+bottom*s.fy+32768)>>16)<<shift;
                }
                destination[i]=color;
            }
            return;
        }
        for(size_t i=0;i<indices.size();++i){const int index=indices[i];destination[i]=index<0?0:source[index];}
    }
    int sourceIndex(int x,int y) const {
        return x>=0 && y>=0 && x<size && y<size?indices[static_cast<size_t>(y)*size+x]:-1;
    }
};
}
