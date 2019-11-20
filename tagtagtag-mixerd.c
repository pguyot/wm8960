// cc tagtagtag-mixerd.c -lasound -lpthread -o tagtagtag-mixerd

#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <alsa/asoundlib.h>
#include <syslog.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <stdarg.h>

#ifdef TEST
#define DEBUG
#include <assert.h>
#include <strings.h>
#endif

#define CONFIG_FILE_PATH "/var/lib/tagtagtag-sound/mixer.conf"
#define DEFAULT_TAG_SPEAKER_LOW         121
#define DEFAULT_TAG_SPEAKER_HIGH        127
#define DEFAULT_TAGTAG_SPEAKER_LOW      121
#define DEFAULT_TAGTAG_SPEAKER_HIGH     127
#define DEFAULT_SPEAKER_BASE            249
#define DEFAULT_HEADPHONE_LOW           227
#define DEFAULT_HEADPHONE_HIGH          249
#define LINEOUT_MODE_LINEOUT            0
#define LINEOUT_MODE_HEADPHONE          1
#define DEFAULT_LINEOUT_MODE            LINEOUT_MODE_LINEOUT
#define DEFAULT_MICROPHONE_ENABLED      1
#define DEFAULT_INPUT1_BASE             3
#define DEFAULT_CAPTURE_BASE            39
#define PID_FILE_PATH "/run/tagtagtag-mixerd.pid"

struct config {
    int debug;
    int tag_speaker_low;
    int tag_speaker_high;
    int tagtag_speaker_low;
    int tagtag_speaker_high;
    int speaker_base;
    int headphone_low;
    int headphone_high;
    int lineout_mode;
    int microphone_enabled;
    int input1_base;
    int capture_base;
};

static void do_log(int daemon, int debug, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if (daemon) {
        if (debug) {
            vsyslog(LOG_DEBUG | LOG_DAEMON, fmt, args);
        }
    } else {
        vfprintf(stderr, fmt, args);
    }

    va_end(args);
}

static int hctl_jack_cb(snd_hctl_elem_t *elem, unsigned int mask) {
    int* jack_value = snd_hctl_elem_get_callback_private(elem);
    snd_ctl_elem_value_t *elem_value;

    snd_ctl_elem_value_alloca(&elem_value);
    snd_hctl_elem_read(elem, elem_value);
    *jack_value = snd_ctl_elem_value_get_boolean(elem_value, 0);

    return 0;
}

static int hctl_button_cb(snd_hctl_elem_t *elem, unsigned int mask) {
    int* buttons = snd_hctl_elem_get_callback_private(elem);
    snd_ctl_elem_value_t *elem_value;

    snd_ctl_elem_value_alloca(&elem_value);
    snd_hctl_elem_read(elem, elem_value);
    buttons[0] = snd_ctl_elem_value_get_integer(elem_value, 0);
    buttons[1] = snd_ctl_elem_value_get_integer(elem_value, 1);

    return 0;
}

static void default_values(struct config* conf) {
    conf->debug = 1;
    conf->tag_speaker_low = DEFAULT_TAG_SPEAKER_LOW;
    conf->tag_speaker_high = DEFAULT_TAG_SPEAKER_HIGH;
    conf->tagtag_speaker_low = DEFAULT_TAGTAG_SPEAKER_LOW;
    conf->tagtag_speaker_high = DEFAULT_TAGTAG_SPEAKER_HIGH;
    conf->speaker_base = DEFAULT_SPEAKER_BASE;
    conf->headphone_low = DEFAULT_HEADPHONE_LOW;
    conf->headphone_high = DEFAULT_HEADPHONE_HIGH;
    conf->lineout_mode = DEFAULT_LINEOUT_MODE;
    conf->microphone_enabled = DEFAULT_MICROPHONE_ENABLED;
    conf->input1_base = DEFAULT_INPUT1_BASE;
    conf->capture_base = DEFAULT_CAPTURE_BASE;
}

