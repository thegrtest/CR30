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
#if HAS_FILAMENT_SENSOR
  #include "../../feature/runout.h"
#endif

namespace {

  constexpr uint8_t TRAY_COLUMNS = 6;
  constexpr uint8_t TRAY_ROWS = 10;
  constexpr uint8_t TRAY_POSITIONS = TRAY_COLUMNS * TRAY_ROWS;

  struct tray_pos_t { float x, z; };

  static constexpr tray_pos_t default_tray_positions[TRAY_POSITIONS] = {
    // 3 trays side-by-side, each tray is a 10x2 grid.
    // For each Z row, hit the 2 X points of tray 1, then tray 2, then tray 3.
    { -44.45f,  57.15f }, { -31.75f,  57.15f }, { -6.35f,  57.15f }, {  6.35f,  57.15f }, { 31.75f,  57.15f }, { 44.45f,  57.15f },
    { -44.45f,  44.45f }, { -31.75f,  44.45f }, { -6.35f,  44.45f }, {  6.35f,  44.45f }, { 31.75f,  44.45f }, { 44.45f,  44.45f },
    { -44.45f,  31.75f }, { -31.75f,  31.75f }, { -6.35f,  31.75f }, {  6.35f,  31.75f }, { 31.75f,  31.75f }, { 44.45f,  31.75f },
    { -44.45f,  19.05f }, { -31.75f,  19.05f }, { -6.35f,  19.05f }, {  6.35f,  19.05f }, { 31.75f,  19.05f }, { 44.45f,  19.05f },
    { -44.45f,   6.35f }, { -31.75f,   6.35f }, { -6.35f,   6.35f }, {  6.35f,   6.35f }, { 31.75f,   6.35f }, { 44.45f,   6.35f },
    { -44.45f,  -6.35f }, { -31.75f,  -6.35f }, { -6.35f,  -6.35f }, {  6.35f,  -6.35f }, { 31.75f,  -6.35f }, { 44.45f,  -6.35f },
    { -44.45f, -19.05f }, { -31.75f, -19.05f }, { -6.35f, -19.05f }, {  6.35f, -19.05f }, { 31.75f, -19.05f }, { 44.45f, -19.05f },
    { -44.45f, -31.75f }, { -31.75f, -31.75f }, { -6.35f, -31.75f }, {  6.35f, -31.75f }, { 31.75f, -31.75f }, { 44.45f, -31.75f },
    { -44.45f, -44.45f }, { -31.75f, -44.45f }, { -6.35f, -44.45f }, {  6.35f, -44.45f }, { 31.75f, -44.45f }, { 44.45f, -44.45f },
    { -44.45f, -57.15f }, { -31.75f, -57.15f }, { -6.35f, -57.15f }, {  6.35f, -57.15f }, { 31.75f, -57.15f }, { 44.45f, -57.15f }
  };

  static tray_pos_t tray_positions[TRAY_POSITIONS];

  static bool tray_positions_initialized = false;
  static uint8_t manual_anchor_count = 0;
  static tray_pos_t manual_row1[TRAY_COLUMNS] = {};
  static bool row2_anchor_ready = false;
  static tray_pos_t row2_first = {};

  constexpr float Z_BUMP_MM = 0.5f;

  uint8_t tray_position_index = 0;

  void initialize_tray_positions() {
    if (tray_positions_initialized) return;
    COPY(tray_positions, default_tray_positions);
    tray_positions_initialized = true;
  }

