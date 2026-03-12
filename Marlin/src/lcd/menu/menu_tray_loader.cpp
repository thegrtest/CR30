/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "../../inc/MarlinConfigPre.h"

#if HAS_LCD_MENU

#include "menu.h"

#include "../../module/motion.h"
#include "../../module/planner.h"
#include "../../gcode/queue.h"

namespace {

  constexpr uint8_t TRAY_COLUMNS = 6;
  constexpr uint8_t TRAY_ROWS = 10;
  constexpr uint8_t TRAY_POSITIONS = TRAY_COLUMNS * TRAY_ROWS;

  struct tray_pos_t { float x, z; };

  // Default 3-tray pattern (10 rows x 2 slots per tray), kept fully inside build limits.
  // Column order per row: Tray1 Slot1, Tray1 Slot2, Tray2 Slot1, Tray2 Slot2, Tray3 Slot1, Tray3 Slot2.
  constexpr float DEFAULT_X_MARGIN_MM = 24.0f;
  constexpr float DEFAULT_Y_OFFSET_MM = 101.6f; // ~4in from Y=0
  constexpr float DEFAULT_Z_START_MM = 40.0f;
  constexpr float DEFAULT_SLOT_SPACING_MM = 16.0f;
  constexpr float DEFAULT_TRAY_GAP_MM = 36.0f;
  constexpr float DEFAULT_ROW_SPACING_MM = 12.7f; // ~0.5in row-to-row Z change

  static tray_pos_t tray_positions[TRAY_POSITIONS];

  static bool tray_positions_initialized = false;
  static uint8_t manual_anchor_count = 0;
  static tray_pos_t manual_row1[TRAY_COLUMNS] = {};
  static bool row2_anchor_ready = false;
  static tray_pos_t row2_first = {};

  constexpr float Z_BUMP_MM = 6.35f;            // 0.25in
  constexpr float MIN_ROW_Z_DELTA_MM = 12.7f;  // ~0.5in

  enum tray_cycle_phase_t : uint8_t {
    PHASE_IDLE,
    PHASE_FORWARD,
    PHASE_REVERSE,
    PHASE_COMPLETE
  };

  static uint16_t completed_tray_count = 0;
  static tray_cycle_phase_t tray_cycle_phase = PHASE_IDLE;
  static uint8_t active_tray = 1;
  static uint8_t active_row = 1;
  static uint8_t active_slot = 1;

  uint8_t tray_position_index = 0;

  bool row_is_forward(const uint8_t row) {
    return (row & 0x01) == 0;
  }

  uint8_t slot_number_for_row_col(const uint8_t row, const uint8_t col) {
    const uint8_t pair_base = row * 2;
    const bool forward = row_is_forward(row);
    const uint8_t slot_in_pair = forward
      ? (col & 0x01)
      : uint8_t((col & 0x01) ? 0 : 1);
    return pair_base + slot_in_pair + 1;
  }

  constexpr float clamp_to_axis_range(const float value, const float minv, const float maxv) {
    return value < minv ? minv : (value > maxv ? maxv : value);
  }

  float tray_loader_y_target() {
    return clamp_to_axis_range(Y_MIN_POS + DEFAULT_Y_OFFSET_MM, Y_MIN_POS, Y_MAX_POS);
  }

  tray_pos_t clamp_tray_position(const tray_pos_t &pos) {
    return {
      clamp_to_axis_range(pos.x, X_MIN_POS, X_MAX_POS),
      clamp_to_axis_range(pos.z, Z_MIN_POS, Z_MAX_POS)
    };
  }

  bool tray_position_within_limits(const tray_pos_t &pos) {
    return WITHIN(pos.x, X_MIN_POS, X_MAX_POS) && WITHIN(pos.z, Z_MIN_POS, Z_MAX_POS);
  }

  uint8_t tray_number_for_col(const uint8_t col) {
    return (col >> 1) + 1;
  }

