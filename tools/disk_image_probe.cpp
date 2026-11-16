#include "floppy_image.h"
#include "floppy_read_separator.h"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc,char** argv){try{
    if(argc!=3){std::cerr<<"usage: disk_image_probe image.emuiifd report2010|hxc2026\n";return 2;}
    const std::string name=argv[2];
    if(name!="report2010"&&name!="hxc2026")throw std::invalid_argument("explicit report2010 or hxc2026 profile required");
    std::ifstream f(argv[1],std::ios::binary);if(!f)throw std::runtime_error("cannot open payload image");
    // Bound input before allocation; a payload container has no extension/header.
    f.seekg(0,std::ios::end);if(f.tellg()!=eii::EmuiiImage::imageBytes)throw std::invalid_argument("EMUIIFD must contain exactly573440 bytes");
    f.seekg(0);std::vector<std::uint8_t> bytes(eii::EmuiiImage::imageBytes);
    if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("incomplete image read");
    eii::EmuiiImage image(std::move(bytes));
    std::cout<<"{\"kind\":\"nominal_synthesized_fm_validation\",\"profile\":\""<<name<<"\",\"original_crc_present\":false,\"machine_mounted\":false,\"os_boot\":false,\"tracks\":[\n";
    for(unsigned id=0;id<160;++id){
        const auto payload=image.payload(id/2,id%2);
        const auto track=image.format(id/2,id%2,name=="report2010"?eii::EmuiiGap::Esynthesist2010:eii::EmuiiGap::Hxc2026);
        const auto decoded=eii::inspectEmuiiFm(track.transitions);
        if(decoded.id!=id||!std::equal(payload.begin(),payload.end(),decoded.payload.begin()))throw std::runtime_error("frame roundtrip mismatch");
        eii::FloppyReadSeparator circuit(87);unsigned received=0;
        const auto sample=[&](std::uint64_t,bool data){if(received>=31250||data!=bool(track.transitions[2*received+1]))throw std::runtime_error("separator data mismatch");++received;};
        for(unsigned slot=0;slot<track.transitions.size();++slot){const std::uint64_t edge=slot*64ull+8;if(track.transitions[slot])circuit.pulse(edge,sample);else circuit.advance(edge,sample);}
        circuit.advance(4000100,sample);if(received!=31250)throw std::runtime_error("separator clock mismatch");
        std::cout<<(id?",":"")<<"{\"id\":"<<id<<",\"cylinder\":"<<id/2<<",\"side\":"<<id%2<<",\"offset\":"<<id*3584<<",\"synthesized_crc16_arc\":"<<eii::emuiiCrc(payload)<<",\"data_marker_bit\":"<<decoded.dataMarkerBit<<",\"recovered_bits\":"<<received<<"}\n";
    }
    std::cout<<"],\"verified_track_sides\":160,\"fm_slots_per_revolution\":62500,\"source_edges_per_slot\":64,\"ls221_experiment_edges\":87,\"read_pulse_trailing_offset_edges\":8}\n";
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
