// SPDX-License-Identifier: AGPL-3.0-only
// Offline driver for Monomodule's public host/DSP APIs. No firmware bytes or
// hardware access. Its output includes the original track chain (not raw OSC).
#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "MonoVoice.h"
#include "util/Wav.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
static LONG WINAPI report_native_fault(EXCEPTION_POINTERS* e)
{
    MEMORY_BASIC_INFORMATION memory{};char module[MAX_PATH]={};
    const auto address=e->ExceptionRecord->ExceptionAddress;
    VirtualQuery(address,&memory,sizeof(memory));
    GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase),module,MAX_PATH);
    std::fprintf(stderr,"Native reference fault 0x%08lx at %p (%s + 0x%llx)\n",
                 e->ExceptionRecord->ExceptionCode,address,module,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(memory.AllocationBase)));
    std::fflush(stderr);return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static std::array<int,8> page(const std::string& text)
{
    std::array<int,8> out{};std::stringstream in(text);std::string part;
    for(auto& value:out){
        if(!std::getline(in,part,','))throw std::runtime_error("eight page values required");
        value=std::stoi(part);
        if(value<0 || value>127)throw std::runtime_error("parameter out of range");
    }
    if(std::getline(in,part,','))throw std::runtime_error("too many page values");
    return out;
}
int main(int argc,char** argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(report_native_fault);
#endif
    try{
        std::string fwPath,outPath,eventsText="0:60";
        int machine=1,frames=44100,dist=64;
        std::array<int,8> syn={0,0,0,0,0,0,0,64};
        for(int i=1;i<argc;i++){
            const std::string key=argv[i];
            if(i+1==argc)throw std::runtime_error("missing option value");
            const std::string value=argv[++i];
            if(key=="--fw")fwPath=value;
            else if(key=="--out")outPath=value;
            else if(key=="--machine")machine=std::stoi(value);
            else if(key=="--frames")frames=std::stoi(value);
            else if(key=="--dist")dist=std::stoi(value);
            else if(key=="--events")eventsText=value;
            else if(key=="--syn")syn=page(value);
            else throw std::runtime_error("unknown option: "+key);
        }
        if(fwPath.empty() || outPath.empty() || frames<16 || frames>44100*60 || dist<0 || dist>127)
            throw std::runtime_error("--fw OS.syx --out FILE.wav --frames N --events frame:note,... [--machine N --syn a,b,c,d,e,f,g,h]");
        const auto kind=static_cast<mnm::host::Machine>(machine);
        if(!mnm::host::machineDef(kind))throw std::runtime_error("unknown machine");
        struct Event {int frame,note;};std::vector<Event> events;
        std::stringstream list(eventsText);std::string item;
        while(std::getline(list,item,',')){
            const auto colon=item.find(':');
            if(colon==std::string::npos)throw std::runtime_error("event must be frame:note");
            Event e{std::stoi(item.substr(0,colon)),std::stoi(item.substr(colon+1))};
            if(e.frame<0 || e.frame>=frames || e.frame%16 || e.note<0 || e.note>127 ||
               (!events.empty() && e.frame<=events.back().frame))throw std::runtime_error("invalid event order, block alignment or note");
            events.push_back(e);
        }
        if(events.empty())throw std::runtime_error("no events");
        std::cerr<<"Loading OS\n"<<std::flush;
        const auto fw=mnm::fw::loadFirmware(fwPath);
        if(fw.version.find("1.32B")==std::string::npos)throw std::runtime_error("this reference requires OS 1.32B");
        std::cerr<<"Initialising DSP/JIT\n"<<std::flush;
        mnm::MonoVoice voice(fw);auto& host=voice.host();host.setMachine(kind);
        // Sustained AMP, open filters without envelope/resonance, neutral EQ,
        // SRR off and zero delay send. Pan and volume retain the original laws.
        const std::array<int,8> amp={0,127,127,0,dist,64,64,0};
        const std::array<int,8> filt={0,127,0,0,0,0,64,64};
        const std::array<int,8> efx={64,64,0,64,64,0,0,127};
        for(int k=0;k<8;k++){
            host.setParam(mnm::host::Page::SYN,k,syn[k]);
            host.setParam(mnm::host::Page::AMP,k,amp[k]);
            host.setParam(mnm::host::Page::FILT,k,filt[k]);
            host.setParam(mnm::host::Page::EFX,k,efx[k]);
        }
        host.setLevel(100);host.setMasterTuneHz(440);host.setBpm(120);
        voice.warmUp(8);
        std::cerr<<"Rendering original DSP\n"<<std::flush;
        std::vector<int32_t> pcm;pcm.reserve(size_t(frames+16)*2);
        std::array<int32_t,32> block;size_t event=0;
        for(int frame=0;frame<frames;frame+=16){
            if(event<events.size() && events[event].frame==frame){
                host.noteOn(events[event].note);event++;
            }
            const auto& packet=host.nextBlock();
            if(!voice.engine().renderBlock(packet.w.data(),block))
                throw std::runtime_error(voice.engine().faultReason());
            pcm.insert(pcm.end(),block.begin(),block.end());
        }
        pcm.resize(size_t(frames)*2);
        if(!mnm::util::writeWav24(outPath,pcm,2,44100))throw std::runtime_error("cannot write output WAV");
        std::cout<<"OS "<<fw.version<<"; machine "<<mnm::host::machineDef(kind)->name
                 <<"; "<<frames<<" frames; "<<event<<" scheduled note events; original track-chain output\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"reference error: "<<e.what()<<'\n';return 1;}
}
