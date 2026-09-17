# Protocol Command Definitions

## Document status

- Scope: Command meanings shared by UART-to-USB and future Bluetooth control
- Common rules: `protocol_core.md`
- Message registry: `protocol_types.md`

This document defines what each command asks the device to do, which state may
accept it, what its successful response must report, and how failure is made
visible. Exact byte offsets will be added only after this semantic review.

## Commands rules

Every command has exactly one expected reply type.
Each command sent by the host expect a reply from the device. Some commands from the device can be asynchronous but are used in special cases of data streaming or errors. Those asynchronus commands dont require replies. Receiving an asynchronous message does not complete the pending command.
Timeouts and retries are handled by the host.

The counters start at 0 at power on or reset of the device. Blocs are a 512 byte data structure that have a 28 byte header, 480 byte of samples data and 4 bytes of CRC32. There is a counter for to keep trac of the order of sampling. This number is related to acquisition only. This 48 byte bloc of data is use to store data in RAM, in SD storage and as the framing to send data from the device to a host.

The sychronisation value for the standard commands is "\CMD" in ASCII (5C 43 4D 44). The streaming blocks comming from the devices are asychronous commands from the device and start with the synchronisation word "\DAT" in ASCII (5C 44 41 54). 

The counters can overflow and it's okay, the monotonic timer will never overflow so it can be used to keep track of the data order.

The device can only be connected to one host at the time. If there is a USB connection the device should not engage in bluetooth connectivity. However, USB has priority and if connected it will disconec the bluetooth and use USB instead.

USB connected is defined after a A valid `HELLO` received over USB establishes the USB session. command is sent.

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

## Discovery and state

### `HELLO` -> `DEVICE_INFO`

Purpose: establish protocol compatibility before any state-changing command.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD"  | Syncgronisation value |
| 4 | 2 | 0x0001 | Command ID |
| 6 | 1 | 0x00   | Command direction: 0x00 goes to the device; 0x01 goes to the host |
| 7 | 4 | 0x0000 | Reserved |
| 11 | 1 | 0x0000 | Number of bytes in the payload |
| 12 | 48 | 0x00...00 | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x00a1 | Command ID |
| 6 | 1 | 0x01   | Command direction: 0x1 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x0f | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4  | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | 0x0 operationnal; 0x1 broken |
| 13 | 6 | - | ESP32 MAC adress |
| 19 | 2 | - | Hardware version |
| 21 | 2 | - | Hardware revision |
| 23 | 4 | - | Firmware version |
| 27 | 33 | 0x00...00 | Empty padding |

### `DEVICE_GET_CONFIG` / `DEVICE_SET_CONFIG` -> `DEVICE_CONFIG`

The command `GET_CONFIG` is used to read the current config and has an empty request. It return the structure as shown below. The command `SET_CONFIG` use the same structue as the answer but the paylaod is applyed where applicable. The read only registers are not considerated. After a `SET_CONFIG` commmand the device answer back with a `CONFIG` echo to see the applied values.

A new configuration can be sent at any time using the `SET_CONFIG`. In case that there is a live stream in progress, it is automaticly applied. If there is a recording in progress, this recording has to be stopped before any modification of the config. So if the command `SET_CONFIG` is sent while there is a recording, it will only eacho the unmodified config with the Recording in progress and no changes applied. In case of both live stream and recording are taking place new config will also be regected due to the requirement to not have recording in progress for any modifications.

When using only 4 channels it is 40 convertions per payload bloc, when all 8 channels are being used, it is 20 conversions per block.

No partial record should be emitted.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | - | Command ID: 0x02 GET_CONFIG; 0x03 SET_CONFIG |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device|
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | - | Number of bytes in the payload: 0x00 for `DEVICE_GET_CONFIG`; 0x27 for `DEVICE_SET_CONFIG` |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x00a2 | Command ID |
| 6 | 1 | 0x01   | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x000000000 | Reserved |
| 11 | 1 | 0x27 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Config payload structure:

