#pragma once
#include "filter_calibration.h"
namespace eii::host {
struct FilterServiceProfiles {
    analog::FilterCalibration ssm,legacy,eiii,ssm2;
    bool hasEiii=true,hasSsm2=true;
    bool operator==(const FilterServiceProfiles&)const=default;
    std::string serialize()const {
        ssm.validate();legacy.validate();eiii.validate();ssm2.validate();
        return "EII_FILTER_SERVICE_GLOBAL=4\n[SSM2045]\n"+ssm.serialize()+"[LEGACY]\n"+legacy.serialize()+"[EIII]\n"+eiii.serialize()+"[SSM2045M2]\n"+ssm2.serialize();
    }
    static FilterServiceProfiles parse(const std::string& text) {
        // =4: same layout as =3, written with the common level model (level_model.h).
        const std::string v1="EII_FILTER_SERVICE_GLOBAL=1\n[SSM2045]\n",v2="EII_FILTER_SERVICE_GLOBAL=2\n[SSM2045]\n",v3="EII_FILTER_SERVICE_GLOBAL=3\n[SSM2045]\n",v4="EII_FILTER_SERVICE_GLOBAL=4\n[SSM2045]\n";
        const bool v4text=text.starts_with(v4),v3text=v4text||text.starts_with(v3),modern=v3text||text.starts_with(v2);const auto& prefix=v4text?v4:v3text?v3:modern?v2:v1;
        if(text.size()>65536||!text.starts_with(prefix))throw std::runtime_error("Not an EII global Filter Service profile");
        const std::string legacy="[LEGACY]\n",custom="[EIII]\n";
        const auto at=text.find(legacy,prefix.size());
        if(at==std::string::npos||text.find(legacy,at+legacy.size())!=std::string::npos)throw std::runtime_error("Incomplete global Filter Service profile");
        FilterServiceProfiles result;result.ssm=analog::FilterCalibration::parse(text.substr(prefix.size(),at-prefix.size()));
        const auto start=at+legacy.size();
        if(modern){const auto end=text.find(custom,start);
            if(end==std::string::npos||text.find(custom,end+custom.size())!=std::string::npos)throw std::runtime_error("Incomplete CEM global profile");
            result.legacy=analog::FilterCalibration::parse(text.substr(start,end-start));
            const auto eiiiStart=end+custom.size();
            if(v3text){const std::string model2="[SSM2045M2]\n";const auto m2=text.find(model2,eiiiStart);
                if(m2==std::string::npos||text.find(model2,m2+model2.size())!=std::string::npos)throw std::runtime_error("Incomplete SSM2045 model-2 global profile");
                result.eiii=analog::FilterCalibration::parse(text.substr(eiiiStart,m2-eiiiStart));result.ssm2=analog::FilterCalibration::parse(text.substr(m2+model2.size()));}
            else{result.eiii=analog::FilterCalibration::parse(text.substr(eiiiStart));result.hasSsm2=false;}
        }else{result.legacy=analog::FilterCalibration::parse(text.substr(start));result.hasEiii=false;result.hasSsm2=false;}
        if(!v4text){analog::migrateLevelModel(result.ssm);analog::migrateLevelModel(result.legacy);if(result.hasEiii)analog::migrateLevelModel(result.eiii);if(result.hasSsm2)analog::migrateLevelModel(result.ssm2);}
        return result;
    }
};
}
