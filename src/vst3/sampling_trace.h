#pragma once
#include <array>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
namespace eii::vst {
// Owner-thread-only diagnostic history; no paths, ROM/media/audio payloads.
class SamplingTrace {
public:
 static constexpr unsigned columns=44,capacity=2048;
 using Row=std::array<double,columns>;
 void start(){if(!rows_)rows_=std::make_unique<std::array<Row,capacity>>();written_=0;enabled_=true;}
 void stop()noexcept{enabled_=false;}
 bool recording()const noexcept{return enabled_;}
 bool allocated()const noexcept{return bool(rows_);}
 void push(const Row& row)noexcept{if(!enabled_)return;(*rows_)[written_%capacity]=row;++written_;}
 std::string csv()const{
  std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17);
  o<<"wall_us,epoch,callbacks,target_frames,machine_ticks,virtual_ticks,offset_ticks,model,imode,pgain_bits,input_peak,input_frames,input_dropped,input_missing,input_trim_db,buffer_v,iint_v,held_v,ref_v,known_mask,acv0,fcv0,qcv0,sar_reads,sar_unknown,rail_clips,rebases,output_underruns,dsp_dropped,machine_load,dsp_load,owner_lag_ms,native_overflows,native_ordering_errors,gui_moves,gui_voltages,gui_paints,gui_paint_ms,gui_paint_max_ms,gui_move_ms,gui_move_max_ms,adc_a,lcd_hash,scan_backlog\n";
  const auto start=written_>capacity?written_-capacity:0;
  for(auto n=start;n<written_;++n){const auto& r=(*rows_)[n%capacity];for(unsigned k=0;k<columns;++k)o<<(k?",":"")<<r[k];o<<'\n';}return o.str();
 }
private:std::unique_ptr<std::array<Row,capacity>> rows_;std::uint64_t written_=0;bool enabled_=false;
};
}
