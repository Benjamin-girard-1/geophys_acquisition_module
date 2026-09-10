# SD Recording and Bluetooth-Control Implementation Plan

## Document information

- Product: Geophysical Acquisition Module V2
- Hardware: V2 Rev-1 with ESP32-S3 DevKitC
- Status: Draft implementation plan
- Purpose: Track the implementation of card-aware ADC recording to SD with Bluetooth control

## How to use this checklist

- Check an item only after its implementation and required verification are complete.
- Keep the format specification, firmware, host decoder, and test vectors consistent.
- Record test evidence in the existing project checklist or in a linked test report.
- Do not start acquisition until storage is prepared and able to accept data.
- `task_acquisition` must never block on SD-card or Bluetooth operations.

## Target data path

```text
Installed analog cards
        |
        v
Card detection and channel mapping
        |
        v
Bluetooth configuration request
        |
        v
AD7779 simultaneous conversions
        |
        v
Bounded acquisition buffers in RAM
        |
        v
512-byte recording encoder
        |
        v
Multi-record FatFs writes
        |
        v
FAT32 file on the SD card
        |
        v
Host decoder and scientific export
```

## Phase 1: Freeze the revised product scope

### Step 1: Confirm the primary workflow

- [ ] Confirm that SD recording is a primary product requirement.
- [ ] Confirm that Bluetooth is initially used for control and status rather than full-rate raw streaming.
- [ ] Confirm that recognized installed analog cards determine the default recording channel mask.
- [ ] Confirm whether users may disable a subset of available channels.
- [ ] Confirm that no supported card means recording cannot start.
- [ ] Confirm that card detection runs at boot and before each acquisition start.
- [ ] Confirm that analog-card hot-plugging remains unsupported during acquisition.
- [ ] Confirm that Bluetooth disconnection does not stop an active SD recording.
- [ ] Confirm that gain, rate, or channel changes require stopping and starting a new file or segment.
- [ ] Record all decisions in the product requirements.

### Step 2: Update the authoritative contracts

- [ ] Update `product_requirements.md` with the revised milestone scope.
- [ ] Update `firmware_interfaces.md` with acquisition, storage, and Bluetooth ownership.
- [ ] Update `ARCHITECTURE.md` with the recording-format module and dependency direction.
- [ ] Update `board_rev_1_contract.md` with verified SDMMC configuration and timing.
- [ ] Update `firmware_implementation_checklist.md` with the revised execution order.
- [ ] Resolve any conflicts between the updated documents before implementation proceeds.

## Phase 2: Freeze the binary recording format

### Step 3: Create the authoritative recording specification

- [ ] Create a tracked recording-format specification outside the AD7779 driver.
- [ ] Define the data-file magic value.
- [ ] Define the event-file magic value.
- [ ] Define file-format and record-format versions.
- [ ] Fix byte order to little-endian.
- [ ] Fix the sample representation to signed packed 24-bit values.
- [ ] Define the exact CRC-32C parameters.
- [ ] Define exactly which bytes each CRC covers.
- [ ] Define channel ordering inside each sample payload.
- [ ] Define zero-padding rules.
- [ ] Define partial final-record behavior.
- [ ] Define corruption and resynchronization behavior.
- [ ] Define file segmentation and recovery behavior.

### Step 4: Freeze the 28-byte sample-record header

- [ ] Confirm the record magic field: 4 bytes.
- [ ] Confirm the record-format version field: 1 byte.
- [ ] Confirm the channel-mask field: 1 byte.
- [ ] Confirm the conversion-count field: 1 byte.
- [ ] Confirm the aggregate status-flags field: 1 byte.
- [ ] Confirm the applied sample-rate field: 4 bytes.
- [ ] Confirm the first conversion-sequence field: 8 bytes.
- [ ] Confirm the first monotonic-timestamp field: 8 bytes.
- [ ] Confirm that the total header size is exactly 28 bytes.
- [ ] Confirm that the sample payload is exactly 480 bytes.
- [ ] Confirm that the trailing CRC-32C is exactly 4 bytes.
- [ ] Confirm that every sample record is exactly 512 bytes.

### Step 5: Freeze the file/session metadata

