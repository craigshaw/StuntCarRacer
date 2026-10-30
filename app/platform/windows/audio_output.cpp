#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "platform/windows/audio_output.h"
#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace scr::runtime {
namespace {
void checked(MMRESULT result,const char* operation){
    if(result==MMSYSERR_NOERROR)return;
    char message[256]{};waveOutGetErrorTextA(result,message,sizeof(message));
    throw std::runtime_error(std::string(operation)+": "+message);
}
}
struct WindowsAudioOutput::Impl {
    mutable std::mutex mutex;
    std::unique_ptr<GameAudio> audio;
    std::thread worker;
    HANDLE changed{},completed{};HWAVEOUT device{};
    bool playing{},muted{},stopping{},flush{};
    std::string failure;
    std::uint64_t completions{};
    struct Buffer {std::array<std::int16_t,960> pcm{};WAVEHDR header{};bool queued{};};
    std::array<Buffer,4> buffers;
    explicit Impl(const GameAudioSeed& seed):audio(std::make_unique<GameAudio>(seed)){
        changed=CreateEventW(nullptr,FALSE,FALSE,nullptr);completed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!changed||!completed){if(changed)CloseHandle(changed);if(completed)CloseHandle(completed);throw std::runtime_error("Cannot create audio events");}
        worker=std::thread([this]{run();});
    }
    ~Impl(){
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;SetEvent(changed);}
        worker.join();CloseHandle(changed);CloseHandle(completed);
    }
    void run(){
        try{
            WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=48000;
            format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
            checked(waveOutOpen(&device,WAVE_MAPPER,&format,reinterpret_cast<DWORD_PTR>(completed),0,CALLBACK_EVENT),"Open audio output");
            for(auto& buffer:buffers){buffer.header.lpData=reinterpret_cast<LPSTR>(buffer.pcm.data());buffer.header.dwBufferLength=DWORD(buffer.pcm.size()*sizeof(std::int16_t));
                checked(waveOutPrepareHeader(device,&buffer.header,sizeof(WAVEHDR)),"Prepare audio buffer");}
            HANDLE events[2]{changed,completed};
            for(;;){
                // Reset before checking every header, so a concurrent completion
                // either appears in this scan or leaves the event signalled.
                ResetEvent(completed);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if(stopping)break;
                    if(flush){checked(waveOutReset(device),"Flush audio output");for(auto& b:buffers)b.queued=false;flush=false;}
                    for(auto& buffer:buffers){
                        if(buffer.queued&&(buffer.header.dwFlags&WHDR_DONE)){buffer.queued=false;++completions;}
                        if(!buffer.queued&&playing){
                            auto pcm=audio->renderFrames(480);
                            if(muted)buffer.pcm.fill(0);else std::copy(pcm.begin(),pcm.end(),buffer.pcm.begin());
                            checked(waveOutWrite(device,&buffer.header,sizeof(WAVEHDR)),"Write audio buffer");buffer.queued=true;
                        }
                    }
                }
                if(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_FAILED)throw std::runtime_error("Audio event wait failed");
            }
        }catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);failure=e.what();}
        if(device){waveOutReset(device);for(auto& b:buffers)if(b.header.dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(device,&b.header,sizeof(WAVEHDR));waveOutClose(device);device=nullptr;}
    }
};
WindowsAudioOutput::WindowsAudioOutput(const GameAudioSeed& seed):impl_(std::make_unique<Impl>(seed)){}
WindowsAudioOutput::~WindowsAudioOutput()=default;
void WindowsAudioOutput::submit(const GameAudioFrame& frame){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->audio->submit(frame);}
void WindowsAudioOutput::reset(const GameAudioSeed& seed){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->audio=std::make_unique<GameAudio>(seed);impl_->flush=true;SetEvent(impl_->changed);}
void WindowsAudioOutput::setPlaying(bool value){std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->playing!=value){impl_->playing=value;impl_->flush=true;SetEvent(impl_->changed);}}
void WindowsAudioOutput::setMuted(bool value){std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->muted!=value){impl_->muted=value;impl_->flush=true;SetEvent(impl_->changed);}}
std::string WindowsAudioOutput::error()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->failure;}
std::uint64_t WindowsAudioOutput::completedBuffers()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->completions;}
} // namespace scr::runtime