  void initialize_tray_positions() {
    if (tray_positions_initialized) return;

    const float pattern_width = (DEFAULT_SLOT_SPACING_MM * 3.0f) + (DEFAULT_TRAY_GAP_MM * 2.0f);
    const float x_start = clamp_to_axis_range((X_MAX_POS - pattern_width) * 0.5f, X_MIN_POS + DEFAULT_X_MARGIN_MM, X_MAX_POS - pattern_width - DEFAULT_X_MARGIN_MM);

    for (uint8_t row = 0; row < TRAY_ROWS; ++row) {
      const float row_z = DEFAULT_Z_START_MM + DEFAULT_ROW_SPACING_MM * row;
      for (uint8_t col = 0; col < TRAY_COLUMNS; ++col) {
        const float step = (col / 2) * (DEFAULT_SLOT_SPACING_MM + DEFAULT_TRAY_GAP_MM) + (col & 0x01 ? DEFAULT_SLOT_SPACING_MM : 0.0f);
        const uint8_t index = row * TRAY_COLUMNS + col;
        tray_positions[index] = clamp_tray_position({ x_start + step, row_z });
      }
    }

    tray_positions_initialized = true;
  }

  bool generate_pattern_from_manual_anchors() {
    if (manual_anchor_count < TRAY_COLUMNS || !row2_anchor_ready) return false;

    const float raw_z_step = row2_first.z - manual_row1[0].z;
    const float z_step = ABS(raw_z_step) < MIN_ROW_Z_DELTA_MM
      ? (raw_z_step < 0 ? -MIN_ROW_Z_DELTA_MM : MIN_ROW_Z_DELTA_MM)
      : raw_z_step;

    for (uint8_t row = 0; row < TRAY_ROWS; ++row) {
      const float row_z = manual_row1[0].z + z_step * row;
      for (uint8_t col = 0; col < TRAY_COLUMNS; ++col) {
        const uint8_t index = row * TRAY_COLUMNS + col;
        tray_positions[index] = clamp_tray_position({ manual_row1[col].x, row_z });
      }
    }

    return true;
  }

  bool tray_loader_ready() {
    if (all_axes_homed()) return true;
    queue.inject_P(PSTR("G28"));
    return false;
  }

  void move_to_tray_position(const uint8_t index) {
    initialize_tray_positions();
    if (!tray_loader_ready()) return;
    if (index >= TRAY_POSITIONS) return;

    const tray_pos_t target = clamp_tray_position(tray_positions[index]);
    if (!tray_position_within_limits(target)) {
      ui.status_printf_P(0, PSTR("Target out of range"));
      return;
    }

    do_blocking_move_to(target.x, tray_loader_y_target(), target.z);
    tray_position_index = index;
    active_row = (index / TRAY_COLUMNS) + 1;
    active_tray = tray_number_for_col(index % TRAY_COLUMNS);
    active_slot = slot_number_for_row_col(index / TRAY_COLUMNS, index % TRAY_COLUMNS);
    ui.completion_feedback();
  }

  void action_home() {
    queue.inject_P(PSTR("G28"));
  }

  void action_center() {
    if (!tray_loader_ready()) return;
    do_blocking_move_to(0.0f, tray_loader_y_target(), 0.0f);
    ui.completion_feedback();
  }

  void action_first() {
    move_to_tray_position(0);
  }

  void action_bump_z_forward() {
    if (!tray_loader_ready()) return;
    const float next_z = clamp_to_axis_range(current_position.z + Z_BUMP_MM, Z_MIN_POS, Z_MAX_POS);
    do_blocking_move_to(current_position.x, tray_loader_y_target(), next_z);
    ui.completion_feedback();
  }

  void action_bump_z_back() {
    if (!tray_loader_ready()) return;
    const float next_z = clamp_to_axis_range(current_position.z - Z_BUMP_MM, Z_MIN_POS, Z_MAX_POS);
    do_blocking_move_to(current_position.x, tray_loader_y_target(), next_z);
    ui.completion_feedback();
  }

