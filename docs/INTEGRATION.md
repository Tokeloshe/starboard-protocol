# Integrating Starboard

*How to wire Starboard into the software you already fly or operate.* It is
one C library with no heap, no threads, no OS calls and no clock reads, so
it fits any executive. You connect **four events** to **four calls**.

The compiled, tested reference for everything on this page is
[`examples/flight_loop.c`](../examples/flight_loop.c). The framework
skeletons below show where the same calls go in NASA cFS, F Prime and an
RTOS. They are sketches against those frameworks' public APIs and are **not
compiled in this repository's CI**; adapt names to your mission.

## The four hooks

| When this happens on your spacecraft | Call | What you do with the result |
|---|---|---|
| **Boot**, and periodically (e.g. each orbit) | `sb_fsw_init()` / `sb_fsw_selftest()` | non-zero bits: Starboard latched SAFE, report it and never act on it |
| **A collision warning arrives** (CDM uplinked, or from on-board screening) | `sb_fsw_ingest_cdm()` | read `enc[slot].decision`; in AUTONOMOUS give the burn to orbit control; broadcast `sb_fsw_notice()` |
| **A neighbour's notice arrives** on the inter-satellite link | `sb_flock_receive()` then `sb_flock_check()` | relay the bytes it hands back; on `conflict`, alert or re-plan |
| **Telemetry cycle** | `sb_fsw_poll_tm()` until 0, `sb_fsw_expire()` | downlink each packet (already CCSDS Space Packets) |

Plus two ground-driven inputs: `sb_fsw_set_mode()` (operator command) and
`sb_fsw_set_counterpart()` (registry verification of the other craft).

```c
#include "starboard.h"          /* dist/starboard.h: everything */

static SbFsw   sb;              /* ~25 KB, static */
static SbFlock flock;

void starboard_boot(void) {
    SbFswConfig cfg;
    sb_easy_profile_config(SB_SAT_SMALLSAT, &cfg);   /* or set fields yourself */
    cfg.self_id = sb_make_object_id("2026-001A");     /* your COSPAR id */
    cfg.operating_mode = SB_MODE_ADVISORY;           /* shadow mode first */
    sb_fsw_init(&sb, &cfg);                          /* runs the self-test */
    sb_flock_init(&flock, cfg.self_id);
}
```

### Bring-up in three phases

1. **ADVISORY (shadow mode).** Starboard decides and downlinks its verdicts;
   humans keep flying the spacecraft. Compare its verdicts with your
   current conjunction process for a few months.
2. **ADVISORY + notices.** Also broadcast maneuver notices so neighbours can
   plan around you. Still no autonomous burns.
3. **AUTONOMOUS.** `sb_fsw_set_mode(&sb, SB_MODE_AUTONOMOUS)`: give-way
   decisions emit maneuver requests (telemetry APID base+2) for your orbit
   control to execute, within whatever limits your FDIR imposes.

## NASA cFS

Make a `STARBOARD` app (the name "SB" is already the cFS Software Bus).

```c
/* starboard_app.c: sketch, not compiled here */
#include "cfe.h"
#include "starboard.h"

static SbFsw   Sb;
static SbFlock Flock;

void STARBOARD_AppMain(void) {
    CFE_SB_Buffer_t *buf;
    starboard_boot();                     /* see above */
    while (CFE_ES_RunLoop(&RunStatus)) {
        CFE_SB_ReceiveBuffer(&buf, CmdPipe, CFE_SB_PEND_FOREVER);
        switch (MsgIdOf(buf)) {
        case STARBOARD_CMD_MID:           /* ground command */
            switch (CommandCode(buf)) {
            case STARBOARD_SET_MODE_CC:   sb_fsw_set_mode(&Sb, ModeArg(buf)); break;
            case STARBOARD_LOAD_CDM_CC:   /* CDM file path in the command */
                LoadFile(PathArg(buf), CdmBuf, &Len);
                sb_fsw_ingest_cdm(&Sb, CdmBuf, Len, GpsNowMs(), &Slot);
                OnDecision(Slot);         /* maneuver request + notice */
                break;
            case STARBOARD_SELFTEST_CC:   sb_fsw_selftest(&Sb); break;
            }
            break;
        case STARBOARD_ISL_NOTICE_MID:    /* from your ISL app */
            OnNotice(PayloadOf(buf));     /* sb_flock_receive + check */
            break;
        case STARBOARD_SEND_HK_MID:       /* scheduler wakeup */
            while ((n = sb_fsw_poll_tm(&Sb, Pkt, sizeof Pkt, GpsNowMs())) != 0)
                CFE_SB_TransmitBuffer(WrapPacket(Pkt, n), true);
            sb_fsw_expire(&Sb, GpsNowMs(), 86400000);
            break;
        }
    }
}
```