- [ ] Define the file-header size and data-record start offset.
- [ ] Define the session UUID representation.
- [ ] Store the device identifier.
- [ ] Store firmware and hardware revisions.
- [ ] Store the recording segment number.
- [ ] Store card type and revision for each slot.
- [ ] Store the ADC channel-to-measurement mapping.
- [ ] Store the active channel mask.
- [ ] Store the gain for every ADC channel.
- [ ] Store requested and applied sample rates.
- [ ] Store the ADC reference configuration.
- [ ] Store the calibration identifier.
- [ ] Store the initial sequence and monotonic timestamp.
- [ ] Reserve fields for UTC time, validity, and uncertainty.
- [ ] Protect the file header with CRC-32C.
- [ ] Reserve space for backward-compatible metadata extensions.

## Phase 3: Create reference vectors and a host decoder

### Step 6: Produce shared golden vectors

- [ ] Create an eight-channel, 20-conversion valid record.
- [ ] Create a slot-1-only, four-channel, 40-conversion valid record.
- [ ] Create a slot-2-only, four-channel, 40-conversion valid record.
- [ ] Include zero, positive, negative, minimum, and maximum signed 24-bit samples.
- [ ] Create a partial final record with canonical padding.
- [ ] Create a record with an aggregate error flag.
- [ ] Create a record with an invalid magic value.
- [ ] Create a record with an invalid CRC.
- [ ] Create a truncated-record example.
- [ ] Document the expected decoded result for every vector.

### Step 7: Implement the host decoder first

- [ ] Read and validate the file/session header.
- [ ] Locate the first 512-byte sample record.
- [ ] Validate record magic and version.
- [ ] Validate every record CRC-32C.
- [ ] Decode signed packed 24-bit samples.
- [ ] Reconstruct channel identities from the channel mask.
- [ ] Reconstruct conversion sequences and timestamps.
- [ ] Load and correlate the matching event file.
- [ ] Detect missing, corrupt, and truncated records.
- [ ] Export decoded data to the selected scientific format.
- [ ] Pass every shared golden vector.

## Phase 4: Implement the portable record encoder

### Step 8: Calculate the record layout at runtime

- [ ] Count enabled channels from the active channel mask.
- [ ] Reject an empty channel mask.
- [ ] Calculate bytes per conversion as `channel_count * 3`.
- [ ] Calculate conversions per record from the 480-byte payload.
- [ ] Calculate used payload bytes and required padding.
- [ ] Verify that mask `0x0F` produces 40 conversions with no padding.
- [ ] Verify that mask `0xF0` produces 40 conversions with no padding.
- [ ] Verify that mask `0xFF` produces 20 conversions with no padding.
- [ ] Define deterministic behavior for future masks that require padding.

### Step 9: Implement 512-byte record construction

- [ ] Initialize a record without dynamic allocation.
- [ ] Copy the applied channel mask and sample rate into the header.
- [ ] Store the first conversion sequence and timestamp.
- [ ] Pack only enabled channels in ascending ADC-channel order.
- [ ] Convert each `int32_t` ADC code to exactly three little-endian bytes.
- [ ] Reject values outside the signed 24-bit range.
- [ ] Preserve every accepted conversion in order.
- [ ] Set aggregate record status flags when required.
- [ ] Zero-fill unused payload bytes.
- [ ] Calculate and append CRC-32C.
- [ ] Prove that the output is exactly 512 bytes.

### Step 10: Verify encoder/decoder compatibility

- [ ] Compare encoded records byte-for-byte with the golden vectors.
- [ ] Decode every firmware-generated vector with the host decoder.
- [ ] Re-encode decoded records and compare them with the originals where applicable.
- [ ] Verify corrupt CRC and malformed-header rejection.
- [ ] Verify operation without ESP-IDF, FreeRTOS, FatFs, or SD hardware.

## Phase 5: Implement SD-card platform support

### Step 11: Add portable SDMMC/FatFs platform operations

- [ ] Initialize the ESP32-S3 SDMMC host.
- [ ] Detect and initialize the SD card.
- [ ] Mount the FAT32 filesystem.
- [ ] Return the SD logical sector size.
- [ ] Return FAT sectors per cluster and cluster size in bytes.
- [ ] Return card capacity and available filesystem space.
- [ ] Return the SD allocation unit when available.
- [ ] Open or create files for sequential recording.
- [ ] Support complete and partial write reporting.
- [ ] Support synchronization and close.
- [ ] Support safe unmount.
- [ ] Translate ESP-IDF and FatFs errors into portable firmware errors.

