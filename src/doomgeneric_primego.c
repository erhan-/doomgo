#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <linux/fb.h>
#include <pthread.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doomstat.h"
#include "m_misc.h"
#include "deh_str.h"

#define FB_DEVICE    "/dev/fb0"
#define MIDI_DEVICE  "/dev/snd/midiC0D0"

static int fb_fd = -1;
static uint32_t *fb_mem = NULL;
static int fb_stride = 0;
static int fb_width = 800;
static int fb_height = 1280;

static volatile int midi_running = 0;
static int midi_fd = -1;

static void *midi_thread_func(void *arg);
static void parse_midi_byte(unsigned char b);
static void midi_note_on(int ch, int note, int vel);
static void midi_note_off(int ch, int note);
static void midi_cc(int ch, int cc, int val);

#define EVENT_QUEUE_SIZE 256
static unsigned short event_queue[EVENT_QUEUE_SIZE];
static volatile int event_write = 0;
static int event_read = 0;

static void push_event(int pressed, unsigned char key)
{
    int next = (event_write + 1) % EVENT_QUEUE_SIZE;
    if (next != event_read) {
        event_queue[event_write] = (pressed << 8) | key;
        event_write = next;
    }
}

static int jog_msb = -1, jog_lsb = -1;
static int jog_prev = -1;
static int jog_last_msb = -1;
static uint32_t jog_last_time = 0;
static int jog_active_dir = 0;
static int jog_streak = 0;

static struct timeval start_time;

void DG_Init(void)
{
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    gettimeofday(&start_time, NULL);

    fb_fd = open(FB_DEVICE, O_RDWR);
    if (fb_fd < 0) { exit(1); }

    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);

    fb_width = vinfo.xres;
    fb_height = vinfo.yres;
    fb_stride = finfo.line_length;

    fb_mem = mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) { close(fb_fd); exit(1); }

    if (vinfo.yoffset != 0 || vinfo.xoffset != 0) {
        vinfo.yoffset = 0;
        vinfo.xoffset = 0;
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
    }

    memset(fb_mem, 0, fb_height * fb_stride);

    midi_fd = open(MIDI_DEVICE, O_RDWR);

    midi_running = 1;
    pthread_t tid;
    pthread_create(&tid, NULL, midi_thread_func, NULL);
    pthread_detach(tid);
}

