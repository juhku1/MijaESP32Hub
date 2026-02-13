# Serial monitor - pikaopas (PlatformIO)

## Käynnistys
- **Serial monitor (115200):**
  - `pio device monitor -b 115200`
- **Serial monitor tietylle portille:**
  - `pio device monitor -p /dev/ttyUSB0 -b 115200`

## Upload (firmware)
- **Build + upload (esp32-s3-devkitc-1):**
  - pio run -e esp32-s3-devkitc-1 -t upload
  pio device monitor -p /dev/ttyACM0 -b 115200

## Portin sulkeminen
- **Pysäytä monitori:**
  - `Ctrl + C`

## Portin listaus ja tarkistus
- **Listaa laitteet/portit:**
  - `pio device list`

## Yleisiä vinkkejä
- Jos portti on varattu, sulje kaikki muut ohjelmat, jotka käyttävät samaa sarjaporttia.
- Varmista oikea baudinopeus (yleensä 115200).
- Jos laite ei näy, irrota ja kytke USB-kaapeli uudelleen.
