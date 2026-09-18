#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace interaction {
struct Vec {
    double x{}, y{};
    Vec operator+(Vec b) const { return {x+b.x,y+b.y}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y}; }
    Vec operator*(double s) const { return {x*s,y*s}; }
    double length() const { return std::hypot(x,y); }
};
inline double dot(Vec a,Vec b) { return a.x*b.x+a.y*b.y; }
inline double cross(Vec a,Vec b) { return a.x*b.y-a.y*b.x; }
inline Vec limit(Vec v,double cap) { const double n=v.length();return n>cap?v*(cap/n):v; }
inline double nextPixelChange(double phase,double origin,double amplitude,double frequency) {
    constexpr double tau=6.283185307179586;
    const double angle=phase*frequency,current=std::round(origin+std::sin(angle)*amplitude);
    double delay=2.0;
    for(const double edge:{current-.5,current+.5}){
        const double level=(edge-origin)/amplitude;
        if(std::abs(level)>=1)continue;
        const double root=std::asin(level);
        for(const double candidate:{root,3.141592653589793-root}){
            double distance=std::fmod(candidate-angle,tau);
            if(distance<=.000001)distance+=tau;
            delay=std::min(delay,distance/frequency);
        }
    }
    return delay;
}
struct FloatMotion { Vec offset,velocity; double roll{}; };
inline FloatMotion floatingMotion(double phase) {
    // Unequal, non-harmonic waves avoid the mechanical straight-line bob while
    // keeping the total travel small enough for a desktop companion.
    FloatMotion result;
    result.offset.x=1.05*std::sin(phase*.69)+.30*std::sin(phase*1.71+.8);
    result.offset.y=1.50*std::sin(phase*.91+.35)+.42*std::sin(phase*.46+2.1);
    result.velocity.x=1.05*.69*std::cos(phase*.69)+.30*1.71*std::cos(phase*1.71+.8);
    result.velocity.y=1.50*.91*std::cos(phase*.91+.35)+.42*.46*std::cos(phase*.46+2.1);
    result.roll=.72*std::sin(phase*.36+1.2)+.24*std::sin(phase*.83);
    return result;
}
inline double nextFloatingPixelChange(double phase,Vec origin,double scale,Vec displayed,double threshold=1) {
    constexpr double step=1.0/120;
    for(double delay=step;delay<=2.0;delay+=step){
        const auto next=floatingMotion(phase+delay).offset;
        const Vec pixel{std::round(origin.x+next.x*scale),std::round(origin.y+next.y*scale)};
        if(std::max(std::abs(pixel.x-displayed.x),std::abs(pixel.y-displayed.y))>=threshold)return delay;
    }
    return 2.0;
}
struct Kinematics { Vec velocity, acceleration; double turn{}; bool shaking{}; };