void DG_DrawFrame(void)
{
    pixel_t *src = DG_ScreenBuffer;
    int sw = DOOMGENERIC_RESX, sh = DOOMGENERIC_RESY;
    int stride = fb_stride / 4;

    for (int sy = 0; sy < sh; sy++) {
        for (int sx = 0; sx < sw; sx++) {
            pixel_t p = src[sy * sw + sx];
            int base_x = sy * 2;
            int base_y = (sw - 1 - sx) * 2;
            fb_mem[base_y * stride + base_x] = p;
            fb_mem[base_y * stride + base_x + 1] = p;
            fb_mem[(base_y + 1) * stride + base_x] = p;
            fb_mem[(base_y + 1) * stride + base_x + 1] = p;
        }
    }
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    if (jog_active_dir != 0 && DG_GetTicksMs() - jog_last_time > 80) {
        if (jog_active_dir > 0) push_event(0, KEY_RIGHTARROW);
        else push_event(0, KEY_LEFTARROW);
        jog_active_dir = 0;
    }

    if (event_read != event_write) {
        unsigned short ev = event_queue[event_read];
        event_read = (event_read + 1) % EVENT_QUEUE_SIZE;
        *pressed = (ev >> 8) & 1;
        *doomKey = ev & 0xFF;
        return 1;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

void DG_SleepMs(uint32_t ms)
{
    usleep(ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long sec = tv.tv_sec - start_time.tv_sec;
    long usec = tv.tv_usec - start_time.tv_usec;
    return (uint32_t)(sec * 1000 + usec / 1000);
}

static void midi_note_on(int ch, int note, int vel)
{
    (void)vel;
    int k = 0;
    if (ch == 2) {
        switch (note) {
        case 10: k = KEY_FIRE;   break;
        case 9:  k = KEY_USE;    break;
        case 8:  k = KEY_RSHIFT; break;
        case 15: k = '1';        break;
        case 16: k = '2';        break;
        case 17: k = '3';        break;
        case 18: k = '4';        break;
        }
    }
    if (ch == 15) {
        switch (note) {
        case 4:  k = KEY_UPARROW;   break;
        case 3:  k = KEY_DOWNARROW; break;
        case 7:  k = KEY_ESCAPE;    break;
        case 6:  k = KEY_ENTER;     break;
        }
    }
    if (k) push_event(1, k);
}

static void midi_note_off(int ch, int note)
{
    int k = 0;
    if (ch == 2) {
        switch (note) {
        case 10: k = KEY_FIRE;   break;
        case 9:  k = KEY_USE;    break;
        case 8:  k = KEY_RSHIFT; break;
        case 15: k = '1';        break;
        case 16: k = '2';        break;
        case 17: k = '3';        break;
        case 18: k = '4';        break;
        }
    }
    if (ch == 15) {
        switch (note) {
        case 4:  k = KEY_UPARROW;   break;
        case 3:  k = KEY_DOWNARROW; break;
        case 7:  k = KEY_ESCAPE;    break;
        case 6:  k = KEY_ENTER;     break;
        }
    }
    if (k) push_event(0, k);
}

static void midi_cc(int ch, int cc, int val)
{
    if (ch != 2) return;
    if (cc == 55) {
        jog_msb = val;
        jog_last_msb = val;
    } else if (cc == 77) {
        jog_lsb = val;
    } else return;

    int msb = (jog_msb >= 0) ? jog_msb : jog_last_msb;
    if (msb < 0 || jog_lsb < 0) return;

    int cur = (msb << 7) | jog_lsb;
    if (jog_prev >= 0 && cur != jog_prev) {
        int diff = cur - jog_prev;
        if (diff > 8192) diff -= 16384;
        else if (diff < -8192) diff += 16384;

        if (diff < 0) {
            if (jog_active_dir == 1) {
                jog_streak--;
                if (jog_streak <= -3) {
                    push_event(0, KEY_RIGHTARROW);
                    push_event(1, KEY_LEFTARROW);
                    jog_active_dir = -1; jog_streak = 0;
                }
            } else if (jog_active_dir == 0 && diff <= -2) {
                push_event(1, KEY_LEFTARROW);
                jog_active_dir = -1; jog_streak = 0;
            }
        } else if (diff > 0) {
            if (jog_active_dir == -1) {
                jog_streak++;
                if (jog_streak >= 3) {
                    push_event(0, KEY_LEFTARROW);
                    push_event(1, KEY_RIGHTARROW);
                    jog_active_dir = 1; jog_streak = 0;
                }
            } else if (jog_active_dir == 0 && diff >= 2) {
                push_event(1, KEY_RIGHTARROW);
                jog_active_dir = 1; jog_streak = 0;
            }
        }
        jog_last_time = DG_GetTicksMs();
    }
    jog_prev = cur;
    jog_msb = -1;
    jog_lsb = -1;
}

static int midi_rb_start = 0;
static unsigned char midi_running_status = 0;

static void parse_midi_byte(unsigned char b)
{
    static unsigned char buf[3];
    static int pos = 0;

    if (b & 0x80) {
        if (b < 0xF0) {
            midi_running_status = b;
            pos = 0;
        } else if (b == 0xF0) {
            midi_rb_start = 1;
            return;
        } else if (b == 0xF7) {
            midi_rb_start = 0;
            return;
        }
        return;
    }

    if (midi_rb_start) return;

    buf[pos++] = b;

    int len;
    if ((midi_running_status & 0xF0) == 0xC0 || (midi_running_status & 0xF0) == 0xD0)
        len = 2;
    else
        len = 3;

    if (pos >= len - 1) {
        int ch = midi_running_status & 0x0F;
        switch (midi_running_status & 0xF0) {
        case 0x80:
            midi_note_off(ch, buf[0]);
            break;
        case 0x90:
            if (buf[1] == 0) midi_note_off(ch, buf[0]);
            else midi_note_on(ch, buf[0], buf[1]);
            break;
        case 0xB0:
            midi_cc(ch, buf[0], buf[1]);
            break;
        }
        pos = 0;
    }
}

static void *midi_thread_func(void *arg)
{
    (void)arg;
    if (midi_fd < 0) return NULL;

    struct pollfd pfd = { .fd = midi_fd, .events = POLLIN };

    while (midi_running) {
        int ret = poll(&pfd, 1, 50);
        if (ret <= 0) continue;

        unsigned char data[256];
        int n = read(midi_fd, data, sizeof(data));
        if (n <= 0) continue;

        for (int i = 0; i < n; i++) {
            parse_midi_byte(data[i]);
        }
    }
    return NULL;
}

#include <alsa/asoundlib.h>

#define ALSA_CHANNELS 8
#define ALSA_RATE 11025
#define HW_RATE 44100
#define HW_CHANNELS 2

static snd_pcm_t *alsa_pcm = NULL;
static int alsa_init = 0;

typedef struct {
    int playing;
    unsigned char *data;
    size_t len, pos;
    int vol;
} alsa_chan_t;

static alsa_chan_t alsa_ch[ALSA_CHANNELS];

static int alsa_getsfx(sfxinfo_t *sfx)
{
    char buf[9];
    if (sfx->link) sfx = sfx->link;
    M_snprintf(buf, sizeof(buf), "ds%s", DEH_String(sfx->name));
    return W_GetNumForName(buf);
}

static int alsa_init_sound(boolean pref)
{
    (void)pref;
    int err = snd_pcm_open(&alsa_pcm, "hw:1,0", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "ALSA open hw:1,0 failed: %s\n", snd_strerror(err));
        return 0;
    }
    err = snd_pcm_set_params(alsa_pcm, SND_PCM_FORMAT_S16_LE,
        SND_PCM_ACCESS_RW_INTERLEAVED, HW_CHANNELS, HW_RATE, 1, 50000);
    if (err < 0) {
        fprintf(stderr, "ALSA params failed: %s\n", snd_strerror(err));
        snd_pcm_close(alsa_pcm); alsa_pcm = NULL; return 0;
    }
    fprintf(stderr, "ALSA opened hw:1,0 S16_LE %dch %dHz\n", HW_CHANNELS, HW_RATE);
    memset(alsa_ch, 0, sizeof(alsa_ch));
    alsa_init = 1;
    return 1;
}

static void alsa_shutdown(void)
{
    if (alsa_pcm) { snd_pcm_drain(alsa_pcm); snd_pcm_close(alsa_pcm); alsa_pcm = NULL; }
    alsa_init = 0;
}

static void alsa_update(void)
{
    if (!alsa_init || !alsa_pcm) return;
    if (snd_pcm_state(alsa_pcm) == SND_PCM_STATE_XRUN) snd_pcm_prepare(alsa_pcm);

    unsigned char buf[256];
    memset(buf, 128, sizeof(buf));
    for (int c = 0; c < ALSA_CHANNELS; c++) {
        if (!alsa_ch[c].playing) continue;
        for (size_t i = 0; i < sizeof(buf) && alsa_ch[c].pos < alsa_ch[c].len; i++) {
            int s = (int)buf[i] - 128;
            s += ((int)alsa_ch[c].data[alsa_ch[c].pos] - 128) * alsa_ch[c].vol / 127;
            if (s > 127) s = 127; if (s < -128) s = -128;
            buf[i] = (unsigned char)(s + 128);
            alsa_ch[c].pos++;
        }
        if (alsa_ch[c].pos >= alsa_ch[c].len) alsa_ch[c].playing = 0;
    }

    int up = HW_RATE / ALSA_RATE;
    static int16_t out[256 * 4 * 2];
    for (int i = 0; i < (int)sizeof(buf); i++) {
        int16_t s = ((int)buf[i] - 128) * 256;
        for (int j = 0; j < up; j++) {
            out[(i * up + j) * 2] = s;
            out[(i * up + j) * 2 + 1] = s;
        }
    }
    snd_pcm_sframes_t w = snd_pcm_writei(alsa_pcm, out, sizeof(buf) * up);
    if (w < 0) snd_pcm_recover(alsa_pcm, (int)w, 0);
}

static void alsa_params(int h, int vol, int sep) {
    if (h >= 0 && h < ALSA_CHANNELS) { alsa_ch[h].vol = vol; (void)sep; }
}

static int alsa_start(sfxinfo_t *sfx, int ch, int vol, int sep)
{
    if (!alsa_init || ch < 0 || ch >= ALSA_CHANNELS) return -1;
    alsa_ch[ch].playing = 0;
    unsigned char *lump = W_CacheLumpNum(sfx->lumpnum, PU_CACHE);
    if (!lump) return -1;
    int len = lump[2] | (lump[3] << 8);
    alsa_ch[ch].data = lump + 4;
    alsa_ch[ch].len = len;
    alsa_ch[ch].pos = 0;
    alsa_ch[ch].vol = vol;
    alsa_ch[ch].playing = 1;
    (void)sep;
    return ch;
}

static void alsa_stop(int h) { if (h >= 0 && h < ALSA_CHANNELS) alsa_ch[h].playing = 0; }
static int alsa_playing(int h) { return (h >= 0 && h < ALSA_CHANNELS) ? alsa_ch[h].playing : 0; }
static void alsa_precache(sfxinfo_t *s, int n) { (void)s; (void)n; }

static snddevice_t alsa_devs[] = { SNDDEVICE_SB };

sound_module_t DG_sound_module = {
    alsa_devs, 1,
    alsa_init_sound, alsa_shutdown, alsa_getsfx, alsa_update,
    alsa_params, alsa_start, alsa_stop, alsa_playing, alsa_precache,
};

static int music_init(void) { return 0; }
static void music_shut(void) {}
static void music_vol(int v) { (void)v; }
static void music_pause(void) {}
static void music_resume(void) {}
static void *music_reg(void *d, int l) { (void)d; (void)l; return NULL; }
static void music_unreg(void *h) { (void)h; }
static void music_play(void *h, int l) { (void)h; (void)l; }
static void music_stop(void) {}
static int music_playing(void) { return 0; }
static void music_poll(void) {}

static snddevice_t music_devs[] = { SNDDEVICE_GENMIDI };

music_module_t DG_music_module = {
    music_devs, 1,
    music_init, music_shut, music_vol, music_pause, music_resume,
    music_reg, music_unreg, music_play, music_stop, music_playing, music_poll,
};

int use_libsamplerate = 0;
float libsamplerate_scale = 1.0f;

static uint32_t t_basetime = 0;
static uint32_t t_basetime_ms = 0;

int I_GetTime(void)
{
    uint32_t ticks = DG_GetTicksMs();
    if (t_basetime == 0) t_basetime = ticks;
    ticks -= t_basetime;
    return (ticks * 35) / 1000;
}

int I_GetTimeMS(void)
{
    uint32_t ticks = DG_GetTicksMs();
    if (t_basetime_ms == 0) t_basetime_ms = ticks;
    return ticks - t_basetime_ms;
}

void I_Sleep(int ms) { DG_SleepMs(ms); }
void I_InitTimer(void) {}
void I_WaitVBL(int count) { (void)count; }
int I_GetTicks(void) { return DG_GetTicksMs(); }

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    while (1) {
        doomgeneric_Tick();
    }
    return 0;
}
