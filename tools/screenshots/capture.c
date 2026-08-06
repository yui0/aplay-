#define _GNU_SOURCE
#define APLAY_SCREENSHOT_BUILD 1
#include ".build/aplay+ui.c"

static void fill_demo_state(int custom_skin)
{
    static const char *tracks[] = {
        "01  First Light.flac",
        "02  Copper Horizon.flac",
        "03  Rain on Glass.flac",
        "04  A Moment Between Stars.flac",
        "05  Quiet Machines.flac",
        "06  After the Last Train.flac",
        "07  Northern Room.flac",
        "08  Paper Constellations.flac",
        "09  Nocturne for USB DAC.flac",
        "10  Ember / Sapphire Reprise.flac",
        "11  BitPerfect Morning.wav",
        "12  Signal Path.flac",
        "13  Soft Clipping (Live).flac",
        "14  Zero Resample.flac",
        "15  Closing Credits.flac"
    };

    g_playlist_name_count = (int)(sizeof(tracks) / sizeof(tracks[0]));
    for (int i = 0; i < g_playlist_name_count; ++i)
        snprintf(g_playlist_names[i], sizeof(g_playlist_names[i]), "%s", tracks[i]);

    memset(&g_gui, 0, sizeof(g_gui));
    snprintf(g_gui.filename, sizeof(g_gui.filename), "A Moment Between Stars");
    snprintf(g_gui.dir, sizeof(g_gui.dir), "/Music/Evening Sessions");
    snprintf(g_gui.codec, sizeof(g_gui.codec), "FLAC");
    snprintf(g_gui.note, sizeof(g_gui.note), "%s",
             custom_skin ? "Custom Winamp skin • BitPerfect" :
                           "Direct ALSA • BitPerfect playback");
    snprintf(g_gui.device, sizeof(g_gui.device), "USB DAC — hw:1,0");
    g_gui.rate = 192000;
    g_gui.bits = 24;
    g_gui.channels = 2;
    g_gui.cur = 102.0;
    g_gui.total = 257.0;
    g_gui.volume = 0.78f;
    g_gui.loop_mode = 1;
    g_gui.xtc_on = 1;
    g_gui.xtc_atten = 0.18f;
    g_gui.track_index = 4;
    g_gui.track_total = g_playlist_name_count;
    g_gui.dirty = GUI_DIRTY_ALL;

    g_playlist_selected = 3;
    g_playlist_scroll_first = 0;
    gui_fill_playlist_rows_locked(0);
    snprintf(g_dev_storage, sizeof(g_dev_storage), "hw:1,0");

    g_eq_enabled = 1;
    g_eq_preamp = 1.5f;
    {
        const float bands[APLAY_EQ_BANDS] =
            {2.0f, 1.2f, 0.4f, -0.8f, -1.1f, -0.2f, 1.1f, 2.2f, 1.3f, 0.5f};
        memcpy(g_eq_bands, bands, sizeof(bands));
    }
}

int main(int argc, char **argv)
{
    const char *output = "aplay-screenshot.png";
    const char *skin = NULL;
    int variant = 0;
    int menu = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--output") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "--skin") && i + 1 < argc) skin = argv[++i];
        else if (!strcmp(argv[i], "--variant") && i + 1 < argc) variant = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--menu")) menu = 1;
        else {
            fprintf(stderr, "usage: %s --output FILE [--skin DIR] [--variant N] [--menu]\n", argv[0]);
            return 2;
        }
    }

    if (menu) setenv("APLAY_SCREENSHOT_MENU", "1", 1);
    else unsetenv("APLAY_SCREENSHOT_MENU");
    screenshot_glfw_set_output(output, variant);
    screenshot_glfw_finish_after_frames(menu ? 3 : 2);

    g_skin_arg = skin;
    g_skins_folder_arg = NULL;
    g_gui_should_close = 0;
    fill_demo_state(skin != NULL);
    gui_run();
    cleanup_winamp_skin();
    return 0;
}
