#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
// Keep windows.h before the dependent SDK headers.
// clang-format off
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
// clang-format on
#include "front_panel_session.h"
#include "windows_audio_endpoints.h"
#include "windows_audio_capture.h"
#include "windows_panel_preferences.h"
#include "windows_audio_output.h"
#include "windows_midi_input.h"
#include "windows_filter_service.h"
#include "windows_vector_panel.h"
#include "windows_thread_capture.h"
#include "performance_trace.h"
#include <iomanip>
#include <fstream>
#include <sstream>

namespace {
eii::host::WindowsThreadCapture threadCapture,performanceThreads;
eii::host::PerformanceTrace performanceTrace;
bool traceNeedsSave=false,traceCalibrationDirty=false;
std::uint64_t nextTraceSample=0,nextTraceThreads=0;
std::uint64_t traceNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void traceEvent(std::string_view name,std::string_view detail={}){if(performanceTrace.active())performanceTrace.event(traceNow(),name,detail);}
HWND window, folderLabel, diskLabel, status, report, diagnostic, command, startup, lcd, audioList,
    audioStatus, settingsStatus, midiList, midiStatus, midiTraceStatus, chipModelList;
std::unique_ptr<eii::host::WindowsAudioEndpoints> audioEndpoints,inputEndpoints;
std::unique_ptr<eii::host::WindowsAudioCapture> audioCapture;
std::shared_ptr<eii::analog::LiveSampleInput> liveInput=std::make_shared<eii::analog::LiveSampleInput>();
HWND inputList,inputStatus,inputChannelList;
std::wstring sampleInputText();
eii::host::WindowsPanelPreferences preferences;
std::unique_ptr<eii::host::FilterServiceWindow> filterService;
std::shared_ptr<eii::host::AudioQueue> audioQueue;
std::unique_ptr<eii::host::WindowsAudioOutput> audioOutput;
std::unique_ptr<eii::host::WindowsMidiInput> midiInput;
std::wstring midiError;
std::array<HWND, 6> sliders{}, sliderLabels{};
std::filesystem::path folder, disk, hddImage;
std::unique_ptr<eii::FrontPanelSession> session;
std::unique_ptr<eii::host::VectorPanelWindow> vectorPanel;
eii::PanelSnapshot shown;
HFONT normalFont;
std::future<std::string> midiExport;
std::filesystem::path midiExportPath;
std::string lastError;
enum class HddAction {None,Save,FloppySave,FloppyMount,Boot,Close};
HddAction hddAction=HddAction::None;
std::future<eii::PanelMediaState> hddPause;
eii::PanelMediaState pausedMedia;
enum class MediaIo {None,HddSave,FloppySave,FloppyMount};
MediaIo mediaIo=MediaIo::None;
std::filesystem::path pendingFloppy;
bool pendingDirect=false;
std::future<std::string> hddSave;
std::filesystem::path hddSavePath;
bool hddCloseApproved=false;
void pollHddAction();
void requestHddAction(HddAction action);
// Cached window text: GDI repaints of the 2x16 dot panel are expensive and keep
// the desktop compositor (and with it the NVIDIA display container) busy, so
// each control is only updated when its content actually changes.
std::wstring lastStatusText, lastReportText, lastMidiText, lastAudioText;
std::uint64_t lastPresentedReportRevision=0;bool wasSettingsVisible=false;
eii::analog::RuntimeAudioMode selectedChipModel() {
    switch(SendMessageW(chipModelList,CB_GETCURSEL,0,0)) {
        case 2:return eii::analog::RuntimeAudioMode::NonlinearChip192;
        case 1:return eii::analog::RuntimeAudioMode::NonlinearChip;
        default:return eii::analog::RuntimeAudioMode::BasicVoice;
    }
}
constexpr const wchar_t *sliderNames[] = {L"Slider A", L"Slider B",   L"Slider C",
                                          L"Slider D", L"Left wheel", L"Right wheel"};
constexpr const wchar_t *buttonNames[] = {L"Select",     L"Stop",      L"Record",
                                          L"Play",       L"Define",    L"Edit",
                                          L"Ext clock",  L"Setup",     L"0",
                                          L"9 / Yes",    L"8",         L"7 / No",
                                          L"6",          L"5",         L"4",
                                          L"3",          L"2",         L"1",
                                          L"Transpose",  L"Tune",      L"Dynamic alloc",
                                          L"Preset def", L"Sample",    L"Disk",
                                          L"Voice def",  L"Special",   L"VCA / LFO",
                                          L"Filter",     L"Real time", L"Enter"};
// Native BUTTON still handles focus, mouse capture and pressed appearance.
// Deliver the contact on down/up, rather than waiting for BN_CLICKED on up.
LRESULT CALLBACK panelContact(HWND h, UINT message, WPARAM w, LPARAM l,
                              UINT_PTR id, DWORD_PTR) {
    auto contact = [&](bool pressed) {
        if(performanceTrace.active())traceEvent(pressed?"panel_down":"panel_up",std::to_string(id-200));
        if (session) session->button(eii::ScannerButton(id - 200), pressed);
    };
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        contact(true);
        break;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        contact(false);
        break;
    case WM_KEYDOWN:
        if (w == VK_SPACE && !(l & (LPARAM(1) << 30))) contact(true);
        break;
    case WM_KEYUP:
        if (w == VK_SPACE) contact(false);
        break;
    case BM_CLICK: // Accessibility/programmatic activation has no hold duration.
        if (session) session->pulse(eii::ScannerButton(id - 200));
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, panelContact, id);
        break;
    }
    return DefSubclassProc(h, message, w, l);
}
std::wstring wide(const std::string &text) {
    if (text.empty())
        return {};
    const auto n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(n, L' ');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
}
std::wstring crlf(const std::string &text) {
    auto s = wide(text);
    std::wstring out;
    for (auto c : s) {
        if (c == L'\n')
            out += L'\r';
        out += c;
    }
    return out;
}
HWND control(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h,
             int id = 0) {
    auto child = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                               (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, (WPARAM)normalFont, TRUE);
    return child;
}
// Own compact display font; procedural dots, not a recovered EII CGROM.
std::array<unsigned, 7> glyph(unsigned char c) {
#define G(a, b, c, d, e, f, g)                                                                     \
    return { a, b, c, d, e, f, g }
    switch (c) {
    case 'A':
        G(14, 17, 17, 31, 17, 17, 17);
    case 'B':
        G(30, 17, 17, 30, 17, 17, 30);
    case 'C':
        G(14, 17, 16, 16, 16, 17, 14);
    case 'D':
        G(30, 17, 17, 17, 17, 17, 30);
    case 'E':
        G(31, 16, 16, 30, 16, 16, 31);
    case 'F':
        G(31, 16, 16, 30, 16, 16, 16);
    case 'G':
        G(14, 17, 16, 23, 17, 17, 15);
    case 'H':
        G(17, 17, 17, 31, 17, 17, 17);
    case 'I':
        G(14, 4, 4, 4, 4, 4, 14);
    case 'J':
        G(7, 2, 2, 2, 18, 18, 12);
    case 'K':
        G(17, 18, 20, 24, 20, 18, 17);
    case 'L':
        G(16, 16, 16, 16, 16, 16, 31);
    case 'M':
        G(17, 27, 21, 21, 17, 17, 17);
    case 'N':
        G(17, 25, 21, 19, 17, 17, 17);
    case 'O':
        G(14, 17, 17, 17, 17, 17, 14);
    case 'P':
        G(30, 17, 17, 30, 16, 16, 16);
    case 'Q':
        G(14, 17, 17, 17, 21, 18, 13);
    case 'R':
        G(30, 17, 17, 30, 20, 18, 17);
    case 'S':
        G(15, 16, 16, 14, 1, 1, 30);
    case 'T':
        G(31, 4, 4, 4, 4, 4, 4);
    case 'U':
        G(17, 17, 17, 17, 17, 17, 14);
    case 'V':
        G(17, 17, 17, 17, 17, 10, 4);
    case 'W':
        G(17, 17, 17, 21, 21, 21, 10);
    case 'X':
        G(17, 17, 10, 4, 10, 17, 17);
    case 'Y':
        G(17, 17, 10, 4, 4, 4, 4);
    case 'Z':
        G(31, 1, 2, 4, 8, 16, 31);
    case 'a':
        G(0, 0, 14, 1, 15, 17, 15);
    case 'b':
        G(16, 16, 30, 17, 17, 17, 30);
    case 'c':
        G(0, 0, 14, 16, 16, 17, 14);
    case 'd':
        G(1, 1, 15, 17, 17, 17, 15);
    case 'e':
        G(0, 0, 14, 17, 31, 16, 14);
    case 'f':
        G(6, 9, 8, 28, 8, 8, 8);
    case 'g':
        G(0, 15, 17, 17, 15, 1, 14);
    case 'h':
        G(16, 16, 30, 17, 17, 17, 17);
    case 'i':
        G(4, 0, 12, 4, 4, 4, 14);
    case 'j':
        G(2, 0, 6, 2, 2, 18, 12);
    case 'k':
        G(16, 16, 18, 20, 24, 20, 18);
    case 'l':
        G(12, 4, 4, 4, 4, 4, 14);
    case 'm':
        G(0, 0, 26, 21, 21, 21, 21);
    case 'n':
        G(0, 0, 30, 17, 17, 17, 17);
    case 'o':
        G(0, 0, 14, 17, 17, 17, 14);
    case 'p':
        G(0, 0, 30, 17, 30, 16, 16);
    case 'q':
        G(0, 0, 15, 17, 15, 1, 1);
    case 'r':
        G(0, 0, 22, 25, 16, 16, 16);
    case 's':
        G(0, 0, 15, 16, 14, 1, 30);
    case 't':
        G(8, 8, 28, 8, 8, 9, 6);
    case 'u':
        G(0, 0, 17, 17, 17, 19, 13);
    case 'v':
        G(0, 0, 17, 17, 17, 10, 4);
    case 'w':
        G(0, 0, 17, 17, 21, 21, 10);
    case 'x':
        G(0, 0, 17, 10, 4, 10, 17);
    case 'y':
        G(0, 0, 17, 17, 15, 1, 14);
    case 'z':
        G(0, 0, 31, 2, 4, 8, 31);
    case '0':
        G(14, 17, 19, 21, 25, 17, 14);
    case '1':
        G(4, 12, 4, 4, 4, 4, 14);
    case '2':
        G(14, 17, 1, 2, 4, 8, 31);
    case '3':
        G(30, 1, 1, 14, 1, 1, 30);
    case '4':
        G(2, 6, 10, 18, 31, 2, 2);
    case '5':
        G(31, 16, 16, 30, 1, 1, 30);
    case '6':
        G(14, 16, 16, 30, 17, 17, 14);
    case '7':
        G(31, 1, 2, 4, 8, 8, 8);
    case '8':
        G(14, 17, 17, 14, 17, 17, 14);
    case '9':
        G(14, 17, 17, 15, 1, 1, 14);
    case ' ':
        G(0, 0, 0, 0, 0, 0, 0);
    case '*':
        G(0, 21, 14, 31, 14, 21, 0);
    case '.':
        G(0, 0, 0, 0, 0, 12, 12);
    case ',':
        G(0, 0, 0, 0, 0, 4, 8);
    case ':':
        G(0, 12, 12, 0, 12, 12, 0);
    case ';':
        G(0, 12, 12, 0, 4, 4, 8);
    case '-':
        G(0, 0, 0, 31, 0, 0, 0);
    case '_':
        G(0, 0, 0, 0, 0, 0, 31);
    case '+':
        G(0, 4, 4, 31, 4, 4, 0);
    case '=':
        G(0, 0, 31, 0, 31, 0, 0);
    case '/':
        G(1, 2, 2, 4, 8, 8, 16);
    case '\\':
        G(16, 8, 8, 4, 2, 2, 1);
    case '!':
        G(4, 4, 4, 4, 4, 0, 4);
    case '?':
        G(14, 17, 1, 2, 4, 0, 4);
    case '<':
        G(2, 4, 8, 16, 8, 4, 2);
    case '>':
        G(8, 4, 2, 1, 2, 4, 8);
    case '(':
        G(2, 4, 8, 8, 8, 4, 2);
    case ')':
        G(8, 4, 2, 2, 2, 4, 8);
    case '[':
        G(14, 8, 8, 8, 8, 8, 14);
    case ']':
        G(14, 2, 2, 2, 2, 2, 14);
    case '"':
        G(10, 10, 0, 0, 0, 0, 0);
    case '\'':
        G(4, 4, 0, 0, 0, 0, 0);
    case '#':
        G(10, 31, 10, 10, 31, 10, 0);
    case '%':
        G(25, 25, 2, 4, 8, 19, 19);
    case '&':
        G(12, 18, 20, 8, 21, 18, 13);
    case '@':
        G(14, 17, 23, 21, 23, 16, 14);
    case '^':
        G(4, 10, 17, 0, 0, 0, 0);
    case '|':
        G(4, 4, 4, 4, 4, 4, 4);
    case '~':
        G(0, 0, 9, 22, 0, 0, 0);
    case '`':
        G(8, 4, 0, 0, 0, 0, 0);
    case '{':
        G(2, 4, 4, 8, 4, 4, 2);
    case '}':
        G(8, 4, 4, 2, 4, 4, 8);
    case '$':
        G(4, 15, 20, 14, 5, 30, 4);
    default:
        G(14, 17, 1, 2, 4, 0, 4);
    }
#undef G
}
LRESULT CALLBACK lcdProc(HWND h, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        auto background = CreateSolidBrush(RGB(123, 155, 101));
        FillRect(dc, &r, background);
        DeleteObject(background);
        const int pitch = std::max(1, int(std::min((r.right - 24) / 96, (r.bottom - 16) / 19)));
        const int left = (r.right - 96 * pitch) / 2, top = (r.bottom - 19 * pitch) / 2;
        auto on = CreateSolidBrush(RGB(21, 41, 29)), off = CreateSolidBrush(RGB(112, 145, 93));
        for (unsigned row = 0; row < 2; ++row)
            for (unsigned col = 0; col < 16; ++col) {
                const auto bits = glyph(static_cast<unsigned char>(shown.lcd[row][col]));
                for (unsigned y = 0; y < 8; ++y)
                    for (unsigned x = 0; x < 5; ++x) {
                        RECT dot{left + int(col * 6 + x) * pitch, top + int(row * 11 + y) * pitch,
                                 left + int(col * 6 + x) * pitch + std::max(1, pitch - 1),
                                 top + int(row * 11 + y) * pitch + std::max(1, pitch - 1)};
                        FillRect(dc, &dot,
                                 shown.lcdOn && y < 7 && (bits[y] & (16 >> x)) ? on : off);
                    }
            }
        DeleteObject(on);
        DeleteObject(off);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, message, w, l);
}
void traceButtonState(){
    const unsigned state=performanceTrace.active()?1:traceNeedsSave?2:0;
    SetWindowTextW(GetDlgItem(window,125),state==1?L"Stop + Save trace":state==2?L"Save trace...":L"Record trace");
    if(vectorPanel)vectorPanel->traceState(state);
}
void recordPerformance(const eii::PanelSnapshot& snapshot,bool force=false){
    if(!performanceTrace.active())return;
    const auto now=traceNow();
    if(force||now>=nextTraceSample){
        performanceTrace.sample(now,snapshot);nextTraceSample=now+250;
        if(traceCalibrationDirty){performanceTrace.event(now,"calibration",preferences.calibration.serialize());traceCalibrationDirty=false;}
    }
    if(force||now>=nextTraceThreads){
        performanceTrace.event(now,"threads",performanceThreads.capture());nextTraceThreads=now+1000;
        if(audioOutput)performanceTrace.event(now,"wasapi",audioOutput->diagnostics());
        if(audioCapture){std::ostringstream o;const auto& l=*liveInput;o<<audioCapture->diagnostics()<<" pushed="<<l.pushed.load()<<" consumed="<<l.consumed.load()<<" buffered="<<l.buffered()<<" active="<<l.active()<<" underruns="<<l.underruns.load()<<" overruns="<<l.overruns.load()<<" producer_drops="<<l.producerDrops.load()<<" discarded="<<l.discarded.load()<<" prefills="<<l.prefills.load()<<" pace_holds="<<l.paceHolds.load()<<" buffered_ms="<<l.buffered()/48.0<<" gain="<<l.gainPot.load()<<" channel="<<audioCapture->channelMode.load();performanceTrace.event(now,"sample_input",o.str());}
        if(midiInput){std::ostringstream info;info<<"messages="<<midiInput->messages.load()<<" errors="<<midiInput->errors.load()<<" more_data="<<midiInput->moreData.load()<<" dropped="<<midiInput->dropped.load();performanceTrace.event(now,"host_midi",info.str());}
    }
}
void refresh() {
    if (!session)
        return;
    const bool settingsVisible=IsWindowVisible(window)&&!IsIconic(window);
    const bool opened=settingsVisible&&!wasSettingsVisible;wasSettingsVisible=settingsVisible;
    if(settingsVisible&&audioCapture&&preferences.inputEnabled){static std::wstring last;const auto t=sampleInputText();if(t!=last){last=t;SetWindowTextW(inputStatus,t.c_str());}}
    if(settingsVisible&&audioOutput) {
        const auto error=audioOutput->error();
        if(!error.empty())SetWindowTextW(audioStatus,wide(error).c_str());
        else if(const auto frames=audioOutput->bufferFrames()) {
            const auto q=audioQueue->stats();
            std::wostringstream info;
            info << L"WASAPI buffer " << std::fixed << std::setprecision(1)
                 << double(frames)/48.0 << L" ms / queue " << double(q.queued)/48.0
                 << L" ms";
            const auto value=info.str();
            if(value!=lastAudioText){lastAudioText=value;SetWindowTextW(audioStatus,value.c_str());}
        }
    }
    if(midiExport.valid() && midiExport.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            const auto text=midiExport.get();
            std::ofstream file(midiExportPath,std::ios::binary);file.write(text.data(),text.size());
            if(!file)throw std::runtime_error("Could not write MIDI trace");
            SetWindowTextW(midiTraceStatus,L"MIDI trace saved");
        } catch(const std::exception& e) {SetWindowTextW(midiTraceStatus,wide(e.what()).c_str());}
    }
    pollHddAction();
    auto next = session->snapshot();
    // Show the engine's returned state, not just the checkbox's requested bit.
    if(settingsVisible){
        const bool requested=SendMessageW(GetDlgItem(window,126),BM_GETCHECK,0,0)==BST_CHECKED;
        const wchar_t* text=!next.loaded?L"S3528 LP (applies after boot)":next.reconstruction?L"S3528 LP: ACTIVE":requested?L"S3528 LP: requested / not active":L"S3528 LP: bypass";
        static std::wstring lastS3528;
        if(lastS3528!=text){lastS3528=text;SetWindowTextW(GetDlgItem(window,126),text);}
    }
    recordPerformance(next);
    if(next.midiWheelRevision!=shown.midiWheelRevision) {
        for(unsigned i=4;i<6;++i)if(GetCapture()!=sliders[i] && next.controls[i].known) {
            const auto v=next.controls[i].volts;
            SendMessageW(sliders[i],TBM_SETPOS,TRUE,std::clamp(int(v*200+.5),0,1000));
            std::wostringstream label;label<<sliderNames[i]<<L"  "<<std::fixed<<std::setprecision(2)<<v<<L" V";
            SetWindowTextW(sliderLabels[i],label.str().c_str());
        }
    }
    if(settingsVisible&&midiInput) {
        std::wostringstream info;
        if(!midiError.empty())info<<midiError;
        else info<<(midiInput->selectedId.empty()?L"MIDI IN: none":L"MIDI IN open")
                 <<L" / messages "<<midiInput->messages.load()<<L" / UART bytes "<<next.midiReceived
                 <<L" / firmware reads "<<next.midiRead<<L" / overrun "<<next.midiOverruns
                 <<L" / dropped "<<midiInput->dropped.load()+next.midiDropped
                 <<L" / WinMM errors "<<midiInput->errors.load()<<L" / congestion "<<midiInput->moreData.load()
                 <<L" / unsupported "<<midiInput->unsupported.load();
        const auto midiText=info.str();
        if(midiText!=lastMidiText){lastMidiText=midiText;SetWindowTextW(midiStatus,midiText.c_str());}
    }
    if (next.revision == shown.revision && !opened)
        return;
    bool lcdChanged = false;
    for (unsigned row = 0; row < 2 && !lcdChanged; ++row)
        for (unsigned col = 0; col < 16 && !lcdChanged; ++col)
            if (next.lcd[row][col] != shown.lcd[row][col] || next.lcdOn != shown.lcdOn) lcdChanged = true;
    for(unsigned i=0;i<30;++i) {
        const auto b=eii::ScannerButton(i);
        if(settingsVisible && (opened || !(eii::ScannerControls::ledState(shown.leds,b)==eii::ScannerControls::ledState(next.leds,b))))
            InvalidateRect(GetDlgItem(window,200+i),nullptr,FALSE);
    }
    shown = std::move(next);
    if(vectorPanel)vectorPanel->update(shown);
    // The dot panel is drawn with 1280 GDI fills; repaint it only when the
    // visible characters (or the display-on state) actually differ.
    if(settingsVisible) {
        if (lcdChanged || opened) InvalidateRect(lcd, nullptr, FALSE);
        std::wostringstream text;
        text << (shown.loaded ? (shown.running ? L"Running" : L"Paused") : L"No firmware loaded")
             << L"  /  " << std::fixed << std::setprecision(1) << double(shown.ticks) / 4000000.0
             << L" s simulated / " << std::setprecision(0) << shown.speedRatio * 100 << L"% speed (1 s avg) / Chip: "
             << (shown.audioMode==eii::analog::RuntimeAudioMode::NonlinearChip192?L"SSM2045 192k":shown.audioMode==eii::analog::RuntimeAudioMode::NonlinearChip?L"SSM2045 96k":L"Legacy zero-Q") << L"\r\n"
             << wide(shown.startup) << L"\r\n"
             << wide(shown.media) << L"\r\nDMA " << shown.dma << L"  /  Track " << shown.cylinder
             << (shown.spinning ? L"  spinning" : L"") << (shown.selected ? L"  selected" : L"")
             << L"\r\nHDD: " << (shown.hdd.mounted?(shown.hdd.direct?L"DIRECT":shown.hdd.dirty?L"COPY / UNSAVED":L"COPY"):L"none")
             << L" / Floppy: " << (shown.floppy.mounted?(!shown.floppy.writable?L"read-only":shown.floppy.direct?L"DIRECT":shown.floppy.dirty?L"COPY / UNSAVED":L"COPY"):L"none");
        const auto statusText = text.str();
        if (statusText != lastStatusText) {lastStatusText = statusText;SetWindowTextW(status, statusText.c_str());}
        if(opened || shown.reportRevision!=lastPresentedReportRevision) {
            const auto reportText = crlf(shown.report);
            if(reportText!=lastReportText){lastReportText=reportText;SetWindowTextW(report,reportText.c_str());}
            lastPresentedReportRevision=shown.reportRevision;
        }
    }
    if (!shown.error.empty() && shown.error != lastError) {
        lastError = shown.error;
        MessageBoxW(window, wide(shown.error).c_str(), L"Emulator paused", MB_ICONERROR);
    }
    if (shown.error.empty())
        lastError.clear();
}
void selectAudio() {
    if (!audioEndpoints)
        return;
    try {
        const auto index = SendMessageW(audioList, CB_GETCURSEL, 0, 0);
        if (index != CB_ERR) {
            audioEndpoints->select(unsigned(index));
            audioOutput->start(audioEndpoints->selectedId,audioQueue);
            lastAudioText.clear();
            SetWindowTextW(audioStatus,L"WASAPI 48 kHz stereo");
        }
    } catch (const std::exception &e) {
        SetWindowTextW(audioStatus, wide(e.what()).c_str());
    }
}
void refreshAudio() {
    if (!audioEndpoints)
        return;
    try {
        const auto selected = audioEndpoints->selectedId;
        audioEndpoints->refresh();
        SendMessageW(audioList, CB_RESETCONTENT, 0, 0);
        unsigned chosen = 0;
        for (unsigned i = 0; i < audioEndpoints->devices.size(); ++i) {
            const auto &d = audioEndpoints->devices[i];
            SendMessageW(audioList, CB_ADDSTRING, 0, (LPARAM)d.name.c_str());
            if (d.id == selected)
                chosen = i;
        }
        SendMessageW(audioList, CB_SETCURSEL, chosen, 0);
        // Sound is enabled on launch again: the measured NVIDIA-container load
        // came from the 10 Hz GDI dot-panel repaint, not from the WASAPI stream,
        // and the change-only repaint path keeps the compositor quiet.
        selectAudio();
    } catch (const std::exception &e) {
        SetWindowTextW(audioStatus, wide(e.what()).c_str());
    }
}
// Sample input (7-42 +INPUT.V): list item 0 = no capture (default),
// 1 = system default input, then devices. Live capture never blocks the
// machine thread; the GAIN pot scales the voltage before +INPUT.V.
std::wstring sampleInputText(){
    std::wostringstream o;const auto& l=*liveInput;const bool on=audioCapture&&audioCapture->running();
    o<<L"Sample input: "<<(on?L"capturing":L"off");
    if(on)o<<L" "<<audioCapture->deviceRate()<<L" Hz/"<<audioCapture->deviceChannels()<<L"ch -> 48 kHz mono";
    o<<std::fixed<<std::setprecision(1)<<L" | GAIN "<<int(l.gainPot.load()*100+.5)<<L"% | buffer "<<double(l.buffered())/48<<L" ms"
     <<(l.active()?L" (sampling)":L" (idle)")<<L" | underruns "<<l.underruns.load()<<L" | overruns "<<l.overruns.load()
     <<L" | prefills "<<l.prefills.load()<<L" | pace holds "<<l.paceHolds.load()<<L" | discarded "<<l.discarded.load();
    if(audioCapture){const auto e=audioCapture->error();if(!e.empty())o<<L" | error: "<<wide(e);}
    return o.str();
}
void selectInput(){
    if(!inputEndpoints||!audioCapture)return;
    try{
        const auto index=SendMessageW(inputList,CB_GETCURSEL,0,0);
        if(index==CB_ERR||index==0){audioCapture->stop();if(session)session->sampleSource(nullptr);preferences.inputEnabled=0;SetWindowTextW(inputStatus,L"No sample input (select a capture device)");return;}
        const auto& d=inputEndpoints->devices.at(unsigned(index)-1);
        audioCapture->start(d.id,liveInput);
        if(session)session->sampleSource(liveInput);
        preferences.inputEnabled=1;preferences.inputId=d.id;inputEndpoints->selectedId=d.id;
        SetWindowTextW(inputStatus,sampleInputText().c_str());
    }catch(const std::exception& e){if(session)session->sampleSource(nullptr);SetWindowTextW(inputStatus,wide(e.what()).c_str());}
}
void refreshInputs(){
    if(!inputEndpoints)return;
    try{
        inputEndpoints->refresh();
        SendMessageW(inputList,CB_RESETCONTENT,0,0);
        SendMessageW(inputList,CB_ADDSTRING,0,(LPARAM)L"None (no sample input)");
        unsigned chosen=0;
        for(unsigned i=0;i<inputEndpoints->devices.size();++i){const auto& d=inputEndpoints->devices[i];
            SendMessageW(inputList,CB_ADDSTRING,0,(LPARAM)d.name.c_str());
            if(preferences.inputEnabled&&d.id==preferences.inputId)chosen=i+1;}
        SendMessageW(inputList,CB_SETCURSEL,chosen,0);
        selectInput();
    }catch(const std::exception& e){SetWindowTextW(inputStatus,wide(e.what()).c_str());}
}
void selectMidi() {
    if(!midiInput)return;
    try {
        const auto index=SendMessageW(midiList,CB_GETCURSEL,0,0);
        if(index==CB_ERR)return;
        midiInput->select(index==0?L"":midiInput->devices.at(unsigned(index)-1).id);
        midiError.clear();
    }catch(const std::exception& e){midiError=wide(e.what());}
}
void refreshMidi() {
    if(!midiInput)return;
    const auto desired=midiInput->selectedId.empty()?preferences.midiId:midiInput->selectedId;
    midiInput->refresh();
    SendMessageW(midiList,CB_RESETCONTENT,0,0);
    SendMessageW(midiList,CB_ADDSTRING,0,(LPARAM)L"None / MIDI input disabled");
    unsigned chosen=0;
    for(unsigned i=0;i<midiInput->devices.size();++i) {
        const auto& d=midiInput->devices[i];
        SendMessageW(midiList,CB_ADDSTRING,0,(LPARAM)d.name.c_str());
        if(d.id==desired)chosen=i+1;
    }
    SendMessageW(midiList,CB_SETCURSEL,chosen,0);selectMidi();
}
void saveMidiTrace() {
    if(midiExport.valid())return;
    wchar_t path[32768]=L"EII-MIDI-trace.tsv";
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
    dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"tsv";
    dialog.lpstrFilter=L"MIDI trace (*.tsv)\0*.tsv\0All files\0*.*\0";
    dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    if(GetSaveFileNameW(&dialog)) {
        midiExportPath=path;midiExport=session->exportMidi();
        SetWindowTextW(midiTraceStatus,L"Preparing MIDI trace...");
    }
}
void togglePerformanceTrace(){
    if(performanceTrace.active()){
        recordPerformance(session->snapshot(),true);
        traceEvent("final_calibration",preferences.calibration.serialize());
        traceEvent("host","logical_processors="+std::to_string(std::thread::hardware_concurrency()));
        performanceTrace.stop(traceNow());
        session->traceMetrics(false);traceNeedsSave=true;traceButtonState();
    } else if(!traceNeedsSave){
        performanceThreads=eii::host::WindowsThreadCapture{};
        performanceTrace.start(traceNow());nextTraceSample=nextTraceThreads=0;traceCalibrationDirty=true;
        session->traceMetrics(true);
        traceEvent("host","logical_processors="+std::to_string(std::thread::hardware_concurrency()));
        traceButtonState();SetWindowTextW(settingsStatus,L"Trace recording (bounded)");return;
    }
    wchar_t path[32768]=L"EII-performance-trace.jsonl";
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=vectorPanel?vectorPanel->handle():window;
    dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"jsonl";
    dialog.lpstrFilter=L"Performance trace (*.jsonl)\0*.jsonl\0All files\0*.*\0";
    dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    if(GetSaveFileNameW(&dialog))try{
        const auto data=performanceTrace.exportJsonl();std::ofstream file(std::filesystem::path(path),std::ios::binary);
        file.write(data.data(),std::streamsize(data.size()));file.close();if(!file)throw std::runtime_error("Could not write performance trace; recording retained for retry.");
        traceNeedsSave=false;SetWindowTextW(settingsStatus,L"Performance trace saved");
    }catch(const std::exception& e){MessageBoxW(window,wide(e.what()).c_str(),L"Trace save failed",MB_ICONERROR);}
    traceButtonState();
}
bool savePreferences() {
    if(midiInput)preferences.midiId=midiInput->selectedId;
    preferences.midiWheels=SendMessageW(GetDlgItem(window,117),BM_GETCHECK,0,0)==BST_CHECKED;
    preferences.noteOffZero=SendMessageW(GetDlgItem(window,118),BM_GETCHECK,0,0)==BST_CHECKED;
    preferences.chipModel=DWORD(unsigned(selectedChipModel())-unsigned(eii::analog::RuntimeAudioMode::BasicVoice));
    preferences.romFolder = folder.native();
    preferences.disk = disk.native();
    preferences.hddImage = hddImage.native();
    preferences.directImageWrites=SendMessageW(GetDlgItem(window,141),BM_GETCHECK,0,0)==BST_CHECKED;
    preferences.nativeAudio=SendMessageW(GetDlgItem(window,127),BM_GETCHECK,0,0)==BST_CHECKED;
    preferences.reconstruction=SendMessageW(GetDlgItem(window,126),BM_GETCHECK,0,0)==BST_CHECKED;
    preferences.liveSerial=SendMessageW(GetDlgItem(window,124),BM_GETCHECK,0,0)==BST_CHECKED;
    preferences.diagnostic = SendMessageW(diagnostic, BM_GETCHECK, 0, 0) == BST_CHECKED;
    preferences.concreteStartup = SendMessageW(startup, CB_GETCURSEL, 0, 0) == 0;
    if (audioEndpoints)
        preferences.audioId = audioEndpoints->selectedId;
    preferences.inputGain=DWORD(liveInput->gainPot.load()*1000+.5f);
    for (unsigned i = 0; i < sliders.size(); ++i)
        preferences.sliders[i] = DWORD(SendMessageW(sliders[i], TBM_GETPOS, 0, 0));
    const bool ok=preferences.save();SetWindowTextW(settingsStatus,ok?L"":L"Could not save panel preferences.");return ok;
}
void bootNow(bool discard) {
    if (folder.empty()) {
        MessageBoxW(window, L"Select the prepared ROM folder first.", L"Emulator II",
                    MB_ICONINFORMATION);
        return;
    }
    eii::PanelBoot config;
    config.romFolder = folder;
    config.disk = disk;config.hddImage=hddImage;config.discardHddChanges=discard;config.directImageWrites=preferences.directImageWrites;
    config.audioMode=selectedChipModel();config.calibration=preferences.calibration;
    config.nativeAudio=SendMessageW(GetDlgItem(window,127),BM_GETCHECK,0,0)==BST_CHECKED;
    config.reconstruction=SendMessageW(GetDlgItem(window,126),BM_GETCHECK,0,0)==BST_CHECKED;
    config.liveSerial=SendMessageW(GetDlgItem(window,124),BM_GETCHECK,0,0)==BST_CHECKED;
    config.diagnostic = SendMessageW(diagnostic, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config.concreteStartup = SendMessageW(startup, CB_GETCURSEL, 0, 0) == 0;
    for (unsigned i = 0; i < 6; ++i)
        config.controls[i] = double(SendMessageW(sliders[i], TBM_GETPOS, 0, 0)) / 200.0;
    savePreferences();
    session->boot(std::move(config));
    session->resume();
}
void finishHddAction(bool discard=false) {
    const auto action=hddAction;
    if(action==HddAction::Close){hddCloseApproved=true;PostMessageW(window,WM_CLOSE,1,0);return;}
    if(action==HddAction::FloppyMount){mediaIo=MediaIo::FloppyMount;hddSave=session->floppyImage(pendingFloppy,pendingDirect,discard);return;}
    hddAction=HddAction::None;
    if(action==HddAction::Boot)bootNow(discard);
}
bool chooseHddOutput(std::filesystem::path& path,const wchar_t* title,const wchar_t* extension=L"img") {
    wchar_t name[32768]={};const auto suggestion=std::wstring(L"EII-copy.")+extension;lstrcpynW(name,suggestion.c_str(),32768);
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
    dialog.lpstrFile=name;dialog.nMaxFile=32768;dialog.lpstrDefExt=extension;
    dialog.lpstrFilter=std::wstring(extension)==L"hfe"?L"HFEv1 floppy\0*.hfe\0All files\0*.*\0":std::wstring(extension)==L"emuiifd"?L"EMUIIFD floppy\0*.emuiifd\0All files\0*.*\0":L"EII raw HDD (*.img)\0*.img\0All files\0*.*\0";
    dialog.lpstrTitle=title;dialog.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetSaveFileNameW(&dialog))return false;
    path=name;
    // Worker publication also refuses replacement, closing the dialog/file race.
    std::error_code error;
    if(std::filesystem::exists(path,error)||error){MessageBoxW(window,L"Choose a new filename. Existing HDD files are never replaced.",L"HDD copy",MB_ICONINFORMATION);return false;}
    return true;
}
void saveNextMedia() {
    if(pausedMedia.hdd.dirty){
        if(!pausedMedia.hdd.idle)throw std::runtime_error("Finish the sampler HDD operation before saving.");
        if(!chooseHddOutput(hddSavePath,L"Save HDD copy to a NEW file")){hddAction=HddAction::None;return;}
        mediaIo=MediaIo::HddSave;hddSave=session->saveHddCopy(hddSavePath);
    }else if(pausedMedia.floppy.dirty){
        if(!pausedMedia.floppy.idle)throw std::runtime_error("Finish the sampler floppy operation before saving.");
        const auto ext=wide(pausedMedia.floppy.format);
        if(!chooseHddOutput(hddSavePath,L"Save floppy copy to a NEW file",ext.c_str())){hddAction=HddAction::None;return;}
        mediaIo=MediaIo::FloppySave;hddSave=session->saveFloppyCopy(hddSavePath);
    }else{finishHddAction();return;}
    SetWindowTextW(settingsStatus,L"Saving disk copy; sampler paused...");
}
void pollHddAction() {
    using namespace std::chrono_literals;
    if(hddPause.valid()&&hddPause.wait_for(0s)==std::future_status::ready)try {
        pausedMedia=hddPause.get();
        if(hddAction==HddAction::Save){if(!pausedMedia.hdd.mounted)throw std::runtime_error("No HDD mounted.");pausedMedia.hdd.dirty=true;pausedMedia.floppy.dirty=false;saveNextMedia();}
        else if(hddAction==HddAction::FloppySave){if(!pausedMedia.floppy.mounted)throw std::runtime_error("No floppy mounted.");pausedMedia.floppy.dirty=true;pausedMedia.hdd.dirty=false;saveNextMedia();}
        else {
            if(hddAction==HddAction::FloppyMount)pausedMedia.hdd.dirty=false;
            if(pausedMedia.hdd.dirty||pausedMedia.floppy.dirty){
                const auto answer=MessageBoxW(window,L"Disk images have unsaved changes. Save copies before reset, eject or exit?\n\nYes: save each changed image to a new file\nNo: discard working-copy changes\nCancel: stay paused",L"Unsaved disk changes",MB_YESNOCANCEL|MB_ICONQUESTION);
                if(answer==IDCANCEL){hddAction=HddAction::None;return;}
                if(answer==IDNO){finishHddAction(true);return;}
                saveNextMedia();
            }else finishHddAction();
        }
    }catch(const std::exception& e){hddAction=HddAction::None;MessageBoxW(window,wide(e.what()).c_str(),L"Disk image",MB_ICONERROR);}
    if(hddSave.valid()&&hddSave.wait_for(0s)==std::future_status::ready)try {
        hddSave.get();
        if(mediaIo==MediaIo::FloppyMount){disk=pendingFloppy;SetWindowTextW(diskLabel,disk.empty()?L"Floppy ejected.":disk.c_str());savePreferences();SetWindowTextW(settingsStatus,L"Floppy changed. Press Run to continue.");hddAction=HddAction::None;mediaIo=MediaIo::None;return;}
        if(mediaIo==MediaIo::HddSave){pausedMedia.hdd.dirty=false;hddImage=hddSavePath;}
        else if(mediaIo==MediaIo::FloppySave){pausedMedia.floppy.dirty=false;disk=hddSavePath;}
        savePreferences();SetWindowTextW(settingsStatus,L"Disk copy saved; selected for next boot. Sampler paused.");mediaIo=MediaIo::None;saveNextMedia();
    }catch(const std::exception& e){hddAction=HddAction::None;mediaIo=MediaIo::None;MessageBoxW(window,wide(e.what()).c_str(),L"Disk copy not saved; changes retained",MB_ICONERROR);}
}
void requestHddAction(HddAction action) {
    if(hddAction!=HddAction::None)return;
    hddAction=action;hddPause=session->pauseForMedia();
}
void boot(){requestHddAction(HddAction::Boot);}
void hddMenu() {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"Select HDD image... (next boot)");
    AppendMenuW(menu,MF_STRING,2,L"Create blank 20 MB HDD... (next boot)");
    AppendMenuW(menu,MF_STRING,3,L"No HDD (next boot)");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,4,L"Save HDD copy... (finish sampler DISK 7 first)");
    AppendMenuW(menu,MF_STRING,5,L"HDD status / selected image");
    POINT point;GetCursorPos(&point);
    const auto id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);
    if(id==1){
        wchar_t name[32768]={};OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=window;
        d.lpstrFile=name;d.nMaxFile=32768;d.lpstrFilter=L"EII raw HDD\0*.img;*.hdd;*.bin\0All files\0*.*\0";
        d.lpstrTitle=L"Select raw EII HDD image (main23.bin required)";d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&d)){hddImage=name;savePreferences();SetWindowTextW(settingsStatus,L"HDD selected. Boot / reset to apply; main23.bin will be used.");}
    }else if(id==2){
        std::filesystem::path path;if(!chooseHddOutput(path,L"Create a NEW blank EII HDD image"))return;
        try {eii::host::saveNewHddImage(path,std::vector<std::uint8_t>(eii::host::hddImageBytes));hddImage=path;savePreferences();
            MessageBoxW(window,L"Blank HDD created and selected.\n\nSelect the HD OS / error-log floppy, Boot / reset with main23.bin, then use SPECIAL 22 to format it.\nUse DISK 7 to save a bank before loading it. Finish the sampler operation, then Save HDD copy to keep changes.",L"New HDD",MB_ICONINFORMATION);
        }catch(const std::exception& e){MessageBoxW(window,wide(e.what()).c_str(),L"Cannot create HDD",MB_ICONERROR);}
    }else if(id==3){hddImage.clear();savePreferences();SetWindowTextW(settingsStatus,L"No HDD selected for next boot. Current mounted HDD remains until reset.");}
    else if(id==4)requestHddAction(HddAction::Save);
    else if(id==5){auto s=session->snapshot();std::wstring text=L"Selected for next boot: "+(hddImage.empty()?std::wstring(L"none"):hddImage.native());
        text+=L"\nMounted source: "+(s.hdd.mounted?s.hdd.source.native():std::wstring(L"none"));
        text+=L"\nLast saved copy: "+(s.hdd.lastSaved.empty()?std::wstring(L"none"):s.hdd.lastSaved.native());
        text+=s.hdd.dirty?L"\nUNSAVED changes in memory":L"\nNo unsaved HDD changes";
        MessageBoxW(window,text.c_str(),L"HDD status",MB_ICONINFORMATION);}
}
void choose(bool rom);
void floppyMenu(){
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Insert floppy image...");AppendMenuW(menu,MF_STRING,2,L"Save floppy copy... (finish DISK7 first)");AppendMenuW(menu,MF_STRING,3,L"Eject floppy");
    POINT point;GetCursorPos(&point);const auto id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);
    if(id==1)choose(false);else if(id==2)requestHddAction(HddAction::FloppySave);else if(id==3)SendMessageW(window,WM_COMMAND,MAKEWPARAM(106,BN_CLICKED),0);
}
void choose(bool rom) {
    wchar_t path[32768] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window;
    ofn.lpstrFile = path;
    ofn.nMaxFile = 32768;
    ofn.lpstrFilter = rom ? L"Prepared main ROM\0main.bin\0Binary ROM\0*.bin\0\0"
                          : L"EII floppy images\0*.hfe;*.emuiifd;*.e2o\0All files\0*.*\0\0";
    ofn.lpstrTitle =
        rom ? L"Select main.bin in your prepared ROM folder" : L"Select a read-only OS / bank disk";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        if (rom) {
            folder = std::filesystem::path(path).parent_path();
            SetWindowTextW(folderLabel, folder.c_str());
        } else {
            if(session->snapshot().loaded){pendingFloppy=path;pendingDirect=preferences.directImageWrites;requestHddAction(HddAction::FloppyMount);return;}
            disk = path;
            SetWindowTextW(diskLabel, disk.c_str());
        }
        savePreferences();
    }
}
LRESULT CALLBACK proc(HWND h, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_DRAWITEM: {
        const auto* item=reinterpret_cast<const DRAWITEMSTRUCT*>(l);
        if(item->CtlID<200||item->CtlID>=230)return FALSE;
        const int savedDc=SaveDC(item->hDC);
        const unsigned index=item->CtlID-200;
        RECT r=item->rcItem;
        DrawFrameControl(item->hDC,&r,DFC_BUTTON,DFCS_BUTTONPUSH|
            ((item->itemState&ODS_SELECTED)?DFCS_PUSHED:0)|
            ((item->itemState&ODS_DISABLED)?DFCS_INACTIVE:0));
        const auto previous=SelectObject(item->hDC,normalFont);
        SetBkMode(item->hDC,TRANSPARENT);
        SetTextColor(item->hDC,GetSysColor((item->itemState&ODS_DISABLED)?COLOR_GRAYTEXT:COLOR_BTNTEXT));
        RECT text=r;text.left+=4;text.right-=18;
        if(item->itemState&ODS_SELECTED)OffsetRect(&text,1,1);
        DrawTextW(item->hDC,buttonNames[index],-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        const auto state=eii::ScannerControls::ledState(shown.leds,eii::ScannerButton(index));
        const COLORREF color=!state.known?RGB(115,115,115):state.high?RGB(255,48,24):RGB(65,22,16);
        const auto brush=CreateSolidBrush(color);const auto pen=CreatePen(PS_SOLID,1,RGB(45,45,45));
        const auto oldBrush=SelectObject(item->hDC,brush),oldPen=SelectObject(item->hDC,pen);
        const int cy=(r.top+r.bottom)/2;
        Ellipse(item->hDC,r.right-14,cy-4,r.right-5,cy+5);
        SelectObject(item->hDC,oldBrush);SelectObject(item->hDC,oldPen);
        DeleteObject(brush);DeleteObject(pen);
        if(item->itemState&ODS_FOCUS){RECT focus=r;InflateRect(&focus,-3,-3);DrawFocusRect(item->hDC,&focus);}
        SelectObject(item->hDC,previous);RestoreDC(item->hDC,savedDc);return TRUE;
    }
    case WM_CREATE: {
        window = h;
        preferences.load();
        folder = preferences.romFolder;
        disk = preferences.disk;hddImage=preferences.hddImage;
        normalFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        control(L"STATIC", L"EMULATOR II  /  firmware settings / R-W preview", 0, 20, 12, 650, 24);
        control(L"STATIC",L"Chip model:",0,645,12,95,24);
        chipModelList=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,740,8,280,120,121);
        SendMessageW(chipModelList,CB_ADDSTRING,0,(LPARAM)L"Legacy VCF/VCA (zero-Q)");
        SendMessageW(chipModelList,CB_ADDSTRING,0,(LPARAM)L"SSM2045 nonlinear (96k DSP)");
        SendMessageW(chipModelList,CB_ADDSTRING,0,(LPARAM)L"SSM2045 nonlinear (192k DSP)");
        SendMessageW(chipModelList,CB_SETCURSEL,preferences.chipModel,0);
        control(L"BUTTON",L"Native DAC + deglitch (test)",BS_AUTOCHECKBOX|WS_TABSTOP,20,34,395,30,127);
        SendMessageW(GetDlgItem(window,127),BM_SETCHECK,preferences.nativeAudio?BST_CHECKED:BST_UNCHECKED,0);
        control(L"BUTTON",L"S3528 LP reconstruction",BS_AUTOCHECKBOX|WS_TABSTOP,420,34,405,30,126);
        SendMessageW(GetDlgItem(window,126),BM_SETCHECK,preferences.reconstruction?BST_CHECKED:BST_UNCHECKED,0);
        control(L"BUTTON",L"Filter service...",BS_PUSHBUTTON,835,34,180,30,122);
        control(L"BUTTON", L"ROM folder...", BS_PUSHBUTTON, 20, 70, 125, 30, 100);
        control(L"BUTTON", L"Disk image...", BS_PUSHBUTTON, 155, 70, 125, 30, 101);
        control(L"BUTTON", L"Boot / reset", BS_PUSHBUTTON, 290, 70, 125, 30, 102);
        control(L"BUTTON", L"Run", BS_PUSHBUTTON, 425, 70, 80, 30, 103);
        control(L"BUTTON", L"Pause", BS_PUSHBUTTON, 515, 70, 80, 30, 104);
        control(L"BUTTON", L"Step 1 s", BS_PUSHBUTTON, 605, 70, 100, 30, 105);
        control(L"BUTTON", L"No disk", BS_PUSHBUTTON, 715, 70, 90, 30, 106);
        diagnostic =
            control(L"BUTTON", L"Diagnostic EPROM", BS_AUTOCHECKBOX, 825, 70, 190, 30, 107);
        folderLabel = control(
            L"STATIC", L"ROMs: select prepared main.bin (scanner.bin and both PROMs beside it).",
            SS_PATHELLIPSIS, 20, 108, 665, 22);
        control(L"BUTTON",L"Floppy...",BS_PUSHBUTTON,705,104,120,28,142);
        control(L"BUTTON",L"HDD...",BS_PUSHBUTTON,835,104,180,28,140);
        diskLabel =
            control(L"STATIC", L"Disk: none selected. Choose an HFE or EMUIIFD, then Boot / reset.",
                    SS_PATHELLIPSIS, 20, 133, 550, 22);
        control(L"BUTTON",L"Direct image writes (next mount / boot)",BS_AUTOCHECKBOX|WS_TABSTOP,580,132,435,24,141);
        SendMessageW(GetDlgItem(window,141),BM_SETCHECK,preferences.directImageWrites?BST_CHECKED:BST_UNCHECKED,0);
        if (!folder.empty())
            SetWindowTextW(folderLabel, folder.c_str());
        if (!disk.empty())
            SetWindowTextW(diskLabel, disk.c_str());
        SendMessageW(diagnostic, BM_SETCHECK, preferences.diagnostic ? BST_CHECKED : BST_UNCHECKED, 0);
        lcd = control(L"EIILcd", L"", WS_BORDER, 20, 165, 520, 128);
        status = control(L"STATIC", L"No firmware loaded.", 0, 565, 165, 450, 105);
        startup = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 565, 273, 450, 200, 108);
        SendMessageW(startup, CB_ADDSTRING, 0,
                     (LPARAM)L"Boot experiment: RAM seed1 / controller phase0");
        SendMessageW(startup, CB_ADDSTRING, 0, (LPARAM)L"Verification: unknown RAM / all16 phases");
        SendMessageW(startup, CB_SETCURSEL, preferences.concreteStartup ? 0 : 1, 0);
        for (unsigned i = 0; i < 30; ++i) {
            auto button = control(L"BUTTON", buttonNames[i],
                    (eii::ScannerControls::ledPin(eii::ScannerButton(i)).latch<0?BS_PUSHBUTTON:BS_OWNERDRAW)|WS_TABSTOP, 20 + int(i % 8) * 126,
                    315 + int(i / 8) * 38, 120, 32, 200 + i);
            SetWindowSubclass(button, panelContact, 200 + i, 0);
        }
        control(L"STATIC",
                L"Panel buttons follow mouse/Space press and release. Run to scan controls. "
                L"Startup selection takes effect on reset.",
                0, 20, 470, 1000, 22);
        for (unsigned i = 0; i < 6; ++i) {
            sliderLabels[i] = control(L"STATIC", sliderNames[i], 0, 20 + i * 167, 502, 155, 24);
            sliders[i] = control(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                                 15 + i * 167, 530, 160, 32, 300 + i);
            SendMessageW(sliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
            SendMessageW(sliders[i], TBM_SETPOS, TRUE, preferences.sliders[i]);
        }
        control(L"BUTTON",L"Live serial",BS_AUTOCHECKBOX|WS_TABSTOP,20,574,145,26,124);
        SendMessageW(GetDlgItem(window,124),BM_SETCHECK,preferences.liveSerial?BST_CHECKED:BST_UNCHECKED,0);
        command = control(L"EDIT", L"HE", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 168, 574, 220,
                          30, 109);
        control(L"BUTTON", L"Send", BS_PUSHBUTTON, 400, 574, 90, 30, 110);
        audioList =
            control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 510, 574, 360, 200, 111);
        control(L"BUTTON", L"Refresh outputs", BS_PUSHBUTTON, 880, 574, 140, 30, 112);
        audioStatus = control(L"STATIC", L"Audio output selection", 0, 510, 610, 510, 24);
        audioQueue = std::make_shared<eii::host::AudioQueue>();
        audioQueue->setOutputGain(float(preferences.outputVolume)/1000);
        audioOutput = std::make_unique<eii::host::WindowsAudioOutput>();
        audioEndpoints = std::make_unique<eii::host::WindowsAudioEndpoints>();
        audioEndpoints->selectedId = preferences.audioId;
        refreshAudio();
        control(L"BUTTON", L"Capture state", BS_PUSHBUTTON, 20, 610, 135, 26, 116);
        control(L"BUTTON",L"Record trace",BS_PUSHBUTTON|WS_TABSTOP,165,610,155,26,125);
        settingsStatus = control(L"STATIC", preferences.calibrationError.c_str(), 0, 325, 610, 175, 24);
        report = control(L"EDIT", L"",
                         WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 20,
                         770, 1005, 80);
        session = std::make_unique<eii::FrontPanelSession>(audioQueue);
        control(L"STATIC",L"MIDI IN:",0,20,655,90,24);
        midiList=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,110,650,560,200,113);
        control(L"BUTTON",L"Refresh MIDI",BS_PUSHBUTTON,680,650,150,30,114);
        control(L"BUTTON",L"All notes off",BS_PUSHBUTTON,840,650,180,30,115);
        midiStatus=control(L"STATIC",L"",0,20,690,1005,35);
        midiInput=std::make_unique<eii::host::WindowsMidiInput>([](auto data){return session && session->midi(data);});
        refreshMidi();
        auto bridge=control(L"BUTTON",L"MIDI to panel wheels",BS_AUTOCHECKBOX|WS_TABSTOP,20,735,225,26,117);
        auto zero=control(L"BUTTON",L"Note Off as 90/0",BS_AUTOCHECKBOX|WS_TABSTOP,250,735,195,26,118);
        control(L"BUTTON",L"Capture MIDI",BS_AUTOCHECKBOX|WS_TABSTOP,450,735,155,26,119);
        control(L"BUTTON",L"Save trace...",BS_PUSHBUTTON|WS_TABSTOP,610,735,130,26,120);
        midiTraceStatus=control(L"STATIC",L"Capture off by default",0,750,738,275,24);
        control(L"STATIC",L"SAMPLE IN:",0,20,862,90,24);
        inputList=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,110,857,265,200,130);
        inputChannelList=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,380,857,90,200,132);
        for(auto n:{L"Mix L+R",L"Left",L"Right"})SendMessageW(inputChannelList,CB_ADDSTRING,0,(LPARAM)n);
        SendMessageW(inputChannelList,CB_SETCURSEL,preferences.inputChannel,0);
        control(L"BUTTON",L"Refresh inputs",BS_PUSHBUTTON,480,857,140,30,131);
        inputStatus=control(L"STATIC",L"",0,630,857,400,40);
        liveInput->gainPot=float(preferences.inputGain)/1000;
        audioCapture=std::make_unique<eii::host::WindowsAudioCapture>();
        audioCapture->channelMode=preferences.inputChannel;
        inputEndpoints=std::make_unique<eii::host::WindowsAudioEndpoints>(eCapture);
        refreshInputs();
        SendMessageW(bridge,BM_SETCHECK,preferences.midiWheels?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(zero,BM_SETCHECK,preferences.noteOffZero?BST_CHECKED:BST_UNCHECKED,0);
        session->midiOptions(preferences.midiWheels,preferences.noteOffZero);
        SetTimer(h, 1, 50, nullptr);
        return 0;
    }
    case WM_TIMER:
        if(w==7){KillTimer(h,7);
            // Watchdog: the machine thread did not answer the close pause request.
            if(hddAction==HddAction::Close&&!hddCloseApproved){traceEvent("close","machine did not confirm pause within 4 s");
                const auto answer=MessageBoxW(h,L"The emulator did not confirm the close request within 4 seconds.\nClose anyway? Unsaved disk changes may be lost.",L"Close",MB_YESNO|MB_ICONWARNING);
                hddAction=HddAction::None;if(answer==IDYES){hddCloseApproved=true;PostMessageW(h,WM_CLOSE,1,0);}}
            return 0;}
        refresh();
        return 0;
    case WM_HSCROLL:
        for (unsigned i = 0; i < 6; ++i)
            if ((HWND)l == sliders[i]) {
                const auto v = double(SendMessageW(sliders[i], TBM_GETPOS, 0, 0)) / 200.0;
                session->voltage(i, v);
                std::wostringstream label;
                label << sliderNames[i] << L"  " << std::fixed << std::setprecision(2) << v
                      << L" V";
                SetWindowTextW(sliderLabels[i], label.str().c_str());
            }
        if (LOWORD(w) == TB_ENDTRACK)
            savePreferences();
        return 0;
    case WM_COMMAND:
        if(hddAction!=HddAction::None)return 0;
        if(LOWORD(w)==127 && HIWORD(w)==BN_CLICKED){
            const bool enabled=SendMessageW(GetDlgItem(window,127),BM_GETCHECK,0,0)==BST_CHECKED;
            traceEvent("native_dac_request",enabled?"on":"reference");session->nativeAudio(enabled);savePreferences();return 0;
        }
        if(LOWORD(w)==126 && HIWORD(w)==BN_CLICKED){
            const bool enabled=SendMessageW(GetDlgItem(window,126),BM_GETCHECK,0,0)==BST_CHECKED;
            traceEvent("s3528_request",enabled?"on":"bypass");session->reconstruction(enabled);savePreferences();return 0;
        }
        if(LOWORD(w)==125 && HIWORD(w)==BN_CLICKED){togglePerformanceTrace();return 0;}
        if(LOWORD(w)==124 && HIWORD(w)==BN_CLICKED){
            session->liveSerial(SendMessageW(GetDlgItem(window,124),BM_GETCHECK,0,0)==BST_CHECKED);
            savePreferences();return 0;
        }
        if(LOWORD(w)==122 && HIWORD(w)==BN_CLICKED){
            if(!filterService)filterService=std::make_unique<eii::host::FilterServiceWindow>(preferences.calibration,
                [](const auto& c){preferences.calibration=c;if(performanceTrace.active())traceCalibrationDirty=true;if(session)session->calibrate(c);},[] {return savePreferences();},[] {eii::host::FilterServiceMetrics m;if(session){auto s=session->snapshot();m.dsp=s.dsp;m.nativeAudio=s.nativeAudio;m.reconstruction=s.reconstruction;m.droppedRate=s.hostDroppedPerSecond;m.missingRate=s.hostMissingPerSecond;m.rebases=s.pacingRebases;m.debtMs=s.pacingDebtMs;}m.sampleInput=sampleInputText();return m;});
            filterService->show(vectorPanel?vectorPanel->handle():window,normalFont);return 0;
        }
        // Combo notifications are CBN_SELCHANGE, not BN_CLICKED.
        if(LOWORD(w)==121 && HIWORD(w)==CBN_SELCHANGE) {
            if(performanceTrace.active())traceEvent("model_request",std::to_string(unsigned(selectedChipModel())));
            session->audioModel(selectedChipModel());savePreferences();return 0;
        }
        if(LOWORD(w)==130 && HIWORD(w)==CBN_SELCHANGE){selectInput();savePreferences();return 0;}
        if(LOWORD(w)==132 && HIWORD(w)==CBN_SELCHANGE){const auto i=SendMessageW(inputChannelList,CB_GETCURSEL,0,0);if(i!=CB_ERR){preferences.inputChannel=DWORD(i);if(audioCapture)audioCapture->channelMode=unsigned(i);savePreferences();}return 0;}
        if(LOWORD(w)==131 && HIWORD(w)==BN_CLICKED){refreshInputs();return 0;}
        if(LOWORD(w)==113 && HIWORD(w)==CBN_SELCHANGE) {
            selectMidi();savePreferences();return 0;
        }
        if (LOWORD(w) == 111 && HIWORD(w) == CBN_SELCHANGE) {
            traceEvent("audio_device_change");
            selectAudio();
            savePreferences();
            return 0;
        }
        if (LOWORD(w) == 108 && HIWORD(w) == CBN_SELCHANGE) {
            savePreferences();
            return 0;
        }
        if (HIWORD(w) == BN_CLICKED) {
            const auto id = LOWORD(w);
            if(performanceTrace.active() && id<200)traceEvent("host_button",std::to_string(id));
            if (id >= 200 && id < 230) {
                // Contacts are delivered by panelContact before this click.
                return 0;
            }
            if(id==117 || id==118) {
                const bool bridge=SendMessageW(GetDlgItem(h,117),BM_GETCHECK,0,0)==BST_CHECKED;
                const bool zero=SendMessageW(GetDlgItem(h,118),BM_GETCHECK,0,0)==BST_CHECKED;
                session->midiOptions(bridge,zero);savePreferences();return 0;
            }
            if(id==119) {
                const bool enabled=SendMessageW(GetDlgItem(h,119),BM_GETCHECK,0,0)==BST_CHECKED;
                session->recordMidi(enabled);
                SetWindowTextW(midiTraceStatus,enabled?L"Recording latest 2048 messages":L"Capture stopped");return 0;
            }
            if(id==120){saveMidiTrace();return 0;}
            switch (id) {
            case 141:
                savePreferences();break;
            case 142:
                floppyMenu();break;
            case 140:
                hddMenu();break;
            case 100:
                choose(true);
                break;
            case 101:
                choose(false);
                break;
            case 102:
                boot();
                break;
            case 103:
                session->resume();
                break;
            case 104:
                session->pause();
                break;
            case 105:
                session->step(4000000);
                break;
            case 106:
                if(session->snapshot().loaded){pendingFloppy.clear();pendingDirect=preferences.directImageWrites;requestHddAction(HddAction::FloppyMount);break;}
                disk.clear();
                SetWindowTextW(diskLabel, L"Disk: none selected. Boot / reset to apply.");
                savePreferences();
                break;
            case 107:
                savePreferences();
                break;
            case 116: {
                std::ostringstream context;
                if(midiInput)context << "HOST MIDI WinMM messages=" << midiInput->messages.load()
                    << " errors=" << midiInput->errors.load() << " more-data=" << midiInput->moreData.load()
                    << " unsupported=" << midiInput->unsupported.load() << " sink-rejected=" << midiInput->dropped.load();
                if(audioOutput)context<<"\n"<<audioOutput->diagnostics();
                context<<"\n"<<threadCapture.capture();
                session->captureState(context.str());
                break;
            }
            case 114:
                refreshMidi();
                break;
            case 115:
                for(const auto& message:eii::host::MidiHostTools::releaseAllMessages())
                    session->midi(message);
                break;
            case 112:
                refreshAudio();
                break;
            case 110: {
                wchar_t text[257];
                GetWindowTextW(command, text, 257);
                std::string ascii;
                for (auto c : text) {
                    if (!c)
                        break;
                    if (c < 128)
                        ascii += char(c);
                }
                session->serial(ascii);
                break;
            }
            }
        }
        return 0;
    case WM_CLOSE:
        if(!w&&vectorPanel){savePreferences();ShowWindow(h,SW_HIDE);return 0;}
        if(!hddCloseApproved){
            if(hddAction!=HddAction::None&&hddAction!=HddAction::Close){
                if(MessageBoxW(h,L"A disk operation is still pending.\nClose anyway? Unsaved disk changes may be lost.",L"Close",MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;
                traceEvent("close","forced over pending media action");hddAction=HddAction::None;hddCloseApproved=true;
            }else{requestHddAction(HddAction::Close);SetTimer(h,7,4000,nullptr);return 0;}
        }
        if(performanceTrace.active()||traceNeedsSave){
            const auto answer=MessageBoxW(h,L"Save the performance trace before closing?",L"Performance trace",MB_YESNOCANCEL|MB_ICONQUESTION);
            if(answer==IDCANCEL){hddCloseApproved=false;hddAction=HddAction::None;return 0;}
            if(answer==IDYES){togglePerformanceTrace();if(traceNeedsSave){hddCloseApproved=false;hddAction=HddAction::None;return 0;}}
        }
        savePreferences();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);KillTimer(h, 7);
        // Stop the capture producer while COM and the session still exist;
        // it used to be left to static destruction after WinMain returned.
        if(audioCapture)audioCapture->stop();
        vectorPanel.reset();
        filterService.reset();
        midiInput.reset();
        session.reset();
        audioCapture.reset();
        audioOutput.reset();
        audioQueue.reset();
        audioEndpoints.reset();
        DeleteObject(normalFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, message, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    eii::host::nameCurrentThread(L"EII UI");
    if(auto dpi=reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext")))dpi(reinterpret_cast<HANDLE>(INT_PTR(-4)));
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
        return 1;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSW display{};
    display.lpfnWndProc = lcdProc;
    display.hInstance = instance;
    display.lpszClassName = L"EIILcd";
    RegisterClassW(&display);
    WNDCLASSW cls{};
    cls.lpfnWndProc = proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"EIIFirmwarePanel";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    const auto h =
        CreateWindowW(cls.lpszClassName, L"Emulator II - firmware settings / R-W preview",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                      CW_USEDEFAULT, 1065, 945, nullptr, nullptr, instance, nullptr);
    if (!h) {
        CoUninitialize();
        return 1;
    }
    try {
        eii::host::VectorPanelWindow::Hooks hooks;
        hooks.contact=[](auto b,bool down){if(performanceTrace.active())traceEvent(down?"panel_down":"panel_up",std::to_string(unsigned(b)));if(session)session->button(b,down);};
        hooks.voltage=[](unsigned i,double v){SendMessageW(sliders[i],TBM_SETPOS,TRUE,std::clamp(int(v*200+.5),0,1000));if(session)session->voltage(i,v);std::wostringstream label;label<<sliderNames[i]<<L"  "<<std::fixed<<std::setprecision(2)<<v<<L" V";SetWindowTextW(sliderLabels[i],label.str().c_str());};
        hooks.volume=[](float value){if(audioQueue)audioQueue->setOutputGain(value);preferences.outputVolume=DWORD(value*1000+.5f);};
        hooks.inputGain=[](float value){liveInput->gainPot=value;preferences.inputGain=DWORD(value*1000+.5f);};
        hooks.save=[] {savePreferences();};hooks.glyph=glyph;
        hooks.command=[](unsigned id){if(id==123){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}else{if(id==102&&folder.empty()){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}if(id==103&&shown.running)id=104;SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}};
        hooks.close=[] {PostMessageW(window,WM_CLOSE,1,0);};
        std::array<double,6> controls;for(unsigned i=0;i<6;++i)controls[i]=preferences.sliders[i]/200.;
        vectorPanel=std::make_unique<eii::host::VectorPanelWindow>(std::move(hooks),controls,float(preferences.outputVolume)/1000,float(preferences.inputGain)/1000);
        vectorPanel->update(shown);vectorPanel->show();
    }catch(const std::exception& e){MessageBoxW(h,wide(e.what()).c_str(),L"Vector panel unavailable — settings view",MB_ICONERROR);ShowWindow(h,show);}

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if(filterService&&filterService->translate(&msg))continue;
        if (!(msg.hwnd==h||IsChild(h,msg.hwnd)) || !IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return 0;
}


