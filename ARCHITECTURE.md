# BLE Live Monitor - Arkkitehtuuri

## Yleiskuvaus

BLE Live Monitor on ESP32-pohjainen järjestelmä BLE-mittareiden seurantaan. Järjestelmä tukee sekä yksittäistä laitetta että hajautettua arkkitehtuuria, jossa useita sensorilaitteita lähettävät tiedot yhdelle keskittimelle.

## Laitteen roolit

### 1. ROLE_UNCONFIGURED (0)
- Oletustila ensimmäisellä käynnistyksellä
- Laitteella ei ole WiFi-asetuksia
- Käynnistää WiFi Access Point -tilan
- Tarjoaa setup-sivun osoitteessa http://192.168.4.1
- AP-verkko: "BLE_Monitor_Setup" (salasana: "12345678")

### 2. ROLE_SENSOR (1)
- Skannaa BLE-mittareita omalla alueellaan
- Lähettää havaintonsa keskittimelle HTTP POST -pyynnöllä
- Ei tallenna laitelistaa pysyvästi
- Tarvitsee keskittimen IP-osoitteen asetuksissa

### 3. ROLE_GATEWAY (2)
- Skannaa BLE-mittareita ja kerää tiedot
- Vastaanottaa sensorien lähettämän datan
- Tallentaa laitteet NVS:ään
- Tarjoaa web-käyttöliittymän kaikkien mittareiden hallintaan
- Lähettää yhdistetyn datan pilveen (tuleva ominaisuus)

## Käyttöönotto

### Ensimmäinen käynnistys
1. ESP32 käynnistyy ROLE_UNCONFIGURED -tilassa
2. Laitteella ei ole WiFi-asetuksia -> käynnistyy AP-tila
3. Yhdistä WiFi-verkkoon "BLE_Monitor_Setup"
4. Avaa selain: http://192.168.4.1
5. Täytä lomake:
   - WiFi-verkon nimi (SSID)
   - WiFi-salasana
   - Laitteen rooli: Keskitin tai Sensori
   - (Sensoreille) Keskittimen IP-osoite
6. Tallenna -> Laite käynnistyy uudelleen valitussa roolissa

### Normaali käyttö
- Laite yhdistyy määriteltyyn WiFi-verkkoon
- Toimii valitun roolin mukaisesti
- Web-käyttöliittymä saatavilla laitteen IP-osoitteessa

## BLE-skannaus

### Jatkuva skannaus
- BLE-skannaus käynnistyy automaattisesti ja pyörii JATKUVASTI
- Olemassa olevien laitteiden arvot päivittyvät reaaliajassa
- Ei kuormita verkkoa turhaan pysäytys/käynnistys-sykleillä

### Discovery mode
- Uusia laitteita lisätään VAIN discovery-moden aikana
- Käyttäjä käynnistää discovery-moden: `/api/start-scan` (POST)
- Kesto: 30 sekuntia
- Skannaus jatkuu normaalisti moden päätyttyä

## NVS-tallennukset

### Config namespace
- `wifi_ssid`: WiFi-verkon nimi
- `wifi_pass`: WiFi-salasana
- `role`: Laitteen rooli (0/1/2)
- `gateway_ip`: Keskittimen IP (sensoreille)

### Devices namespace
- Laitteiden tiedot (MAC, nimi, näkyvyys, asetukset)
- Vain keskittimellä

## API-endpointit

### Setup-tila (ROLE_UNCONFIGURED)
- `GET /` - Setup-sivu
- `POST /api/setup` - Tallenna asetukset ja käynnistä uudelleen

### Normaali tila
- `GET /` - Web-käyttöliittymä
- `GET /api/devices` - Palauta laitteet JSON-muodossa
- `POST /api/start-scan` - Käynnistä discovery mode (30s)
- `POST /api/toggle-visibility` - Vaihda laitteen näkyvyys
- `POST /api/update-settings` - Päivitä laitteen asetukset

### Keskitin-tila (ROLE_GATEWAY)
- `POST /api/sensor-data` - Vastaanota sensorin lähettämä data

## Hajautettu arkkitehtuuri

### Esimerkki: 4 laitetta talossa

```
┌─────────────┐
│  Sensori 1  │─┐
│ (makuuhuone)│ │
└─────────────┘ │
                │
┌─────────────┐ │    ┌─────────────┐
│  Sensori 2  │─┼───▶│  Keskitin   │──▶ Pilvipalvelu
│ (olohuone)  │ │    │  (etuhalli) │
└─────────────┘ │    └─────────────┘
                │
┌─────────────┐ │
│  Sensori 3  │─┘
│  (kellari)  │
└─────────────┘
```

### Tiedonkulku
1. Jokainen sensori skannaa BLE-mittareita omalla alueellaan
2. Sensorit lähettävät havainnot keskittimelle HTTP POST:lla
3. Keskitin yhdistää kaikkien tiedot
4. Web-UI näyttää kaikki mittarit keskitetysti
5. Keskitin lähettää yhteenvedon pilveen

## Asetukset

### Factory reset
- Poista `config` namespace NVS:stä -> laite palaa setup-tilaan
- Voidaan toteuttaa esim. napista tai web-UI:sta

### Roolin vaihtaminen
- Vaatii uuden setupin (factory reset)
- Tai: lisätään web-UI:hin roolin vaihtomahdollisuus

## Tulevat ominaisuudet

- [ ] Sensori lähettää datan keskittimelle (HTTP client)
- [ ] Keskitin parsii ja yhdistää sensorien datan
- [ ] Pilvi-integraatio (MQTT, HTTP)
- [ ] Hälytykset ja logiikka
- [ ] OTA-päivitykset
- [ ] mDNS (löydä keskitin automaattisesti)
