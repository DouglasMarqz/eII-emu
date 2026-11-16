Emulator II0.57 – host DC és hangkimeneti helyreállítás

Indítás:EII.exe. A ROM, lemez, MIDI/audio beállítás és filterkalibráció
megmarad; a csomag nem tartalmaz ROM-ot vagy hangbankot.

Az új SSM hangút digitális kimenete2Hz-es DC-leválasztást kapott a limiter
előtt. Ez host hangkezelés, nem új, kalibrált EII alkatrészmodell. A VCA
valódi számított offsetje és az elmentett paraméterek megmaradnak. A nagy DC
így nem válik erős kattogássá, ha a pufferhiány digitális nullát illeszt be.
A hiányzó hangadatot ez önmagában nem pótolja.

A WASAPI folyamat leállását a capture most külön mutatja:running,submitted,
events,timeouts,restarts,last-submit-ms,error,last-error. Runtime hiba vagy
500ms-os esemény/progresszióhiány után legfeljebb3 újranyitási kísérlet van.
A firmware nem indul újra. Kézi helyreállítás:Refresh outputs.

Próba:ugyanaz a hangbank és elmentett profil, gyors akkordok, majd hangelengedés.
Ha elnémul,Capture state még újraindítás előtt. Ha a külön szólamhoz kötött
zaj marad, annak javítását ez a változat még nem igazolja.

Natív tesztek és Windows x64 fordítás ellenőrzése külön dokumentálva.
A Windows hangkártyás automatikus újranyitás célgépes ellenőrzése szükséges.
