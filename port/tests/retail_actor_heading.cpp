#include "retail_actor_heading.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Point = RetailActorHeadingPoint;
using Quaternion = RetailActorHeadingQuaternion;
using Matrix = std::array<float, 16>;
const Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool close(float a, float b) { return std::abs(a-b)<.000001f; }
uint32_t bits(float value) { uint32_t result; std::memcpy(&result,&value,4); return result; }
float number(uint32_t value) { float result; std::memcpy(&result,&value,4); return result; }
struct Scene final : RetailActorHeadingHost {
    Matrix absolute = identity;
    Quaternion local{0,0,0,1};
    uint32_t flags=0x84;
    std::vector<std::string> calls;
    std::function<void()> onPosition, onRight, onSet;
    Point getPosition() override {
        calls.push_back("position"); const Point value{absolute[12],absolute[13],absolute[14]};
        if(onPosition) onPosition(); return value;
    }
    Point getRight() override {
        calls.push_back("right"); const Point value{-absolute[0],-absolute[1],-absolute[2]};
        if(onRight) onRight(); return value;
    }
    Point getFront() override { calls.push_back("front"); return {-absolute[4],-absolute[5],-absolute[6]}; }
    Quaternion getRotation() override { calls.push_back("rotation"); return local; }
    void setRotation(const Quaternion& value) override {
        calls.push_back("set"); local=value; flags|=0x10;
        if(onSet) onSet();
    }
};
void pointCases() {
    const float half = std::sqrt(.5f);
    for (const auto& row : std::vector<std::pair<Point,Quaternion>>{
             {{0,-100,999},{0,0,0,1}}, {{100,0,-999},{0,0,-half,half}},
             {{-100,0,0},{0,0,half,half}}, {{0,100,0},{0,0,1,-.000000043711388f}},
             {{0,0,10000},{0,0,0,1}}}) {
        Scene scene; scene.local={1,2,3,4}; const auto before=scene.absolute;
        retailActorTurnTowardPoint(scene,row.first);
        for(size_t i=0;i<4;++i) require(close(scene.local[i],row.second[i]),"Original cardinal/zero point quaternion");
        require(scene.calls==std::vector<std::string>{"position","set"} && scene.flags==0x94 && scene.absolute==before,
                "Point facing reads position/writes rotation once without refreshing absolute cache");
    }
    Scene first,second;
    first.absolute[12]=second.absolute[12]=13; first.absolute[13]=second.absolute[13]=-29;
    first.absolute[14]=std::numeric_limits<float>::quiet_NaN();
    retailActorTurnTowardPoint(first,{123,47,std::numeric_limits<float>::infinity()});
    retailActorTurnTowardPoint(second,{123,47,-999});
    require(first.local==second.local,"Both actor and destination Z are ignored, including nonfinite Z");
    Scene mutation; Point target{0,-100,0};
    mutation.onPosition=[&]{target={100,0,0};};
    retailActorTurnTowardPoint(mutation,target);
    require(close(mutation.local[2],-half),"Const-reference point is read after the original virtual position call");
    Scene zero;
    retailActorTurnTowardPoint(zero,{0,0,0});
    require(bits(zero.local[0])==0x80000000u && bits(zero.local[1])==0x80000000u &&
            bits(zero.local[2])==0 && bits(zero.local[3])==0x3f7fffffu,
            "Original zero-distance result retains signed zeros and nonexact identity W");
}
void directionCases() {
    Scene scene; scene.local={0,0,0,1};
    scene.absolute[0]=0; scene.absolute[1]=1; scene.absolute[4]=-1; scene.absolute[5]=0;
    const auto cache=scene.absolute;
    const bool done=retailActorTurnTowardDirection(scene,{-1,0,0},180);
    require(done && scene.absolute==cache && std::abs(scene.local[2])>.99f &&
            scene.calls==std::vector<std::string>{"right","rotation","set","front"},
            "Return uses original cached front after setter, not a freshly derived front");
    for (const float dot : {std::nextafter(-.99f, -1.f),-.99f,std::nextafter(-.99f,0.f)}) {
        Scene threshold; threshold.absolute[4]=-dot; threshold.absolute[5]=threshold.absolute[6]=0;
        require(retailActorTurnTowardDirection(threshold,{1,0,0},0)==(dot<-.99f),
                "Original front threshold is strict and has no angle epsilon");
    }
    Scene nonunit; nonunit.absolute[4]=2; nonunit.absolute[5]=0; nonunit.local={1,2,3,4};
    require(retailActorTurnTowardDirection(nonunit,{.5f,0,0},0),"Direction/front vectors are not normalized for completion");
    require(nonunit.local==Quaternion{1,2,3,4},"Existing nonunit quaternion product is not renormalized");
    Scene zero;
    require(!retailActorTurnTowardDirection(zero,{0,0,0},8) && zero.local[2]>0,
            "Zero right dot chooses negative sign and still performs original turn");
    Scene positive; positive.absolute[0]=-1;
    retailActorTurnTowardDirection(positive,{1,0,0},8);
    require(positive.local[2]<0,"Strictly positive right dot selects original negative-radian turn");
    Scene live; live.absolute[4]=-1; live.absolute[5]=0;
    live.onSet=[&]{live.absolute[4]=1;};
    require(retailActorTurnTowardDirection(live,{1,0,0},0),"Front getter is live after setter, not prefetched");
    Scene snapshot; Point direction{1,0,0}; snapshot.absolute[0]=-1; snapshot.absolute[4]=1; snapshot.absolute[5]=0;
    snapshot.onRight=[&]{direction={-1,0,0};};
    require(retailActorTurnTowardDirection(snapshot,direction,8) && snapshot.local[2]<0 && direction[0]==-1,
            "By-value direction snapshot survives getter-side mutation of caller storage");
}
template<size_t N> void emit(const std::array<float,N>& value) { for(float f:value) std::cout<<' '<<bits(f); }
void dump() {
    size_t index=0;
    std::vector<Point> offsets{{0,0,0},{0,-1,0},{0,1,0},{1,0,0},{-1,0,0},{1,1,0},{-1,1,0},
        {1,-1,0},{-1,-1,0},{.00001f,1,0},{-.00001f,1,0},{.0001f,1,0},
        {.0004f,1,0},{.0005f,1,0},{.001f,1,0},{-.001f,1,0},{1e-20f,0,0},
        {1e20f,1e20f,0},{-0.f,-0.f,0},{-0.f,1,0},{0.f,-1,0}};
    uint32_t random=0x1a2b3c4d;
    const auto next=[&] {random=random*1664525u+1013904223u;return float(int((random>>8)%20001)-10000)/17.f;};
    for(size_t i=0;i<128;++i) offsets.push_back({next(),next(),next()});
    for(const Point position : {Point{0,0,0},Point{13,-29,79}}) {
        for(const auto& offset:offsets) {
            Scene scene;scene.absolute[12]=position[0];scene.absolute[13]=position[1];scene.absolute[14]=position[2];
            const Point point{position[0]+offset[0],position[1]+offset[1],position[2]+offset[2]};
            retailActorTurnTowardPoint(scene,point);
            require(scene.calls==std::vector<std::string>{"position","set"},"Every point fixture original call order");
            std::cout<<"point "<<index++;emit(position);emit(point);emit(scene.local);std::cout<<'\n';
        }
    }
    for(const Point point : {Point{-0.f,0.f,1},Point{0.f,-0.f,1},Point{-0.f,-0.f,0},
                             Point{0,0,std::numeric_limits<float>::infinity()}}) {
        Scene scene; retailActorTurnTowardPoint(scene,point);
        std::cout<<"point "<<index++;emit(Point{0,0,0});emit(point);emit(scene.local);std::cout<<'\n';
    }
    index=0;
    for(const Quaternion input : {Quaternion{0,0,0,1},Quaternion{1,2,3,4},Quaternion{0,0,0,0},Quaternion{-.2f,.3f,-.4f,.5f}}) {
        for(const float step : {-720.f,-180.f,-8.f,-0.f,0.f,8.f,180.f,450.f}) {
            for(const Point direction : {Point{0,0,0},Point{1,0,0},Point{-1,0,0},Point{.5f,2,3},Point{-.99f,-0.f,0.f}}) {
                Scene scene;scene.local=input;
                scene.absolute[0]=-.5f;scene.absolute[1]=1;scene.absolute[2]=.25f;
                scene.absolute[4]=1;scene.absolute[5]=-.5f;scene.absolute[6]=.125f;
                const bool done=retailActorTurnTowardDirection(scene,direction,step);
                require(scene.calls==std::vector<std::string>{"right","rotation","set","front"},
                        "Every incremental fixture original call order");
                std::cout<<"turn "<<index++;emit(direction);std::cout<<' '<<bits(step);emit(input);emit(scene.absolute);emit(scene.local);
                std::cout<<' '<<done<<'\n';
            }
        }
    }
    for(const float dot : {std::nextafter(-.99f,-1.f),-.99f,std::nextafter(-.99f,0.f)}) {
        Scene scene;scene.absolute[4]=-dot;scene.absolute[5]=scene.absolute[6]=0;
        const Quaternion input=scene.local;const Point direction{1,0,0};
        const bool done=retailActorTurnTowardDirection(scene,direction,8);
        std::cout<<"turn "<<index++;emit(direction);std::cout<<' '<<bits(8);emit(input);emit(scene.absolute);emit(scene.local);
        std::cout<<' '<<done<<'\n';
    }
}
}
int main(int argc,char**argv) {
    try {pointCases();directionCases();
        if(argc==2 && std::string(argv[1])=="--oracle-dump")dump();
        std::cout<<"Original actor heading point/direction/cached-transform fixtures passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
