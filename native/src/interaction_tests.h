#pragma once
#include "interaction.h"

template<class Check> void testInteractions(Check check) {
    using namespace interaction;
    auto line=[](double vx,double vy,int hz) {
        Trajectory t;
        for(int i=0;i<=hz;++i){const double s=static_cast<double>(i)/hz;t.add({vx*s,vy*s},s);}
        return t;
    };
    const auto slow=line(45,0,125),fast=line(1000,-500,125);
    check(slow.release(1).length()==0,"careful placement has no unwanted drift");
    const auto thrown=fast.release(1);
    check(thrown.x>250 && thrown.y< -100,"flick follows measured diagonal trajectory");
    check(line(0,-1400,125).release(1).y< -300,"upward toss retains upward momentum without artificial gravity");
    check(fast.release(1.13).length()==0,"holding still cancels stale throw after release grace");
    check((fast.release(1.032)-fast.release(1)).length()<.001,"mouse-up scheduling delay does not erase a flick");
    check(fast.release(1.08).length()<fast.release(1.04).length(),"holding still progressively removes throw energy");
    const auto low=line(1000,-500,60).release(1),high=line(1000,-500,1000).release(1);
    check((low-high).length()<.01,"throw velocity independent of 60 versus 1000 Hz input");
    check(line(30000,30000,125).release(1).length()<=360.001,"extreme flick speed remains bounded");
    Trajectory reverse;
    for(int i=0;i<=100;++i){const double t=i*.008;reverse.add({t<.72?t*800:576-(t-.72)*1000,0},t);}
    check(reverse.release(.8).x< -250,"release follows last reversal instead of averaging opposite directions");
    Trajectory shake,circle,noise;
    for(int i=0;i<=125;++i){const double t=i*.008;
        shake.add({55*std::sin(t*2*3.141592653589793*6),0},t);
        circle.add({70*std::cos(t*2*3.141592653589793*2),70*std::sin(t*2*3.141592653589793*2)},t);
        noise.add({std::sin(t*90)*.6,std::cos(t*130)*.6},t);
    }
    check(shake.measure(1).shaking,"repeated directional reversals detect shaking");
    check(!fast.measure(1).shaking && !noise.measure(1).shaking,"straight flick and subpixel jitter do not trigger shaking");
    check(!circle.measure(1).shaking && std::abs(circle.measure(1).turn)>5,"circular motion produces signed turning without false shake");
    Trajectory accelerated;
    for(int i=0;i<=125;++i){const double t=i*.008;accelerated.add({1000*t*t,0},t);}
    check(accelerated.measure(1).acceleration.x>1900,"acceleration responds to increasing cursor speed");
    Attitude top,bottom,center;top.grab={0,-.8};bottom.grab={0,.8};
    const Kinematics pull{{500,0},{2000,0},0,false};
    for(int i=0;i<12;++i){top.step(pull,true,.008,0);bottom.step(pull,true,.008,0);center.step(pull,true,.008,0);}
    check(top.roll.value>1 && bottom.roll.value< -1,"top and bottom grips create opposite torque");
    check(std::abs(center.roll.value)<std::abs(bottom.roll.value),"center grab reduces off-center torque");
    const double yawBefore=top.yaw.value;
    top.step({},false,.016,0);
    check(top.yaw.value>yawBefore,"release preserves existing angular momentum");
    for(int i=0;i<400;++i)top.step({},false,.016,0);
    check(top.settled(),"released rotation settles without a permanent timer");
    Attitude a,b;
    for(int i=0;i<100;++i)a.step(pull,true,.008,12);
    for(int i=0;i<25;++i)b.step(pull,true,.032,12);
    check(std::abs(a.yaw.value-b.yaw.value)<.001,"angular spring independent of timer interval");
    for(int i=0;i<300;++i){a.step({{5000,-5000},{18000,18000},70,true},true,.1,12);}
    check(std::isfinite(a.yaw.value) && std::abs(a.roll.value)<=7 && a.pitch.value+12<=24,"extreme gesture stays within renderable attitude limits");
    Trajectory duplicates=line(600,0,125);
    for(int i=0;i<30;++i)duplicates.add({600,0},1);
    check(std::abs(duplicates.release(1).x-line(600,0,125).release(1).x)<.001,"duplicate timestamps do not create artificial velocity");
    check(line(600,0,125).release(20).length()==0,"long idle never resurrects old momentum");
    Trajectory collinear;
    for(int i=0;i<=125;++i){const double t=i*.008;collinear.add({50*std::sin(t*40),0},t);}
    bool noSpin=true;
    for(int i=45;i<=125;++i)noSpin=noSpin && std::abs(collinear.measure(i*.008).turn)<.001;
    check(noSpin,"straight reversals do not invent circular torque");
    Trajectory wrapped;
    for(int i=0;i<=2000;++i)wrapped.add({i*.5,-i*.25},i*.008);
    check((wrapped.at(15.752)-Vec{984.5,-492.25}).length()<.001,"binary search interpolates correctly after ring buffer wrap");
    check(wrapped.at(17).x==1000,"trajectory lookup clamps future timestamps");
    Trajectory interrupted;interrupted.add({0,0},0);interrupted.add({80,0},.08);interrupted.add({80,0},4);
    check(interrupted.release(4).length()==0 && interrupted.measure(4).velocity.length()==0,"long sampling gap cannot create motion");
    bool crossings=true;
    for(const double phase:{0.0,.35,1.2,3.0,6.0,12.0}){
        const double wait=nextPixelChange(phase,100.2,1.35,.9);
        const double before=std::round(100.2+1.35*std::sin(phase*.9));
        const double almost=std::round(100.2+1.35*std::sin((phase+wait-.001)*.9));
        const double after=std::round(100.2+1.35*std::sin((phase+wait+.001)*.9));
        crossings=crossings && wait>0 && before==almost && (wait>=2 || after!=before);
    }
    check(crossings,"idle wake predicts next visible pixel crossing");
    check(nextPixelChange(0,100,.4,.65)==2,"subpixel float does not request unnecessary fast wakes");
    double path=0,minX=100,maxX=-100,minY=100,maxY=-100;bool bounded=true;auto previous=floatingMotion(0);
    for(int i=1;i<=1200;++i){const auto sample=floatingMotion(i/120.0);path+=(sample.offset-previous.offset).length();previous=sample;
        minX=std::min(minX,sample.offset.x);maxX=std::max(maxX,sample.offset.x);minY=std::min(minY,sample.offset.y);maxY=std::max(maxY,sample.offset.y);
        bounded=bounded && std::abs(sample.offset.x)<1.36 && std::abs(sample.offset.y)<1.93 && std::abs(sample.roll)<.97;}
    check(bounded,"organic float remains within its subtle motion envelope");
    check(path>4 && maxX-minX>1 && maxY-minY>1,"organic float follows a curved multi-axis path");
    const auto currentDrift=floatingMotion(.7).offset;
    const Vec displayed{std::round(100.2+currentDrift.x*1.5),std::round(80.4+currentDrift.y*1.5)};
    const double floatWake=nextFloatingPixelChange(.7,{100.2,80.4},1.5,displayed,2);
    check(floatWake>0 && floatWake<=2,"organic float predicts the next visible pixel without a continuous timer");
}
