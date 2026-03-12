# CR30 Tray Loader Firmware (WIP)

This repository is being transitioned away from a general-purpose 3D printer firmware setup into a **tray loader controller** focused on **custom task execution only**.

## Current status

This is an early migration stage. The codebase still contains upstream Marlin structures while we progressively remove print-centric behavior and align motion/control logic with tray handling workflows.

## Migration goals

- Rebrand firmware identity and UI messaging for tray-loader operation.
- Disable or remove printer-only pathways (hotend / filament workflows, print-focused UX).
- Introduce custom task primitives (load, index, present, return, park, recover).
- Keep hardware safety constraints and motion reliability as first-class requirements.

See `docs/CUSTOM_TASKS_ROADMAP.md` for the implementation plan.

## Build note

Until migration is complete, platform build flow remains Marlin-compatible (PlatformIO / Arduino), with board configuration currently scoped to the existing CR30 control stack.

## License

This project remains under GPL v3 per upstream obligations. See [LICENSE](LICENSE).
