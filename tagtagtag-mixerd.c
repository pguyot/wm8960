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

#ifdef TEST
#define DEBUG
#include <assert.h>
#include <strings.h>
#endif

#define CONFIG_FILE_PATH "/var/lib/tagtagtag-sound/mixer.conf"
#define DEFAULT_TAG_SPEAKER_BASE        121
#define DEFAULT_TAGTAG_SPEAKER_BASE     121
#define DEFAULT_SPEAKER_HIGH            249
#define DEFAULT_SPEAKER_LOW             227
#define DEFAULT_HEADPHONE               227
#define PID_FILE_PATH "/run/tagtagtag-mixerd.pid"

struct config {
    int debug;
    int tag_speaker_base;
    int tagtag_speaker_base;
    int speaker_high;
    int speaker_low;
    int headphone;
};

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
    conf->debug = 0;
    conf->tag_speaker_base = DEFAULT_TAG_SPEAKER_BASE;
    conf->tagtag_speaker_base = DEFAULT_TAGTAG_SPEAKER_BASE;
    conf->speaker_high = DEFAULT_SPEAKER_HIGH;
    conf->speaker_low = DEFAULT_SPEAKER_LOW;
    conf->headphone = DEFAULT_HEADPHONE;
}

static void load_config(struct config* conf, const char* config_file_path) {
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
                if (conf->debug) {
                    syslog(LOG_DEBUG | LOG_DAEMON, "Syntax error in configuration file %s line %i", config_file_path, line_ix);
                }
                valid = 0;
                break;
            }
            value++;
            if (key_len == strlen("debug") && strncmp(line, "debug", key_len) == 0) {
                new_config.debug = strcmp(value, "true") == 0;
            } else if (key_len == strlen("tag-speaker-base") && strncmp(line, "tag-speaker-base", key_len) == 0) {
                new_config.tag_speaker_base = atoi(value);
            } else if (key_len == strlen("tagtag-speaker-base") && strncmp(line, "tagtag-speaker-base", key_len) == 0) {
                new_config.tagtag_speaker_base = atoi(value);
            } else if (key_len == strlen("speaker-high") && strncmp(line, "speaker-high", key_len) == 0) {
                new_config.speaker_high = atoi(value);
            } else if (key_len == strlen("speaker-low") && strncmp(line, "speaker-low", key_len) == 0) {
                new_config.speaker_low = atoi(value);
            } else if (key_len == strlen("headphone") && strncmp(line, "headphone", key_len) == 0) {
                new_config.headphone = atoi(value);
            }
        }
        fclose(config_file);
    } else if (conf->debug) {
        syslog(LOG_DEBUG | LOG_DAEMON, "Could not open configuration file %s (errno=%i)", config_file_path, errno);
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

void set_volumes(snd_hctl_t *hctl, struct config* conf, int jack, int buttons[2]) {
    snd_ctl_elem_id_t *id;
    snd_hctl_elem_t *elem;
    snd_ctl_elem_value_t *elem_value;
    int speaker_volume;
    int headphone_volume;
    int playback_volume;

    snd_ctl_elem_value_alloca(&elem_value);
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    
    // Workaround bug in hardware.
    // Volume 1 (27)    Volume 2 (22)        Tag            TagTag
    //    0                0                 mute           medium volume
    //    0                1                 max volume     max volume
    //    1                0                 medium volume  mute
    //    1                1                        impossible

    if (jack) {
        speaker_volume = 0;
        headphone_volume = 0;
        playback_volume = conf->headphone;
    } else if (buttons[0] == 0 && buttons[1] == 0) {
        speaker_volume = 0;
        headphone_volume = conf->tagtag_speaker_base;
        playback_volume = conf->speaker_low;
    } else if (buttons[0] == 0 && buttons[1] == 1) {
        speaker_volume = conf->tag_speaker_base;
        headphone_volume = conf->tagtag_speaker_base;
        playback_volume = conf->speaker_high;
    } else if (buttons[0] == 1 && buttons[1] == 0) {
        speaker_volume = conf->tag_speaker_base;
        headphone_volume = 0;
        playback_volume = conf->speaker_low;
    } else if (conf->debug) {
        syslog(LOG_DEBUG | LOG_DAEMON, "Unexpected button values (both are high)");
        return;
    }

    snd_ctl_elem_id_set_name(id, "Speaker Playback Volume");
    elem = snd_hctl_find_elem(hctl, id);
    snd_ctl_elem_value_set_integer(elem_value, 0, speaker_volume);
    snd_ctl_elem_value_set_integer(elem_value, 1, speaker_volume);
    snd_hctl_elem_write(elem, elem_value);

    snd_ctl_elem_id_set_name(id, "Headphone Playback Volume");
    elem = snd_hctl_find_elem(hctl, id);
    snd_ctl_elem_value_set_integer(elem_value, 0, headphone_volume);
    snd_ctl_elem_value_set_integer(elem_value, 1, headphone_volume);
    snd_hctl_elem_write(elem, elem_value);
    
    snd_ctl_elem_id_set_name(id, "Playback Volume");
    elem = snd_hctl_find_elem(hctl, id);
    snd_ctl_elem_value_set_integer(elem_value, 0, playback_volume);
    snd_ctl_elem_value_set_integer(elem_value, 1, playback_volume);
    snd_hctl_elem_write(elem, elem_value);

    snd_ctl_elem_id_set_name(id, "Mono Output Mixer Left Switch");
    elem = snd_hctl_find_elem(hctl, id);
    snd_ctl_elem_value_set_boolean(elem_value, 0, jack);
    snd_hctl_elem_write(elem, elem_value);

    snd_ctl_elem_id_set_name(id, "Mono Output Mixer Right Switch");
    elem = snd_hctl_find_elem(hctl, id);
    snd_ctl_elem_value_set_boolean(elem_value, 0, jack);
    snd_hctl_elem_write(elem, elem_value);
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
    load_config(&priv, CONFIG_FILE_PATH);
    
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
        
        set_volumes(hctl, &priv, jack, buttons);
        
        npolldescs = snd_hctl_poll_descriptors_count(hctl);
        num_fds = (unsigned) npolldescs;
        fds = (struct pollfd*) realloc(fds, num_fds * sizeof(struct pollfd));
        memset(fds, 0, num_fds * sizeof(struct pollfd));
        err = snd_hctl_poll_descriptors(hctl, fds, num_fds);
        if (err < 0) {
            syslog(LOG_DEBUG | LOG_DAEMON, "Unable to get poll descriptors\n");
            return 1;
        }
        err = ppoll(fds, num_fds, NULL, &zeromask);
        if (err < 0) {
            if (errno == EINTR) {
                if (reload_config) {
                    reload_config = 0;
                    if (priv.debug) {
                        syslog(LOG_DEBUG | LOG_DAEMON, "Got USR1, reloading configuration");
                    }
                    load_config(&priv, CONFIG_FILE_PATH);
                }
            } else {
                syslog(LOG_DEBUG | LOG_DAEMON, "Error with poll (%i)\n", errno);
                return 1;
            }
        }

        err = snd_hctl_poll_descriptors_revents(hctl, fds, num_fds, &revents);
        if (err < 0) {
            syslog(LOG_DEBUG | LOG_DAEMON, "Unable to get poll revents\n");
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
    load_config(&loaded_conf, "mixer.conf.default");
    assert(default_conf.tag_speaker_base == DEFAULT_TAG_SPEAKER_BASE);
    assert(loaded_conf.tag_speaker_base == DEFAULT_TAG_SPEAKER_BASE);
    assert(default_conf.tagtag_speaker_base == DEFAULT_TAGTAG_SPEAKER_BASE);
    assert(loaded_conf.tagtag_speaker_base == DEFAULT_TAGTAG_SPEAKER_BASE);
    assert(default_conf.speaker_high == DEFAULT_SPEAKER_HIGH);
    assert(loaded_conf.speaker_high == DEFAULT_SPEAKER_HIGH);
    assert(default_conf.speaker_low == DEFAULT_SPEAKER_LOW);
    assert(loaded_conf.speaker_low == DEFAULT_SPEAKER_LOW);
    assert(default_conf.headphone == DEFAULT_HEADPHONE);
    assert(loaded_conf.headphone == DEFAULT_HEADPHONE);

    return 0;
}
#endif