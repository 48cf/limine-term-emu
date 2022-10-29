#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(__linux__)
#  include <pty.h>
#elif defined(__APPLE__)
#  include <util.h>
#else
#  error "Unsupported platform"
#endif

#include <SDL2/SDL.h>
#include <backends/framebuffer.h>

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

#define DEFAULT_COLS 120
#define DEFAULT_ROWS 35

#define WINDOW_WIDTH (DEFAULT_COLS * (FONT_WIDTH + 1))
#define WINDOW_HEIGHT (DEFAULT_ROWS * FONT_HEIGHT)

static bool is_running = true;
static struct term_context *ctx;
static int pty_master, pty_slave;

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
        case TERM_CB_BELL: printf("TERM_CB_BELL()\n"); break;
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
#define MODS(key, regular, shift, caps, shift_caps) \
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

#define NOMODS(key, output) \
    case key: \
        write(pty_master, output, sizeof(output) - 1); \
        break;

    switch (ev->keysym.sym) {
        MODS(SDLK_BACKQUOTE, "`", "~", "`", "~")
        MODS(SDLK_1, "1", "!", "1", "!")
        MODS(SDLK_2, "2", "@", "2", "@")
        MODS(SDLK_3, "3", "#", "3", "#")
        MODS(SDLK_4, "4", "$", "4", "$")
        MODS(SDLK_5, "5", "%", "5", "%")
        MODS(SDLK_6, "6", "^", "6", "^")
        MODS(SDLK_7, "7", "&", "7", "&")
        MODS(SDLK_8, "8", "*", "8", "*")
        MODS(SDLK_9, "9", "(", "9", "(")
        MODS(SDLK_0, "0", ")", "0", ")")
        MODS(SDLK_MINUS, "-", "_", "-", "_")
        MODS(SDLK_EQUALS, "=", "+", "=", "+")
        NOMODS(SDLK_BACKSPACE, "\b")

        NOMODS(SDLK_TAB, "\t")
        MODS(SDLK_q, "q", "Q", "Q", "q")
        MODS(SDLK_w, "w", "W", "W", "w")
        MODS(SDLK_e, "e", "E", "E", "e")
        MODS(SDLK_r, "r", "R", "R", "r")
        MODS(SDLK_t, "t", "T", "T", "t")
        MODS(SDLK_y, "y", "Y", "Y", "y")
        MODS(SDLK_u, "u", "U", "U", "u")
        MODS(SDLK_i, "i", "I", "I", "i")
        MODS(SDLK_o, "o", "O", "O", "o")
        MODS(SDLK_p, "p", "P", "P", "p")
        MODS(SDLK_LEFTBRACKET, "[", "{", "[", "{")
        MODS(SDLK_RIGHTBRACKET, "]", "}", "]", "}")
        MODS(SDLK_BACKSLASH, "\\", "|", "\\", "|")

        MODS(SDLK_a, "a", "A", "A", "a")
        MODS(SDLK_s, "s", "S", "S", "s")
        MODS(SDLK_d, "d", "D", "D", "d")
        MODS(SDLK_f, "f", "F", "F", "f")
        MODS(SDLK_g, "g", "G", "G", "g")
        MODS(SDLK_h, "h", "H", "H", "h")
        MODS(SDLK_j, "j", "J", "J", "j")
        MODS(SDLK_k, "k", "K", "K", "k")
        MODS(SDLK_l, "l", "L", "L", "l")
        MODS(SDLK_SEMICOLON, ";", ":", ";", ":")
        MODS(SDLK_QUOTE, "'", "\"", "'", "\"")
        NOMODS(SDLK_RETURN, "\r")

        MODS(SDLK_z, "z", "Z", "Z", "z")
        MODS(SDLK_x, "x", "X", "X", "x")
        MODS(SDLK_c, "c", "C", "C", "c")
        MODS(SDLK_v, "v", "V", "V", "v")
        MODS(SDLK_b, "b", "B", "B", "b")
        MODS(SDLK_n, "n", "N", "N", "n")
        MODS(SDLK_m, "m", "M", "M", "m")
        MODS(SDLK_COMMA, ",", "<", ",", "<")
        MODS(SDLK_PERIOD, ".", ">", ".", ">")
        MODS(SDLK_SLASH, "/", "?", "/", "?")

        case SDLK_UP: write(pty_master, "\x1b[A", 3); break;
        case SDLK_LEFT: write(pty_master, "\x1b[D", 3); break;
        case SDLK_DOWN: write(pty_master, "\x1b[B", 3); break;
        case SDLK_RIGHT: write(pty_master, "\x1b[C", 3); break;
        case SDLK_HOME: write(pty_master, "\x1b[1~", 4); break;
        case SDLK_END: write(pty_master, "\x1b[4~", 4); break;
        case SDLK_PAGEUP: write(pty_master, "\x1b[5~", 4); break;
        case SDLK_PAGEDOWN: write(pty_master, "\x1b[6~", 4); break;
        case SDLK_INSERT: write(pty_master, "\x1b[2~", 4); break;
        case SDLK_DELETE: write(pty_master, "\x1b[3~", 4); break;
        case SDLK_SPACE: write(pty_master, " ", 1); break;
    }
}

static void *read_from_pty(void *arg) {
    (void)arg;

    int read_bytes;
    char buffer[512];

    while (is_running && (read_bytes = read(pty_master, buffer, 512)) > 0) {
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

    if (openpty(&pty_master, &pty_slave, NULL, NULL, &win_size) < 0) {
        printf("Could not create a PTY\n");
        return 1;
    }

    int pid = fork();

    if (pid == 0) {
        close(pty_master);
        setsid();
        ioctl(pty_slave, TIOCSCTTY, 0);

        dup2(pty_slave, 0);
        dup2(pty_slave, 1);
        dup2(pty_slave, 2);
        close(pty_slave);

        execlp("/bin/bash", "/bin/bash", "-l", NULL);
    }

    close(pty_slave);

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

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        printf("SDL could not create renderer: %s\n", SDL_GetError());
        return 1;
    }

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
        SDL_RenderPresent(renderer);
    }

    close(pty_slave);
    close(pty_master);

    kill(pid, SIGTERM);
    kill(pid, SIGKILL);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    ctx->deinit(ctx, free_with_size);
}
