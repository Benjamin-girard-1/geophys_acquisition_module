# Communication Protocol

## Document status

- Status: Authoritative wire protocol
- Scope: Commands and data blocks shared by UART-to-USB and future Bluetooth control
- Authority: This is the sole protocol-definition document

This document defines the complete wire protocol, including framing, byte order,
CRC, command identifiers, command results, payload layouts, data blocks, command
behavior, and failure visibility.

## Commands rules

Every command has exactly one expected reply type.
Each command sent by the host expect a reply from the device. Some commands from the device can be asynchronous but are used in the special cases of data streaming. This asynchronous command doesn't require replies. Receiving an asynchronous message does not complete the pending command.

Timeouts and retries are handled by the host.

Only one host command may be outstanding at a time. An asynchronous `\DAT` message does not satisfy the outstanding command.

All multi-byte integers are encoded in little-endian byte order, including the CRC-32 field. All reserved and padding bytes must be set to zero.

A request with an invalid CRC-32 is silently discarded by the device. The host detects the discarded request through its timeout.

Read-only commands may be retried after a timeout. Before retrying a state-changing command, the host must query the current device state to determine whether the original command was applied.

The counters start at 0 at power on or reset of the device. Blocks are a 512 byte data structure that have a 28 byte header, 480 byte of samples data and 4 bytes of CRC32. There is a counter for to keep track of the order of sampling. This number is related to acquisition only. This 512 byte block of data is use to store data in RAM, in SD storage and as the framing to send data from the device to a host.

The synchronization value for the standard commands is "\CMD" in ASCII (5C 43 4D 44). The streaming blocks coming from the devices are asynchronous commands from the device and start with the synchronisation word "\DAT" in ASCII (5C 44 41 54). 

The counters can overflow and it's okay, the monotonic timer will never overflow so it can be used to keep track of the data order.

The device can only be connected to one host at the time. If there is a USB connection the device should not engage in Bluetooth connectivity. However, USB has priority and if connected it will disconnect the Bluetooth and use USB instead.

A valid `HELLO` received over USB establishes the USB session.

After five seconds without valid USB activity, USB loses priority and Bluetooth may connect again.

A recording continues if its host connection disappears; live streaming stops.

After a `HELLO` command the host will send `DEVICE_GET_CONFIG`. After that the host will send `RECORDING_GET_NUMBER` and will send the command `RECORDING_GET_INFO` a number of time require to get information about all the files.

The command `DEVICE_GET_CONFIG` is run every second to keep connection established.

This is the CRC-32 enforced in this protocol:
|---|---|
| Name:  | CRC-32/ISO-HDLC (IEEE 802.3) |
| Width: | 32 |
| Poly:   | 0x04C11DB7 |
| Init:    | 0xFFFFFFFF |
| RefIn:   | true |
| RefOut:  | true |
| XorOut:  | 0xFFFFFFFF |
| Check:   | CRC("123456789") = 0xCBF43926 |

Command result and its meaning, the replies of the commands have the byte 12 reserved for those possible codes:
|---|---|
| 0x00 | Success |
| 0x01 | Invalid argument |
| 0x02 | Invalid state |
| 0x03 | Busy |
| 0x04 | Unsupported |
| 0x05 | Not found |
| 0x06 | Already exists |
| 0x07 | Not ready |
| 0x08 | Timeout |
| 0x09 | Storage media absent |
| 0x0A | Storage full |
| 0x0B | I/O error |
| 0x0C | Integrity error |
| 0x0D | Hardware fault |
| 0x0E | Internal firmware error |
| 0x0F | Limit reached |

## Discovery and state

### `HELLO` -> `DEVICE_INFO`

The purpose of this command is to establish protocol compatibility before any state-changing command.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD"  | Synchronization value |
| 4 | 2 | 0x0001 | Command ID |
| 6 | 1 | 0x00   | Command direction: 0x00 goes to the device; 0x01 goes to the host |
| 7 | 4 | 0x0000 | Reserved |
| 11 | 1 | 0x0000 | Number of bytes in the payload |
| 12 | 48 | 0x00...00 | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x00a1 | Command ID |
| 6 | 1 | 0x01   | Command direction: 0x1 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x10 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4  | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 6 | - | ESP32 MAC address |
| 19 | 2 | - | Hardware version |
| 21 | 2 | - | Hardware revision |
| 23 | 4 | - | Firmware version |
| 27 | 1 | - | Protocol version |
| 28 | 32 | 0x00...00 | Empty padding |

