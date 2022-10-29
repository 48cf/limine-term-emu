#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <backends/framebuffer.h>

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

#define DEFAULT_COLS 120
#define DEFAULT_ROWS 35

#define WINDOW_WIDTH (DEFAULT_COLS * (FONT_WIDTH + 1))
#define WINDOW_HEIGHT (DEFAULT_ROWS * FONT_HEIGHT)

static void free_with_size(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

static int pty_master, pty_slave;

static void handle_key(struct term_context *ctx, SDL_KeyboardEvent *ev) {
    (void)ctx;
    (void)ev;

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
        NOMODS(SDLK_RETURN, "\n")

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
        case SDLK_DELETE: write(pty_master, "\x1b[3~", 4); break;
        case SDLK_SPACE: write(pty_master, " ", 1); break;
    }
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
        dup2(pty_slave, 0);
        dup2(pty_slave, 1);
        dup2(pty_slave, 2);
        execlp("bash", "bash", "-l", NULL);
    }

    fcntl(pty_master, F_SETFL, fcntl(pty_master, F_GETFL, 0) | O_NONBLOCK);
    fcntl(pty_slave, F_SETFL, fcntl(pty_slave, F_GETFL, 0) | O_NONBLOCK);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        printf("SDL could not be initialized: %s\n", SDL_GetError());
        return 1;
    }

    if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
        printf("SDL could not disable compositor bypass!\n");
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Limine Terminal",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
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

    struct term_context *ctx = fbterm_init(
        malloc,
        framebuffer, WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_WIDTH * 4,
        NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 1, 1, 0
    );

    for (int running = 1; running;) {
        SDL_Event ev;

        if (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = 0;
                    break;
                case SDL_KEYDOWN:
                    handle_key(ctx, &ev.key);
                    break;
            }
        }

        struct pollfd fds[2] = {
            { .fd = pty_master, .events = POLLIN, .revents = 0 },
            { .fd = pty_slave, .events = POLLOUT, .revents = 0 },
        };

        poll(fds, 2, 0);

        char buffer[512];

        if (fds[1].revents & POLLOUT) {
            int read_bytes;

            while ((read_bytes = read(pty_master, buffer, 512)) > 0) {
                printf("Writing %d bytes to terminal\n", read_bytes);
                term_write(ctx, buffer, read_bytes);
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