  #if HAS_FILAMENT_SENSOR
    bool wait_for_filament_sensor_state_change() {
      const bool start_state = READ(FIL_RUNOUT_PIN) == FIL_RUNOUT_STATE;

      while (true) {
        if ((READ(FIL_RUNOUT_PIN) == FIL_RUNOUT_STATE) != start_state) {
          ui.status_printf_P(0, PSTR("Runout state changed"));
          return true;
        }

        if (current_position.z >= (Z_MAX_POS - 0.01f)) {
          ui.status_printf_P(0, PSTR("No state change before Z max"));
          return false;
        }

        const float next_z = clamp_to_axis_range(current_position.z + Z_BUMP_MM, Z_MIN_POS, Z_MAX_POS);
        do_blocking_move_to(current_position.x, tray_loader_y_target(), next_z);
      }
    }
  #endif

  void align_pattern_start_to_current_z() {
    const float z_offset = current_position.z - tray_positions[0].z;
    for (uint8_t i = 0; i < TRAY_POSITIONS; ++i)
      tray_positions[i] = clamp_tray_position({ tray_positions[i].x, tray_positions[i].z + z_offset });
  }

  void run_tray_pattern_once() {
    for (uint8_t row = 0; row < TRAY_ROWS; ++row) {
      const bool forward = row_is_forward(row);
      const int8_t start_col = forward ? 0 : (TRAY_COLUMNS - 1);
      const int8_t end_col = forward ? TRAY_COLUMNS : -1;
      const int8_t step = forward ? 1 : -1;

      for (int8_t col = start_col; col != end_col; col += step) {
        const uint8_t index = row * TRAY_COLUMNS + uint8_t(col);
        const tray_pos_t target = clamp_tray_position(tray_positions[index]);

        if (!tray_position_within_limits(target)) {
          ui.status_printf_P(0, PSTR("Blocked out-of-range target"));
          return;
        }

        active_tray = tray_number_for_col(uint8_t(col));
        active_row = row + 1;
        active_slot = slot_number_for_row_col(row, uint8_t(col));
        ui.status_printf_P(0, PSTR("Cycle T%u R%u S%u"), int(active_tray), int(active_row), int(active_slot));

        do_blocking_move_to(target.x, tray_loader_y_target(), target.z);
        tray_position_index = index;

        if (row == TRAY_ROWS - 1 && (uint8_t(col) & 0x01))
          ++completed_tray_count;
      }
    }
  }

  void save_manual_anchor(const uint8_t index) {
    if (!tray_loader_ready()) return;
    if (index >= TRAY_COLUMNS) return;

    manual_row1[index] = clamp_tray_position({ current_position.x, current_position.z });
    if (manual_anchor_count < index + 1) manual_anchor_count = index + 1;

    if (generate_pattern_from_manual_anchors())
      ui.status_printf_P(0, PSTR("Pattern updated"));
    else
      ui.status_printf_P(0, PSTR("Saved Spot %u"), int(index + 1));

    ui.completion_feedback();
  }

  void action_save_spot_1() { save_manual_anchor(0); }
  void action_save_spot_2() { save_manual_anchor(1); }
  void action_save_spot_3() { save_manual_anchor(2); }
  void action_save_spot_4() { save_manual_anchor(3); }
  void action_save_spot_5() { save_manual_anchor(4); }
  void action_save_spot_6() { save_manual_anchor(5); }

  void action_set_row2_first() {
    if (!tray_loader_ready()) return;
    row2_first = clamp_tray_position({ current_position.x, current_position.z });
    row2_anchor_ready = true;

    if (generate_pattern_from_manual_anchors())
      ui.status_printf_P(0, PSTR("Pattern updated"));
    else
      ui.status_printf_P(0, PSTR("Saved Row2 Spot1"));

    ui.completion_feedback();
  }

  void action_next() {
    move_to_tray_position((tray_position_index + 1) % TRAY_POSITIONS);
  }

  void action_prev() {
    move_to_tray_position((tray_position_index + TRAY_POSITIONS - 1) % TRAY_POSITIONS);
  }

