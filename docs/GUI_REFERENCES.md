# Future EII GUI references

User direction, 2026-10-17: scalable vector panel, with a more carefully
finished LCD like the existing EIII emulator. Use manual geometry plus a
colour photograph to establish layout, panel colours, materials and legends.
This is a future presentation task; analog work remains the current priority.

User clarification: a thin functional GUI is sufficient initially. Physical
keyboard/matrix stimulation and an independent MIDI-in path are desired
eventually, but not priorities. Verify actual EII key-contact/scanner hardware
before implementing GUI key timing/velocity; do not assume the GUI keyboard
must synthesize MIDI messages. Resume hardware construction first.

## Existing projects inspected read-only

- https://github.com/Tuth/ms2000r-emu/blob/main/source/gui/vector_panel.h
  Header describes MS2000R owner's manual page 11 / printed page 5 as the
  vector geometry source, with path coordinates extracted using PyMuPDF and
  remaining details checked visually. One backend-independent panel layout
  serves ImGui standalone and JUCE plugin drawing. Colour comment explicitly
  cites the user's hardware photograph. This corroborates the workflow the
  user described; no code has been copied into EII.
- https://github.com/Tuth/Emulated-3/blob/main/ui/panel_view.cpp
  Panel geometry is measured from a reference photograph. LCD drawing uses
  character dots, contrast-dependent ink/ghost colours, rounded dots,
  optional glow, shading and glass reflection. It is not simply a large
  photographed LCD texture. Static panel caching and dirty-region updates
  are relevant efficiency references.
- https://github.com/Tuth/Emulated-3/blob/main/panel/eiii_lcd.h
  Separates LCD controller state, glyph/dot presentation and painting.
  EIII uses a 4x20 layout; EII's current target is 2x16. Do not copy EIII
  row mapping or controller behavior without checking EII hardware.

## EII manual check

The supplied EMU-Emulator-II-Owners-Manual.pdf has 226 PDF pages and a mix
of text/vector primitives and embedded raster diagrams. The initial
inspection did not locate an MS2000R-style complete extractable front-panel
vector figure. PDF being partly vector does not establish that the panel
illustration itself is vector. Service-manual schematics are scanned.
Continue locating and visually checking any candidate figure before treating
it as a geometric source; a clear user-supplied panel photo may be needed.

## Implementation direction

Keep panel geometry in logical coordinates and renderer-independent drawing
primitives. Separate static paint, changing controls/LEDs, input hit regions
and LCD presentation. Use the user's requested photographs as visual references
when supplied, and preserve the EII's own appearance.

Render LCD contents from actual emulated DDRAM/CGRAM/controller state.
Current Lcd/report is an early subset; do not substitute canned screen
strings or assume complete CGRAM, cursor/blink and shift behavior. A richer
renderer must follow expanded controller support. No new GUI code or mockup
has been added in this reference checkpoint.
