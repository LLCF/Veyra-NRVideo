#include "veyra/engine/LiveGpuScheduler.h"
#include "veyra/engine/TimingWindow.h"
#include "veyra/engine/FgRecoveryBudget.h"
#include "veyra/engine/LivePairLatency.h"
#include <iostream>
#include <memory>
#include <atomic>
int main(){
    using Scheduler=veyra::engine::LiveGpuScheduler;using State=Scheduler::State;
    int failures=0;auto check=[&](bool pass,const char* text){std::cout<<(pass?"PASS ":"FAIL ")<<text<<'\n';failures+=!pass;};
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
        using Window=veyra::engine::TimingWindow;
        const Window::Clock::time_point start{};
        Window present{std::chrono::seconds(1)};
        present.add(80,start);
        check(present.p95(start)==80,"temporary Present stall is visible to admission");
        check(!budget.admit(10000000,10200000,0,present.p95(start)),"stall limits FG admission");
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
    budget.complete(200,true,true,13000000);
    check(*budget.predicted(13000000)<32,"expensive reset warmup does not poison steady cost");
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