Identity-field conventions:

- The MAC address is serialized in canonical display order: the first octet
  returned by the ESP32 factory base-MAC API is stored at offset 13.
- Hardware version is the numeric product generation. V2 hardware reports
  `0x0002`.
- Hardware revision is the numeric PCB revision. Rev-1 reports `0x0001`.
- Firmware version is an opaque unsigned 32-bit application version. The
  current unreleased development firmware reports zero; no semantic-version
  bit packing is implied by this field.
- The current wire-protocol version is `0x01`. It changes only when an
  incompatible wire-contract revision requires host compatibility handling.

### `DEVICE_GET_CONFIG` / `DEVICE_SET_CONFIG` -> `DEVICE_CONFIG`

The command `GET_CONFIG` is used to read the current config and has an empty request. It return the structure as shown below. The command `SET_CONFIG` use the same structure as the answer but the payload is applied where applicable. The read only registers are not considered. After a `SET_CONFIG` command the device answer back with a `CONFIG` echo to see the applied values.

A new configuration can be sent at any time using the `SET_CONFIG`. In case that there is a live stream in progress, it is automatically applied. If there is a recording in progress, this recording has to be stopped before any modification of the config. So if the command `SET_CONFIG` is sent while there is a recording, it will only echo the unmodified config with the Recording in progress and no changes applied. In case of both live stream and recording are taking place new config will also be rejected due to the requirement to not have recording in progress for any modifications.

When using only 4 channels it is 40 conversions per payload block, when all 8 channels are being used, it is 20 conversions per block.

No partial record should be emitted.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | - | Command ID: 0x02 GET_CONFIG; 0x03 SET_CONFIG |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device|
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | - | Number of bytes in the payload: 0x00 for `DEVICE_GET_CONFIG`; 0x27 for `DEVICE_SET_CONFIG` |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x00a2 | Command ID |
| 6 | 1 | 0x01   | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x000000000 | Reserved |
| 11 | 1 | 0x28 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Config payload structure:

| Offset | Size | Action | Notes |
|---:|---:|:---|---| 
| 12 | 1 | R  | Command result |
| 13 | 8 | R  | Monotonic timestamp 64 bits at 10 MHz (100ns increments) |
| 21 | 1 | R  | Recording in progress: 0x00 no recording; 0x01 recording in progress |
| 22 | 1 | R  | Card slot 1 info: 0x00 absent; 0x01 magnetic; 0x02 acc_geoph |
| 23 | 1 | R  | Card slot 2 info: 0x00 absent; 0x01 magnetic; 0x02 acc_geoph |
| 24 | 1 | RW | ADC sampling rate: 0x00 0.5kHz; 0x01 1kHz; 0x02 2kHz; 0x03 4kHz; 0x04 8kHz; 0x05 16kHz |
| 25 | 1 | RW | ADC channel active mask: 0x00 no active channels; 0x0f channels 0 to 3 active; 0xf0 channels 4 to 7 active; 0xff channels 0 to 7 active, only those mask are valid |
| 26 | 2 | RW | ADC gain: 0b00 x1; 0b01 x2; 0b10 x4; 0b11 x8. Each channel is shifted  by 2 times the number of the channel (<<(2*id)) |
| 28 | 2 | R  | ADC temperature: signed 16 bits, 0.01°C per count|
| 30 | 1 | RW | 0x00 +3.3VA off; 0x01 +3.3VA on |
| 31 | 1 | RW | 0x00 +5VA off; 0x01 +5VA on |
| 32 | 1 | RW | 0x00 +9VA off; 0x01 +9VA on |
| 33 | 1 | RW | 0x00 -5VA off; 0x01 -5VA on |
| 34 | 1 | RW | 0x00 +18VA off; 0x01 +18VA on |
| 35 | 1 | R  | 0x00 Solar not present; 0x01 Solar present |
| 36 | 1 | R  | 0x00 +5V USB not present; 0x01 +5V USB present |
| 37 | 1 | R  | GNSS state: 0x00 disabled/not present; 0x01 ready; 0x02 faulted; 0x03 searching |
| 38 | 1 | R  | GNSS Satellite count |
| 39 | 1 | R  | IMU state: 0x00 disabled/not present; 0x01 ready; 0x02 faulted/invalid data |
| 40 | 2 | RW | IMU averaging time ms |
| 42 | 2 | R  | IMU roll: signed 16 bits, 0.01° per count, 0° being perfectly leveled |
| 44 | 2 | R  | IMU pitch: signed 16 bits, 0.01° per count, 0° being perfectly leveled |
| 46 | 2 | R  | IMU temperature: signed 16 bits, 0.01°C per count |
| 48 | 1 | R  | SD card present: 0x00 not present; 0x01 present; 0x02 faulted |
| 49 | 2 | R  | ESP32 temperature: signed 16 bits, 0.01°C per count |
| 51 | 1 | R  | Error! Check command diagnostic: 0x00 no error; 0x01 check diagnostic |
| 52 | 8 | -  | Empty padding |

