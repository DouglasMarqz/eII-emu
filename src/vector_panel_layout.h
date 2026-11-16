#pragma once
#include "scanner_controls.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace eii::panel {
struct Point{float x=0,y=0;};
struct Rect{float x,y,w,h;bool contains(Point p)const{return p.x>=x&&p.y>=y&&p.x<x+w&&p.y<y+h;}};
constexpr float width=1960,height=760;
struct Viewport {
 float scale=1,x=0,y=0;
 static Viewport fit(float w,float h){float s=std::max(.01f,std::min(w/width,h/height));return {s,(w-width*s)/2,(h-height*s)/2};}
 Point design(Point p)const{return {(p.x-x)/scale,(p.y-y)/scale};}
 Point screen(Point p)const{return {p.x*scale+x,p.y*scale+y};}
};
// Geometry measured from a flat photograph of the original overlay (frame
// mapped onto 5..1955 x 5..610): every key is the same 38-unit square, LEDs
// are 14-unit lamps at their own holes, digit keys carry no lamp.
struct Key{ScannerButton id;float x,y;const wchar_t* label;bool red=false;float lx=0,ly=0;Rect rect()const{return {x-19,y-19,38,38};}Rect led()const{return {lx-7,ly-7,14,14};}};
inline const std::array<Key,30> keys={{
 {ScannerButton::DynamicAllocation,97,203,L"DYNAMIC\nALLOCATION",false,98,167},{ScannerButton::Tune,178,203,L"TUNE",false,178,166},{ScannerButton::Transpose,260,202,L"TRANSPOSE",false,259,165},
 {ScannerButton::Digit1,471,334,L"1"},{ScannerButton::Digit2,529,334,L"2"},{ScannerButton::Digit3,587,334,L"3"},
 {ScannerButton::Digit4,472,393,L"4"},{ScannerButton::Digit5,529,393,L"5"},{ScannerButton::Digit6,587,393,L"6"},
 {ScannerButton::Digit7,472,452,L"7"},{ScannerButton::Digit8,529,452,L"8"},{ScannerButton::Digit9,587,452,L"9"},{ScannerButton::Digit0,530,511,L"0"},
 {ScannerButton::ExternalClock,704,133,L"EXT CLK",false,704,96},{ScannerButton::Setup,785,132,L"SETUP",false,785,95},
 {ScannerButton::Define,706,260,L"DEFINE",false,706,223},{ScannerButton::Edit,785,260,L"EDIT",false,785,223},
 {ScannerButton::Record,707,386,L"RECORD",true,706,350},{ScannerButton::Play,787,386,L"PLAY",false,786,350},
 {ScannerButton::Select,707,512,L"SELECT",false,707,475},{ScannerButton::Stop,787,512,L"STOP",false,787,475},
 {ScannerButton::Enter,910,434,L"ENTER",false,911,397},{ScannerButton::Filter,1075,292,L"FILTER",false,1075,255},
 {ScannerButton::VcaLfo,1248,293,L"VCA / LFO",false,1248,256},{ScannerButton::VoiceDefinition,1418,293,L"VOICE DEFINITION",false,1418,257},
 {ScannerButton::PresetDefinition,1736,294,L"PRESET DEFINITION",false,1736,257},
 {ScannerButton::RealTimeControl,1042,434,L"",false,1041,397},{ScannerButton::Special,1399,434,L"",false,1399,397},
 {ScannerButton::Disk,1542,434,L"",false,1543,397},{ScannerButton::Sample,1753,434,L"SAMPLE",true,1755,397}
}};
struct Fader{Rect track;unsigned control;const wchar_t* label;};
inline const std::array<Fader,6> faders={{{{100,327,20,191},0,L"A"},{{176,327,20,191},1,L"B"},{{254,327,20,191},2,L"C"},{{332,327,20,191},3,L"D"},{{375,640,26,82},4,L"LEFT WHEEL"},{{480,640,26,82},5,L"RIGHT WHEEL"}}};
inline int keyAt(Point p){for(unsigned i=0;i<keys.size();++i)if(keys[i].rect().contains(p))return int(i);return -1;}
inline int faderAt(Point p){for(unsigned i=0;i<faders.size();++i){auto r=faders[i].track;r.x-=20;r.w+=40;r.y-=10;r.h+=20;if(r.contains(p))return int(i);}return -1;}
inline float faderVolts(unsigned i,Point p){const auto&r=faders.at(i).track;return std::clamp(5.f*(1-(p.y-r.y)/r.h),0.f,5.f);}
// LCD module bezel and knob caps from a close-up of a real unit (scaled by the
// 38-unit key): the overlay cut-outs are smaller than the parts seated in them.
inline constexpr Rect lcd={435,166,210,77},volume={322,168,58,58},gain={1640,405,58,58};
struct HostButton{Rect rect;const wchar_t*label;unsigned command;};
inline constexpr std::array<HostButton,6> hostButtons={{{{25,688,285,43},L"RECORD TRACE",125},{{595,654,250,54},L"SETTINGS / DEVICES",123},{{865,654,220,54},L"FILTER SERVICE",122},{{1105,654,200,54},L"BOOT / RESET",102},{{1325,654,200,54},L"RUN / PAUSE",103},{{1570,665,350,43},L"ALL NOTES OFF",115}}};
// Ruled legend columns as printed: one row per item with a fine rule under it,
// a blank entry leaves an unruled gap, a leading tab indents a sub-item.
struct Legend{float x,y,w,pitch,size;const wchar_t*lines;};
struct LegendBox{float x,y,w;const wchar_t*label;};
inline constexpr std::array<LegendBox,3> legendBoxes={{{870,55,64,L"SETUP"},{870,150,64,L"EDIT"},{870,249,64,L"DEFINE"}}};
// x, first row top, width, row pitch, text size; rules follow every row.
inline constexpr std::array<Legend,15> legends={{
 {843,65.2f,117,13.6f,9.5f,L"1-SELECT TRACK\n2-TEMPO\n3-AUTO CORRECT\n4-CUE\n5-SMPTE START\n6-COUNTDOWN"},
 {843,163.9f,117,13.6f,9.5f,L"1-ERASE TRACK\n2-PUNCH IN\n3-BOUNCE TRACKS\n4-STORE CONTROLS\n5-REASSIGN PRESET\n6-APPEND SEQUENCE"},
 {843,264.2f,117,13.6f,9.5f,L"1-CREATE SEQUENCE\n2-TIME SIGNATURE\n3-SEQUENCE LENGTH"},
 {1031,66.8f,137,13.6f,8.5f,L"1A-FREQUENCY\n\tB-Q\n\tC-ENVELOPE AMOUNT\n\n2A-LFO AMOUNT\n\tB-KYBD AMOUNT\n\n3A-ATTACK\n\tB-DECAY\n\tC-SUSTAIN\n\tD-RELEASE\n\n0-CHANGE CURRENT VOICE"},
 {1207,74.1f,135,13.6f,8.5f,L"1A-ATTACK\n\tB-DECAY\n\tC-SUSTAIN\n\tD-RELEASE\n\n2A-LFO RATE\n\tB-LFO DELAY\n\tC-LFO VARIATION\n\tD-LFO > VCA\n\n0-CHANGE CURRENT VOICE"},
 {1377,74.9f,120,13.6f,8.5f,L"11A,B-TRUNCATE START\n\tC,D-TRUNCATE END\n\n12A,B-LOOP START\n\tC,D-LOOP LENGTH\n\n13-LOOP\n\n14A,B-SPLICE PT 1\n\tC,D-SPLICE PT 2\n\n15-SOUND LENGTH"},
 {1505,75.7f,118,13.6f,8.5f,L"21A-VEL > VCA LEVEL\n\tB-VEL > VCA ATTACK\n\n22A-VEL > VCF FREQ\n\tB-VEL > VCF ATTACK\n\tC-VEL > VCF Q\n\n23A-VIBRATO DEPTH\n24A-VOICE ATTN\n\tB-VOICE TUNING\n\n25-SOLO MODE\n26-LOOP IN RELEASE\n27-BACKWARDS MODE\n28-COMBINE VOICES\n29-CONTROL ENABLE\n30-SAVE VOICE TO DISK"},
 {1686,76.5f,119,13.6f,8.5f,L"11-GET VOICE\n12-COPY/NAME VOICE\n13-ERASE VOICE\n14-ERASE SEQUENCE\n15-ERASE PRESET\n16-ERASE BANK\n17-CATALOG VOICES\n18-CATALOG SEQ\n19-CATALOG PRESETS\n20-MEM REMAINING"},
 {1832,77.3f,103,13.6f,8.5f,L"21-CREATE PRESET\n22-ASSIGN VOICE\n23-EDIT ASSIGNMENT\n24-DEASSIGN VOICE\n25-VEL SWITCH\n26-VEL XFADE\n27-POS XFADE\n28-NONTRANSPOSE\n29-ARPEGGIATOR\n30-MIDI\n31-COPY/NAME PRESET\n32-REPLICATE PRESET"},
 {1104,393.4f,88,13.4f,9,L"1-LEFT WHEEL\n2-RIGHT WHEEL\n3-PEDAL A / D\n4-MIDI CTRL A\n5-MIDI CTRL B\n6-MIDI CTRL C"},
 {1104,520.4f,88,13.4f,9,L"7-FOOT SW 1\n8-FOOT SW 2"},
 {1208,379.8f,99,13.4f,9,L"0-OFF\n1-PITCH\n2-FILT Fc\n3-LEVEL\n4-LFO > PITCH\n5-LFO > FILT Fc\n6-LFO > LEVEL\n7-ATK RATE"},
 {1208,494,99,13.4f,9,L"0-OFF\n1-SEQ CTRL\n2-SUSTAIN\n3-RELEASE\n4-SUSTENUTO\n5-ADVANCE PRESET"},
 {1479,468,108,13.4f,9,L"1-GET BANK DISK 1\n2-GET BANK DISK 2\n3-SPACE REMAINING\n4-COPY DISK\n5-CATALOG VOICE\n6-ERASE VOICE\n7-SAVE BANK\n8-FORMAT DISK"},
 {1818,396.8f,95,13.4f,9,L"1-VU MODE\n2-DEFINE VOICE\n3-GAIN SET\n4-THRESHOLD SET\n5-SAMPLE LENGTH\n7-ARM SAMPLING\n9-FORCE SAMPLING\n0-STOP SAMPLING"}
}};
// Vertical column rules: x, top, bottom.
inline constexpr std::array<std::array<float,3>,6> legendDividers={{{1023,62,300},{1199,62,300},{1366,62,300},{1672,62,300},{1200,381,488},{1200,494,580}}};
}