static void load_config(int daemon, struct config* conf, const char* config_file_path) {
    char buffer[512];
    struct config new_config = *conf;
    int valid = 0;
    FILE* config_file = fopen(config_file_path, "r");
    if (config_file != NULL) {
        valid = 1;
        int line_ix = 0;
        while (1) {
            char* value;
            int key_len;
            char* line = fgets(buffer, sizeof(buffer), config_file);
            if (line == NULL)
                break;
            line_ix++;

            while (*line == ' ' && *line != 0) line++;
            if (*line == ';')
                continue;
            value = line;
            while (*value != '=' && *value != 0 && *value != '\n') {
                value++;
            }
            key_len = value - line;
            // Empty lines.
            if (key_len == 0 && *value != '=')
                continue;
            if (key_len == 0 && *value != '=') {
                do_log(daemon, conf->debug, "Syntax error in configuration file %s line %i", config_file_path, line_ix);
                valid = 0;
                break;
            }
            value++;
            if (value[strlen(value) - 1] == '\n') {
                value[strlen(value) - 1] = 0;
            }
            if (key_len == strlen("debug") && strncmp(line, "debug", key_len) == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    new_config.debug = 1;
                } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                    new_config.debug = 0;
                } else {
                    do_log(daemon, conf->debug, "Unknown debug value %s, ignoring", value);
                }
            } else if (key_len == strlen("tag-speaker-low") && strncmp(line, "tag-speaker-low", key_len) == 0) {
                new_config.tag_speaker_low = atoi(value);
            } else if (key_len == strlen("tag-speaker-high") && strncmp(line, "tag-speaker-high", key_len) == 0) {
                new_config.tag_speaker_high = atoi(value);
            } else if (key_len == strlen("tagtag-speaker-low") && strncmp(line, "tagtag-speaker-low", key_len) == 0) {
                new_config.tagtag_speaker_low = atoi(value);
            } else if (key_len == strlen("tagtag-speaker-high") && strncmp(line, "tagtag-speaker-high", key_len) == 0) {
                new_config.tagtag_speaker_high = atoi(value);
            } else if (key_len == strlen("speaker-base") && strncmp(line, "speaker-base", key_len) == 0) {
                new_config.speaker_base = atoi(value);
            } else if (key_len == strlen("headphone-high") && strncmp(line, "headphone-high", key_len) == 0) {
                new_config.headphone_high = atoi(value);
            } else if (key_len == strlen("headphone-low") && strncmp(line, "headphone-low", key_len) == 0) {
                new_config.headphone_low = atoi(value);
            } else if (key_len == strlen("lineout-mode") && strncmp(line, "lineout-mode", key_len) == 0) {
                if (strcmp(value, "headphone") == 0) {
                    new_config.lineout_mode = LINEOUT_MODE_HEADPHONE;
                } else if (strcmp(value, "lineout") == 0) {
                    new_config.lineout_mode = LINEOUT_MODE_LINEOUT;
                } else {
                    do_log(daemon, conf->debug, "Unknown lineout-mode value %s, ignoring", value);
                }
            } else if (key_len == strlen("microphone-enabled") && strncmp(line, "microphone-enabled", key_len) == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    new_config.microphone_enabled = 1;
                } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                    new_config.microphone_enabled = 0;
                } else {
                    do_log(daemon, conf->debug, "Unknown microphone-enabled value %s, ignoring", value);
                }
            } else if (key_len == strlen("input1-base") && strncmp(line, "input1-base", key_len) == 0) {
                new_config.input1_base = atoi(value);
            } else if (key_len == strlen("capture-base") && strncmp(line, "capture-base", key_len) == 0) {
                new_config.capture_base = atoi(value);
            }
        }
        fclose(config_file);
    } else {
        do_log(daemon, conf->debug, "Could not open configuration file %s (errno=%i)", config_file_path, errno);
    }
    if (valid) {
        *conf = new_config;
    }
}