The telemetry packets are already CCSDS Space Packets with their own APIDs
(`apid_base`, +1, +2); either downlink them as-is or re-wrap the payload in
your cFS telemetry message. Time: convert `CFE_TIME_GetTime()` to GPS
milliseconds once, in `GpsNowMs()`.

## F Prime (F´)

One **active component**, `Starboard`, with:

| Port / command | Kind | Calls |
|---|---|---|
| `LOAD_CDM(path)` | command | read file → `sb_fsw_ingest_cdm` |
| `SET_MODE(mode)` | command | `sb_fsw_set_mode` |
| `SELF_TEST` | command | `sb_fsw_selftest`, emit an event with the bits |
| `noticeIn` | async input port (`Fw::Buffer`) | `sb_flock_receive` → `noticeOut` if relayed |
| `noticeOut` | output port | to your ISL component |
| `burnRequest` | output port | to your GNC component (AUTONOMOUS only) |
| `schedIn` | rate-group input | `sb_fsw_poll_tm` → `tmOut` (`Fw::ComBuffer`), `sb_fsw_expire` |

Telemetry channels worth defining: mode, self-test bits, `cdm_accepted`,
`cdm_rejected`, `seu_corrected`, `maneuver_requests` (all fields of `SbFsw`).

## FreeRTOS, Zephyr, bare metal

One task owns the context (the `sb_fsw_*` calls are not re-entrant on a
single context); everything else talks to it through a queue.

```c
/* sketch, not compiled here */
void starboard_task(void *arg) {
    starboard_boot();
    for (;;) {
        Msg m;
        xQueueReceive(starboard_q, &m, pdMS_TO_TICKS(1000));   /* 1 Hz tick */
        if (m.kind == MSG_CDM)    { sb_fsw_ingest_cdm(&sb, m.cdm, m.len, gps_now_ms(), &slot);
                                    on_decision(slot); }
        if (m.kind == MSG_NOTICE) { on_notice(m.bytes); }
        while ((n = sb_fsw_poll_tm(&sb, pkt, sizeof pkt, gps_now_ms())) != 0)
            radio_send(pkt, n);
        sb_fsw_expire(&sb, gps_now_ms(), 86400000);
    }
}
```

Stack: give the task **at least 8 KB** (measured worst case 6.4 KB on
Cortex-M7; `tools/stack_depth.py` recomputes it for your build). Static RAM
is ~25 KB; shrink `SB_FSW_MAX_ENCOUNTERS` / `SB_FLOCK_MAX_ENTRIES` for small
MCUs.

## Build systems

The single-file library needs one flag, `-ffp-contract=off`:

```cmake
# CMake
add_library(starboard STATIC dist/starboard.c)
target_include_directories(starboard PUBLIC dist)
target_compile_options(starboard PRIVATE -ffp-contract=off)
target_link_libraries(starboard PUBLIC m)
```

```make
# Make
starboard.o: dist/starboard.c
	$(CC) -std=c99 -O2 -ffp-contract=off -c $< -o $@
```

Cross-compile with your normal toolchain. On **FPU-less ARM** (Cortex-M0/M3,
ARMv5) the library carries its own correctly-rounded double addition
(`softdouble.c`, included) because the stock libgcc routine misrounds a case
Starboard hits; it is linked automatically. On **MSVC** use `/fp:precise`
(the default), never `/fp:fast`.

**Always run the self-test on the real target.** It is the final word on
whether a build computes the same bits as every other Starboard craft.

## Ground segment

* **Python:** `pip install "git+https://github.com/Tokeloshe/starboard-protocol#subdirectory=python"`,
  then `starboard.decide(...)`; see [QUICKSTART.md](QUICKSTART.md).
* **CLI / pipelines:** `starboard incoming/*.cdm --me <id> --json`, or
  `sbctl cdm FILE --json`. Both exit non-zero when a warning can't be read.
* **Predicting a neighbour:** anyone holding the same CDM can recompute a
  craft's decision and its maneuver notice bit for bit (`sb_fsw_decide`,
  `sb_notice_from_plan`), with no link to that craft.

## Checklist

- [ ] built with `-ffp-contract=off`; self-test passes **on the target**
- [ ] `self_id` is your COSPAR international designator, as in the CDMs you receive
- [ ] GPS-ms time source wired (`now` is an input; nothing reads a clock)
- [ ] telemetry APIDs (`apid_base`, +1, +2) allocated in your mission database
- [ ] ADVISORY period completed and reviewed before AUTONOMOUS
- [ ] maneuver requests bounded by your own GNC/FDIR limits
- [ ] inter-satellite notices relayed (optional, Layer 4)
