#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include "persistence/hall_table_codec.h"
#include "platform/windows/shared_hall_storage.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace {
using namespace scr;using namespace scr::runtime;namespace fs=std::filesystem;
constexpr HallCatalog catalog{0x123456789abcdef0ULL,1};
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
std::string currentHallUserSid(){
    HANDLE token{};require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=0,"Cannot open proof identity");
    DWORD size{};GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> bytes(size);
    const bool okay=GetTokenInformation(token,TokenUser,bytes.data(),size,&size)!=0;CloseHandle(token);require(okay,"Cannot read proof identity");
    LPSTR sid{};require(ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid)!=0,"Cannot format proof identity");
    const std::string result=sid;LocalFree(sid);return result;
}
std::wstring executable(){std::wstring path(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));require(n&&n<path.size(),"Cannot locate proof executable");path.resize(n);return path;}
std::string executableHash(){
    std::ifstream file(fs::path(executable()),std::ios::binary);require(bool(file),"Cannot read proof executable");
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Cannot open proof hash");
    auto status=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);std::array<char,16384> block{};
    while(status>=0&&file){file.read(block.data(),block.size());const auto n=file.gcount();if(n)status=BCryptHashData(hash,reinterpret_cast<PUCHAR>(block.data()),ULONG(n),0);}
    std::array<UCHAR,32> bytes{};if(status>=0)status=BCryptFinishHash(hash,bytes.data(),ULONG(bytes.size()),0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);require(status>=0,"Cannot hash proof executable");
    std::ostringstream out;for(auto b:bytes)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return out.str();
}
void textFile(const fs::path& path,const std::string& value){std::ofstream out(path,std::ios::binary);out<<value;require(bool(out),"Cannot write proof receipt");}
std::string readText(const fs::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"Cannot read proof receipt");return {std::istreambuf_iterator<char>(in),{}};}
void checkRoot(const fs::path& root){require(readText(root/L"proof-marker.txt")=="SCR Hall storage proof 1","This is not a disposable Hall proof directory");}
HallOfFame load(const fs::path& root){
    const auto path=root/L"index.scr-hall";if(!fs::exists(path))return HallOfFame(catalog);
    require(fs::file_size(path)<=hallTableMaximumBytes,"Proof index exceeds limit");std::ifstream in(path,std::ios::binary);require(bool(in),"Cannot read proof index");
    return decodeHallTable({std::istreambuf_iterator<char>(in),{}},catalog);
}
void publish(const fs::path& root,const HallOfFame& hall){
    const auto bytes=encodeHallTable(hall);const auto temporary=root/(L"index.tmp."+std::to_wstring(GetCurrentProcessId()));
    const auto h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);require(h!=INVALID_HANDLE_VALUE,"Cannot create proof index");
    DWORD count{};const bool written=WriteFile(h,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(h);CloseHandle(h);require(written,"Cannot flush proof index");
    require(MoveFileExW(temporary.c_str(),(root/L"index.scr-hall").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot publish proof index");
}
HallAttempt attempt(unsigned worker,unsigned n){
    HallAttempt a;const auto id=worker*100+n+1;for(unsigned b=0;b<4;++b)a.id[b]=std::uint8_t(id>>(b*8));
    a.name=worker==1?"Writer A":"Writer B";const auto sub=n*2+(worker-1);a.time={0,0x40,std::uint8_t(((sub/10)<<4)|(sub%10))};return a;
}
void writeAttempts(const fs::path& root,unsigned worker){
    checkRoot(root);
    for(unsigned n=0;n<10;++n){SharedHallLock lock(root);auto hall=load(root);const auto a=attempt(worker,n);const auto first=hall.offer(a);
        require(first==HallAdmission::Accepted||first==HallAdmission::Duplicate,"Proof attempt missed its board");
        require(hall.offer(a)==HallAdmission::Duplicate,"Proof retry added a duplicate");publish(root,hall);
        std::this_thread::yield();
    }
    textFile(root/(L"writer-"+std::to_wstring(worker)+L".json"),"{\"status\":\"passed\",\"sid\":\""+currentHallUserSid()+"\",\"executable_sha256\":\""+executableHash()+"\"}\n");
}
void verify(const fs::path& root){
    SharedHallLock lock(root);const auto hall=load(root);require(hall.entries({}).size()==20,"Competing writers lost an attempt");
    for(unsigned n=0;n<20;++n){const auto& entry=hall.entries({})[n];const auto worker=n%2+1;
        require(entry.attempt.id==attempt(worker,n/2).id,"Competing writer ordering/content differs");require(hall.rank({},n)==n+1,"Process proof rank differs");}
    require(sharedHallUsersAccess(root/L"index.scr-hall"),"Published index did not inherit shared access");
}
struct Process {
    HANDLE handle{};
    ~Process(){if(handle){DWORD code{};if(GetExitCodeProcess(handle,&code)&&code==STILL_ACTIVE){TerminateProcess(handle,3);WaitForSingleObject(handle,5000);}CloseHandle(handle);}}
    void start(std::wstring args){const auto exe=executable();std::wstring command=L"\""+exe+L"\" "+args;STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
        require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=0,"Cannot launch proof child");handle=pi.hProcess;CloseHandle(pi.hThread);}
    void wait(){require(WaitForSingleObject(handle,15000)==WAIT_OBJECT_0,"Proof child timed out");DWORD code{};require(GetExitCodeProcess(handle,&code)&&code==0,"Proof child failed");}
};
std::wstring quoted(const fs::path& path){return L"\""+fs::absolute(path).wstring()+L"\"";}
void initialise(const fs::path& root){require(!fs::exists(root),"Proof output already exists; use a new disposable directory");prepareSharedHallDirectory(root);textFile(root/L"proof-marker.txt","SCR Hall storage proof 1");}
void processProof(const fs::path& root){
    initialise(root);require(sharedHallUsersAccess(root),"Shared Users permissions are missing");
    Process a,b;a.start(L"--worker "+quoted(root)+L" 1");b.start(L"--worker "+quoted(root)+L" 2");a.wait();b.wait();verify(root);
    // A dead writer must release its OS lock. Kill only the child we created.
    Process holder;holder.start(L"--hold "+quoted(root));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!fs::exists(root/L"lock-held.txt")&&std::chrono::steady_clock::now()<deadline)Sleep(5);
    require(fs::exists(root/L"lock-held.txt"),"Lock holder did not become ready");
    bool blocked=false;try{SharedHallLock locked(root,std::chrono::milliseconds(30));}catch(const std::exception&){blocked=true;}require(blocked,"Separate process did not exclude competing writer");
    require(TerminateProcess(holder.handle,77)!=0,"Cannot terminate owned lock-holder proof child");require(WaitForSingleObject(holder.handle,5000)==WAIT_OBJECT_0,"Owned proof child did not exit");
    {SharedHallLock released(root,std::chrono::seconds(2));}verify(root);
    textFile(root/L"results.json","{\"status\":\"passed\",\"writers\":2,\"entries\":20,\"lost_updates\":0,\"retry_duplicates\":0,\"dead_process_lock_released\":true,\"real_second_account_tested\":false,\"executable_sha256\":\""+executableHash()+"\"}\n");
}
}
int wmain(int argc,wchar_t** argv)try{
    require(argc>=3,"Usage: native_hall_storage_proof --proof|--prepare-account|--account-worker|--worker|--hold disposable-root [worker]");
    const std::wstring mode=argv[1];const auto root=fs::absolute(argv[2]);
    if(mode==L"--proof"){require(argc==3,"Invalid proof arguments");processProof(root);}
    else if(mode==L"--prepare-account"){require(argc==3,"Invalid account proof arguments");initialise(root);textFile(root/L"first-user.sid",currentHallUserSid());writeAttempts(root,1);}
    else if(mode==L"--account-worker"){
        require(argc==3,"Invalid account worker arguments");checkRoot(root);const auto first=readText(root/L"first-user.sid"),second=currentHallUserSid();
        require(first!=second,"Run this step from a different Windows account; no account change has been made");writeAttempts(root,2);verify(root);
        textFile(root/L"account-results.json","{\"status\":\"passed\",\"first_sid\":\""+first+"\",\"second_sid\":\""+second+"\",\"real_second_account_tested\":true,\"entries\":20,\"executable_sha256\":\""+executableHash()+"\"}\n");
    }else if(mode==L"--worker"){require(argc==4,"Invalid worker arguments");const auto worker=std::stoul(argv[3]);require(worker==1||worker==2,"Invalid proof writer");writeAttempts(root,unsigned(worker));}
    else if(mode==L"--hold"){require(argc==3,"Invalid holder arguments");checkRoot(root);SharedHallLock lock(root);textFile(root/L"lock-held.txt","ready");Sleep(30000);}
    else throw std::runtime_error("Unknown Hall proof mode");
    std::cout<<"Hall storage proof step passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
