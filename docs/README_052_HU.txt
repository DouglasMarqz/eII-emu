Emulator II - 0.52 - választható SSM2045 VCF/VCA

A jobb felső Chip model menüben:
  Legacy VCF/VCA (zero-Q): a stabil korábbi hangút, alapértelmezett.
  SSM2045 nonlinear (96k DSP): rezonanciás, nemlineáris chipmodell.
  SSM2045 nonlinear (192k DSP): ugyanez nagyobb belső mintavétellel.

Mindkét új mód külön DSP-szálon fut, nyolc külön chipállapottal. Élőben váltható; nem indul újra a firmware, de az audioállapot és puffer törlése rövid kattanást/szünetet okozhat. A választást megjegyzi.

A firmware saját FCV/QCV jelei vezérlik a chipet. A QCV-áram és VCA-bias még névleges panelközelítés. Behaviorális/SPICE-illesztett modell, nem hardveresen kalibrált. Nincs zaj/thermal; a VCA összvezérlési árama500uA. Az S3528 továbbra is bypass, a teljes analóg jelút kalibrációja külön feladat.

Capture state: HOST CHIP sor megmutatja a módot, DSP-puffert és dropped/failures számlálókat. Ha a dropped nő vagy recseg, válts96k-ra vagy Legacy-ra. A korlátos DSP-puffer nem növekedhet végtelen késleltetésre. Az új módok CPU-igénye nagyobb; a stabil Legacy megmaradt.

MIDI-hiba esetén játék előtt Capture MIDI, utána Capture state és Save trace. ROM/bank nincs a csomagban. Windows-fordítás nem jelent itt elvégzett Windows-hangtesztet. BUILD.json: forrás és bináris-azonosítók.

A jelenlegi mérőgépen a nyolchangú valós idejű próba a nonlinear módokban puffervesztést mutatott (96k:624,192k:29664 képkocka/193920). Ez itt nem realtime teljesítmény-PASS. A gyorsabb célgépen külön próba szükséges. A pontos működést külön offline teszt ellenőrzi.