### Step 12: Verify Rev-1 SD hardware configuration

- [ ] Confirm that the SD mux remains fixed toward the ESP32.
- [ ] Confirm that the USB2641 remains reset and isolated.
- [ ] Verify every SDMMC GPIO against the schematic.
- [ ] Select and document the initial SDMMC bus width.
- [ ] Select and document the initial SDMMC clock.
- [ ] Verify DMA buffer requirements.
- [ ] Verify card-detect behavior or document its absence.
- [ ] Mount, write, synchronize, close, and remount successfully on Rev-1.

### Step 13: Benchmark write transaction sizes

- [ ] Benchmark 512-byte writes.
- [ ] Benchmark 1024-byte writes.
- [ ] Benchmark 2048-byte writes.
- [ ] Benchmark 4096-byte writes.
- [ ] Benchmark 8192-byte writes.
- [ ] Benchmark 16384-byte writes.
- [ ] Benchmark 32768-byte writes.
- [ ] Measure average write latency.
- [ ] Measure maximum and high-percentile write latency.
- [ ] Measure synchronization latency.
- [ ] Measure throughput and CPU usage.
- [ ] Repeat with multiple representative SD cards.
- [ ] Select the smallest aligned batch that provides adequate worst-case margin.
- [ ] Record the evidence supporting the selected batch size.

## Phase 6: Implement card detection and channel derivation

### Step 14: Complete analog-card detection

- [x] Implement averaged analog-ID measurement for both slots.
- [ ] Classify no-card conditions.
- [ ] Classify supported magnetic cards.
- [ ] Classify unknown or ambiguous cards safely.
- [ ] Expose card type, revision, confidence, and measurement voltage.
- [ ] Keep controls inactive for absent or unknown cards.
- [ ] Verify each card independently in each physical slot.
- [ ] Verify both cards installed simultaneously.

### Step 15: Derive the recording channel mask

- [ ] Map a slot-1 magnetic card to channels 0 through 3.
- [ ] Map a slot-2 magnetic card to channels 4 through 7.
- [ ] Combine mappings from all recognized installed cards.
- [ ] Reject recording when the derived mask is empty.
- [ ] Report the derived mask and channel descriptions through Bluetooth.
- [ ] Prevent Bluetooth from enabling unavailable channels.

## Phase 7: Complete real AD7779 acquisition

### Step 16: Complete AD7779 streaming operations

- [ ] Implement the stopped-to-running transition.
- [ ] Enable the configured channel clocks and conversion readback.
- [ ] Implement one complete 32-byte conversion-frame SPI read.
- [ ] Implement the running-to-stopped transition.
- [ ] Maintain correct AD7779 lifecycle state.
- [ ] Keep start and stop operations idempotent where required.
- [ ] Verify all transitions with fake SPI callbacks.
- [ ] Verify streaming transitions on Rev-1 hardware.

### Step 17: Implement the DRDY path

- [ ] Attach a falling-edge interrupt to Rev-1 `ADC_DRDY`.
- [ ] Capture the ISR-safe monotonic timestamp.
- [ ] Insert timestamps into a fixed ISR-safe ring.
- [ ] Notify `task_acquisition` from the ISR.
- [ ] Increment an ISR-safe overflow counter when the ring is full.
- [ ] Confirm that the ISR performs no SPI, allocation, blocking, or logging.

### Step 18: Produce complete ADC frames

- [ ] Assign a sequence number to every expected conversion.
- [ ] Attach the corresponding DRDY timestamp.
- [ ] Read the complete simultaneous conversion frame.
- [ ] Validate channel IDs and selected status/CRC information.
- [ ] Decode all eight signed 24-bit samples into `int32_t` values.
- [ ] Apply configured channel and validity masks.
- [ ] Preserve invalid conversions and loss information visibly.
- [ ] Deliver frames to the bounded storage pipeline.

## Phase 8: Design and size the bounded RAM pipeline

### Step 19: Separate record and buffering dimensions

