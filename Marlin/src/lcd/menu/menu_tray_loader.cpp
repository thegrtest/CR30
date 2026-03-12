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

  constexpr uint8_t TRAY_POSITIONS = 60;

  static constexpr xy_pos_t tray_positions[TRAY_POSITIONS] = {
    { -44.45f,  57.15f }, { -31.75f,  57.15f }, { -6.35f,  57.15f }, {  6.35f,  57.15f }, { 31.75f,  57.15f }, { 44.45f,  57.15f },
    {  44.45f,  44.45f }, {  31.75f,  44.45f }, {  6.35f,  44.45f }, { -6.35f,  44.45f }, { -31.75f,  44.45f }, { -44.45f,  44.45f },
    { -44.45f,  31.75f }, { -31.75f,  31.75f }, { -6.35f,  31.75f }, {  6.35f,  31.75f }, { 31.75f,  31.75f }, { 44.45f,  31.75f },
    {  44.45f,  19.05f }, {  31.75f,  19.05f }, {  6.35f,  19.05f }, { -6.35f,  19.05f }, { -31.75f,  19.05f }, { -44.45f,  19.05f },
    { -44.45f,   6.35f }, { -31.75f,   6.35f }, { -6.35f,   6.35f }, {  6.35f,   6.35f }, { 31.75f,   6.35f }, { 44.45f,   6.35f },
    {  44.45f,  -6.35f }, {  31.75f,  -6.35f }, {  6.35f,  -6.35f }, { -6.35f,  -6.35f }, { -31.75f,  -6.35f }, { -44.45f,  -6.35f },
    { -44.45f, -19.05f }, { -31.75f, -19.05f }, { -6.35f, -19.05f }, {  6.35f, -19.05f }, { 31.75f, -19.05f }, { 44.45f, -19.05f },
    {  44.45f, -31.75f }, {  31.75f, -31.75f }, {  6.35f, -31.75f }, { -6.35f, -31.75f }, { -31.75f, -31.75f }, { -44.45f, -31.75f },
    { -44.45f, -44.45f }, { -31.75f, -44.45f }, { -6.35f, -44.45f }, {  6.35f, -44.45f }, { 31.75f, -44.45f }, { 44.45f, -44.45f },
    {  44.45f, -57.15f }, {  31.75f, -57.15f }, {  6.35f, -57.15f }, { -6.35f, -57.15f }, { -31.75f, -57.15f }, { -44.45f, -57.15f }
  };

  uint8_t tray_position_index = 0;

  bool tray_loader_ready() {
    if (all_axes_homed()) return true;
    queue.inject_P(PSTR("G28"));
    return false;
  }

  void move_to_tray_position(const uint8_t index) {
    if (!tray_loader_ready()) return;
    do_blocking_move_to_xy(tray_positions[index]);
    tray_position_index = index;
    ui.completion_feedback();
  }

  void action_home() {
    queue.inject_P(PSTR("G28"));
  }

  void action_center() {
    if (!tray_loader_ready()) return;
    do_blocking_move_to_xy(0.0f, 0.0f);
    ui.completion_feedback();
  }

  void action_first() {
    move_to_tray_position(0);
  }

  void action_next() {
    move_to_tray_position((tray_position_index + 1) % TRAY_POSITIONS);
  }

  void action_prev() {
    move_to_tray_position((tray_position_index + TRAY_POSITIONS - 1) % TRAY_POSITIONS);
  }

  void action_auto_run() {
    if (!tray_loader_ready()) return;
    for (uint8_t i = 0; i < TRAY_POSITIONS; ++i)
      do_blocking_move_to_xy(tray_positions[(tray_position_index + i) % TRAY_POSITIONS]);
    tray_position_index = (tray_position_index + TRAY_POSITIONS - 1) % TRAY_POSITIONS;
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
