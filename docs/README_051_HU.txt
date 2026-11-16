Emulator II - 0.51

Javítás: az All Notes Off előbb felengedi a sustain pedált (CC64=0), majd CC123-at küld mind a 16 MIDI-csatornára, a valódi firmware útvonalán. A lassú PAD release ettől még normálisan lecseng; ez nem azonnali némítás.

A Capture state új CC64 enqueued seen_mask/down_mask sora a host által sorba állított üzeneteket mutatja, nem a firmware belső állapotát.

Ha a hang újra beragad: játék ELŐTT kapcsold be a Capture MIDI-t, hibánál ments Capture state-et és Save trace-et. A korábbi hiba pontos üzenetsora még nem áll rendelkezésre.

Az analóg modell és a keverő headroomja ebben a verzióban nem változott; a szűrő/resonance és a presetek közti túlvezérlés külön feladat marad. Natív tesztek: PASS. A Windows futtatási/hallgatási tesztet ez a csomag nem helyettesíti.

ROM és bank nincs a csomagban. BUILD.json tartalmazza a forrás-commitot és a bináris SHA256-ot.
