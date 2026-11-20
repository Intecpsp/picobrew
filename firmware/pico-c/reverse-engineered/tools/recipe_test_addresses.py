"""Select real C addresses for the shared recipe boundary-test vocabulary.

No original PC is translated by the emulator. Unmapped functions are omitted
and an attempted test call fails rather than using any S/Pro address.
"""
import json
from pathlib import Path

from inspect_oem import REPO
from original_arm import VARIANT
from entry_points import named_address, variant_address

ROOT = Path(__file__).resolve().parents[1]


def reference():
    source = REPO / 'firmware/pico-s-pro/reverse-engineered/tests/recipe_reference.json'
    result = json.loads(source.read_text())
    result['services'] += ['first_rinse_required', 'save_first_rinse_complete']
    comparison = json.loads((ROOT / 'analysis/function-correspondence.json').read_text())[
        'variants'][VARIANT.upper()]['comparison']
    mapped = {int(x['source'], 16): int(x['target'], 16)
              for x in comparison['candidates'] + comparison['different_by_call_context']}
    functions = {}
    for name, address in result['original_functions'].items():
        if address in mapped:
            functions[name] = mapped[address]
        try:
            functions[name] = named_address(name)
        except KeyError:
            pass
    aliases = {
        'pico_vacuum_compartments': 'pico_recipe_vacuum',
        'pico_heat_flush_compartments': 'pico_recipe_heat_flush_compartments',
        'pico_run_first_rinse': 'pico_recipe_first_rinse',
        'pico_reprime_steam_pump': 'pico_recipe_reprime_steam',
        'pico_run_descale_program': 'pico_recipe_descale',
        'pico_manual_brew_program': 'pico_recipe_manual_program',
        'pico_run_post_session_instructions': 'pico_recipe_post_session_instructions',
        'pico_run_control_program': 'pico_recipe_run_control_program_default',
        'pico_run_still_program': 'pico_recipe_still_program',
        'pico_run_deep_clean_program': 'pico_recipe_deep_clean_confirmation',
        'pico_run_required_maintenance': 'pico_recipe_required_maintenance',
        'pico_sousvide_coldbrew_program': 'pico_recipe_run_cooking_program',
        'pico_setup_instructions': 'pico_recipe_setup_step',
        'pico_ui_prompt_vacuum': 'pico_ui_vacuum_screen',
        'pico_ui_draw_continue_hint': 'pico_ui_draw_stop_footer_text',
    }
    for old, selected in aliases.items():
        functions[old] = named_address(selected)
    # Entries reviewed directly in the C caller/disassembly. UI bodies remain
    # outside this procedure-level test boundary.
    explicit = {'pico_display_clear_scrolling': 0x200050a8,
                'pico_ui_prompt_first_rinse': 0x2000eb94,
                'pico_recovery_delete_state': 0x20005a48,
                'pico_wifi_log_progress': 0x200103a0,
                'pico_ui_confirm_thermal_recovery': 0x2000ebe8,
                'pico_ui_show_still_communication_failure': 0x20009ab8,
                'pico_ui_show_still_temperature_warning': 0x2000e3d0,
                'pico_ui_show_still_completion': 0x2000e3b0,
                'pico_ui_draw_skip_step_confirmation': 0x20007064,
                'pico_ui_draw_done_footer': 0x2000b270,
                'pico_screen_starting_program': 0x20008ae4,
                'pico_ui_show_rinse_finished': 0x20009e10,
                'pico_ui_prompt_deep_clean_setup': 0x20009dd0,
                'pico_ui_start_descale': 0x200105c4,
                'pico_ui_choose_binary_option': 0x2000dc90,
                'pico_ui_descale_step': 0x2000eaa4,
                'pico_ui_walk_program_steps': 0x2000dd14,
                'pico_ui_wait_for_continue': 0x2000e394,
                'pico_ui_show_scrolling_message': 0x20009a84,
                'pico_ui_confirm_still_cleaned': 0x20009af0,
                'pico_storage_first_rinse_required': 0x2000b448,
                'pico_storage_save_first_rinse_complete': 0x200059d0,
                'pico_api_has_cleaned_ack': 0x2000f7b4,
                'pico_api_set_cleaned_ack': 0x2000f7c0,
                'pico_select_builtin_font': 0x200053f0,
                'pico_select_custom_font': 0x200053fc,
                'pico_display_temperature_summary': 0x2000a558,
                'pico_screen_ending_first_rinse': 0x2000a0f0,
                'pico_ui_draw_stop_confirmation': 0x20008a14}
    for name, address in explicit.items():
        if name in ('pico_display_clear_scrolling','pico_select_builtin_font','pico_select_custom_font'):
            functions[name] = address + (4 if VARIANT == 'ssd1309' else 0)
        else:
            functions[name] = address - (24 if VARIANT == 'ssd1309' else 0)
    result['original_functions'] = functions
    result['library_functions'] = {name: functions[name]
                                   for name in result['library_functions']}
    return result
