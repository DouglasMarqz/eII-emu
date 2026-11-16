Source: https://github.com/steinbergmedia/vst3sdk (MIT since VST 3.8)
Release tag: v3.8.1_build_84, superproject commit 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
Submodules (pinned by that tag):
- base            fcf9da0bd27a16f7f03773a3a39822f28f5c8477
- pluginterfaces  4f547e8e102b47de4a8b8aaf343c73b700786372
- public.sdk      586dc5e6c8012c3e4b01c79389375cbe96bdb1da
Not vendored: cmake, doc, tutorials, vstgui4 (no VSTGUI is used).

Kept subset: LICENSE.txt and README.md of each part; base/ and pluginterfaces/
complete; public.sdk/source/ without aaxwrapper, auwrapper, auv3wrapper and
interappaudio; public.sdk/samples/vst-hosting/validator/ (Steinberg validator)
and audiohost/source/media/miditovst.h (header the validator's MIDI test needs).

The plug-in itself compiles only pluginterfaces plus coreiids.cpp, funknown.cpp
and public.sdk/source/vst/vstinitiids.cpp. The headless host test adds the
public.sdk hosting classes; the validator additionally uses base/.

Local patches: none. Everything builds unmodified with LLVM-MinGW 20261006
UCRT (clang 23.1.3); the plug-in build passes -Wno-pragma-pack because the
SDK headers intentionally push/pop packing.