  bool generate_pattern_from_manual_anchors() {
    if (manual_anchor_count < TRAY_COLUMNS || !row2_anchor_ready) return false;

    const float z_step = row2_first.z - manual_row1[0].z;

    for (uint8_t row = 0; row < TRAY_ROWS; ++row) {
      const float row_z = manual_row1[0].z + z_step * row;
      for (uint8_t col = 0; col < TRAY_COLUMNS; ++col) {
        const uint8_t index = row * TRAY_COLUMNS + col;
        tray_positions[index] = { manual_row1[col].x, row_z };
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
    do_blocking_move_to(tray_positions[index].x, current_position.y, tray_positions[index].z);
    tray_position_index = index;
    ui.completion_feedback();
  }

  void action_home() {
    queue.inject_P(PSTR("G28"));
  }

  void action_center() {
    if (!tray_loader_ready()) return;
    do_blocking_move_to(0.0f, current_position.y, 0.0f);
    ui.completion_feedback();
  }

  void action_first() {
    move_to_tray_position(0);
  }

  void action_bump_z_forward() {
    if (!tray_loader_ready()) return;
    do_blocking_move_to(current_position.x, current_position.y, current_position.z + Z_BUMP_MM);
    ui.completion_feedback();
  }

  void action_bump_z_back() {
    if (!tray_loader_ready()) return;
    do_blocking_move_to(current_position.x, current_position.y, current_position.z - Z_BUMP_MM);
    ui.completion_feedback();
  }

  void save_manual_anchor(const uint8_t index) {
    if (!tray_loader_ready()) return;
    if (index >= TRAY_COLUMNS) return;

    manual_row1[index] = { current_position.x, current_position.z };
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
    row2_first = { current_position.x, current_position.z };
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

    auto run_cycle = [&]() {
      for (uint8_t i = 0; i < TRAY_POSITIONS; ++i) {
        const uint8_t index = (tray_position_index + i) % TRAY_POSITIONS;
        const tray_pos_t &tray = tray_positions[index];
        do_blocking_move_to(tray.x, current_position.y, tray.z);
        tray_position_index = index;
      }
    };

    #if HAS_FILAMENT_SENSOR
      runout.reset();
      runout.enabled = true;
      while (!runout.filament_ran_out) run_cycle();
      run_cycle(); // Enter a second cycle once runout triggers.
    #else
      run_cycle();
      ui.status_printf_P(0, PSTR("No runout sensor"));
    #endif

    if (tray_position_index + 1 >= TRAY_POSITIONS)
      tray_position_index = 0;
    else
      tray_position_index++;

    #if HAS_FILAMENT_SENSOR
      runout.reset();
    #endif

    ui.completion_feedback();
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

void menu_tray_loader() {
  START_MENU();
  BACK_ITEM(MSG_MAIN);
  ACTION_ITEM_P(PSTR("Home"), action_home);
  ACTION_ITEM_P(PSTR("Center"), action_center);
  ACTION_ITEM_P(PSTR("Go To First"), action_first);
  ACTION_ITEM_P(PSTR("Bump Z +0.5"), action_bump_z_forward);
  ACTION_ITEM_P(PSTR("Bump Z -0.5"), action_bump_z_back);
  ACTION_ITEM_P(PSTR("Save Spot 1"), action_save_spot_1);
  ACTION_ITEM_P(PSTR("Save Spot 2"), action_save_spot_2);
  ACTION_ITEM_P(PSTR("Save Spot 3"), action_save_spot_3);
  ACTION_ITEM_P(PSTR("Save Spot 4"), action_save_spot_4);
  ACTION_ITEM_P(PSTR("Save Spot 5"), action_save_spot_5);
  ACTION_ITEM_P(PSTR("Save Spot 6"), action_save_spot_6);
  ACTION_ITEM_P(PSTR("Save Row2 Spot1"), action_set_row2_first);
  ACTION_ITEM_P(PSTR("Next Position"), action_next);
  ACTION_ITEM_P(PSTR("Prev Position"), action_prev);
  ACTION_ITEM_P(PSTR("Auto Run"), action_auto_run);
  ACTION_ITEM_P(PSTR("Speed 80%"), action_speed_80);
  ACTION_ITEM_P(PSTR("Speed 100%"), action_speed_100);
  ACTION_ITEM_P(PSTR("Speed 130%"), action_speed_130);
  ACTION_ITEM_P(PSTR("Motors Off"), action_motors_off);
  END_MENU();
}

#endif // HAS_LCD_MENU