  void action_auto_run() {
    initialize_tray_positions();
    if (!tray_loader_ready()) return;

    do_blocking_move_to(current_position.x, tray_loader_y_target(), current_position.z);

    tray_cycle_phase = PHASE_FORWARD;
    while (true) {
      run_tray_pattern_once();

    #if HAS_FILAMENT_SENSOR
      tray_cycle_phase = PHASE_REVERSE;
      if (!wait_for_filament_sensor_state_change()) break;
      align_pattern_start_to_current_z();
      tray_cycle_phase = PHASE_FORWARD;
    #else
      break;
    #endif
    }

    tray_cycle_phase = PHASE_COMPLETE;
    queue.inject_P(PSTR("G28"));

    ui.completion_feedback();
  }

  void draw_info_screen() {
    START_MENU();
    BACK_ITEM(MSG_BACK);

    static char trays_done_line[24];
    static char cycle_line[24];
    static char where_line[24];

    sprintf_P(trays_done_line, PSTR("Trays Loaded: %u"), completed_tray_count);

    const char *phase = "Idle";
    switch (tray_cycle_phase) {
      case PHASE_FORWARD: phase = "Forward"; break;
      case PHASE_REVERSE: phase = "Reverse"; break;
      case PHASE_COMPLETE: phase = "Complete"; break;
      default: break;
    }
    sprintf_P(cycle_line, PSTR("Cycle: %s"), phase);
    sprintf_P(where_line, PSTR("Tray %u Row %u S%u"), active_tray, active_row, active_slot);

    STATIC_ITEM_P(PSTR("The Kinetic Group"), SS_DEFAULT|SS_CENTER);
    STATIC_ITEM_P(trays_done_line);
    STATIC_ITEM_P(cycle_line);
    STATIC_ITEM_P(where_line);
    END_MENU();
  }

  void set_speed_percent(const int16_t percent) {
    feedrate_percentage = percent;
    ui.completion_feedback();
  }

  void action_speed_80()  { set_speed_percent(80); }
  void action_speed_100() { set_speed_percent(100); }
  void action_speed_130() { set_speed_percent(130); }

  void action_motors_off() {
    queue.inject_P(PSTR("M84"));
  }

}

void menu_tray_loader_info_screen() {
  draw_info_screen();
}

void menu_tray_loader() {
  START_MENU();
  BACK_ITEM(MSG_MAIN);
  ACTION_ITEM_P(PSTR("Home"), action_home);
  ACTION_ITEM_P(PSTR("Center"), action_center);
  ACTION_ITEM_P(PSTR("Go To First"), action_first);
  ACTION_ITEM_P(PSTR("Bump Z +0.25in"), action_bump_z_forward);
  ACTION_ITEM_P(PSTR("Bump Z -0.25in"), action_bump_z_back);
  ACTION_ITEM_P(PSTR("Save Spot 1"), action_save_spot_1);
  ACTION_ITEM_P(PSTR("Save Spot 2"), action_save_spot_2);
  ACTION_ITEM_P(PSTR("Save Spot 3"), action_save_spot_3);
  ACTION_ITEM_P(PSTR("Save Spot 4"), action_save_spot_4);
  ACTION_ITEM_P(PSTR("Save Spot 5"), action_save_spot_5);
  ACTION_ITEM_P(PSTR("Save Spot 6"), action_save_spot_6);
  ACTION_ITEM_P(PSTR("Save Spot 7 (Row2S1)"), action_set_row2_first);
  ACTION_ITEM_P(PSTR("Next Position"), action_next);
  ACTION_ITEM_P(PSTR("Prev Position"), action_prev);
  SUBMENU_P(PSTR("Information"), draw_info_screen);
  ACTION_ITEM_P(PSTR("Auto Run"), action_auto_run);
  ACTION_ITEM_P(PSTR("Speed 80%"), action_speed_80);
  ACTION_ITEM_P(PSTR("Speed 100%"), action_speed_100);
  ACTION_ITEM_P(PSTR("Speed 130%"), action_speed_130);
  ACTION_ITEM_P(PSTR("Motors Off"), action_motors_off);
  END_MENU();
}

#endif // HAS_LCD_MENU
