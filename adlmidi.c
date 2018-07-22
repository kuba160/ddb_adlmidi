/*
    DeaDBeeF ADLMIDI plugin
    Copyright (C) 2018 Jakub Wasylków <kuba_160@protonmail.com>

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.

    3. This notice may not be removed or altered from any source distribution.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <adlmidi.h>
#include <math.h>
#include "deadbeef/deadbeef.h"
#include "banks.h"

//#define trace(...) { deadbeef->log ( __VA_ARGS__); }
#define trace(...) { deadbeef->log_detailed (&plugin.plugin, 0, __VA_ARGS__); }
#define trace_err(...) { deadbeef->log ( __VA_ARGS__); }

// todo
#define SAMPLERATE deadbeef->conf_get_int("adlmidi.samplerate", 44100);

extern const char * banks;

static DB_decoder_t plugin;
static DB_functions_t *deadbeef;

static DB_fileinfo_t *
adlmidi_open (uint32_t hints);
static int
adlmidi_init (DB_fileinfo_t *_info, DB_playItem_t *it);
static void
adlmidi_free (DB_fileinfo_t *);
static int
adlmidi_read (DB_fileinfo_t *, char *bytes, int size);
static int
adlmidi_seek_sample (DB_fileinfo_t *, int sample);
static int
adlmidi_seek (DB_fileinfo_t *, float time);
static DB_playItem_t *
adlmidi_insert (ddb_playlist_t *plt, DB_playItem_t *after, const char *fname);
static int
adlmidi_start (void);
static int
adlmidi_stop (void);

typedef struct {
    DB_fileinfo_t info;
    DB_playItem_t *it;
    int currentsample;
    float calc_time;
    const char * fname;
    struct ADL_MIDIPlayer *midi_player;
} adlmidi_info_t;

static const char * adlmidi_exts[] = { "midi", "mid", NULL };

// allocate codec control structure
static DB_fileinfo_t *
adlmidi_open (uint32_t hints) {
    trace ("adlmidi_open\n");
    DB_fileinfo_t *_info = malloc (sizeof (adlmidi_info_t));
    adlmidi_info_t *info = (adlmidi_info_t *)_info;
    memset (info, 0, sizeof (adlmidi_info_t));
    info->midi_player = adl_init (deadbeef->conf_get_int("adlmidi.samplerate", 44100));
    return _info;
}

static int
adlmidi_init (DB_fileinfo_t *info_p, DB_playItem_t *it) {
    trace ("adlmidi_init\n");
    adlmidi_info_t *info = (adlmidi_info_t *) info_p;
    info->it = it;
    deadbeef->pl_item_ref (it);

    // todo 
    info_p->fmt.bps = 16;
    info_p->fmt.channels = 2;
    info_p->fmt.samplerate  = 44100;
    for (int i = 0; i < info_p->fmt.channels; i++) {
        info_p->fmt.channelmask |= 1 << i;
    }
    info_p->readpos = 0;
    info_p->plugin = &plugin;

    if (adl_openFile(info->midi_player, deadbeef->pl_find_meta(it, ":URI")) < 0)
    {
        trace("Couldn't open midi file: %s\n", adl_errorInfo(info->midi_player));
        adl_close(info->midi_player);
        return -1;
    }
    else {
        trace ("adl_openFile sucess\n");
    }

    // set banks based on BANKS meta
    deadbeef->pl_lock ();
    const char *banknum = deadbeef->pl_find_meta(it, "BANK");
    deadbeef->pl_unlock ();
    int b_num = 0;
    if (banknum) {
        char *pEnd;
        int num = strtol (banknum, &pEnd, 10);
        if (banknum == pEnd) {
            b_num = deadbeef->conf_get_int("adlmidi.banknum", 14);
        }
        else {
            b_num = num;
        }
    }
    else {
        // else use settings value
        b_num = deadbeef->conf_get_int("adlmidi.banknum", 14);
    }
    adl_setBank(info->midi_player, b_num);
    return 0;
}

// free everything allocated in _init
static void
adlmidi_free (DB_fileinfo_t *_info) {
    trace ("adlmidi_free\n");
    adlmidi_info_t *info = (adlmidi_info_t *)_info;
    if (info) {
        deadbeef->pl_item_unref (info->it);
        adl_close (info->midi_player);
        free (info);
    }
}

static int
adlmidi_read (DB_fileinfo_t *info_o, char *bytes, int size) {
    //trace ("adlmidi_read, %d bytes\n", size);
    adlmidi_info_t *info = (adlmidi_info_t *) info_o;

    int channels = 2;
    int samples = size/channels;

    int samples_read = adl_play(info->midi_player, samples, (short int *)bytes);
    /*if (samples_count != size) {
        trace_err ("samples_count != size\n");

    }*/

    info->currentsample += samples;
    info->calc_time += ((float) samples*2) / 44100.0 / 8.0; //SAMPLERATE;
    return samples_read*2;
}

static int
adlmidi_seek_sample (DB_fileinfo_t *_info, int sample) {
    //trace ("adlmidi_seek_sample\n");
    float time = (float) sample / (float) SAMPLERATE;
    return adlmidi_seek (_info, time);
}