int setup_hctl(snd_hctl_t **hctlp) {
    int err = snd_hctl_open(hctlp, "hw:CARD=tagtagtagsound", SND_CTL_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "Failed to open hctl (is card present?)\n");
        return -1;
    }
    err = snd_hctl_nonblock(*hctlp, 1);
    if (err < 0) {
        fprintf(stderr, "Failed to nonblock hctl\n");
        return -1;
    }
    err = snd_hctl_load(*hctlp);
    if (err < 0) {
        fprintf(stderr, "Failed to load hctl\n");
        return -1;
    }
    
    return 0;
}

void teardown_hctl(snd_hctl_t *hctl) {
    snd_hctl_close(hctl);
}

void setup_callbacks(snd_hctl_t *hctl, int *jack, int *buttons) {
    snd_ctl_elem_id_t *id;
    snd_hctl_elem_t *elem;

    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_CARD);
    snd_ctl_elem_id_set_name(id, "Headphones Jack");
    elem = snd_hctl_find_elem(hctl, id);
    
    snd_hctl_elem_set_callback(elem, hctl_jack_cb);
    snd_hctl_elem_set_callback_private(elem, jack);

    // Initial callback to set value.
    hctl_jack_cb(elem, 0);

    snd_ctl_elem_id_set_name(id, "Volume Button");
    elem = snd_hctl_find_elem(hctl, id);
    
    snd_hctl_elem_set_callback(elem, hctl_button_cb);
    snd_hctl_elem_set_callback_private(elem, buttons);

    // Initial callback to set value.
    hctl_button_cb(elem, 0);
}

typedef enum ctl_elem_value_type {
    single_bool,
    dual_bool,
    single_integer,
    dual_integer
} ctl_elem_value_type;

static void set_ctl_elem_value(snd_hctl_t *hctl, const char *name, ctl_elem_value_type val_type, int val) {
    snd_ctl_elem_id_t *id;
    snd_hctl_elem_t *elem;
    snd_ctl_elem_value_t *elem_value;

    snd_ctl_elem_value_alloca(&elem_value);
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);

    snd_ctl_elem_id_set_name(id, name);
    elem = snd_hctl_find_elem(hctl, id);
    switch (val_type) {
        case single_bool:
            snd_ctl_elem_value_set_boolean(elem_value, 0, val);
            break;

        case dual_bool:
            snd_ctl_elem_value_set_boolean(elem_value, 0, val);
            snd_ctl_elem_value_set_boolean(elem_value, 1, val);
            break;

        case single_integer:
            snd_ctl_elem_value_set_integer(elem_value, 0, val);
            break;

        case dual_integer:
            snd_ctl_elem_value_set_integer(elem_value, 0, val);
            snd_ctl_elem_value_set_integer(elem_value, 1, val);
            break;
    }
    snd_hctl_elem_write(elem, elem_value);
}

