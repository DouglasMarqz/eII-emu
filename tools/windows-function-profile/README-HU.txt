EII 0.3.7 – külső Windows függvényprofil, 30 másodperc

NINCS új pluginbuild. A telepített, elfogadott0.3.7 változatlanul fut.
A mérő SHA256 alapján ellenőrzi a pontos DLL-t, egy gép- és egy DSP-szálat vár.
A plugin buildreceptje nem módosult, a pluginba nem került új műszer.

1. Bontsd ki a csomagot írható mappába. Egyetlen EII példány legyen a hostban.
   Maradjon SSM192 /128sample /10ms margin /trace OFF, mint az összevetésben.
2. Run-EII-profile.cmd: jobb klikk -> Futtatás rendszergazdaként.
   Az ideiglenes Windows CPU trace miatt kell emelt jogosultság.
3. Add meg a host PID-jét és a terhelés rövid leírását. Indítsd a Marcato
   La Primavera demót; amikor már szól, nyomj Entert.30másodpercig ne válts
   filtert, ne tölts új plugint és ne zárd be a hostot.
4. A végén a ...-RETURN.zip fájlt küldd vissza. Ez már csak a kiválasztott
   EII-szálak címenkénti mintaszámait, modulneveket/címeket és metaadatot tartalmazza.
   Ha hiba van, a kis ZIP-et vagy a hiba szövegét küldd; ne a teljes ETL-t.

A WPR ideiglenesen rendszerszintű CPU-adatot rögzít. A nyers local-only.etl
más folyamatok neveit/útvonalait és rendszeradatokat is tartalmazhat;
HELYBEN MARAD, nincs benne a RETURN.zip-ben. Nincs hang-/ROM-/projektmentés.
A mérő nem állít prioritást, affinitást, registryt vagy profiler-intervallumot.
Saját, egyedi nevű WPR-menetét zárja le; másik felvételt nem töröl.
A mérés és a lemezre írás némi pluszterhelést okozhat. Ezért függvényarányok
vizsgálatára használjuk, nem pontos végleges CPU-százalék igazolására.

A külső ETL-olvasó sikeresen cross-buildelt O3/LTO/eredetiPGO beállításokkal.
Natív Windows/WPR próba itt nem volt. Nullamintás/dekódolási hibás mérésből
nem vonunk le teljesítménykövetkeztetést. Elveszett eseményeket külön jelzi.
Az adat mintavételezett aktuális utasításcím, nem teljes hívási verem és nem
hívásszámláló. Az LTO által beolvasztott függvények költsége a befoglaló
függvényhez kerülhet; a címekből később a forró gépikód-részek is vizsgálhatók.
A helyi ETL csak további helyi elemzéshez kell; a mappája később törölhető.

Ha a mérőablakot erővel bezárod felvétel közben, a helyi profile-... mappában
a Stop-only-this-profile.cmd rendszergazdaként leállítja kizárólag ezt a mérést.