| Offset | Size | Action | Notes |
|---:|---:|:---|---|
| 12 | 8 | R  | Monolitic timestamp 64 bits at 10 MHz (100ns increments) |
| 20 | 1 | R  | Recording in progress: 0x00 no recording; 0x01 recording in progress |
| 21 | 1 | R  | Card slot 1 info: 0x00 absent; 0x01 magnetic; 0x02 acc_geoph |
| 22 | 1 | R  | Card slot 2 info: 0x00 absent; 0x01 magnetic; 0x02 acc_geoph |
| 23 | 1 | RW | ADC sampling rate: 0x00 0.5kHz; 0x01 1kHz; 0x02 2kHz; 0x03 4kHz; 0x04 8kHz; 0x05 16kHz |
| 24 | 1 | RW | ADC channel active mask: 0x00 no active channels; 0x0f channels 0 to 3 active; 0xf0 channels 4 to 7 active; 0xff channels 0 to 7 active, only those mask are valid |
| 25 | 2 | - | ADC gain: 0b00 x1; 0b01 x2; 0b10 x4; 0b11 x8. Each channel is shifted  by 2 times the number of the channel (<<(2*id)) |
| 27 | 2 | R  | ADC temperature: signed 16 bits, 0.01°C per count|
| 29 | 1 | RW | 0x00 +3.3VA off; 0x01 +3.3VA on |
| 30 | 1 | RW | 0x00 +5VA off; 0x01 +5VA on |
| 31 | 1 | RW | 0x00 +9VA off; 0x01 +9VA on |
| 32 | 1 | RW | 0x00 -5VA off; 0x01 -5VA on |
| 33 | 1 | RW | 0x00 +18VA off; 0x01 +18VA on |
| 34 | 1 | R  | 0x00 Solar not present; 0x01 Solar present |
| 35 | 1 | R  | 0x00 +5V USB not present; 0x01 +5V USB present |
| 36 | 1 | R  | GNSS state: 0x00 disabled/not present; 0x01 ready; 0x02 faulted; 0x03 searching |
| 37 | 1 | R  | GNSS Satellite count |
| 38 | 1 | R  | IMU state: 0x00 disabled/not present; 0x01 ready; 0x02 faulted/invalid data |
| 39 | 2 | RW | IMU averaging time ms |
| 41 | 2 | R  | IMU roll: signed 16 bits, 0.01° per count, 0° being perfectly leveled |
| 43 | 2 | R  | IMU pitch: signed 16 bits, 0.01° per count, 0° being perfectly leveled |
| 45 | 2 | R  | IMU temperature: signed 16 bits, 0.01°C per count |
| 47 | 1 | R  | SD card present: 0x00 not present; 0x01 present; 0x02 faulted |
| 48 | 2 | R  | ESP32 temperature: signed 16 bits, 0.01°C per count |
| 50 | 1 | R  | CONFIG command status: 0x00 ok; 0x01 invalid argument; 0x02 invalid state; 0x03 busy; 0x04 harware error |
| 51 | 9 | -  | Empty padding |

## Live streaming

### `STREAMING_START` -> `STREAMING_START_RESULT`

A live stream is the raw data sent via USB or bluetooth. This allow the user the see live data of the device proving it is active and functionnal easily. A decimation filter can be applied to reduce the bandwidth required for the transfer. The decimation filters are 2, 4, 5, 10 and 20, so to stay aligned with the block data structure.

This command starts the acquisition of channels 0 to 3, channels 3 to 7 or all the channels (0 to 7) and will output the data related to those channels.

If acquisition is already taking place for a recording then the command will disregard the channels requested and send the data being recorded. 

This command can be use at the same time as recording to the SD card.

This command is always preceeded by a GET_CONFIG command to make sure the correct and expeected values are being used.

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
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x0004 | Command ID |
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
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x00a4 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x04 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Status: 0x00 fail; 0x01 succes |
| 13 | 1 | - | Decimation filter applied: 0x00 no filter, same as sampling rate; other value is the decimation rate |
| 14 | 1 | - | ADC channel active mask: 0x00 no active channels; 0x0f channels 0 to 3 active; 0xf0 channels 4 to 7 active; 0xff channels 0 to 7 active |
| 15 | 1 | - | Recording already in progress : 0x00 No; 0x01 Yes |

| 16 | 44 | 0x00...00 | Empty padding |

#### Stream semantics:

| Offset | Size | Field | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\DAT" | ADC-record synchronization value |
| 4 | 1 | - | ADC channel active mask: 0x00 no active channels; 0x0f channels 0 to 3 active; 0xf0 channels 4 to 7 active; 0xff channels 0 to 7 active |
| 5 | 1 |  | Flags: 0x00 all good; 0x01 critical error; 0x02 conversion error; 0x03 timing error (we can add more when needed) |
| 6 | 2 | - | ADC gain: 0b00 x1; 0b01 x2; 0b10 x4; 0b11 x8. Each channel is shifted  by 2 times the number of the channel (<<(2*id)) |
| 8 | 4 | - | Payload number |
| 12 | 4 | - | Number of the first sample in this payload |
| 16 | 8 | - | Monotonic timer at 10MHz (100ns) |
| 24 | 4 | - | sample period in 100ns |
| 28 | 480 | Packed sample payload | Conversion-major signed 24-bit values |
| 508 | 4 | - | CRC-32 covers bytes 0 through 507 |

### `STREAMING_STOP` -> `STREAMING_STOP_RESULT`

Stops the live stream completely on all channels. But if there is a recording in progress, it doesnt stop the acquisition.
The streaming is also stopped when a recording is stop with the command. This measure is in place to help the user feel more sure that when they clic on stop recording they also see the streaming stop reasuring them that it is really stopped. An user could think that since the streaming is still in progress it might still be recording.

#### Command semantics:

| Offset | Size | Value | Notes |
| 0 | 4 | "\CMD" | Syncgronisation|
| 4 | 2 | 0x0005 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x00 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
| 0 | 4 | "\CMD" | Syncgronisation|
| 4 | 2 | 0x00a5 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x1 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x02 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
| 12 | 1 | - | Status: 0x0 ok; 0x1 faillure |
| 13 | 1 | - | Recording already in progress : 0x00 No; 0x01 Yes |
| 14 | 46 | 0x00...00 | Empty padding |

## SD recording

### `RECORDING_START` -> `RECORDING_START_RESULT`

This command start the acquisition if not already started by the streaming and record the data on the SD card in the same block structure as shown in the `START_STREAMING` funtion. Configuration cannot be changed while a recording is in progress, it has to be stopped first and restarted. There should be no more than 255 different recording on a SD card by design, this is to prevent bug when reding the data in the SD card.

The name of the file has the following restrictions:
- only a-z, A-Z, 0-9 _ and - can be used (upper letters are converted to lower case)
- ends by a NUL ASCII carater (00), so the real writable lenght is 31
- values after the name are padded to zeros
- file names are case-insensitive
- no empty name
- all names must be different, duplicate name is rejected
- no extension is required since the recording are saved in memory in a custom format

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x0006 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x20 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Command payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 32 | - | File name: ASCII caracters |
| 44 | 16 | 0x00...00 | Empty padding |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x00a6 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x22 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Status: 0x00 success; 0x01 fail |
| 13 | 1 | - | Recording already in progress : 0x00 No; 0x01 Yes |
| 14 | 32 | - | File name accepted |
| 46 | 14 | 0x00...00 | Empty padding |

### `RECORDING_STOP` -> `RECORDING_STOP_RESULT`

Stops the recording in progress, if there is a streaming in progress it also stops it. Stop the acquisition process on all channels. Finishes the recording footer properly.

The result is sent after the recording has properly stopped or tryed to.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x0007 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x00 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Answer semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x00a7 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x21 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Status: 0x00 success; 0x01 fail |
| 13 | 32 | - | Name of the record in question |
| 45 | 15 | 0x00...00 | Empty padding |

### `RECORDING_GET_NUMBER` -> `RECORDING_NUMBER`

This command is used to determine the number of recordings currently in the SD card. It will return a number that represent the number of recording, 0 means there is no recordings.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x0008 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x00 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Recording info semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value  |
| 4 | 2 | 0x00a8 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x03 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Command success: 0x00 success; 0x01 faillure |
| 13 | 2 | - | Number of recordings currently in memory |
| 15 | 45 | 0x00...00 | Empty padding |

### `RECORDING_GET_INFO` -> `RECORDING_INFO`

This command is used to get information about the recordings currently in the SD card. After inquiring about the number of recordings present in memory, this command can be used to get more informations about a specific recording. This command should be used a number of times equal to the number of recording present to identify all of them in the GUI.

The host should keep this recording inforamtions into a chache to display on the GUI.

The cache should be refreshed after:
- reconnecting;
- successfully starting a recording;
- stopping or deleting a recording;
- an SD-card removal/remount;
- an unexpected duplicate rejection.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x0009 | Command ID |
| 6 | 1 | 0x00 | Command direction: 0x0 goes to the device |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x00 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

#### Recording info semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value  |
| 4 | 2 | 0x00a9 | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x30 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 2 | - | Recording indice |
| 14 | 1 | - | Recording status: 0x00 ok; 0x01 corrupted; 0x02 recording in progress |
| 15 | 32 | - | Recording name |
| 47 | 8 | - | Unix timestamp in microseconds of the start of the recording |
| 55 | 5 | - | Size in bytes of the recording |

### `RECORDING_DELETE` -> `RECORDING_DELETE_RESULT`

This command is used to delete a recording from the SD card. One record is deleted at the time. A recording in progess cannot be deleted, it has to be stopped first, if tryed it will return the fail status and record in progress message. It should be stop using the recording stop command.

#### Command semantics:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x000a | Command ID |
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
| 0 | 4 | "\CMD" | Syncgronisation value |
| 4 | 2 | 0x00aa | Command ID |
| 6 | 1 | 0x01 | Command direction: 0x01 goes to the host |
| 7 | 4 | 0x00000000 | Reserved |
| 11 | 1 | 0x22 | Number of bytes in the payload |
| 12 | 48 | - | Payload |
| 60 | 4 | - | CRC32 byte 0 to 59 |

Answer payload:

| Offset | Size | Value | Notes |
|---:|---:|:---|---|
| 12 | 1 | - | Status: 0x00 success; 0x01 fail |
| 13 | 1 | - | Recording already in progress : 0x00 No; 0x01 Yes |
| 14 | 32 | - | Name of the target recording |
| 46 | 14 | 0x00...00 | Empty padding |


## Magnetic SET/RESET operation

### `PULSE_REQUEST` -> `PULSE_RESULT`

This command is not implemented for now. A set-reset pulse should be sent before starting a new recording. This should be implemented in the firmware not by host commands.
- The card must be identified as pulse-capable.

## Reserved commands for testing

Commands from `0xf0` to `0xff`

Those commands are reserved for testing functionnalities that will not be included in the final product.