### `DEVICE_GET_DIAGNOSTIC` -> `DEVICE_DIAGNOSTIC`

This command is used when the flag error is raised in a config command. It give detail information about the error that happened, including when and what happen.

Using this command successfully clears the byte 51 error flag of config. The diagnostic counters are not cleared.

The diagnostic counters are cumulative since power on or reset of the device.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD"  | Synchronization value |
| 4 | 2 | 0x0004 | Command ID |
| 6 | 1 | 0x00   | Command direction: 0x00 goes to the device; 0x01 goes to the host |
| 7 | 4 | 0x0000 | Reserved |
| 11 | 1 | 0x0000 | Number of bytes in the payload |
| 12 | 48 | 0x00...00 | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x00a4 | Command ID |
| 6 | 1 | 0x01   | Command direction: 0x1 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x28 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4  | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Notes |
|---:|---:|---|
| 12 | 1 | Command result |
| 13 | 8 | Timestamp of when the most recent error happened since boot in 100ns unit |
| 21 | 1 | ESP error: 0x00 no error; 0x01 TBA error |
| 22 | 1 | ADC error: 0x00 no error; 0x01 TBA error |
| 23 | 1 | IMU error: 0x00 no error; 0x01 TBA error |
| 24 | 1 | GNSS error: 0x00 no error; 0x01 TBA error |
| 25 | 1 | Analog card error: 0x00 no error; 0x01 TBA error |
| 26 | 1 | Power error: 0x00 no error; 0x01 TBA error |
| 27 | 1 | SD card error: 0x00 no error; 0x01 TBA error |
| 28 | 4 | Protocol receive CRC-error counter |
| 32 | 4 | ADC data CRC-error counter |
| 36 | 4 | ADC header-error counter |
| 40 | 4 | Acquisition-overrun counter |
| 44 | 4 | Dropped-conversion counter |
| 48 | 4 | Storage-error counter |
| 52 | 8 | Empty padding |

## Live streaming

### `STREAMING_START` -> `STREAMING_START_RESULT`

A live stream is the raw data sent via USB or Bluetooth. This allow the user the see live data of the device proving it is active and functional easily. A decimation filter can be applied to reduce the bandwidth required for the transfer. The decimation filters are 2, 4, 5, 10 and 20, so to stay aligned with the block data structure.

This command starts the acquisition of channels 0 to 3, channels 4 to 7 or all the channels (0 to 7) and will output the data related to those channels.

If acquisition is already taking place for a recording then the command will disregard the channels requested and send the data being recorded. 

This command can be use at the same time as recording to the SD card.

This command is always preceded by a GET_CONFIG command to make sure the correct and expected values are being used.

For each conversion, enabled channels are packed in ascending ADC-channel order.
Conversions are then appended in chronological order:

```text
conversion 0: channel 0, channel 1, ... channel 7
conversion 1: channel 0, channel 1, ... channel 7
...
```

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x0005 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x0002 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Command payload

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Decimation filter: 0x00 no decimation; 0x02 decimation by 2; 0x04 decimation by 4; 0x5 decimation by 5; 0x0a decimation by 10; 0x14 decimation by 20|
| 13 | 1 | - | ADC channel active mask: 0x00 no active channels; 0x0f channels 0 to 3 active; 0xf0 channels 4 to 7 active; 0xff channels 0 to 7 active |
| 14 | 46 | 0x00...00 | Empty padding |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x00a5 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x04 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 1 | - | Decimation filter applied: 0x00 no filter, same as sampling rate; other value is the decimation rate |
| 14 | 1 | - | ADC channel active mask: 0x00 no active channels; 0x0f channels 0 to 3 active; 0xf0 channels 4 to 7 active; 0xff channels 0 to 7 active |
| 15 | 1 | - | Recording already in progress : 0x00 No; 0x01 Yes |

