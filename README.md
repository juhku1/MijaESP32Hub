# BLE Live Monitor

**⚠️ ALPHA-VERSIO** - Projekti on aktiivisessa kehityksessä ja kaikki ominaisuudet eivät ole vielä valmiit.

ESP32-S3 -pohjainen BLE-laitteiden monitorointijärjestelmä, joka kerää ja näyttää BLE-sensoreiden (kuten Xiaomi Mijia) tietoja web-käyttöliittymässä.

## Ominaisuudet

### ✅ Toteutettu ja testattu
- **WiFi-provisiointi**: AP-tila (BLE_Monitor_Setup) ensimmäisellä käynnistyskerralla
- **Gateway-rooli**: Kerää BLE-sensoreiden tietoja ja näyttää ne web-UI:ssa (**toimii**)
- **Jatkuva BLE-skannaus**: Päivittää sensoreiden arvot reaaliajassa
- **Discovery-tila**: 30 sekunnin ikkuna uusien laitteiden löytämiseen
- **Web-käyttöliittymä**: Näyttää laitteet ja niiden tiedot (lämpötila, kosteus, akku)
- **Laitehallinta**: 
  - Piilota/näytä laitteet
  - Nimeä laitteet
  - Valitse näytettävät kentät
- **NVS-tallennus**: Asetukset ja laitteet säilyvät uudelleenkäynnistyksissä
- **BOOT-napin nollaus**: Pidä BOOT-nappia 5s pohjassa → tehdasasetukset

### 🚧 Keskeneräiset / Testaamatta
- **Sensor-rooli**: Ohjelmallisesti toteutettu, mutta **ei vielä testattu**
- **mDNS-tuki**: Automaattinen keskittimen löytäminen (väliaikaisesti pois käytöstä)
- **Sensor → Gateway -kommunikaatio**: HTTP POST -lähetys keskittimelle (ei toteutettu)
- **Pilviyhteys**: Datan lähetys pilvipalveluun

## Laitteisto

- **ESP32-S3 DevKitC-1** (4MB Flash)
- BLE 5.0 -tuki
- WiFi 2.4 GHz

## Asennus

### Vaatimukset
- [PlatformIO](https://platformio.org/)
- USB-kaapeli ESP32-S3:n ohjelmointiin

### Käännös ja lataus
```bash
# Käännä projekti
pio run -e esp32-s3-devkitc-1

# Lataa laitteeseen
pio run --target upload -e esp32-s3-devkitc-1

# Avaa sarjamonitori
pio device monitor -e esp32-s3-devkitc-1
```

## Käyttöönotto

### 1. Ensimmäinen käynnistys
- Laite luo WiFi AP:n: **BLE_Monitor_Setup** (salasana: `12345678`)
- Yhdistä AP:hen ja avaa selaimella: `http://192.168.4.1`
- Valitse rooli:
  - **Gateway**: Kerää ja näyttää BLE-sensoreiden tietoja
  - **Sensor**: Skannaa BLE-laitteita ja lähettää datan keskittimelle
- Syötä WiFi-verkko ja salasana
- (Sensoreille) Syötä keskittimen IP-osoite
- Laite käynnistyy uudelleen ja yhdistää verkkoon

### 2. Normaali käyttö (Gateway)
- Laite yhdistyy WiFi-verkkoon
- Tarkista IP-osoite sarjaportista tai reitittimestä
- Avaa web-UI: `http://<laitteen-ip>`
- BLE-skannaus alkaa automaattisesti (30s discovery-tila)
- Laitteet ilmestyvät listalle sitä mukaa kun niitä löytyy
- Käynnistä uusi discovery-skannaus "Start Discovery Scan" -napista

### 3. Laitehallinta
- **Piilota laite**: Klikkaa "Hide" (laite ei näy listalla, mutta data kerätään)
- **Nimeä laite**: Syötä nimi kenttään ja klikkaa "Update"
- **Näytä MAC-osoite**: Valitse "Show MAC Address"
- **Valitse kentät**: Valitse mitä tietoja näytetään (temp, hum, bat, batMv, rssi)

### 4. Tehdasasetusten palautus
- Pidä **BOOT-nappia** (sinisen LEDin lähellä) 5 sekuntia pohjassa
- Laite nollaa asetukset ja käynnistyy uudelleen setup-tilaan

## Arkkitehtuuri

Katso [ARCHITECTURE.md](ARCHITECTURE.md) yksityiskohtaiselle kuvaukselle järjestelmästä.

## Kehitystila

Projekti on **alpha-vaiheessa**. Odotettavissa olevia muutoksia:

- [ ] mDNS-automaattihaku (sensorit löytävät keskittimen ilman IP:tä)
- [ ] Sensor-to-Gateway HTTP POST -toteutus
- [ ] Pilviyhteys (AWS IoT / Azure IoT Hub)
- [ ] HTTPS-tuki
- [ ] OTA-päivitykset
- [ ] Käyttäjätunnistautuminen

## Tunnetut ongelmat

- **Sensor-rooli ei testattu**: Setup ja konfiguraatio toimii, mutta sensorin toimintaa ei ole vielä käytännössä kokeiltu
- mDNS-tuki väliaikaisesti pois käytöstä (build-cache -ongelma)
- Sensorirooli ei vielä lähetä dataa keskittimelle (toteutus puuttuu)
- Web-UI:n ulkoasu on minimalistinen

## Lisenssi

Projekti on kehitysvaiheessa. Lisenssi määritellään myöhemmin.

## Tekijä

Juha-Matti Kuusisto

## Linkit

- GitHub: [juhku1/MijaESP32Hub](https://github.com/juhku1/MijaESP32Hub)
- ESP-IDF: v5.5.0
- PlatformIO: Espressif 32 platform v6.12.0