void set_volumes(int daemon, snd_hctl_t *hctl, struct config* conf, int jack, int buttons[2]) {
    int speaker_volume;
    int headphone_volume;
    int playback_volume;

    // Workaround bug in hardware.
    // Volume 1 (27)    Volume 2 (22)        Tag            TagTag
    //    0                0                 mute           medium volume
    //    0                1                 max volume     mute
    //    1                0                 medium volume  max volume
    //    1                1                        impossible

    if (jack && conf->lineout_mode == LINEOUT_MODE_HEADPHONE) {
        speaker_volume = 0;
        headphone_volume = 0;
        if (buttons[0] == 0 && buttons[1] == 0) {
            playback_volume = conf->headphone_low;
        } else if (buttons[0] == 0 && buttons[1] == 1) {
            playback_volume = 0;
        } else if (buttons[0] == 1 && buttons[1] == 0) {
            playback_volume = conf->headphone_high;
        }
    } else if (buttons[0] == 0 && buttons[1] == 0) {
        speaker_volume = 0;
        headphone_volume = conf->tagtag_speaker_low;
        playback_volume = conf->speaker_base;
    } else if (buttons[0] == 0 && buttons[1] == 1) {
        speaker_volume = conf->tag_speaker_high;
        headphone_volume = 0;
        playback_volume = conf->speaker_base;
    } else if (buttons[0] == 1 && buttons[1] == 0) {
        speaker_volume = conf->tag_speaker_low;
        headphone_volume = conf->tagtag_speaker_high;
        playback_volume = conf->speaker_base;
    } else {
        do_log(daemon, conf->debug, "Unexpected button values (both are high)");
        return;
    }

    set_ctl_elem_value(hctl, "Speaker Playback Volume", dual_integer, speaker_volume);
    set_ctl_elem_value(hctl, "Headphone Playback Volume", dual_integer, headphone_volume);
    set_ctl_elem_value(hctl, "Playback Volume", dual_integer, playback_volume);
    set_ctl_elem_value(hctl, "Mono Output Mixer Left Switch", single_bool, jack);
    set_ctl_elem_value(hctl, "Mono Output Mixer Right Switch", single_bool, jack);

    // Input volumes
    set_ctl_elem_value(hctl, "Left Boost Mixer LINPUT1 Switch", single_bool, conf->microphone_enabled);
    set_ctl_elem_value(hctl, "Right Boost Mixer RINPUT1 Switch", single_bool, conf->microphone_enabled);
    set_ctl_elem_value(hctl, "Capture Switch", dual_bool, conf->microphone_enabled);
    set_ctl_elem_value(hctl, "Left Input Boost Mixer LINPUT1 Volume", single_integer, conf->input1_base);
    set_ctl_elem_value(hctl, "Right Input Boost Mixer RINPUT1 Volume", single_integer, conf->input1_base);
    set_ctl_elem_value(hctl, "Capture Volume", dual_integer, conf->capture_base);
}

static int reload_config = 0;

void handle_signal(int signal) {
    reload_config = 1;
}

int install_signal_handler() {
    struct sigaction sa;

    sa.sa_handler = &handle_signal;
    sa.sa_flags = SA_RESTART;
    sigfillset(&sa.sa_mask);

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        syslog(LOG_DEBUG | LOG_DAEMON, "Cannot install USR1 handler");
        return -1;
    }
    
    return 0;
}

#ifndef TEST

int main(int argc, char** argv) {
    snd_hctl_t *hctl;
    struct config priv;
    int err;
    int jack;
    int buttons[2];
    sigset_t sigmask;
    sigset_t zeromask;
    int daemon = 0;
    struct pollfd *fds = (struct pollfd*) malloc(0);

    if (argc > 2 || (argc == 2 && strcmp(argv[1], "-d"))) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return 1;
    }
    
    sigemptyset(&sigmask);
    sigemptyset(&zeromask);
    sigaddset(&sigmask, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

    default_values(&priv);
    load_config(daemon, &priv, CONFIG_FILE_PATH);
    
    err = setup_hctl(&hctl);
    if (err < 0)
        return 1;

    err = install_signal_handler();
    if (err < 0)
        return 1;

    setup_callbacks(hctl, &jack, buttons);
    
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        int pfile_fd;
        pid_t pid;
        daemon = 1;
        pfile_fd = open(PID_FILE_PATH, O_CREAT | O_WRONLY, 0644);
        if (pfile_fd < 0) {
            if (errno == EACCES) {
                fprintf(stderr, "Cannot open pid file (not running as root?)\n");
                return 1;
            } else {
                fprintf(stderr, "Cannot open pid file\n");
                return 1;
            }
        }
        
        err = flock(pfile_fd, LOCK_EX | LOCK_NB);
        if (err < 0) {
            if (errno == EWOULDBLOCK) {
                fprintf(stderr, "Pid file locked, daemon is already running?\n");
                return 1;
            } else {
                fprintf(stderr, "Cannot lock pid file (errno=%i)\n", errno);
                return 1;
            }
        }
        pid = fork();
        if (pid != 0) {
            char pid_str[512];
            ssize_t written;
            snprintf(pid_str, sizeof(pid_str), "%i", pid);
            written = write(pfile_fd, pid_str, strlen(pid_str));
            if (written != strlen(pid_str)) {
                syslog(LOG_DEBUG | LOG_DAEMON, "Error writing pid file");
                return 1;
            }
            return 0;
        }
    }
    while (1) {
        unsigned num_fds;
        unsigned short revents;
        int npolldescs;
        
        set_volumes(daemon, hctl, &priv, jack, buttons);
        
        npolldescs = snd_hctl_poll_descriptors_count(hctl);
        num_fds = (unsigned) npolldescs;
        fds = (struct pollfd*) realloc(fds, num_fds * sizeof(struct pollfd));
        memset(fds, 0, num_fds * sizeof(struct pollfd));
        err = snd_hctl_poll_descriptors(hctl, fds, num_fds);
        if (err < 0) {
            do_log(daemon, 1, "Unable to get poll descriptors\n");
            return 1;
        }
        err = ppoll(fds, num_fds, NULL, &zeromask);
        if (err < 0) {
            if (errno == EINTR) {
                if (reload_config) {
                    reload_config = 0;
                    do_log(daemon, priv.debug, "Got USR1, reloading configuration");
                    load_config(daemon, &priv, CONFIG_FILE_PATH);
                }
            } else {
                do_log(daemon, 1, "Error with poll (%i)\n", errno);
                return 1;
            }
        }

        err = snd_hctl_poll_descriptors_revents(hctl, fds, num_fds, &revents);
        if (err < 0) {
            do_log(daemon, 1, "Unable to get poll revents\n");
            return 1;
        }

        if (revents) {
            snd_hctl_handle_events(hctl);
        }
    }

    teardown_hctl(hctl);

    return 0;
}

