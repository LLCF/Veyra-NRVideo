#include "veyra/engine/LiveGpuScheduler.h"
#include "veyra/engine/TimingWindow.h"
#include "veyra/engine/FgRecoveryBudget.h"
#include "veyra/engine/PreviewFgCapacity.h"
#include "veyra/engine/LivePairLatency.h"
#include "veyra/engine/PreviewFrameReadiness.h"
#include "veyra/engine/PresentationGeometry.h"
#include <iostream>
#include <memory>
#include <atomic>
int main(){
    using Scheduler=veyra::engine::LiveGpuScheduler;using State=Scheduler::State;
    int failures=0;auto check=[&](bool pass,const char* text){std::cout<<(pass?"PASS ":"FAIL ")<<text<<'\n';failures+=!pass;};
    {
        veyra::engine::PreviewFgCapacity capacity;
        check(capacity.select(6,16.667)==6,"unknown work preserves requested multiplier");
        for(int i=0;i<60;++i){capacity.observe(18,10,5);capacity.select(6,16.667);}
        check(capacity.select(6,16.667)==4,"sustained overload retains an evenly spaced sustainable group");
        for(int i=0;i<30;++i){capacity.observe(8,4,3);capacity.select(6,16.667);}
        check(capacity.select(6,16.667)==4,"brief headroom does not oscillate multiplier");
        for(int i=0;i<200;++i){capacity.observe(8,4,3);capacity.select(6,16.667);}
        check(capacity.select(6,16.667)==6,"sustained headroom restores requested multiplier");
        capacity.reset();check(capacity.select(2,16.667)==2,"reset and 2X request cannot inherit a larger multiplier");
    }
    {
        using namespace veyra;
        using R=engine::PreviewFrameReadiness;
        pipeline::BatchFrame generated;generated.kind=pipeline::FrameKind::Generated;generated.validity=pipeline::GenerationValidity::Pending;
        unsigned polls=0;auto pending=[&]{++polls;return false;};
        check(engine::previewFrameReadiness(generated,false,true,pending)==R::Expired&&polls==0,"expired pending generation never waits on GPU readiness");
        check(engine::previewFrameReadiness(generated,true,false,pending)==R::Suppressed&&polls==0,"old history generation never waits on GPU readiness");
        check(engine::previewFrameReadiness(generated,false,false,pending)==R::Pending&&polls==1,"current unexpired generation keeps its producer fence");
        check(engine::previewFrameReadiness(generated,false,false,[&]{generated.validity=pipeline::GenerationValidity::Disabled;return true;})==R::Invalid,"provider-disabled output is never presented");
        pipeline::BatchFrame real;
        check(engine::previewFrameReadiness(real,true,true,pending)==R::Pending&&polls==2,"original never bypasses its producer fence or expires with generated output");
        check(engine::previewFrameReadiness(real,true,true,[]{return true;})==R::Ready,"ready original survives expiration and history suppression");
        const auto fit=engine::presentationRegion(1920,1080,1000,1000,2560,1440,{});
        check(fit.left==0&&fit.right==2560&&fit.top==314&&fit.bottom==1124,"retained wide buffer uses square client letterbox mapping");
        const auto zoom=engine::presentationRegion(1920,1080,1000,1000,2560,1440,{2,.5f,.5f});
        check(zoom.left==0&&zoom.right==2560&&zoom.top==0&&zoom.bottom==1440,"zoom region clips to retained buffer");
        const auto outside=engine::presentationRegion(1920,1080,1000,1000,2560,1440,{1,3,.5f});
        check(outside.left==outside.right,"offscreen image cannot invent an out-of-bounds provider rectangle");
    }
    Scheduler scheduler;unsigned finished=0,secondRan=0,firstSteps=0;bool gpuReady=false;
    const auto owner=std::this_thread::get_id();bool sameOwner=true;
    auto lease=std::make_shared<int>(1);std::weak_ptr<int> weak=lease;
    check(scheduler.push([&,lease](int64_t now){++firstSteps;sameOwner&=std::this_thread::get_id()==owner;if(!gpuReady)return Scheduler::Step{State::Pending,now+2};return Scheduler::Step{now<100?State::Pending:State::Complete,100};},[&]{++finished;}),"accept first leased GPU batch");
    lease.reset();scheduler.advance(0);
    check(firstSteps==1&&scheduler.wakeAt()==2&&!weak.expired(),"GPU-pending step yields immediately and retains lease");
    check(scheduler.push([&](int64_t){++secondRan;return Scheduler::Step{State::Complete};},[&]{++finished;}),"next batch accepted while current batch awaits GPU");
    check(!scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[]{})&&scheduler.occupancy()==2,"capacity includes both current and next batch");
    gpuReady=true;scheduler.advance(20);
    check(scheduler.wakeAt()==100&&secondRan==0,"deadline wait yields without presenting early or reordering batches");
    unsigned submission=0;++submission;
    check(submission==1&&finished==0,"owner remains available to submit work during future presentation deadline");
    scheduler.advance(100);
    check(finished==2&&secondRan==1&&weak.expired()&&scheduler.occupancy()==0&&sameOwner,"due work finishes on owner and releases leases exactly once");
    scheduler.advance(200);check(finished==2,"completed jobs cannot execute or finalize twice");
    unsigned cancelled=0;
    scheduler.push([](int64_t){return Scheduler::Step{State::Pending,1000};},[&]{++cancelled;});
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++cancelled;});
    scheduler.advance(300);scheduler.cancel();scheduler.cancel();
    check(cancelled==2&&scheduler.occupancy()==0&&scheduler.wakeAt()==0,"cancellation finalizes pending and queued batches exactly once without waiting");
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++finished;});scheduler.advance(400);
    check(finished==3,"new work accepted after cancellation");
    std::atomic<bool> rejected=false;std::thread foreign([&]{try{scheduler.advance(500);}catch(const std::logic_error&){rejected=true;}});foreign.join();
    check(rejected,"foreign thread cannot advance GPU owner state");
    unsigned failedFinished=0;
    scheduler.push([](int64_t){return Scheduler::Step{State::Failed};},[&]{++failedFinished;});
    scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[&]{++failedFinished;});scheduler.advance(600);
    check(scheduler.failed()&&failedFinished==2&&scheduler.occupancy()==0,"failure propagates and finalizes queued work");
    check(!scheduler.push([](int64_t){return Scheduler::Step{State::Complete};},[]{}),"failed scheduler rejects new work");
    veyra::engine::TimingWindow timing;for(unsigned i=0;i<100;++i)timing.add(i);check(timing.p95()==94,"percentile order statistic");timing.clear();timing.add(6);check(timing.p95()==6,"reset removes old timing window");
    veyra::engine::FgRecoveryBudget budget;
    {
        veyra::engine::FgRecoveryBudget prefix;
        prefix.complete(18,true,false,10000000,10);
        check(!prefix.admitFile(10000000,10070000,10250000,27778,0,0,3,false,0),"late first subframe rejects even when the last output fits");
        check(prefix.admitFile(10000000,10100000,10250000,27778,0,0,3,false,0),"first deadline uses prefix cost rather than full MFG group");
        check(!prefix.admitFile(10000000,10100000,10140000,27778,0,0,3,false,0),"last subframe also has to fit when later FG work is slower");
        check(!prefix.admitFile(10000000,10100000,10250000,27778,0,0,3,false,5),"pending previous batch cannot be mistaken for free first-frame headroom");
        check(!prefix.admitFile(10000000,10000000,10250000,27778,100,0,3,false,0),"CPU delay cannot erase future first interpolation work");
        prefix.complete(4,true,true,10000000);
        check(prefix.admitFile(10000000,9900000,10010000,27778,0,0,3,true,0),"history seed has no generated prefix to present");
    }
    {
        veyra::engine::FgRecoveryBudget queued;
        queued.complete(10,true,false,10000000,4);
        check(queued.admit(10000000,10020000,0,0),"idle GPU can fit the measured batch");
        check(!queued.admit(10000000,10020000,0,0,false,5),"uncompleted earlier work consumes the new pair deadline");
        check(queued.admit(10000000,10020000,5,0,false,5),"elapsed queue work is counted only once");
        check(!queued.admit(10000000,9920000,100,0,false,5),"CPU time cannot erase unsubmitted FG even with queued work");
        check(queued.admit(10000000,10020000,0,0,false,-5),"negative queue prediction cannot invent extra GPU work");
    }
    {
        using Window=veyra::engine::TimingWindow;
        const Window::Clock::time_point start{};
        Window present{std::chrono::seconds(1)};
        present.add(80,start);
        check(present.p95(start)==80,"temporary GPU blit stall is visible to admission");
        check(!budget.admit(10000000,10200000,0,present.p95(start)),"GPU blit stall limits FG admission");
        check(present.p95(start+std::chrono::seconds(1))==0,"Present stall expires without successful FG or new samples");
        present.add(2,start+std::chrono::seconds(1));
        check(budget.admit(20000000,20200000,0,present.p95(start+std::chrono::seconds(1))),"admission recovers after expired Present stall");
        present.clear();check(present.p95(start+std::chrono::seconds(1))==0,"new metrics window clears admission samples");
        budget.reset();
    }
    budget.fgCost(3,10000000);budget.complete(31,true,false,10000000);
    check(budget.processingAllowance(10000000,166667)==166667,"live processing allowance is bounded to one source interval");
    check(budget.predicted(10000000)&&std::abs(*budget.predicted(10000000)-31)<.001,"base and FG cost do not double count additional generation");
    check(!budget.admit(10000000,10100000,0,0),"insufficient pair deadline enters limited state");
    check(budget.admit(10100000,10600000,0,0)&&budget.recovering(),"fresh deadline admits warmup without a fixed cooldown");
    check(budget.admit(10266667,10766667,0,0)&&!budget.recovering(),"second consecutive source pair exits recovery");
    budget.complete(200,true,true,13000000,180);
    check(*budget.predicted(13000000)<32,"expensive reset warmup does not poison steady cost");
    budget.complete(std::nullopt,true,true,13000001,190);
    check(*budget.predicted(13000001)<32,"warmup FG timestamp without graph timestamp cannot poison steady cost");
    budget.complete(10,false,false,24000000);
    check(budget.processingAllowance(24000000,166667)==100000,"live deadline includes measured enhancement before interpolation cadence");
    check(*budget.predicted(24000000)<14,"old slow completion expires even when no FG succeeds");
    check(!budget.admit(24000000,23800000,0,0),"recovery never admits an already expired deadline");
    budget.reset();check(!budget.predicted(25000000),"settings revision reset clears prior backend costs");
    check(budget.processingAllowance(25000000,166667)==0,"unknown GPU work cannot invent a processing allowance");
    budget.fgCost(20,25000000);budget.complete(12,true,false,25000000,3);
    check(budget.predicted(25000000)==29,"subtract same-frame FG time instead of another frame's higher percentile");
    check(!budget.admit(25000000,25050000,100,0),"long CPU stall cannot discount FG that has not been submitted");
    budget.complete(std::nullopt,true,false,36000000);
    check(!budget.predicted(36000000),"missing GPU timing cannot retain a stale CPU polling cost");
    check(!budget.admit(36000000,35800000,0,0),"unknown cost still rejects expired presentation deadlines");
    for(unsigned multiplier:{2u,4u,6u}){
        budget.reset();const double fg=double(multiplier-1)*1.5;
        budget.complete(5+fg,true,false,40000000,fg);
        check(budget.predicted(40000000)==5+fg,"completion samples steady FG with its matching graph identity");
        for(int64_t t=40100000;t<42100000;t+=166667){
            budget.complete(200,true,true,t,190);
            check(budget.admit(t,t+166667,0,1),"repeated reset cost cannot trap healthy 2X/4X/6X pairs in rejection");
            budget.complete(5+fg,true,false,t+1,fg);
        }
        budget.complete(100,true,false,42200000,90);
        check(!budget.admit(42200000,42366667,0,1),"real steady GPU overload remains limited");
    }
    budget.reset();
    budget.complete(20.719,true,false,50000000,11);
    budget.complete(7,true,true,50000001,2);
    check(!budget.admit(50000002,50084552,1.217,.047),"observed 6X deadline rejects the full steady batch");
    check(budget.admit(50000002,50084552,1.217,.047,true),"measured single-call recovery fits without discounting steady MFG cost");
    check(budget.predicted(50000002)==20.719&&budget.predicted(50000002,true)==7,"recovery and steady costs remain independent");
    budget.complete(100,false,true,50000003);
    check(budget.predicted(50000003,true)==7,"rejected reset without FG cannot become a warmup timing sample");
    check(!budget.admit(50000002,49800002,1.217,.047,true),"cheap recovery cannot bypass an expired deadline");
    check(budget.predicted(60000002,true)==budget.predicted(60000002),"expired recovery measurement falls back to steady estimate");
    veyra::engine::LivePairLatency phase;
    check(phase.select(10000000,420000,333333,4)==420000,"unknown live readiness keeps legacy phase");
    for(int i=0;i<8;++i)phase.observe(10000000+i,370000);
    check(phase.select(10000008,420000,333333,4)==417500,"phase advance is gradual rather than a burst");
    for(int i=0;i<32;++i)phase.select(10000100+i,420000,333333,4);
    check(phase.select(10000200,420000,333333,4)==380000,"4X keeps measured readiness plus jitter margin");
    phase.observe(10000201,410000);
    check(phase.select(10000202,420000,333333,4)==420000,"slow batch immediately restores conservative phase");
    check(phase.select(21000000,420000,333333,4)==420000,"stale readiness cannot shorten a new pair");
    for(unsigned multiplier:{2u,4u,6u}){
        phase.reset();
        const int64_t interval=333667,ready=120000;
        const int64_t needed=ready+interval*(multiplier-1)/multiplier;
        for(int i=0;i<8;++i)phase.observe(30000000+i,needed);
        auto last=interval+90000;bool bounded=true;
        for(int i=0;i<80;++i){const auto delay=phase.select(30000100+i,interval+90000,interval,multiplier);
            bounded&=delay>=needed&&delay<=interval+90000&&last-delay<=2500;last=delay;}
        check(bounded,"2X/4X/6X preserves readiness and bounded phase at 29.97 Hz");
        phase.reset();check(phase.select(30001000,interval+90000,interval,multiplier)==interval+90000,"reset discards earlier readiness");
    }
    phase.observe(40000000,-1);phase.observe(40000000,30000000);
    check(phase.select(40000001,420000,333333,4)==420000,"invalid observations cannot train phase");
    return failures?1:0;
}