static int
adlmidi_seek (DB_fileinfo_t *_info, float time) {
    //trace ("adlmidi_seek\n");
    adlmidi_info_t *info = (adlmidi_info_t *)_info;
    adl_positionSeek (info->midi_player, time);
    //info->calc_time = time;//adl_positionTell(midi_player);//time;
    _info->readpos = time;//adl_positionTell(midi_player);//time;
    return 0;
    //return adlmidi_seek_sample (_info, time * _info->fmt.samplerate);
}

static DB_playItem_t *
adlmidi_insert (ddb_playlist_t *plt, DB_playItem_t *after, const char *fname) {
    //trace ("adlmidi_insert\n");
    // open file
    /*DB_FILE *fp = deadbeef->fopen (fname);
    if (!fp) {
        trace ("adlmidi: failed to fopen %s\n", fname);
        return NULL;
    }*/
    struct ADL_MIDIPlayer *midi_player = adl_init (deadbeef->conf_get_int("adlmidi.samplerate", 44100));
    
    if (!midi_player) {
        trace ("adlmidi: failed to init decoder: %s\n", adl_errorString());
        return NULL;
    }
    int ret = adl_openFile(midi_player, fname);
    if (ret) {
        
        trace ("adlmidi: failed to open file: %s\n", adl_errorInfo(midi_player));
        adl_close (midi_player);
    }

    // cannot read track info/tags from midi file (shrug)

    // no cuesheet, prepare track for addition
    DB_playItem_t *it = deadbeef->pl_item_alloc_init (fname, plugin.plugin.id);

    deadbeef->pl_replace_meta (it, ":FILETYPE", "MIDI");
    deadbeef->plt_set_item_duration (plt, it, adl_totalTimeLength (midi_player));

    // no title (and any other metadata)
    deadbeef->pl_add_meta (it, "title", NULL);

    // free decoder
    adl_close (midi_player);

    // now the track is ready, insert into playlist

    after = deadbeef->plt_insert_item (plt, after, it);
    deadbeef->pl_item_unref (it);
    return after;
}

static int
adlmidi_start (void) {
    return 0;
}

static int
adlmidi_stop (void) {
    return 0;
}

static const char settings_dlg[] =
    "property \"Samplerate\" entry adlmidi.samplerate 44100;\n";
//    "property \"Bank number\" entry adlmidi.banknum 14;\n"

// define plugin interface
static DB_decoder_t plugin = {
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 10,
    .plugin.version_major = 0,
    .plugin.version_minor = 2,
    .plugin.type = DB_PLUGIN_DECODER,
    .plugin.id = "adlmidi",
    .plugin.name = "adlmidi plugin",
    .plugin.descr = "MIDI player using libADLMIDI\n"
    "Default Banks:\n"
    BANKS
    ,
    .plugin.copyright = 
    "adlmidi plugin\n"
    "Copyright (C) 2018 Jakub Wasylków <kuAba_160@protonmail.com>\n"
    "\n"
    "This software is provided 'as-is', without any express or implied\n"
    "warranty.  In no event will the authors be held liable for any damages\n"
    "arising from the use of this software.\n"
    "\n"
    "Permission is granted to anyone to use this software for any purpose,\n"
    "including commercial applications, and to alter it and redistribute it\n"
    "freely, subject to the following restrictions:\n"
    "\n"
    "1. The origin of this software must not be misrepresented; you must not\n"
    " claim that you wrote the original software. If you use this software\n"
    " in a product, an acknowledgment in the product documentation would be\n"
    " appreciated but is not required.\n"
    "\n"
    "2. Altered source versions must be plainly marked as such, and must not be\n"
    " misrepresented as being the original software.\n"
    "\n"
    "3. This notice may not be removed or altered from any source distribution.\n",
    .plugin.website = "http://github.com/kuba160/",
    .plugin.start = adlmidi_start,
    .plugin.stop = adlmidi_stop,
    .plugin.configdialog = settings_dlg,
    .open = adlmidi_open,
    .init = adlmidi_init,
    .free = adlmidi_free,
    .read = adlmidi_read,
    .seek = adlmidi_seek,
    .seek_sample = adlmidi_seek_sample,
    .insert = adlmidi_insert,
    .exts = adlmidi_exts,
};

DB_plugin_t *
adlmidi_load (DB_functions_t *api) {
    #if 1
    // let's hope it doesn't overflow
    char *properties = malloc (4096 + strlen(plugin.plugin.configdialog));
    strcpy (properties, settings_dlg);
    int num = adl_getBanksCount();
    sprintf (properties+strlen(settings_dlg), "property \"Bank\" select[%d] adlmidi.banknum 14", num);
    int i;
    char const * const * banks = adl_getBankNames();
    for (i = 0 ; banks[i]; i++) {
        strcat (properties, " \"");
        strcat (properties, banks[i]);
        strcat (properties, "\"");
    }
    strcat (properties, ";\n");
    properties = realloc (properties, strlen(properties)+1);
    plugin.plugin.configdialog = (const char *) properties;
    #endif
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}