#else

int main(int argc, char** argv) {
    struct config default_conf;
    struct config loaded_conf;
    default_values(&default_conf);
    bzero(&loaded_conf, sizeof(loaded_conf));
    load_config(0, &loaded_conf, "mixer.conf.default");
    assert(default_conf.tag_speaker_high == DEFAULT_TAG_SPEAKER_HIGH);
    assert(loaded_conf.tag_speaker_high == DEFAULT_TAG_SPEAKER_HIGH);
    assert(default_conf.tag_speaker_low == DEFAULT_TAG_SPEAKER_LOW);
    assert(loaded_conf.tag_speaker_low == DEFAULT_TAG_SPEAKER_LOW);
    assert(default_conf.tagtag_speaker_high == DEFAULT_TAGTAG_SPEAKER_HIGH);
    assert(loaded_conf.tagtag_speaker_high == DEFAULT_TAGTAG_SPEAKER_HIGH);
    assert(default_conf.tagtag_speaker_low == DEFAULT_TAGTAG_SPEAKER_LOW);
    assert(loaded_conf.tagtag_speaker_low == DEFAULT_TAGTAG_SPEAKER_LOW);
    assert(default_conf.speaker_base == DEFAULT_SPEAKER_BASE);
    assert(loaded_conf.speaker_base == DEFAULT_SPEAKER_BASE);
    assert(default_conf.headphone_high == DEFAULT_HEADPHONE_HIGH);
    assert(loaded_conf.headphone_high == DEFAULT_HEADPHONE_HIGH);
    assert(default_conf.headphone_low == DEFAULT_HEADPHONE_LOW);
    assert(loaded_conf.headphone_low == DEFAULT_HEADPHONE_LOW);
    assert(default_conf.lineout_mode == DEFAULT_LINEOUT_MODE);
    assert(loaded_conf.lineout_mode == DEFAULT_LINEOUT_MODE);
    assert(default_conf.microphone_enabled == DEFAULT_MICROPHONE_ENABLED);
    assert(loaded_conf.microphone_enabled == DEFAULT_MICROPHONE_ENABLED);
    assert(default_conf.input1_base == DEFAULT_INPUT1_BASE);
    assert(loaded_conf.input1_base == DEFAULT_INPUT1_BASE);
    assert(default_conf.capture_base == DEFAULT_CAPTURE_BASE);
    assert(loaded_conf.capture_base == DEFAULT_CAPTURE_BASE);

    return 0;
}
#endif
