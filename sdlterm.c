#if defined(__APPLE__)
#  include <SDL2/SDL.h>
#  define _XOPEN_SOURCE 600
#else
#  define _XOPEN_SOURCE 600
#  include <SDL2/SDL.h>
#endif

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <config.h>
#include <terminal/backends/framebuffer.h>

#define LOGO_PATH "/assets/logo.bmp"
#define INSTALLED_LOGO_PATH PACKAGE_DATADIR LOGO_PATH
#define LOCAL_LOGO_PATH SOURCE_ROOT LOGO_PATH

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

#define DEFAULT_COLS 120
#define DEFAULT_ROWS 35

#define WINDOW_WIDTH (DEFAULT_COLS * (FONT_WIDTH + 1))
#define WINDOW_HEIGHT (DEFAULT_ROWS * FONT_HEIGHT)

#define MIN(A, B) ({ \
    __auto_type MIN_a = A; \
    __auto_type MIN_b = B; \
    MIN_a < MIN_b ? MIN_a : MIN_b; \
})

static bool is_running = true;
static struct term_context *ctx;
static int pty_master;
static Uint64 bell_start = 0;

static void free_with_size(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

static void terminal_callback(struct term_context *ctx, uint64_t type, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    (void)ctx;
    (void)arg1;
    (void)arg2;
    (void)arg3;

    switch (type) {
        case TERM_CB_DEC:
            printf("TERM_CB_DEC");
            goto values;
        case TERM_CB_MODE:
            printf("TERM_CB_MODE");
            goto values;
        case TERM_CB_LINUX:
            printf("TERM_CB_LINUX");
            values:
                printf("(count=%lu, values={", arg1);
                for (uint64_t i = 0; i < arg1; i++) {
                    printf(i == 0 ? "%u" : ", %u", ((uint32_t*)arg2)[i]);
                }
                printf("}, final='%c')\n", (int)arg3);
                break;
        case TERM_CB_BELL:
            printf("TERM_CB_BELL()\n");
            bell_start = SDL_GetTicks64();
            break;
        case TERM_CB_PRIVATE_ID: printf("TERM_CB_PRIVATE_ID()\n"); break;
        case TERM_CB_STATUS_REPORT: printf("TERM_CB_STATUS_REPORT()\n"); break;
        case TERM_CB_POS_REPORT: printf("TERM_CB_POS_REPORT(x=%lu, y=%lu)\n", arg1, arg2); break;
        case TERM_CB_KBD_LEDS:
            printf("TERM_CB_KBD_LEDS(state=");
            switch (arg1) {
                case 0: printf("CLEAR_ALL"); break;
                case 1: printf("SET_SCRLK"); break;
                case 2: printf("SET_NUMLK"); break;
                case 3: printf("SET_CAPSLK"); break;
            }
            printf(")\n");
            break;
        default:
            printf("Unknown callback type %lu: %lx, %lx, %lx\n", type, arg1, arg2, arg3);
            break;
    }
}

static void handle_key(SDL_KeyboardEvent *ev) {
#define MOD_SHIFT(key, regular, shift, caps, shift_caps) \
    case key: { \
        char ctrl_value = shift[0] - 0x40; \
        const char *buf = regular; size_t len = sizeof(regular) - 1; \
        if (ev->keysym.mod & KMOD_CAPS && ev->keysym.mod & KMOD_SHIFT) { \
            buf = shift_caps; len = sizeof(shift_caps) - 1; \
        } else if (ev->keysym.mod & KMOD_CAPS) { \
            buf = caps; len = sizeof(caps) - 1; \
        } else if (ev->keysym.mod & KMOD_SHIFT) { \
            buf = shift; len = sizeof(shift) - 1; \
        } else if (ev->keysym.mod & KMOD_CTRL && isalpha(shift[0])) { \
            buf = &ctrl_value; len = 1; \
        } \
        write(pty_master, buf, len); \
        break; \
    }

#define MOD_CTRL_ALT(key, regular, ctrl, alt) \
    case key: { \
        const char *buf = regular; size_t len = sizeof(regular) - 1; \
        if (ev->keysym.mod & KMOD_CTRL) { \
            buf = ctrl; len = sizeof(ctrl) - 1; \
        } else if (ev->keysym.mod & KMOD_ALT) { \
            buf = alt; len = sizeof(alt) - 1; \
        } \
        write(pty_master, buf, len); \
        break; \
    }

#define NO_MODS(key, output) \
    case key: \
        write(pty_master, output, sizeof(output) - 1); \
        break;

    switch (ev->keysym.sym) {
        MOD_SHIFT(SDLK_BACKQUOTE, "`", "~", "`", "~")
        MOD_SHIFT(SDLK_1, "1", "!", "1", "!")
        MOD_SHIFT(SDLK_2, "2", "@", "2", "@")
        MOD_SHIFT(SDLK_3, "3", "#", "3", "#")
        MOD_SHIFT(SDLK_4, "4", "$", "4", "$")
        MOD_SHIFT(SDLK_5, "5", "%", "5", "%")
        MOD_SHIFT(SDLK_6, "6", "^", "6", "^")
        MOD_SHIFT(SDLK_7, "7", "&", "7", "&")
        MOD_SHIFT(SDLK_8, "8", "*", "8", "*")
        MOD_SHIFT(SDLK_9, "9", "(", "9", "(")
        MOD_SHIFT(SDLK_0, "0", ")", "0", ")")
        MOD_SHIFT(SDLK_MINUS, "-", "_", "-", "_")
        MOD_SHIFT(SDLK_EQUALS, "=", "+", "=", "+")
        NO_MODS(SDLK_BACKSPACE, "\b")

        NO_MODS(SDLK_TAB, "\t")
        MOD_SHIFT(SDLK_q, "q", "Q", "Q", "q")
        MOD_SHIFT(SDLK_w, "w", "W", "W", "w")
        MOD_SHIFT(SDLK_e, "e", "E", "E", "e")
        MOD_SHIFT(SDLK_r, "r", "R", "R", "r")
        MOD_SHIFT(SDLK_t, "t", "T", "T", "t")
        MOD_SHIFT(SDLK_y, "y", "Y", "Y", "y")
        MOD_SHIFT(SDLK_u, "u", "U", "U", "u")
        MOD_SHIFT(SDLK_i, "i", "I", "I", "i")
        MOD_SHIFT(SDLK_o, "o", "O", "O", "o")
        MOD_SHIFT(SDLK_p, "p", "P", "P", "p")
        MOD_SHIFT(SDLK_LEFTBRACKET, "[", "{", "[", "{")
        MOD_SHIFT(SDLK_RIGHTBRACKET, "]", "}", "]", "}")
        MOD_SHIFT(SDLK_BACKSLASH, "\\", "|", "\\", "|")

        MOD_SHIFT(SDLK_a, "a", "A", "A", "a")
        MOD_SHIFT(SDLK_s, "s", "S", "S", "s")
        MOD_SHIFT(SDLK_d, "d", "D", "D", "d")
        MOD_SHIFT(SDLK_f, "f", "F", "F", "f")
        MOD_SHIFT(SDLK_g, "g", "G", "G", "g")
        MOD_SHIFT(SDLK_h, "h", "H", "H", "h")
        MOD_SHIFT(SDLK_j, "j", "J", "J", "j")
        MOD_SHIFT(SDLK_k, "k", "K", "K", "k")
        MOD_SHIFT(SDLK_l, "l", "L", "L", "l")
        MOD_SHIFT(SDLK_SEMICOLON, ";", ":", ";", ":")
        MOD_SHIFT(SDLK_QUOTE, "'", "\"", "'", "\"")
        NO_MODS(SDLK_RETURN, "\r")

        MOD_SHIFT(SDLK_z, "z", "Z", "Z", "z")
        MOD_SHIFT(SDLK_x, "x", "X", "X", "x")
        MOD_SHIFT(SDLK_c, "c", "C", "C", "c")
        MOD_SHIFT(SDLK_v, "v", "V", "V", "v")
        MOD_SHIFT(SDLK_b, "b", "B", "B", "b")
        MOD_SHIFT(SDLK_n, "n", "N", "N", "n")
        MOD_SHIFT(SDLK_m, "m", "M", "M", "m")
        MOD_SHIFT(SDLK_COMMA, ",", "<", ",", "<")
        MOD_SHIFT(SDLK_PERIOD, ".", ">", ".", ">")
        MOD_SHIFT(SDLK_SLASH, "/", "?", "/", "?")

        NO_MODS(SDLK_KP_DIVIDE, "/")
        NO_MODS(SDLK_KP_MULTIPLY, "*")
        NO_MODS(SDLK_KP_MINUS, "-")
        NO_MODS(SDLK_KP_PLUS, "+")
        NO_MODS(SDLK_SPACE, " ")
        NO_MODS(SDLK_ESCAPE, "\x1b")

#define UP_ESC "\x1bOA"
#define DOWN_ESC "\x1bOB"
#define RIGHT_ESC "\x1bOC"
#define LEFT_ESC "\x1bOD"
#define INSERT_ESC "\x1b[2~"
#define DELETE_ESC "\x1b[3~"
#define HOME_ESC "\x1b[H"
#define END_ESC "\x1b[F"
#define PAGEUP_ESC "\x1b[5~"
#define PAGEDOWN_ESC "\x1b[6~"

        NO_MODS(SDLK_UP, UP_ESC)
        NO_MODS(SDLK_DOWN, DOWN_ESC)
        NO_MODS(SDLK_RIGHT, RIGHT_ESC)
        NO_MODS(SDLK_LEFT, LEFT_ESC)
        NO_MODS(SDLK_INSERT, INSERT_ESC)
        NO_MODS(SDLK_DELETE, DELETE_ESC)
        NO_MODS(SDLK_HOME, HOME_ESC)
        NO_MODS(SDLK_END, END_ESC)
        NO_MODS(SDLK_PAGEUP, PAGEUP_ESC)
        NO_MODS(SDLK_PAGEDOWN, PAGEDOWN_ESC)

        NO_MODS(SDLK_KP_8, UP_ESC)
        NO_MODS(SDLK_KP_2, DOWN_ESC)
        NO_MODS(SDLK_KP_6, RIGHT_ESC)
        NO_MODS(SDLK_KP_4, LEFT_ESC)
        NO_MODS(SDLK_KP_0, INSERT_ESC)
        NO_MODS(SDLK_KP_PERIOD, DELETE_ESC)
        NO_MODS(SDLK_KP_7, HOME_ESC)
        NO_MODS(SDLK_KP_1, END_ESC)
        NO_MODS(SDLK_KP_9, PAGEUP_ESC)
        NO_MODS(SDLK_KP_3, PAGEDOWN_ESC)

        NO_MODS(SDLK_F1, "\x1bOP")
        NO_MODS(SDLK_F2, "\x1bOQ")
        NO_MODS(SDLK_F3, "\x1bOR")
        NO_MODS(SDLK_F4, "\x1bOS")
        NO_MODS(SDLK_F5, "\x1b[15~")
        NO_MODS(SDLK_F6, "\x1b[17~")
        NO_MODS(SDLK_F7, "\x1b[18~")
        NO_MODS(SDLK_F8, "\x1b[19~")
        NO_MODS(SDLK_F9, "\x1b[20~")
        NO_MODS(SDLK_F10, "\x1b[21~")
        NO_MODS(SDLK_F11, "\x1b[23~")
        NO_MODS(SDLK_F12, "\x1b[24~")
    }
}

static void *read_from_pty(void *arg) {
    (void)arg;

    int read_bytes;
    char buffer[512];

    while (is_running) {
        read_bytes = read(pty_master, buffer, sizeof(buffer));
        if (read_bytes < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                // retry
                continue;
            }

            // abort
            printf("read_from_pty: read() failed (errno=%d)\n", errno);
            break;
        }

        term_write(ctx, buffer, read_bytes);
    }

    is_running = false;
    return NULL;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    struct winsize win_size = {
        .ws_col = DEFAULT_COLS,
        .ws_row = DEFAULT_ROWS,
        .ws_xpixel = WINDOW_WIDTH,
        .ws_ypixel = WINDOW_HEIGHT,
    };

    pty_master = posix_openpt(O_RDWR);

    if (pty_master < 0) {
        printf("Could not open a PTY\n");
        return 1;
    }

    unlockpt(pty_master);
    grantpt(pty_master);

    int pid = fork();

    if (pid == 0) {
        int pty_slave = open(ptsname(pty_master), O_RDWR | O_NOCTTY);

        close(pty_master);
        ioctl(pty_slave, TIOCSCTTY, 0);
        ioctl(pty_slave, TIOCSWINSZ, &win_size);
        setsid();

        dup2(pty_slave, 0);
        dup2(pty_slave, 1);
        dup2(pty_slave, 2);
        close(pty_slave);

        execlp("/bin/bash", "/bin/bash", "-l", NULL);
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not be initialized: %s\n", SDL_GetError());
        return 1;
    }

    if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
        printf("SDL could not disable compositor bypass!\n");
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Limine Terminal",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_HIDDEN
    );

    if (!window) {
        printf("SDL could not create window: %s\n", SDL_GetError());
        return 1;
    }

    char *icon_path;

    if (access(INSTALLED_LOGO_PATH, F_OK) == 0) {
        icon_path = INSTALLED_LOGO_PATH;
    } else if (access(LOCAL_LOGO_PATH, F_OK) == 0) {
        icon_path = LOCAL_LOGO_PATH;
    } else {
        printf("sdl: window icon not found\n");
        return -1;
    }

    SDL_Surface *icon = SDL_LoadBMP(icon_path);

    if (!icon) {
        printf("sdl: failed to load window icon\n");
        return -1;
    }

    SDL_SetWindowIcon(window, icon);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        printf("SDL could not create renderer: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    void *framebuffer = malloc(WINDOW_WIDTH * WINDOW_HEIGHT * 4);

    if (!framebuffer) {
        printf("Could not allocate framebuffer\n");
        return 1;
    }

    memset(framebuffer, 0, WINDOW_WIDTH * WINDOW_HEIGHT * 4);

    SDL_Texture *framebuffer_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        WINDOW_WIDTH, WINDOW_HEIGHT
    );

    if (!framebuffer_texture) {
        printf("SDL could not create renderer: %s\n", SDL_GetError());
        return 1;
    }

    ctx = fbterm_init(
        malloc,
        framebuffer, WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_WIDTH * 4,
        NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 1, 1, 0
    );

    if (!ctx) {
        printf("Could not create terminal context\n");
        return 1;
    }

    ctx->callback = terminal_callback;

    pthread_t pty_thread;

    if (pthread_create(&pty_thread, NULL, read_from_pty, NULL) != 0) {
        printf("Could not create PTY reader thread\n");
        return 1;
    }

    SDL_ShowWindow(window);

    for (; is_running;) {
        SDL_Event ev;

        if (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    is_running = false;
                    break;
                case SDL_KEYDOWN:
                    handle_key(&ev.key);
                    break;
            }
        }

        SDL_UpdateTexture(framebuffer_texture, NULL, framebuffer, WINDOW_WIDTH * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, framebuffer_texture, NULL, NULL);

        Uint64 bell_length = 180;
        if (bell_start > 0 && SDL_GetTicks64() < bell_start + bell_length) {
            Uint64 elapsed = MIN(bell_length, SDL_GetTicks64() - bell_start);
            Uint64 remaining = bell_length - elapsed;
            Uint64 alpha = ((remaining * 1000) / bell_length) / 10;

            SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
            SDL_RenderFillRect(renderer, NULL);
        }

        SDL_RenderPresent(renderer);
    }

    close(pty_master);

    kill(pid, SIGTERM);
    kill(pid, SIGKILL);

    SDL_FreeSurface(icon);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    ctx->deinit(ctx, free_with_size);
}
