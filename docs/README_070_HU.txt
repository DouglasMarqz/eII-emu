EMULATOR II - 0.70 NATIVE DAC / DEGLITCH TESZTBUILD

Indítás: EII-Bringup.exe. A korábbi ROM-, lemez-, eszköz- és
filterbeállításokat továbbra is a mentett beállításokból olvassa.
A csomag nem tartalmaz ROM-ot, banklemezt vagy privát kalibrációt.

Az új út alapból KIKAPCSOLVA marad. Kipróbálás:
1. SETTINGS / DEVICES.
2. Válassz SSM2045 nonlinear (96k DSP) modellt.
3. Kapcsold be a Native DAC + deglitch (test) jelölőt.
4. Az S3528 reconstruction jelölő ettől függetlenül kapcsolható.
A két beállítást menti. Legacy módban az új út nem aktív.
A Native jelölő kikapcsolásával visszatérsz a 0.69 referencia-hangúthoz.

Mi változott?
A DAC-kód, a fázisok, a CV-k és a kapcsolójelek saját hardveres
időbélyeggel jutnak el a DSP-hez. A 13202/TL084 névleges kondenzátoros
deglitch modell is beköthető. Az analóg fázisok megfigyelése nem sűríti
a digitális CPU/controller időzítőjének kiszolgálását.

Ez NEM globális 48 -> 27,777 kHz átállítás. A digitális mintaléptetés
az eredeti firmware időzítőit követi, a lejátszási sebesség hangonként
változhat. A 48 kHz a PCM-átadás üteme; az SSM megoldója 96/192 kHz-en
számol. Az időbélyegek miatt a DAC váltásai nem kerekítődnek 48 kHz-re.

FONTOS: az új névleges átvitel jelentősen halkabb lehet a régi, állandó
-50 erősítést használó közelítésnél. A Marcato tesztben az RMS kb. 19 dB-lel
alacsonyabb volt. Az abszolút analóg átvitel és a hangszín még ellenőrzendő;
nem állítjuk, hogy a fizikai hardverhez már kalibrálva van. Nincs automatikus
hangerő-kompenzáció. Emiatt ez kapcsolható tesztút, nem kötelező csere.

Ellenőrzések:
- 212378 pontosan egyező DAC/fázisesemény a teljes 50 ns referenciához.
- Azonos digitális állapot és digitális időzítő-eseményszám.
- Valódi firmware, Marcato bank és La Primavera szekvencia.
- 288000 PCM frame: nincs csomagvesztés, sorrendhiba vagy túlcsordulás.
- 0,5/1/2 ms csomagolás: bitazonos PCM.
- S3528 kapcsolása bizonyítottan megváltoztatja a végső PCM-et.
- Megmaradt a régi hangút bitazonos referencia-ellenőrzése.

A CPU-adatok Linux próbapadon készültek; nem Windows mérések.
Az új, részletesebb hangút többletköltségét külön kell mérni.
Ez a build LLVM-MinGW 20261006 O3/LTO, EII_DIAGNOSTICS=0 fordítás.
A fordítás és a natív tesztek sikeresek; Windows futtatást/hallgatást
itt nem végeztünk.

RECORD TRACE és manuális capture:
native_dac / nativeDAC jelzi az aktív új utat.
native_overflow / eventOverflow és native_order_errors /
eventOrderErrors normál működésben 0 kell legyen.

Ismert modellkorlátok: névleges nulla kezdeti töltés, ideális műveleti
erősítő; nincs mért zaj, töltésinjektálás, tápfeszültség-korlát és véges
műveleti erősítő-sávszélesség. Az S3528 korábbi dokumentált görbeillesztése
maradt meg. A mintavételi/recording SAR bemeneti út továbbra sincs kész.