class Trajectory {
    struct Sample { Vec p; double t{}; };
    std::array<Sample,128> samples{};
    size_t head{}, count{};
    Sample latest{};
    double start{}, next{}, lastMotion{};
    void push(Sample s) { samples[head]=s;head=(head+1)%samples.size();count=std::min(count+1,samples.size()); }
public:
    void reset() { count=0;head=0; }
    void add(Vec p,double t) {
        if(!count){start=lastMotion=t;latest={p,t};push(latest);next=t+.008;return;}
        if(t<latest.t)return;
        if((p-latest.p).length()>.2)lastMotion=t;
        if(t==latest.t){latest.p=p;return;}
        // Uniform time samples prevent high polling mice from shortening the history.
        if(t-latest.t>1.0){push(latest);next=t-.992;}
        for(;next<=t;next+=.008){
            const double f=std::clamp((next-latest.t)/(t-latest.t),0.0,1.0);
            push({latest.p+(p-latest.p)*f,next});
        }
        latest={p,t};
    }
    Vec at(double t) const {
        if(!count)return {};
        const size_t oldest=(head+samples.size()-count)%samples.size();
        const auto sample=[&](size_t i)->const Sample& {return samples[(oldest+i)%samples.size()];};
        if(t<=sample(0).t)return sample(0).p;
        if(t>=latest.t)return latest.p;
        size_t low=0,high=count;
        while(low<high){const size_t mid=low+(high-low)/2;if(sample(mid).t<=t)low=mid+1;else high=mid;}
        const auto& older=sample(low-1);
        const auto& newer=low<count?sample(low):latest;
        return older.p+(newer.p-older.p)*((t-older.t)/(newer.t-older.t));
    }
    Vec velocityAt(double t,double span) const {
        // Five equally spaced positions fit a line without overweighting event bursts.
        const double step=span*.25;
        return limit((at(t)*2+at(t-step)-at(t-3*step)-at(t-4*step)*2)*(1/(10*step)),5000);
    }
    Kinematics measure(double t) const {
        Kinematics result;
        if(!count || t-start<.008)return result;
        const double span=std::min(.040,t-start);
        result.velocity=velocityAt(t,span);
        const Vec previous=velocityAt(t-span,span);
        result.acceleration=limit((result.velocity-previous)*(1/span),18000);
        const double product=previous.length()*result.velocity.length();
        // A straight reversal has no circular torque; its acceleration already drives sway.
        if(product>10000)result.turn=std::clamp(cross(previous,result.velocity)/(product*span),-18.0,18.0);
        Vec last{};int reversals=0;double path=0;
        Vec newer=at(t);
        for(int i=0;i<9;++i){
            const Vec older=at(t-(i+1)*.040),segment=newer-older;newer=older;
            const double distance=segment.length();path+=distance;
            if(distance>4){if(last.length()>4 && dot(segment,last)<-.35*distance*last.length())++reversals;last=segment;}
        }
        result.shaking=reversals>=3 && path>100;
        return result;
    }
    Vec release(double t) const {
        if(!count || t-lastMotion>=.120)return {};
        // Mouse-up can arrive after a scheduling gap. Preserve the last moving
        // window briefly, then fade it out when the user deliberately holds still.
        if(lastMotion-start<.008)return {};
        Vec v=velocityAt(lastMotion,std::min(.040,lastMotion-start));
        const double speed=v.length();
        // Smooth dead zone makes careful placement stay put; fast throws remain gentle.
        if(speed<=70)return {};
        const double freshness=std::clamp((.120-(t-lastMotion))/.080,0.0,1.0);
        return limit(v*(.30*(1-70/speed)),360)*freshness;
    }
};

struct Spring {
    double value{}, velocity{};
    void step(double target,double dt,double frequency,double damping=.72) {
        // Closed-form damped harmonic oscillator, stable under uneven timer intervals.
        const double a=frequency*damping,b=frequency*std::sqrt(1-damping*damping);
        const double x=value-target,c=std::cos(b*dt),s=std::sin(b*dt),decay=std::exp(-a*dt);
        value=target+decay*(x*c+(velocity+a*x)*s/b);
        velocity=decay*(velocity*c-(a*velocity+frequency*frequency*x)*s/b);
    }
    void constrain(double low,double high) {
        if(value<low){value=low;velocity=std::max(0.0,velocity);}
        if(value>high){value=high;velocity=std::min(0.0,velocity);}
    }
    bool settled() const { return std::abs(value)<.12 && std::abs(velocity)<.5; }
};
struct Attitude {
    Spring yaw,pitch,roll;
    Vec grab;
    void step(const Kinematics& k,bool held,double dt,double basePitch) {
        double y=0,p=0,r=0;
        if(held){
            y=std::clamp(k.velocity.x*.035+k.acceleration.x*.0015+k.turn*1.3,-55.0,55.0);
            p=std::clamp(-k.velocity.y*.040-k.acceleration.y*.001,-24.0,24.0);
            r=std::clamp(cross(grab,k.velocity)*.04-k.velocity.x*.003+k.turn*.3,-7.0,7.0);
        }
        yaw.step(y,dt,held?19.0:5.5);pitch.step(p,dt,held?20.0:7.0);roll.step(r,dt,held?22.0:8.0);
        yaw.constrain(-70,70);pitch.constrain(-12-basePitch,24-basePitch);roll.constrain(-7,7);
    }
    bool settled() const { return yaw.settled() && pitch.settled() && roll.settled(); }
};
}
