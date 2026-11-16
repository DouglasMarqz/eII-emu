#pragma once
#include <span>
#include <cstdint>
#include <stdexcept>
namespace eii::vst {
inline std::uint64_t scannerPanelFingerprint(std::span<const std::uint8_t> bytes)noexcept{
 std::uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=b;h*=1099511628211ull;}return h;
}
// Explicit custom firmware behavior. Only the freshly loaded in-memory copy
// is changed, before either CPU executes. No file writes, clocks, RAM injection
// or model calibration changes. Stock behavior is the old-project default.
inline bool applyFastScannerPanel(std::span<std::uint8_t> rom,bool enabled){
 if(!enabled)return false;
 // Fingerprint guards the entire owner-supplied8KiB scannerROM, not just a
 // guessed byteoffset. SHA256 is recorded in docs, never ROMdata in the repo.
 const auto h=scannerPanelFingerprint(rom);
 if(rom.size()!=8192)throw std::runtime_error("Fast panel needs the supported 8 KiB scanner ROM; disable Fast panel for other firmware.");
 constexpr std::uint64_t originalFingerprint=0x925a64d3bbf6bd2cull;
 constexpr std::uint64_t fastFingerprint=0xfb123fff3cea1274ull;
 if(h==fastFingerprint&&rom[0xc1e]==0x18&&rom[0xc1f]==0x60)return true;
 if(h!=originalFingerprint||rom[0xc1e]!=0x30||rom[0xc1f]!=0x60)
  throw std::runtime_error("Unsupported scanner ROM for Fast panel. Original mode remains available; no firmware was changed.");
 // JR NC -> JR: sliders take the existing direct ADC/hysteresis path used
 // for wheels, bypassing the two software averaging stages. This is a custom
 // behavior option, not a claim that original EII hardware is unfiltered.
 rom[0xc1e]=0x18;return true;
}
}