| 16 | 44 | 0x00...00 | Empty padding |

#### Stream semantics:

| Offset | Size | Field | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\DAT" | ADC-record synchronization value |
| 4 | 1 | - | ADC channel active mask: 0x00 no active channels; 0x0f channels 0 to 3 active; 0xf0 channels 4 to 7 active; 0xff channels 0 to 7 active |
| 5 | 1 |  | Status code: 0x00 all good; 0x01 critical error; 0x02 conversion error; 0x03 timing error (we can add more when needed). This is a single value, not a bitmask |
| 6 | 2 | - | ADC gain: 0b00 x1; 0b01 x2; 0b10 x4; 0b11 x8. Each channel is shifted  by 2 times the number of the channel (<<(2*id)) |
| 8 | 4 | - | Payload number |
| 12 | 4 | - | Source ADC conversion sequence number of the first conversion stored in this payload |
| 16 | 8 | - | Monotonic timer at 10MHz (100ns) for the first conversion stored in this payload |
| 24 | 4 | - | Interval between consecutive conversions stored in the block, after any applied decimation, in 100ns units |
| 28 | 480 | Packed sample payload | Conversion-major signed 24-bit two's-complement values, least-significant byte first |
| 508 | 4 | - | CRC-32 covers bytes 0 through 507 |

### `STREAMING_STOP` -> `STREAMING_STOP_RESULT`

Stops the live stream completely on all channels. But if there is a recording in progress, it doesn't stop the acquisition.
The streaming is also stopped when a recording is stop with the command. This measure is in place to help the user feel more sure that when they click on stop recording they also see the streaming stop reassuring them that it is really stopped. An user could think that since the streaming is still in progress it might still be recording.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization|
| 4 | 2 | 0x0006 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x00 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization|
| 4 | 2 | 0x00a6 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x1 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x02 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 1 | - | Recording already in progress : 0x00 No; 0x01 Yes |
| 14 | 46 | 0x00...00 | Empty padding |

## SD recording

### `RECORDING_START` -> `RECORDING_START_RESULT`

This command start the acquisition if not already started by the streaming and record the data on the SD card in the same block structure as shown in the `START_STREAMING` function. Configuration cannot be changed while a recording is in progress, it has to be stopped first and restarted. There should be no more than 255 different recording on a SD card by design, this is to prevent bug when reading the data in the SD card.

The name of the file has the following restrictions:

- only a-z, A-Z, 0-9 _ and - can be used (upper letters are converted to lower case)
- ends by a NUL ASCII character (00), so the real writable length is 31
- values after the name are padded to zeros
- file names are case-insensitive
- no empty name
- all names must be different, duplicate name is rejected
- no extension is required since the recording are saved in memory in a custom format

A recording file is a direct concatenation of complete 512-byte `\DAT` blocks.
It has no separate file header or footer, and its size is therefore always a
multiple of 512 bytes. On stop, a partially filled block is discarded, all
complete queued blocks are written, the filesystem is synchronized, and the
file is closed before the reply is sent. A zero-byte recording is valid when
recording was stopped before the first complete block was produced.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x0007 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x20 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Command payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 32 | - | File name: ASCII characters |
| 44 | 16 | 0x00...00 | Empty padding |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x00a7 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x22 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 1 | - | Recording already in progress : 0x00 No; 0x01 Yes |
| 14 | 32 | - | File name accepted |
| 46 | 14 | 0x00...00 | Empty padding |

### `RECORDING_STOP` -> `RECORDING_STOP_RESULT`

Stops the recording in progress, if there is a streaming in progress it also stops it. Stop the acquisition process on all channels. Writes all complete queued 512-byte blocks, synchronizes the filesystem, and closes the recording file. The recording format has no footer.

The result is sent after the recording has properly stopped or tried to.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x0008 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x00 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x00a8 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x21 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 32 | - | Name of the record in question |
| 45 | 15 | 0x00...00 | Empty padding |

