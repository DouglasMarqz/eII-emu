#include "panel_report_text.h"
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() try {
    eii::host::PanelReportText view;
    std::string bytes(60000,char(0x90));
    require(view.serial(bytes,false),"initial quiet view");
    const auto revision=view.revision();
    require(view.text()==view.offMessage,"quiet view is constant");
    bytes.append(1000,char(0xff));
    require(!view.serial(bytes,false)&&view.revision()==revision,"history growth cannot revise quiet text");
    require(view.serial(bytes,true),"explicit live view");
    require(view.text().size()<2200,"live text bounded independently of total history");
    require(view.text().find("61000 captured bytes")!=std::string::npos,"total count retained");
    require(view.text().find("\\xff")!=std::string::npos,"live binary escaped");
    const auto liveRevision=view.revision();
    require(!view.serial(bytes,true)&&view.revision()==liveRevision,"unchanged history is cached");
    std::string report="FULL\n";report+=std::string(70000,char(0));report+="END\t";
    view.capture(report);
    require(view.text().size()==280009&&view.text().ends_with("END\t"),"manual capture full and untruncated");
    const auto captureRevision=view.revision();view.reset();
    require(view.revision()>captureRevision,"reset revision monotonic");
    bytes.assign(61000,'A');view.serial(bytes,true);
    require(view.text().ends_with(std::string(512,'A')),"new boot same-size history refreshes");
    view.diagnostic(std::string("x\r\n\t")+char(0x80));
    require(view.text()=="x\r\n\t\\x80","diagnostic escaping preserves text layout");
    std::cout<<"PASS: constant quiet report; bounded explicit serial; revision cache; full capture; reboot refresh.\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
