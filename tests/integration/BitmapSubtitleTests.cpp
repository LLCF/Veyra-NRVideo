#include "veyra/engine/Subtitles.h"
#include <cmath>
#include <iostream>

int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    if(argc!=2)return 2;
    int failed=0;
    auto check=[&](bool pass,const char* name){std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';failed+=!pass;};
    const auto tracks=loadEmbeddedSubtitleTracks(argv[1]);
    check(tracks.size()==1,"bitmap embedded track discovered");
    if(tracks.size()!=1)return 1;
    const auto& track=tracks.front();
    check(track.cues.size()==2,"clear display sets close cues without becoming visible cues");
    if(track.cues.size()!=2)return 1;
    const bool dvd=track.codec==L"dvd_subtitle";
    const double tolerance=dvd?.012:.001; // DVD dates use 1024/90000-second ticks.
    for(const auto& cue:track.cues)std::cout<<"cue="<<cue.begin<<","<<cue.end<<'\n';
    check(std::abs(track.cues[0].begin-1)<tolerance&&std::abs(track.cues[0].end-3)<tolerance,"first cue has codec-accurate display timing");
    check(std::abs(track.cues[1].begin-5)<tolerance&&std::abs(track.cues[1].end-7)<tolerance,"second cue has codec-accurate display timing");
    for(double t:{0.,3.,4.,7.,8.})check(cuesAt(track,t).empty(),"clear gap remains empty including exact boundary");
    for(double t:{6.,2.,5.,1.})check(cuesAt(track,t).size()==1,"random access and backward seek retain correct bitmap");
    const auto frame=track.cues[0].bitmap;
    if(dvd){
        check(frame&&frame->images.size()==1,"DVD encoder merges authored rectangles");
        if(!frame||frame->images.size()!=1)return 1;
        const auto& image=frame->images[0];const auto pixels=image.pixels();
        check(pixels.size()==size_t(image.width)*image.height,"merged DVD bitmap expands exactly");
        auto alpha=[&](int x,int y){x-=image.x;y-=image.y;return x>=0&&y>=0&&x<image.width&&y<image.height&&!pixels.empty()?pixels[size_t(y)*image.width+x]>>24:0u;};
        check(alpha(110,610)>0&&alpha(910,110)>0&&alpha(500,350)==0,"DVD retains both authored areas and transparent gap");
        return failed?1:0;
    }
    check(frame&&frame->images.size()==2,"multiple authored rectangles retained in one display set");
    if(!frame||frame->images.size()!=2)return 1;
    check(frame->width==1280&&frame->height==720,"authored canvas retained");
    bool lower=false,upper=false;
    for(const auto& image:frame->images){lower|=image.x==100&&image.y==600;upper|=image.x==900&&image.y==100;}
    check(lower&&upper,"authored positions retained regardless of codec rectangle order");
    const auto pixels=frame->images[0].pixels();
    check(pixels.size()==64*24,"indexed runs expand exactly");
    if(pixels.size()!=64*24)return 1;
    check(pixels[0]==0&&(pixels[5]>>24)==255&&(pixels[35]>>24)==128,"transparent opaque and partial alpha preserved");
    check(((pixels[35]>>16)&255)<=128&&((pixels[35]>>8)&255)<=128&&(pixels[35]&255)<=128,"alpha is premultiplied for layered window");
    check(track.cues[1].bitmap->images[0].pixels()[5]!=pixels[5],"palette change preserved in immutable snapshots");
    auto corrupt=frame->images[0];corrupt.runs.push_back(256);
    check(corrupt.pixels().empty(),"malformed cached runs fail without overrun");
    return failed?1:0;
}
