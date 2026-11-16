// EII+ 1 MB option: two 512 KB banks selected by SIO channel B /RTS (WR5 D1).
// Optional argument: a prepared ROM folder; then the real main boot ROM must
// detect the second bank (20FFh = 60h) and a 512 KB machine must not (22h).
// argv[2]: a bank floppy (needs main21dm.bin = main boot 2.1 DM in the folder;
// plain 2.1 has FFh at the 0FDBh swap vector). argv[3]: a raw HDD image
// (main23.bin).
#include "machine.h"
#include "floppy_hfe.h"
#include "hdd_image_file.h"
#include <filesystem>
#include <iostream>
#include <memory>
#include <algorithm>
#include <array>
#include <stdexcept>
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static void wr5(eii::Machine& m,std::uint8_t v){m.output(m.main,0x3b,5);m.output(m.main,0x3b,v);}
int main(int argc,char** argv)try{
    {
        // Machine is large (PC histograms): heap, not the 1 MB default stack.
        auto plainP=std::make_unique<eii::Machine>(0);auto& plain=*plainP;plain.initializeDramPowerOn(1);
        auto bigP=std::make_unique<eii::Machine>(0);auto& big=*bigP;big.installSecondBank(true);big.initializeDramPowerOn(1);
        check(plain.memory==big.memory&&plain.dramKnown==big.dramKnown&&plain.dramUnderlay==big.dramUnderlay,"bank A keeps the 512 KB power-on contents");
        check(!big.rtsBankSelected()&&big.bankSwaps==0,"reset state: RTS off");
        const auto a=big.memory;const auto aUnder=big.dramUnderlay;const std::uint8_t sram=big.memory[0x20ff];
        wr5(big,0x62);check(big.rtsBankSelected()&&big.bankSwaps==1,"WR5 D1 set selects the other bank");
        check(big.memory!=a&&big.dramUnderlay!=aUnder,"other bank has its own contents (DRAM window and underlay)");
        check(big.memory[0x20ff]==sram,"SRAM 2000h-27FFh is not banked");
        big.memory[0x2800]^=0xa5;big.dramUnderlay[0x100]^=0x5a;const auto b=big.memory;const auto bUnder=big.dramUnderlay;
        wr5(big,0x62);check(big.bankSwaps==1,"rewriting the same RTS state does not swap");
        wr5(big,0x60);check(!big.rtsBankSelected()&&big.memory==a&&big.dramUnderlay==aUnder,"back to bank A unchanged");
        wr5(big,0x62);check(big.memory==b&&big.dramUnderlay==bUnder,"bank B retained its writes");
        auto smallP=std::make_unique<eii::Machine>(0);auto& small=*smallP;small.initializeDramPowerOn(1);const auto before=small.memory;
        wr5(small,0x62);wr5(small,0x60);check(small.memory==before&&small.bankSwaps==0,"512 KB machine ignores /RTSB");
        bool threw=false;try{auto late=std::make_unique<eii::Machine>(0);late->run(4);late->installSecondBank(true);}catch(const std::logic_error&){threw=true;}
        check(threw,"memory size is fixed after the first tick");
    }
    std::cout<<"PASS bank swap model: bank A = 512 KB contents, SRAM unbanked, retained banks, 512 KB ignores RTS\n";
    if(argc>1){
        const std::filesystem::path f=std::filesystem::u8path(argv[1]);
        for(bool big:{false,true}){
            auto m=std::make_unique<eii::Machine>(0);
            m->load(f/"main.bin",f/"scanner.bin",f/"ic135.bin",f/"ic137.bin");
            m->installSecondBank(big);m->initializeDramPowerOn(1);m->enableProductionController();
            m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
            m->run(4000000); // main boot memory probe runs in the first second
            const unsigned flag=m->memory[0x20ff];
            std::cout<<"BOOT "<<(big?"1 MB":"512 KB")<<": 20FFh="<<std::hex<<flag<<std::dec<<" swaps="<<m->bankSwaps<<" rts="<<m->rtsBankSelected()<<"\n";
            if(big)check(flag==0x60&&!m->rtsBankSelected(),"main boot detects the second bank and runs in the RTS-off bank");
            else check(flag==0x22,"main boot reports no second bank on 512 KB");
        }
        std::cout<<"PASS real main boot ROM memory probe\n";
    }
    if(argc>2){ // full OS boot from a bank disk with 1 MB; a note must sound
        const std::filesystem::path f=std::filesystem::u8path(argv[1]);
        auto m=std::make_unique<eii::Machine>(0);
        m->channelTimers.enableControllerTiming();m->channelTimers.controllerTiming.enableRegisterWrites();
        m->load(f/"main21dm.bin",f/"scanner.bin",f/"ic135.bin",f/"ic137.bin");
        m->installSecondBank(true);m->initializeDramPowerOn(1);m->enableProductionController();
        m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
        auto bytes=eii::readFile(std::filesystem::u8path(argv[2]));
        if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
        m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
        m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
        m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
        for(unsigned i=0;i<250&&!m->lcd.line(0).starts_with("P");++i)m->run(4000000);
        m->run(4000000);
        std::cout<<"OS BOOT 1 MB: lcd='"<<m->lcd.line(0)<<"' swaps="<<m->bankSwaps<<" rts="<<m->rtsBankSelected()<<"\n";
        check(m->lcd.line(0).starts_with("P"),"OS boots with 1 MB");
        const std::array<std::uint8_t,3> on{0x90,60,110};m->receiveMidi(on);int peak=0;
        for(unsigned k=0;k<300;++k){m->run(4000);for(unsigned ch=0;ch<8;++ch)peak=std::max<int>(peak,m->controlVoltage.held[0][ch].code);}
        check(peak>40,"a note sounds from bank A with 1 MB");
        std::cout<<"PASS OS boot and note with 1 MB (amplitude CV peak "<<peak<<")\n";
    }
    if(argc>3){ // HD boot (main 2.3, OS 3.1 HD) with 1 MB
        const std::filesystem::path f=std::filesystem::u8path(argv[1]);
        auto m=std::make_unique<eii::Machine>(0);
        m->channelTimers.enableControllerTiming();m->channelTimers.controllerTiming.enableRegisterWrites();
        m->load(f/"main23.bin",f/"scanner.bin",f/"ic135.bin",f/"ic137.bin");
        m->installSecondBank(true);m->initializeDramPowerOn(1);m->enableProductionController();
        m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
        m->installHardDisk(eii::host::loadHddImage(std::filesystem::u8path(argv[3])));
        for(unsigned i=0;i<120&&!m->lcd.line(0).starts_with("P");++i)m->run(4000000);
        std::cout<<"HD BOOT 1 MB: lcd='"<<m->lcd.line(0)<<"' swaps="<<m->bankSwaps<<" 20FFh="<<std::hex<<unsigned(m->memory[0x20ff])<<std::dec<<"\n";
        check(m->lcd.line(0).starts_with("P")&&m->bankSwaps>0&&m->memory[0x20ff]==0x60,"HD OS boots with 1 MB and uses the bank swap");
        std::cout<<"PASS HD boot with 1 MB\n";
    }
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
