# Provenance and modifications

M88V means **V is for Vibe coding**. It is a development-oriented derivative,
not an official release from either upstream author.

## Sources

1. [M88 by cisc](http://retropc.net/cisc/m88/): original emulation core.
2. [bubio/M88M](https://github.com/bubio/M88M): portable CMake/raylib frontend.
   The development fork originally imported
   [6fc74b51d678c1942664e37c80c6e04892dd84e2](https://github.com/bubio/M88M/commit/6fc74b51d678c1942664e37c80c6e04892dd84e2)
   on 2026-07-30.
3. [marinyan/80_test](https://github.com/marinyan/80_test): Defender80 development
   repository, extracted at
   [f291a0635043235716a647dbbf37cbc510bace24](https://github.com/marinyan/80_test/commit/f291a0635043235716a647dbbf37cbc510bace24).
   M88V imports emulator sources, build/control scripts and documentation, not
   the game, NBLD loader, local ROMs or connection state. Git history was not imported.

## Development changes carried into M88V

- Deterministic, loopback-only, authenticated headless HTTP frontend.
- Frame stepping, direct BIN loading, keyboard matrix injection, register/memory
  inspection, PNG screenshots, T88 opening and M88DMP1 dumps.
- GUI direct-BIN startup and PC-8001 mode selection corrections.
- PC-80 development fixes including GVRAM mapping/display and Z80 wait accounting.
  Detailed earlier changes remain in the linked source repository history.

## M88V changes

- Shared PC-8001/PC-8801 development profiles and temporary ROM alias handling
  in `src/development`, used by GUI and headless frontends.
- N802, N80V2, N, N88V1, N88V1H and N88V2 selection; accurate machine/mode status.
- PC-88 startup no longer requires a PC-8001 BIOS. Explicit PC-80 ROM selection
  takes precedence over the default alias without changing the original ROM files.
- GUI BIN startup no longer forces PC-8001 mode; generic launcher parameters.
- Configurable BASIC typing cadence, ROM-free unit tests and six-mode integration tests.
- Optional main-Z80 observers, symbol/region T-state profiling, byte-write provenance,
  instruction-boundary watch stops and register history, including interrupt events.
- Actual CPU read/write bank inspection shared across PC-80/88 mappings.
- Shared validated checkpoint codec, scheduler/fetch-cache restoration, headless
  emulated-time calendar, keyboard recording/replay, and GUI BIN-session checkpoints.
  External media rollback and sample-exact audio are not included.
- Modified legacy headers and memory/calendar/scheduler sources were converted from
  CP932 to UTF-8 while retaining their original notices and comment text.

## Licensing

The original M88 core is copyright cisc and remains subject to the original
terms reproduced in [docs/README.md](docs/README.md#ライセンス).
M88V new files and added code are BSD-2-Clause, as are M88M's new porting layers;
see [LICENSE](LICENSE). Existing files and bundled third-party components retain
their original terms. **This is not a BSD-only relicensing of the entire tree.**

Preserve all original notices, publish the required source alongside modified
distributions, and retain the modification/provenance documentation. The bundled
font notices and SIL OFL are in [assets/NOTICE.md](assets/NOTICE.md) and
[assets/OFL.txt](assets/OFL.txt). ROM data must not be redistributed with M88V.
