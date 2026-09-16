#include "veyra/engine/PresetStore.h"
#include "veyra/ngx/NrArchitecturePolicy.h"
#include <iostream>
#include <fstream>
#include <sstream>
namespace {
bool legacyBackends(const std::filesystem::path& path) {
 using namespace veyra::engine;
 unsigned checks=0;
 for(int version=4;version<=11;++version)for(int backend=0;backend<=2;++backend)for(int multiplier:{2,4}){
  std::ostringstream fixture;
  fixture<<"VEYRA_PRESETS "<<version<<"\n\"legacy\" 1\n\"legacy\" 1 1 1 -1 0 0 0 1 1 1 1 1 1 0 "<<multiplier<<" 0 1 0";
  fixture<<" 0 0";
  for(int region=0;region<4;++region)fixture<<" 0 0 0 0";
  fixture<<" 2 "<<backend;
  if(version>=5)fixture<<" 1";
  if(version>=6)fixture<<" 0 0";
  if(version>=7)fixture<<" 1 137";
  if(version>=9)fixture<<" 1";
  if(version>=10)fixture<<" 1";
  if(version>=11)fixture<<" 0";
  fixture<<'\n';
  {std::ofstream file(path);file<<fixture.str();}
  const bool xess=version<8?backend==2:backend==1;
  const bool supportedValue=backend<2||(version>=6&&version<=7);
  const bool expected=supportedValue&&(!xess||multiplier==2);
  PresetStore store(path);
  if(store.load()!=expected)return false;
  if(expected){
   const auto value=store.defaultSettings();
   if(value.lowLatency||value.forceSdrPreview)return false;
   if(value.captureCompatible!=(version>=10))return false;
   if(value.nrRuntime!=(version>=9?NrRuntime::Community:NrRuntime::Original))return false;
   if(value.frameGenerationBackend!=(xess?FrameGenerationBackend::XeSS:FrameGenerationBackend::Dlss)||value.multiplier!=multiplier||value.videoSrQuality!=2)return false;
   if(version>=7&&(value.audioSync!=AudioSyncMode::Manual||value.audioOffsetMs!=137))return false;
   if(!store.put(L"legacy",value,true))return false;
   std::ifstream file(path);std::string magic;int savedVersion=0;file>>magic>>savedVersion;
   if(magic!="VEYRA_PRESETS"||savedVersion!=13)return false;
   PresetStore reloaded(path);
   if(!reloaded.load()||reloaded.defaultSettings()!=value)return false;
  }else{
   if(store.put(L"must not overwrite",{}))return false;
   std::ifstream file(path);std::string unchanged((std::istreambuf_iterator<char>(file)),{});
   if(unchanged!=fixture.str())return false;
  }
  ++checks;
 }
 std::cout<<"legacy/current backend migration cases="<<checks<<'\n';
 return true;
}
}
int main(int argc,char** argv){if(argc!=2)return 2;using namespace veyra::engine;const std::filesystem::path p=argv[1];PresetStore a(p);bool ok=a.load();EnhancementSettings s;s.videoSrQuality=2;s.frameGenerationBackend=FrameGenerationBackend::Dlss;s.opticalFlowBackend=OpticalFlowBackend::AmdFidelityFx;s.amdFlowHalfResolution=true;s.protection.enabled=true;s.protection.featherPixels=3.5f;s.protection.regions[0]={.1f,.2f,.7f,.8f};s.protection.regions[3]={0,0,1,1};s.model.intensity=.375f;s.model.skin=1.5f;s.residual.darken=1.2f;s.multiplier=4;s.flow=FlowQuality::Quality;s.content=ContentRate::Fps50;s.nrPolicy=veyra::pipeline::NrSizePolicy::Native;
 for(uint32_t family:{0x160u,0x170u,0x171u,0x190u,0x1A0u,0x1B0u})for(bool selected:{false,true})for(int result:{0,-1}){
  auto value=family;const bool expected=selected&&result==0&&(family==0x170u||family==0x171u);
  ok=ok&&(veyra::ngx::rewriteNrAmpereArchitecture(value,selected,result)==expected)&&value==(expected?0x1B0u:family);
 }
 s.srTarget=veyra::pipeline::SrTarget::Uhd8K;
 s.audioSync=AudioSyncMode::Manual;s.audioOffsetMs=137;
 s.nrRuntime=NrRuntime::Community;s.captureCompatible=true;s.lowLatency=true;s.forceSdrPreview=true;
 ok=ok&&a.put(L"test",s)&&a.setDefault(0)&&!a.put(L"test",s);PresetStore b(p);ok=ok&&b.load()&&b.defaultSettings()==s&&b.rename(0,L"renamed")&&b.defaultSettings()==s;
 auto badTarget=s;badTarget.srTarget=static_cast<veyra::pipeline::SrTarget>(3);ok=ok&&!b.put(L"invalid target",badTarget);
 auto xess=s;xess.frameGenerationBackend=FrameGenerationBackend::XeSS;ok=ok&&!b.put(L"XeSS 4X rejected",xess);
 xess.multiplier=2;ok=ok&&b.put(L"XeSS 2X",xess);PresetStore xessReload(p);ok=ok&&xessReload.load()&&xessReload.entries().back().settings==xess&&b.erase(1);
 auto badBackend=s;badBackend.frameGenerationBackend=static_cast<FrameGenerationBackend>(3);ok=ok&&!b.put(L"invalid backend",badBackend);
 auto dis=s;dis.opticalFlowBackend=OpticalFlowBackend::GpuDis;ok=ok&&b.put(L"GPU DIS",dis)&&b.save();PresetStore disReload(p);ok=ok&&disReload.load()&&disReload.entries().back().settings==dis&&b.erase(1);
 auto badFlow=s;badFlow.opticalFlowBackend=static_cast<OpticalFlowBackend>(3);ok=ok&&!b.put(L"invalid flow backend",badFlow);
 auto ampere=s;ampere.nr=true;ampere.nrRuntime=NrRuntime::Ampere;ok=ok&&b.put(L"RTX30",ampere);PresetStore ampereReload(p);ok=ok&&ampereReload.load()&&ampereReload.entries().back().settings==ampere&&b.erase(1);
 auto badNr=s;badNr.nrRuntime=static_cast<NrRuntime>(3);ok=ok&&!b.put(L"invalid NR runtime",badNr);
 auto half=s;half.content=ContentRate::Capture60To30;ok=ok&&b.put(L"capture half rate",half);PresetStore halfReload(p);ok=ok&&halfReload.load()&&halfReload.entries().back().settings.content==ContentRate::Capture60To30&&b.erase(1);
 auto badSr=s;badSr.videoSrQuality=5;ok=ok&&!b.put(L"invalid video SR",badSr);
 auto invalid=s;invalid.model.tone=9;ok=ok&&!b.put(L"bad",invalid)&&b.entries().size()==1&&b.erase(0)&&b.entries().empty();
 // Manual capture vertical flip (v13) round-trips with the rest of the preset.
 auto flipped=s;flipped.captureFlipVertical=true;ok=ok&&b.put(L"capture flip",flipped);
 PresetStore flipReload(p);ok=ok&&flipReload.load()&&flipReload.entries().size()==1&&flipReload.entries().back().settings.captureFlipVertical&&b.erase(0);
 auto badRegion=s;badRegion.protection.regions[0].left=2;ok=ok&&!b.put(L"invalid region",badRegion);
 {std::ofstream legacy(p);legacy<<"VEYRA_PRESETS 1\n\"legacy\" 1\n\"legacy\" 1 1 1 -1 0 0 0 1 1 1 1 1 1 0 1 0 1 0\n";}
 PresetStore old(p);ok=ok&&old.load()&&!old.defaultSettings().protection.enabled&&old.defaultSettings().srTarget==veyra::pipeline::SrTarget::Uhd4K&&old.put(L"v2",s);
 PresetStore upgraded(p);ok=ok&&upgraded.load()&&upgraded.entries().size()==2&&upgraded.entries()[1].settings==s;
 {std::ofstream f(p);f<<"VEYRA_PRESETS 99\ncorrupt mediaPath executable must reject";}PresetStore c(p);ok=ok&&!c.load()&&!c.put(L"override",{});std::ifstream f(p);std::string data((std::istreambuf_iterator<char>(f)),{});ok=ok&&data=="VEYRA_PRESETS 99\ncorrupt mediaPath executable must reject";
 f.close();ok=legacyBackends(p)&&ok;
 std::cout<<"preset roundtrip, all fields, duplicate, rename-default, delete, validation, unknown schema, corrupt-preservation="<<ok<<'\n';return ok?0:1;}
