# Estuary v1 · Final Archive

An independently implemented C++ bot for Cambridge Battlecode 2026 Autumn. After the event ended, the final retained v1 source code and original release artifacts were archived in this repository on **2026-11-06**.

All six strategy source files match the SHA-256 hashes recorded for the final release on 2026-10-23 and the source used by the subsequent formal v1 baseline. The archiving process did not change the strategy, parameters, or draft order, and did not rebuild the bot or run any matches.

## Repository Contents

| File | Contents |
| --- | --- |
| [Strategy source](arena/bots/estuary/src/) | Six C++ files covering state, navigation, combat, economy, task planning, and the program entry point |
| [Official-format source bundle](artifacts/estuary-v1-submission.zip) | The original `cambc bundle` output, containing the same six files as the source directory, byte for byte |
| [Original WASM](artifacts/estuary-v1-sdk3.0.5.wasm) | The original release's local executable, built with CLI/SDK 3.0.5 and historically tested with engine 1.0.3 |
| [Build instructions](BUILD.md) (Chinese) | How to create a fresh project and rebuild using the pinned official tools |
| [Strategy design](docs/DESIGN_ZH.md) (Chinese) | The implemented strategy structure and its known limitations |
| [Provenance](PROVENANCE.md) (Chinese) | The scope of the independent implementation and its official dependencies |
| [Historical release evidence](evidence/HISTORICAL_RELEASE.json) | Versions, hashes, and independent rebuild results extracted from the original release verification record |
| [Archive manifest](ARCHIVE_MANIFEST.json) / [SHA-256 checksums](SHA256SUMS) | File sizes and checksums for the archived repository contents |

## Versions and Verification Scope

Here, v1 identifies the frozen strategy source version, not the latest SDK. The included WASM is the original release artifact built with CLI/SDK **3.0.5**. It is a different file from the later formal v1 baseline WASM built with CLI/SDK 4.0.0; the two artifacts are kept distinct and are not claimed to be byte-identical.

The original release record states that the source bundle was rebuilt in a fresh official project, producing the same SHA-256 hash as the original release WASM, and that one independent full-match verification was completed. These are **historical records from 2026-10-23**, not checks rerun during this archival work. The archive verification checked all six source hashes, ZIP CRCs, byte-for-byte agreement between the bundled and unpacked source files, and the original WASM hash. It makes no new claims about compatibility with the current server version or tournament rankings.

The strategy's main loop is to clear camps, make purchases, transport gems, and sacrifice them at altars. It uses legally available vision and map information, with task claims stored in 8 bytes of shared memory. See the design document for implementation details and limitations.

The SDK, maps, compiler toolchain, third-party opponents, research replays, training data, credentials, and machine-local control files are not distributed with this repository. Dependencies are obtained separately through the official tools. This archive does not add an open-source license.
