#include "ui_internal.h"

void pico_copy_firmware_version(char *destination)
{
    strcpy(destination, "v0.1.34");
    return;
}

void pico_append_program_state_label(char *destination)
{
    strcat(destination, pico_ui_active->app->recipe.program->state_label);
    return;
}

void pico_append_program_name(char *destination)
{
    strcat(destination, pico_ui_active->app->recipe.program->name);
    return;
}
