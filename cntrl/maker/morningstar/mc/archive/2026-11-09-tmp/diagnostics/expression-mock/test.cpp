#include "expression_pedals.h"
#include <cassert>
#include <vector>
int bias, raw=-1;
void pinMode(int pin,int mode) { assert(pin==26);bias=mode; }
int analogRead(int pin) { assert(pin==26);return raw<0 ? (bias==INPUT_PULLUP ? 1023 : 0) : raw; }
int main() {
    cntrl::UtilityPageSettings settings;
    auto &a=settings.expression[0];std::strcpy(a.output,"din");a.channel=7;a.controller=11;
    uint32_t now=0;bool output=true,ready=true;std::vector<int> writes;
    auto run=[&](ExpressionPedals &p,int ms) {
        while(ms--) p.poll(now++, [](unsigned j){return j ? -1 : 26;}, [&](const char*){return output;}, [&](const char*out,const uint8_t*msg){
            assert(!std::strcmp(out,"din") && msg[0]==0xb6 && msg[1]==11);
            if(ready) writes.push_back(msg[2]); return ready;
        });
    };
    ExpressionPedals p;p.configure(settings);run(p,60);raw=120;run(p,300);
    assert(writes.empty());
    p.calibrate(true);run(p,300);p.calibrate(true);
    for (raw=160;raw<920;raw+=40) run(p,12);raw=920;run(p,400);p.calibrate(true);run(p,400);
    char t[40],v[40],m[40]; p.status(true,t,v,m); std::fprintf(stderr,"writes=%zu bytes=%zu status %s, %s, %s\n",writes.size(),SD.sdfs.data.size(),t,v,m);
    assert(writes.empty() && SD.sdfs.data.size()==12);
    ExpressionPedals reboot;reboot.load_calibrations();reboot.configure(settings);run(reboot,400);
    assert(writes.empty());
    for(raw=880;raw>80;raw-=40) run(reboot,12);raw=80;run(reboot,400);assert(!writes.empty() && writes.back()==0);
    auto count=writes.size();raw=-1;run(reboot,400);raw=920;run(reboot,400);
    assert(writes.size()==count);
    ready=false;for(raw=880;raw>80;raw-=40) run(reboot,12);raw=80;run(reboot,400);for(raw=120;raw<480;raw+=40) run(reboot,12);raw=480;run(reboot,400);ready=true;run(reboot,12);
    assert(writes.size()==count+1 && writes.back()>50 && writes.back()<65);
    output=false;raw=920;run(reboot,400);output=true;run(reboot,400);
    assert(writes.size()==count+1);
}
