# Custom Tasks Migration Roadmap

## Objective

Convert this fork from print-centric firmware behavior into a tray-loader controller with custom task execution as the primary interface.

## Phase 1 (started)

- Rebrand user-facing identity to tray loader terminology.
- Establish migration documentation and task taxonomy.
- Keep current hardware platform definitions while reducing printer-specific defaults.

## Phase 2

- Define task-oriented G-code/M-code interface for custom operations:
  - `TASK_LOAD`
  - `TASK_PRESENT`
  - `TASK_RETURN`
  - `TASK_INDEX`
  - `TASK_PARK`
  - `TASK_RECOVER`
- Route command handlers through a dedicated task state machine instead of print pipeline assumptions.

## Phase 3

- Remove/disable remaining print-only UX and feature paths (filament workflows, print-status semantics, etc.).
- Harden interlocks for tray motion, homing validation, and failure recovery.
- Add task simulation / acceptance checks for each supported motion sequence.

## Immediate constraints

- Preserve existing board compatibility during migration.
- Avoid broad HAL-level edits until task control APIs stabilize.
- Maintain safe defaults in configuration while printer features are being phased out incrementally.
