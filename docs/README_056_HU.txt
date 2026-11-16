Emulator II 0.56 – nonlinear filter stabilitás

Indítás: EII.exe. ROM és lemez nincs a csomagban; a korábbi beállításokat
és az elmentett filterkalibrációt ugyanarról a helyről olvassa.

Javítás: a merev numerikus rendszerben hibás explicit predictor/tartalék lépés
megszüntetése. A hiba külön, firmware nélküli tesztben is előidézhető volt.
Korlátos implicit helyreállítás; sikertelenség esetén az előző véges állapot
marad egy mintáig. A normál szűrőegyenletek és a kalibráció nem változtak.

A régi ütemezési lemaradást nem próbálja korlátlan ideig hangként bepótolni;
a CPU minden emulált órajele továbbra is lefut. Tartós túlterhelést ez nem old
meg. A tényleges gépsebesség és az ütemezési korrekció külön látható.

A Filter service ablakban: recovery/held és friss host drop/missing per sec.
A manuális capture ezenfelül tartalmazza a DSP várakozási idejét és a Windows
multimédia-ütemezési kérések sikerét is. A régi összesített számlálók megmaradtak.

Próba: ugyanazzal a kalibrációval és hangbankkal 96k, majd 192k mód; gyors
akkordok, hangelengedés, majd új hangok. Ha újra szétesik a hang, ments capture-t
még modellváltás előtt. A Windows hangkártyás működést ezen a buildgépen nem
lehetett futtatni; ez célzott javítás tesztváltozata, nem hardverkalibráció.

A modellválasztó és a Legacy út megmaradt. S3528 továbbra is bypass.
