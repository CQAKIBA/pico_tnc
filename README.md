# PICO TNC

PICO TNC is the Terminal Node Controler for Amateur Packet Radio powered by Raspberry Pi Pico.

This TNC has same functionality as WB8WGA's PIC TNC.

## PIC TNC features

- Encode and decode Bell 202 AFSK signal without modem chip
- Digipeat UI packet up to 1024 byte length
- Send beacon packet
- Support converse mode
- Support GPS tracker feature
- Support both USB serial and UART serial interface

## Additional features

- Support KISS mode
- Support multi-port up to 3 ports

## Help command

- `help` : English help
- `help ja` / `help ja sjis` : Japanese help in Shift_JIS
- `help ja utf8` : Japanese help in UTF-8
- If `MYCALL` or `UNPROTO` is unset, help shows a warning message to set both values.
- `txdelay n|nms|ns` : TX delay (`0..1000ms`, unitless `n` keeps legacy `10ms` units)
- `axdelay n|nms|ns` : AX.25 preamble delay (`0..1000ms`, unitless `n` keeps legacy `10ms` units)
- `axhang n|nms|ns` : hold PTT after frame end (`0..1000ms`, unitless `n` keeps legacy `10ms` units)
- `privkey show` : display persisted key material after interactive security confirmation
- `privkey gen [m|p|mona1|p2pkh|p2sh|p2wpkh]` : generate and store Monacoin private key (32-byte raw + compressed + active type)
- `privkey set [m|p|mona1|p2pkh|p2sh|p2wpkh|WIF|RAW]` : set active address type only (`m/p/mona1/...`) or import/store private key (`WIF/RAW`); typed WIF updates active type, untyped WIF/RAW keeps current type

## How to build

```
git clone https://github.com/amedes/pico_tnc.git
cd pico_tnc
mkdir build
cd build
cmake ..
make -j4
(flash 'pico_tnc/pico_tnc.uf2' file to your Pico)
```
![bell202-wave](bell202-wave.png)
![command line](command.png)
[![schemantic](schematic.jpg)](schematic.png)