### `RECORDING_GET_NUMBER` -> `RECORDING_NUMBER`

This command is used to determine the number of recordings currently in the SD card. It will return a number that represent the number of recording, 0 means there is no recordings.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x0009 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x00 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Recording info semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value  |
| 4 | 2 | 0x00a9 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x03 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 2 | - | Number of recordings currently in memory |
| 15 | 45 | 0x00...00 | Empty padding |

### `RECORDING_GET_INFO` -> `RECORDING_INFO`

This command is used to get information about the recordings currently in the SD card. After inquiring about the number of recordings present in memory, this command can be used to get more information about a specific recording. This command should be used a number of times equal to the number of recording present to identify all of them in the GUI.

The host should keep this recording information into a cache to display on the GUI.

Recording indices are zero-based and valid from 0 to one less than the number returned by `RECORDING_GET_NUMBER`.

The cache should be refreshed after:
- reconnecting;
- successfully starting a recording;
- stopping or deleting a recording;
- an SD-card removal/remount;
- an unexpected duplicate rejection.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x000a | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x02 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Command payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 2 | - | Requested recording index |
| 14 | 46 | 0x00...00 | Empty padding |

#### Recording info semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value  |
| 4 | 2 | 0x00aa | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x30 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Recording info payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 2 | - | Recording index |
| 15 | 1 | - | Recording in progress: 0x00 No; 0x01 Yes |
| 16 | 32 | - | Recording name |
| 48 | 8 | - | Unix timestamp in microseconds of the start of the recording; zero if UTC was unavailable at the start |
| 56 | 4 | - | Size in bytes of the recording |

### `RECORDING_DELETE` -> `RECORDING_DELETE_RESULT`

This command is used to delete a recording from the SD card. One record is deleted at the time. A recording in progress cannot be deleted, it has to be stopped first, if tried it will return the fail status and record in progress message. It should be stop using the recording stop command.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x000b | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x20 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Command payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 32 | - | Name of the recording to delete |
| 44 | 16 | 0x00...00 | Empty padding |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0x00ab | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x22 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 1 | - | Recording already in progress : 0x00 No; 0x01 Yes |
| 14 | 32 | - | Name of the target recording |
| 46 | 14 | 0x00...00 | Empty padding |


### `TEMP_RECORDING_READ` (temporary, command ID `0xf000`)

> **Temporary test command — not part of the final product protocol.** This
> command exists only to extract and validate SD recordings over UART when the
> SD card cannot conveniently be removed. Host software must not depend on it
> for normal operation. The same command ID is used for the request and reply;
> the direction field distinguishes them.

The command reads at most 38 bytes from a closed recording. The host repeats
requests with increasing byte offsets until the returned offset plus data
length equals the returned file size. An offset equal to the file size is a
valid end-of-file request and returns zero data bytes. An offset greater than
the file size is invalid. The command is rejected while any recording is in
progress so that `task_storage` remains the sole filesystem owner and the file
cannot change during extraction.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0xf000 | Temporary command ID |
| 6 | 1 | 0x00 | Command direction: device-bound |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x24 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 of bytes 0 through 59 |

Command payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 32 | - | NUL-terminated, zero-padded recording name |
| 44 | 4 | - | Little-endian byte offset |
| 48 | 12 | 0x00...00 | Empty padding |

#### Reply semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Synchronization value |
| 4 | 2 | 0xf000 | Temporary command ID |
| 6 | 1 | 0x01 | Command direction: host-bound |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x30 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 of bytes 0 through 59 |

Reply payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command result |
| 13 | 4 | - | Little-endian total file size; zero on failure |
| 17 | 4 | - | Little-endian byte offset echoed from the request |
| 21 | 1 | - | Number of valid data bytes, from 0 through 38 |
| 22 | 38 | - | File data followed by zero padding |


## Magnetic SET/RESET operation

### `PULSE_REQUEST` -> `PULSE_RESULT`

This command is not implemented for now. A set-reset pulse should be sent before starting a new recording. This should be implemented in the firmware not by host commands.
- The card must be identified as pulse-capable.

## Reserved commands for testing

Command IDs from `0xf000` to `0xf0ff`.

Those commands are reserved for testing functionalities that will not be included in the final product.
