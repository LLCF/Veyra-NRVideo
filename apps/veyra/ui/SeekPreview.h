// A single in-flight preview plus the latest pointer target. Waiting for the
// presented frame prevents rapid mouse events from cancelling every decode.
struct SeekPreview {
    bool active=false,released=false,resume=false;
    uint64_t session=0;
    double target=-1,submitted=-1;
    void update(double seconds,bool release) {
        auto s=engine.snapshot();
        if(!s.running||s.capture||s.image||s.duration<=0)return;
        if(!active||session!=s.sessionId){
            *this={};active=true;session=s.sessionId;
            resume=s.transport==veyra::engine::TransportState::Playing;
            engine.pause(true);
        }
        target=seconds;released=release;
        // Mouse release supersedes even an outstanding preview: the final
        // exact target must never be dropped behind a busy preview.
        if(release&&target!=submitted){engine.seek(target);submitted=target;}
        else tick();
    }
    void tick() {
        if(!active)return;
        const auto s=engine.snapshot();
        if(s.sessionId!=session||!s.running||s.failed){*this={};return;}
        if(s.seekRequested!=s.seekPresented)return;
        if(target!=submitted){engine.seek(target);submitted=target;return;}
        if(released){const bool play=resume;*this={};engine.pause(!play);}
    }
} seekPreview;
