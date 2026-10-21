#ifndef OPENMW_MWLUA_STUDIOMIDI_H
#define OPENMW_MWLUA_STUDIOMIDI_H
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace MWLua
{
    // Main-thread draining; callbacks only copy short messages into bounded storage.
    struct StudioMidiQueue
    {
        struct Event { unsigned status, a, b; double time; };
        std::mutex mutex;
        std::array<Event, 1024> events{};
        unsigned count=0, dropped=0;
        std::atomic<bool> connected{true};
        void push(unsigned bytes, double time)
        {
            const unsigned status=bytes&255, a=(bytes>>8)&255, b=(bytes>>16)&255;
            if (status<0x80 || status>=0xf0 || a>127 || b>127) return;
            std::lock_guard<std::mutex> lock(mutex);
            if (count==events.size()) { ++dropped;return; }
            events[count++]={status,a,b,time};
        }
        void fault() {std::lock_guard<std::mutex> lock(mutex);++dropped;}
        std::vector<Event> drain(unsigned& lost)
        {
            std::lock_guard<std::mutex> lock(mutex);
            lost=dropped;dropped=0;
            std::vector<Event> result;
            if (!lost) result.assign(events.begin(),events.begin()+count);
            count=0;return result;
        }
    };
    class StudioMidiInput
    {
    public:
        struct Device { unsigned id;std::string name; };
        static double now() {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
        std::unique_ptr<StudioMidiQueue> queue=std::make_unique<StudioMidiQueue>();
        double started=0;
        StudioMidiInput()=default;
        StudioMidiInput(const StudioMidiInput&)=delete;
        StudioMidiInput& operator=(const StudioMidiInput&)=delete;
        ~StudioMidiInput() {close();}
#ifdef _WIN32
    private:
        struct Api
        {
            HMODULE module=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
#define STUDIO_MIDI_PROC(name) decltype(&name) name##Fn=module ? reinterpret_cast<decltype(&name)>(GetProcAddress(module,#name)) : nullptr
            STUDIO_MIDI_PROC(midiInGetNumDevs);STUDIO_MIDI_PROC(midiInGetDevCapsW);STUDIO_MIDI_PROC(midiInOpen);
            STUDIO_MIDI_PROC(midiInStart);STUDIO_MIDI_PROC(midiInStop);STUDIO_MIDI_PROC(midiInReset);
            STUDIO_MIDI_PROC(midiInClose);STUDIO_MIDI_PROC(midiInGetID);
#undef STUDIO_MIDI_PROC
            bool valid() const {return midiInGetNumDevsFn && midiInGetDevCapsWFn && midiInOpenFn && midiInStartFn
                && midiInStopFn && midiInResetFn && midiInCloseFn && midiInGetIDFn;}
        };
        static Api& api() {static Api value;return value;}
        inline static std::atomic<bool> sClaimed{false};
        HMIDIIN mHandle=nullptr;
        static void CALLBACK callback(HMIDIIN,UINT message,DWORD_PTR instance,DWORD_PTR bytes,DWORD_PTR milliseconds)
        {
            auto* q=reinterpret_cast<StudioMidiQueue*>(instance);
            if (message==MIM_DATA || message==MIM_MOREDATA) q->push(static_cast<unsigned>(bytes),milliseconds/1000.0);
            else if (message==MIM_ERROR || message==MIM_LONGERROR) q->fault();
            else if (message==MIM_CLOSE) q->connected=false;
        }
    public:
        static bool available() {return api().valid();}
        static std::vector<Device> devices()
        {
            std::vector<Device> out;if (!available()) return out;
            for (unsigned id=0;id<std::min(128u,api().midiInGetNumDevsFn());++id)
            {
                MIDIINCAPSW caps{};if (api().midiInGetDevCapsWFn(id,&caps,sizeof(caps))!=MMSYSERR_NOERROR) continue;
                const int size=WideCharToMultiByte(CP_UTF8,0,caps.szPname,-1,nullptr,0,nullptr,nullptr);
                if (size<=1) continue;
                std::string name(size,'\0');WideCharToMultiByte(CP_UTF8,0,caps.szPname,-1,name.data(),size,nullptr,nullptr);name.pop_back();
                out.push_back({id,std::move(name)});
            }
            return out;
        }
        static std::shared_ptr<StudioMidiInput> open(unsigned id)
        {
            if (!available() || id>=api().midiInGetNumDevsFn()) throw std::runtime_error("MIDI input device is unavailable; refresh the device list.");
            if (sClaimed.exchange(true)) throw std::runtime_error("A MIDI input is already open, or its driver failed to close; restart the client if necessary.");
            auto input=std::make_shared<StudioMidiInput>();
            const auto result=api().midiInOpenFn(&input->mHandle,id,reinterpret_cast<DWORD_PTR>(&callback),reinterpret_cast<DWORD_PTR>(input->queue.get()),CALLBACK_FUNCTION|MIDI_IO_STATUS);
            if (result!=MMSYSERR_NOERROR) {sClaimed=false;throw std::runtime_error("MIDI input could not be opened (driver error "+std::to_string(result)+").");}
            input->started=now();
            if (api().midiInStartFn(input->mHandle)!=MMSYSERR_NOERROR) {input->close();throw std::runtime_error("MIDI input could not be started.");}
            return input;
        }
        bool connected() const {UINT id=0;return mHandle && queue && queue->connected && api().midiInGetIDFn(mHandle,&id)==MMSYSERR_NOERROR;}
        void close()
        {
            if (!mHandle) return;
            queue->connected=false;api().midiInStopFn(mHandle);api().midiInResetFn(mHandle);
            const auto result=api().midiInCloseFn(mHandle);mHandle=nullptr;
            if (result==MMSYSERR_NOERROR) sClaimed=false;
            else
            {
                // A broken/unplugged driver may retain the callback. Keep its one
                // bounded queue alive and the process lease claimed, preventing UAF.
                (void)queue.release();
            }
        }
#else
        static bool available() {return false;}
        static std::vector<Device> devices() {return {};}
        static std::shared_ptr<StudioMidiInput> open(unsigned) {throw std::runtime_error("Hardware MIDI input currently requires Windows.");}
        bool connected() const {return false;}
        void close() {}
#endif
    };
}
#endif
