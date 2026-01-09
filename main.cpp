#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <filesystem>
#include <sys/wait.h>
#include <spawn.h>

using namespace std;

#define WIDTH 240
#define HEIGHT 320

char path[512] = "/";
char jar_path[512] = "";

vector<string> filenames;
int sel = 0;

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
TTF_Font* font;
bool cursor;
bool was_drm = false;
void deinit_sdl() {
    TTF_CloseFont(font);
    TTF_Quit();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_ShowCursor(cursor);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();
}
void init_sdl() {
    if(was_drm) {
        setenv("SDL_VIDEODRIVER", "kmsdrm", 1);
        unsetenv("DISPLAY");
        unsetenv("WAYLAND_DISPLAY");
        unsetenv("WAYLAND_SOCKET");
    }
    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer(WIDTH, HEIGHT, SDL_WINDOW_FULLSCREEN_DESKTOP, &window, &renderer);
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);

    TTF_Init();
    font = TTF_OpenFont("font.ttf", 18);
    cursor = SDL_ShowCursor(0) == 1;
}

void update_filenames() {
    filenames.erase(filenames.begin(), filenames.end());

    int c = 0;
    for(int i = 0; i < strlen(path); i++)
        if(path[i] == '/') c++;
    if(c > 1)
        filenames.push_back("..");
    
    for(const auto& entry : filesystem::directory_iterator(path)) {
        string s = entry.path().filename().string();
        if(!entry.is_directory() && (s.size() < 4 || s.substr(s.size() - 4) != ".jar"))
            continue;
        filenames.push_back(s);
    }
}
void append_slash(char* str) {
    size_t strsize = strlen(str);
    if(str[strsize - 1] != '/')
        strcat(str, "/");
}
void open_file_or_dir() {
    string& fname = filenames[sel];
    if(fname == "..") {
        std::filesystem::path path_obj = path;
        // C++ bug: can't properly get parent dir if trailing slash is present
        strcpy(path, path_obj.parent_path().parent_path().c_str());
        append_slash(path);
        update_filenames();
        sel = 0;
    } else {
        char full_path[512];
        strcpy(full_path, path);
        strcat(full_path, fname.c_str());
        
        if(filesystem::is_directory(full_path)) {
            strcpy(path, full_path);
            append_slash(path);
            update_filenames();
            sel = 0;
        } else {
            char fpath[512] = "file://";
            strcat(fpath, path);
            strcat(fpath, fname.c_str());
            const char* args[] = { "/usr/bin/java", "-jar", jar_path, fpath, "240", "320", NULL };

            deinit_sdl();

            pid_t pid;
            posix_spawn(&pid, "/usr/bin/java", NULL, NULL, (char**)args, environ);
            waitpid(pid, NULL, 0);

            SDL_Delay(500);
            init_sdl();
        }
    }
}

// https://stackoverflow.com/a/38169008
void get_text_and_rect(SDL_Renderer* renderer, int x, int y, const char* text, TTF_Font* font, SDL_Texture** texture, SDL_Rect* rect) {
    int w, h;
    SDL_Surface* surface;
    SDL_Color color = { 0xff, 0xff, 0xff, 0xff };
    surface = TTF_RenderText_Solid(font, text, color);
    *texture = SDL_CreateTextureFromSurface(renderer, surface);
    w = surface->w; h = surface->h;
    SDL_FreeSurface(surface);
    rect->x = x;
    rect->y = y;
    rect->w = w;
    rect->h = h;
}

int main(int argc, char** argv) {
    was_drm = !strcmp(getenv("SDL_VIDEODRIVER"), "kmsdrm");
    if(was_drm) printf("drm detected\n");

    if(argc > 2) {
        strcpy(path, argv[2]);
        strcpy(jar_path, argv[1]);
    } else if(argc > 1) {
        strcpy(jar_path, argv[1]);
    } else {
        printf("usage: launcher <jar_path> <initial_path>\n");
        return 1;
    }

    append_slash(path);

    init_sdl();
    update_filenames();

    bool quit = false;
    SDL_Event e;
    while(!quit) {
        // Render
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 0x1a, 0x1b, 0x24, 0xff);
        SDL_RenderFillRect(renderer, NULL);

        SDL_Texture* texture;
        SDL_Rect rect;
        for(int i = max(sel - 4, 0); i <= min(sel + 4, (int)(filenames.size() - 1)); i++) {
            int h = i - sel + 4;
            if(i == sel) {
                SDL_Rect sel_rect = { .x = 0, .y = 31 * h, .w = WIDTH, .h = 31 };
                SDL_SetRenderDrawColor(renderer, 0xcf, 0xa3, 0x15, 0xff);
                SDL_RenderFillRect(renderer, &sel_rect);
            }
            get_text_and_rect(renderer, 4, 31 * h + 4, filenames[i].c_str(), font, &texture, &rect);
            rect.w = min(rect.w, WIDTH - 8);
            SDL_RenderCopy(renderer, texture, NULL, &rect);
        }

        get_text_and_rect(renderer, 4, HEIGHT - 31 + 4, "J2ME Launcher", font, &texture, &rect);
        SDL_RenderCopy(renderer, texture, NULL, &rect);

        SDL_SetRenderDrawColor(renderer, 0x1a, 0x1b, 0x24, 0xff);

        SDL_RenderPresent(renderer);

        // Keys
        while(SDL_PollEvent(&e) != 0)
            if(e.type == SDL_QUIT)
                quit = 1;
            else if(e.type == SDL_KEYDOWN)
                if(e.key.keysym.sym == SDLK_UP) {
                    sel -= 1;
                    if(sel < 0) sel = filenames.size() - 1;
                } else if(e.key.keysym.sym == SDLK_DOWN) {
                    sel += 1;
                    if(sel >= filenames.size()) sel = 0;
                } else if(e.key.keysym.sym == SDLK_RETURN) {
                    open_file_or_dir();
                } else if(e.key.keysym.sym == SDLK_ESCAPE)
                    quit = 1;
    }

    deinit_sdl();

    return 0;
}