- [ ] Remove the unexplained 32-frame value as a recording-format assumption.
- [ ] Keep the 512-byte logical record size independent of acquisition batching.
- [ ] Keep the filesystem write-batch size independent of the logical record size.
- [ ] Define the acquisition queue in units appropriate for bounded real-time behavior.
- [ ] Define the storage write buffers in whole 512-byte records.

### Step 20: Size the backlog from measured SD latency

- [ ] Select the maximum supported recording sample rate.
- [ ] Select a required SD-stall tolerance.
- [ ] Calculate the required buffered conversion count.
- [ ] Calculate RAM use for full decoded frames.
- [ ] Calculate RAM use for compact packed frames.
- [ ] Calculate RAM use for preformatted record buffers.
- [ ] Include Bluetooth and other subsystem memory requirements.
- [ ] Select a representation that fits the ESP32-S3 memory budget.
- [ ] Add a documented safety margin.
- [ ] Allocate every steady-state buffer before acquisition starts.

### Step 21: Define overflow behavior

- [ ] Ensure acquisition never waits for storage buffers.
- [ ] Count every conversion lost because of buffer exhaustion.
- [ ] Preserve the sequence discontinuity.
- [ ] Set a sticky RAM fault and counter.
- [ ] Set an aggregate gap flag on the next writable record.
- [ ] Queue a detailed event when storage remains usable.
- [ ] Report the fault through Bluetooth.

## Phase 9: Implement `task_storage`

### Step 22: Define storage commands and states

- [ ] Define `UNINITIALIZED`.
- [ ] Define `MOUNTED`.
- [ ] Define `READY`.
- [ ] Define `RECORDING`.
- [ ] Define `STOPPING`.
- [ ] Define `FAULTED`.
- [ ] Define mount, prepare, start, stop, synchronize, status, and recovery commands.
- [ ] Make `task_storage` the sole filesystem owner.

### Step 23: Implement the normal recording path

- [ ] Receive ADC frames without blocking acquisition.
- [ ] Accumulate the runtime-calculated number of conversions per record.
- [ ] Finalize each 512-byte record and CRC.
- [ ] Accumulate whole records into the selected write-batch size.
- [ ] Submit aligned sequential writes through FatFs.
- [ ] Check the exact number of bytes written.
- [ ] Return or recycle every consumed buffer exactly once.
- [ ] Maintain written-record, byte, and error counters.

### Step 24: Implement the event/error file

- [ ] Create a matching event file for every recording session.
- [ ] Correlate both files using the same session UUID.
- [ ] Record ADC validation failures with exact sequences.
- [ ] Record missing sequence ranges and queue overflows.
- [ ] Record recording start and stop events.
- [ ] Record applied card and acquisition configuration.
- [ ] Record SET/RESET timing and affected sequence ranges.
- [ ] Record unexpected resets and storage warnings.
- [ ] Coalesce consecutive identical errors into ranges and counts.
- [ ] Protect event entries with a CRC.
- [ ] Retain sticky RAM/Bluetooth reporting when the SD card itself fails.

### Step 25: Implement synchronization and orderly stop

- [ ] Select and document the periodic synchronization interval.
- [ ] Define which severe events require prompt synchronization.
- [ ] Stop accepting new recording configuration during shutdown.
- [ ] Stop acquisition without abandoning owned buffers.
- [ ] Drain all complete frames and records.
- [ ] Finalize the partial final record using the specified padding policy.
- [ ] Write and synchronize the remaining data.
- [ ] Write final event/counter information.
- [ ] Close both files.
- [ ] Unmount safely when required.

## Phase 10: Implement segmentation and recovery

### Step 26: Implement file segmentation

- [ ] Start a new segment after one hour or 1 GiB, whichever occurs first.
- [ ] Give every segment a complete file header.
- [ ] Preserve the session UUID across segments.
- [ ] Increment the segment number monotonically.
- [ ] Preserve conversion sequence continuity across segments.
- [ ] Synchronize and close the old segment before activating the new one.

### Step 27: Implement startup recovery

- [ ] Detect an incomplete prior session.
- [ ] Validate the file/session header.
- [ ] Scan data records in 512-byte increments.
- [ ] Stop at the first invalid, corrupt, or truncated record.
- [ ] Determine the final trustworthy sequence.
- [ ] Report the damaged or incomplete tail.
- [ ] Apply the documented truncate or quarantine policy.
- [ ] Start recovery recording in a new segment.

