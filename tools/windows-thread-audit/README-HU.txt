EII 0.3.7 – külső Windows szálankénti CPU-mérés

Ez mérőeszköz, NEM új plugin vagy javítás. Nem módosítja a hostot, a plugint,
a prioritást, a CPU-hozzárendelést vagy a rendszerbeállításokat.
Külön PowerShell folyamatból, másodpercenként kérdezi le a host számlálóit.
A mérő saját költsége nem része a host folyamat CPU-adatának, de a rendszer
ütemezését minimálisan ez a külső mérés is befolyásolhatja.

1. Bontsd ki a csomagot írható mappába. Nyisd meg a hostot és a 0.3.7-et.
   Egyetlen EII példány legyen, ha a motorok számát akarjuk ellenőrizni.
2. Indítsd a Run-thread-audit.cmd fájlt. A felsorolt host PID-jét írd be
   (a PID-et, nem a lista sorszámát). Adminisztrátori jogot nem kér.
   Ha a host emelt joggal fut és nem olvasható, a mérés hiányos lehet.
3. Két percig maradjon ugyanaz a terhelés és beállítás. Ha idővel nő a CPU,
   akkor kezdd akkor, amikor már látható a növekedés. Ne válts filtert közben.
4. Az elkészült results-....zip fájlt küldd vissza. Írd mellé: host neve,
   SSM96 vagy SSM192, alapjárat vagy folyamatos MIDI, trace OFF/ON.
   Ha a jelenség később indul, egy korai és egy késői mérés összevethető.

A CSV-k szálankénti CPU-időt, szálneveket, szálak számát és host memóriaméretet
rögzítenek. Nem tartalmaznak hangot, projektet, ROM-ot vagy lemezképet.
EII VST3 machine = gépemuláció; EII SSM2045 = DSP-munkaszál (Legacy is ezt
az általános nevet használja). Több példány több ilyen szálat jelenthet.
100% a szál oszlopában egy teljes logikai CPU; a process.csv teljes gépre
normalizált értéke közelíti a Feladatkezelő folyamat CPU-százalékát.
A szál hiánya/üres neve lehet lekérdezési korlát is, önmagában nem bizonyíték.

A script itt statikusan ellenőrzött; natív Windows/PowerShell futtatás nem állt
rendelkezésre. A korábbi 0.3.7 plugin változatlan marad.
