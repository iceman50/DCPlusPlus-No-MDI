<!-- Copyright (C) 2026 iceman50 -->

# Experimental ZST1 transfer protocol

This patch proposes **ZST1**, a client-to-client capability for Zstandard binary
transfers. It is an experimental extension, not a registered ADC/NMDC standard.
It builds on existing GET/SND framing; it does not change hub traffic, ZPipe,
ZLIF, TLS, file identifiers, TTH verification, or list XML syntax.

## Capability and commands

* ADC: advertise `ADZST1` in `CSUP`, alongside `ADZLIG`.
* NMDC: advertise `ZST1` in `$Supports`, alongside `ADCGet` and `ZLIG`.
* Request Zstandard with `ZS1` in `CGET` / `$ADCGET`.
* Confirm it with `ZS1` in `CSND` / `$ADCSND`.
* `ZS1` and `ZL1` are mutually exclusive. Unsolicited Zstandard replies and
  conflicting requests/replies are rejected.
* Positions and lengths describe **uncompressed** bytes, just as for ZL1.
* A sender may decline compression by omitting the compression flag in SND.
  Receivers select decoding from SND, not merely from their request.

ADC example (newline terminators):

```text
CSUP ADBASE ADTIGR ADBZIP ADZLIG ADZST1
CGET file files.xml 0 -1 ZS1
CSND file files.xml 0 123456789 ZS1
<one Zstandard frame expanding to 123456789 bytes>
```

NMDC example (pipe terminators):

```text
$Supports MiniSlots XmlBZList ADCGet TTHL TTHF ZLIG ZST1|
$ADCGET file files.xml 0 -1 ZS1|
$ADCSND file files.xml 0 123456789 ZS1|
<one Zstandard frame expanding to 123456789 bytes>
```

The same flag applies to ordinary `file TTH/...`, `list /Directory/`, recursive
`list ... RE1`, and `tthl` requests. A ZST1 implementation must support XML full
lists (`files.xml`), including NMDC ADCGet requests. A new filelist filename or
separate command is unnecessary: transfer compression is independent of the
resource name. `files.xml.bz2` remains available to all existing clients.

Each transfer is exactly one ordinary Zstandard frame without a dictionary,
skippable frame, or concatenated frames. The maximum window is 8 MiB. This
implementation uses level 6 and enables the frame checksum. Decoders bound
window memory and output length, reject corruption and trailing input, and
require both frame completion and the declared uncompressed length. Even an
empty transfer must send/consume its complete frame.

## Settings and compatibility

Under **Experimental > Transfers and hashing**, **Prefer Zstandard over zlib
when the peer supports both** (`PreferZstd`) defaults to **off**. The existing
Compress transfers setting remains the master switch for transfer compression.
Capable clients advertise both codecs while that switch is enabled, allowing a
remote downloader to choose. The preference controls outgoing download requests.

With the preference off, existing zlib and bzip2 choices are retained when
available. With it on, Zstandard is requested only if the peer advertises ZST1.
A peer offering only Zstandard can use it regardless of the preference. Older
peers continue to receive existing requests. Full bzip2 lists are not wrapped
in another compression stream. Zstandard full lists request XML instead.
This extension does not advertise a `files.xml.zst` resource. Changing the
transfer preference affects new transfers.

## References

* [ADC extensions, ZLIG](https://adc.sourceforge.io/ADC-EXT.html)
* [Zstandard library](https://github.com/facebook/zstd/tree/v1.5.7)
* The repository's `NMDC extensions.txt` describes ADCGet and XmlBZList.

## Validation

`testzstd.cpp` exercises transfer framing, fragmented/incompressible/empty data,
corrupt/truncated input, output limits, and the request selection matrix.
Run these with the existing zlib, ADC, NMDC, and connection tests. Mixed-client
interoperability still needs testing with another implementation of the proposed
extension.