### Step 28: Test interrupted-power behavior

- [ ] Remove power during a normal data write.
- [ ] Remove power during FAT metadata extension.
- [ ] Remove power during an event-file write.
- [ ] Remove power during synchronization.
- [ ] Remove power during file close.
- [ ] Remove power during a segment transition.
- [ ] Verify that earlier complete records remain extractable.
- [ ] Verify that every damaged tail is detected and reported.

## Phase 11: Implement Bluetooth control

### Step 29: Define Bluetooth control messages

- [ ] Report device and firmware information.
- [ ] Report detected cards and available channel mappings.
- [ ] Get the current acquisition configuration.
- [ ] Set supported sample rates and gains while stopped.
- [ ] Start an SD recording.
- [ ] Stop an SD recording.
- [ ] Report current recording state and session identifier.
- [ ] Report SD capacity and storage status.
- [ ] Report sticky errors and counters.

### Step 30: Preserve resource ownership

- [ ] Ensure Bluetooth never manipulates AD7779 registers directly.
- [ ] Ensure Bluetooth never reads streaming ADC data directly.
- [ ] Ensure Bluetooth never opens or writes recording files.
- [ ] Ensure Bluetooth never manipulates board GPIOs or shift-register bits.
- [ ] Route validated requests through application command queues.
- [ ] Keep `task_acquisition` as the sole ADC owner.
- [ ] Keep `task_storage` as the sole filesystem owner.

### Step 31: Implement atomic recording start

- [ ] Detect cards while acquisition is stopped.
- [ ] Derive and validate the active channel mask.
- [ ] Calculate the recording layout.
- [ ] Mount and validate the SD card.
- [ ] Check available storage capacity.
- [ ] Create the session files and headers.
- [ ] Prepare all queues and buffers.
- [ ] Configure the AD7779 rate and gains.
- [ ] Confirm that storage is ready before starting the ADC.
- [ ] Roll back safely if any operation fails.
- [ ] Allow Bluetooth to disconnect and reconnect without stopping the recording.

## Phase 12: System validation

### Step 32: Run functional tests

- [ ] Record with slot 1 only.
- [ ] Record with slot 2 only.
- [ ] Record with both magnetic cards.
- [ ] Reject recording with no recognized card.
- [ ] Reject or isolate an unknown card safely.
- [ ] Test every supported gain.
- [ ] Test every supported sample rate.
- [ ] Test partial final records.
- [ ] Test automatic segment transitions.
- [ ] Test Bluetooth disconnect and reconnect.
- [ ] Decode every resulting file on the host.

### Step 33: Run fault-injection tests

- [ ] Inject an ADC CRC error.
- [ ] Inject an ADC channel-header error.
- [ ] Inject a missing DRDY timestamp.
- [ ] Force acquisition-buffer exhaustion.
- [ ] Fill the SD card.
- [ ] Remove the SD card during recording.
- [ ] Simulate a short filesystem write.
- [ ] Simulate a long SD write stall.
- [ ] Corrupt a stored record CRC.
- [ ] Trigger an unexpected reset.
- [ ] Verify data/event-file correlation for every injected fault.

### Step 34: Run final long-duration acceptance tests

- [ ] Install both magnetic cards.
- [ ] Acquire all eight channels synchronously at 1 kSPS.
- [ ] Record continuously for eight hours.
- [ ] Confirm that every data record passes CRC-32C.
- [ ] Confirm that extracted sample counts match expected counts.
- [ ] Confirm that there are no unexplained sequence gaps.
- [ ] Confirm that every reported anomaly has a matching event entry.
- [ ] Confirm that Bluetooth status remains available throughout recording.
- [ ] Record SD-card model, firmware version, test date, and result as evidence.
- [ ] Update all implementation-status documents to match verified behavior.

## First implementation milestone

- [ ] Generate deterministic synthetic ADC frames.
- [ ] Encode the frames into valid 512-byte records.
- [ ] Write records to an SD card using aligned multi-record writes.
- [ ] Remove the SD card and open the recording on a PC.
- [ ] Decode the complete file with the host decoder.
- [ ] Verify all metadata, sample values, sequences, timestamps, and CRCs.

The first milestone is complete only when the same binary recording can be
independently generated, written, read, and validated by the firmware and host
tools.
