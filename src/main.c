#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BOARD_SIZE 100
#define MAX_PLAYERS 6
static const char *ASSET_PATH = "assets/";

struct Jump { int start, end; };
static struct Jump snakes[] = {
    {23,3}, {30,10}, {39,20}, {47,26}, {56,36}, {78,24}, {71,9}, {86,66}, {98,79}
};
static struct Jump ladders[] = {
    {13,27}, {16,67}, {33,49}, {42,63}, {62,80}, {53,87}, {72,90}, {85,95}
};

typedef struct {
    char name[32];
    int pos;
    SDL_Color tokenColor;
    SDL_Texture* iconTex;
} Player;

/* --- helpers --- */
static SDL_Texture* loadTexture(SDL_Renderer* r, const char* file) {
    char path[512]; snprintf(path, sizeof(path), "%s%s", ASSET_PATH, file);
    SDL_Texture* t = IMG_LoadTexture(r, path);
    if (!t) fprintf(stderr, "IMG load fail %s : %s\n", path, IMG_GetError());
    return t;
}
static void playSound(const char *file) {
    char path[512]; snprintf(path, sizeof(path), "%s%s", ASSET_PATH, file);
    Mix_Chunk *c = Mix_LoadWAV(path);
    if (!c) { printf("SND load fail %s : %s\n", path, Mix_GetError()); return; }
    Mix_PlayChannelTimed(-1, c, 0, 1000);  // play up to 1 second, NEVER cut instantly
}
static void drawTriangle(SDL_Renderer *r,
                               int x1,int y1, int x2,int y2, int x3,int y3,
                               SDL_Color c)
{
    SDL_Vertex verts[3] = {
        { { (float)x1, (float)y1 }, c, {0,0} },
        { { (float)x2, (float)y2 }, c, {0,0} },
        { { (float)x3, (float)y3 }, c, {0,0} }
    };

    SDL_RenderGeometry(r, NULL, verts, 3, NULL, 0);
}

/* map board position [1..100] to pixel (center of cell) inside boardRect */
static void posToXY(int pos, const SDL_Rect *boardRect, int *outX, int *outY) {
    int cell = boardRect->w / 10;                 // square board, w==h
    int idx = pos - 1;
    int row = idx / 10;
    int col = idx % 10;
    if (row % 2 == 1) col = 9 - col;
    int x = boardRect->x + col * cell + cell/2;
    int y = boardRect->y + (9 - row) * cell + cell/2;
    *outX = x; *outY = y;
}

int main(void) {
    srand((unsigned)time(NULL));

    /* init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    int want_img = IMG_INIT_PNG;
    int got_img = IMG_Init(want_img);
    if ((got_img & want_img) != want_img) {
        fprintf(stderr, "IMG_Init PNG failed: %s\n", IMG_GetError());
        // continue so you can still see window
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
        fprintf(stderr, "Mix_OpenAudio: %s\n", Mix_GetError());
    Mix_Volume(-1, MIX_MAX_VOLUME);

    /* window/renderer (resizable, default 600x600) */
    const int DEF = 600;
    SDL_Window *win = SDL_CreateWindow("Snakes & Ladders",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DEF, DEF,
        SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); return 1; }

    /* assets (PNG!) */
    SDL_Texture *boardTex = loadTexture(ren, "board.png");
    SDL_Texture *playerIconTex = loadTexture(ren, "player_icon.png");
    SDL_Texture *diceTex[7] = {0};
    for (int i=1;i<=6;i++){ char n[32]; snprintf(n,sizeof(n),"dice%d.png",i); diceTex[i]=loadTexture(ren,n); }

    /* players */
    int numPlayers = 0;
    printf("Enter number of players (2-%d): ", MAX_PLAYERS);
    if (scanf("%d",&numPlayers)!=1) return 0;
    if (numPlayers<2) numPlayers=2;
    if (numPlayers>MAX_PLAYERS) numPlayers=MAX_PLAYERS; getchar();

    SDL_Color baseColors[6] = {
        {255, 0, 0, 255}, {0, 96, 255, 255}, {0, 200, 0, 255},
        {255, 200, 0, 255}, {220, 0, 220, 255}, {255, 128, 0, 255}
    };

    Player players[MAX_PLAYERS];
    for (int i=0;i<numPlayers;i++){
        printf("Enter name for player %d: ", i+1);
        fgets(players[i].name, sizeof(players[i].name), stdin);
        players[i].name[strcspn(players[i].name,"\n")] = 0;
        if (players[i].name[0]=='\0') snprintf(players[i].name, sizeof(players[i].name), "Player%d", i+1);
        players[i].tokenColor = baseColors[i];
        players[i].iconTex = playerIconTex;
        players[i].pos = 1;
    }

    printf("\nGame started! Press ENTER in console for each roll. (ESC in window to quit)\n");

    int current = 0;
    int running = 1;
    SDL_Event e;

    while (running) {
        /* compute square board rect (preserve, centered) */
        int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
        int size = (ww < wh ? ww : wh);
        SDL_Rect boardRect = { (ww - size)/2, (wh - size)/2, size, size };

        /* draw background */
        SDL_SetRenderDrawColor(ren, 0,0,0,255);
        SDL_RenderClear(ren);
        if (boardTex) SDL_RenderCopy(ren, boardTex, NULL, &boardRect);

        /* top bar (semi-transparent) */
        int topH = (int)(size * 0.12f);
        SDL_Rect topbar = { boardRect.x, boardRect.y, boardRect.w, topH };
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0,0,0,160);
        SDL_RenderFillRect(ren, &topbar);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

        /* player icons + color bars */
        int spacing = boardRect.w / (numPlayers + 1);
        for (int i=0;i<numPlayers;i++){
            int cx = boardRect.x + spacing*(i+1);
            SDL_Rect icon = { cx-24, boardRect.y + 8, 48, 48 };
            if (players[i].iconTex) SDL_RenderCopy(ren, players[i].iconTex, NULL, &icon);
            SDL_Rect bar  = { cx-24, icon.y + icon.h + 6, 48, 10 };
            SDL_SetRenderDrawColor(ren, players[i].tokenColor.r, players[i].tokenColor.g, players[i].tokenColor.b, 255);
            SDL_RenderFillRect(ren, &bar);
        }

        /* draw tokens (outline triangles) */
        int cell = boardRect.w/10;
        int tri = (int)(cell * 0.35f);
        for (int p=0;p<numPlayers;p++){
            int x,y; posToXY(players[p].pos, &boardRect, &x,&y);
            drawTriangle(ren, x, y-tri, x-tri, y+tri, x+tri, y+tri, players[p].tokenColor);
        }

        SDL_RenderPresent(ren);

        /* input to roll (console) + keep window responsive */
        printf("\n%s's turn. Press Enter to roll...", players[current].name);
        int ch = getchar(); (void)ch;

        /* animate dice in top-right of top bar */
        SDL_Rect diceRect = { boardRect.x + boardRect.w - topH + (topH-80)/2, boardRect.y + (topH-80)/2, 80, 80 };
        int shown = 0;
        for (int k=0;k<12;k++){
            int face = (rand()%6)+1;
            SDL_RenderCopy(ren, boardTex, NULL, &boardRect);           // redraw board
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0,0,0,160); SDL_RenderFillRect(ren, &topbar);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            /* re-draw icons/bars */
            for (int i=0;i<numPlayers;i++){
                int cx = boardRect.x + spacing*(i+1);
                SDL_Rect icon = { cx-24, boardRect.y + 8, 48, 48 };
                if (players[i].iconTex) SDL_RenderCopy(ren, players[i].iconTex, NULL, &icon);
                SDL_Rect bar  = { cx-24, icon.y + icon.h + 6, 48, 10 };
                SDL_SetRenderDrawColor(ren, players[i].tokenColor.r, players[i].tokenColor.g, players[i].tokenColor.b, 255);
                SDL_RenderFillRect(ren, &bar);
            }
            /* tokens */
            for (int p=0;p<numPlayers;p++){
                int x,y; posToXY(players[p].pos, &boardRect, &x,&y);
                drawTriangle(ren, x, y-tri, x-tri, y+tri, x+tri, y+tri, players[p].tokenColor);
            }
            if (diceTex[face]) SDL_RenderCopy(ren, diceTex[face], NULL, &diceRect);
            SDL_RenderPresent(ren);
            SDL_Delay(70);
            shown = face;

            while (SDL_PollEvent(&e)) if (e.type==SDL_QUIT) { running=0; break; }
            if (!running) break;
        }
        if (!running) break;

        int roll = (rand()%6)+1;           // final roll (no dice SFX as requested)
        shown = roll;
        if (diceTex[shown]) {
            SDL_RenderCopy(ren, diceTex[shown], NULL, &diceRect);
            SDL_RenderPresent(ren);
        }
        printf("%s rolled %d\n", players[current].name, roll);

        /* move */
        int np = players[current].pos + roll;
        if (np <= BOARD_SIZE) players[current].pos = np;

        /* snake BEFORE movement (sound first, then move) */
        for (int i=0;i<(int)(sizeof(snakes)/sizeof(snakes[0]));i++) {
            if (players[current].pos == snakes[i].start) {
                printf("Snake: %d -> %d\n", snakes[i].start, snakes[i].end);
                playSound("snake.wav");
                SDL_Delay(300);
                players[current].pos = snakes[i].end;
                break;
            }
        }
        /* ladder BEFORE movement (sound first, then move) */
        for (int i=0;i<(int)(sizeof(ladders)/sizeof(ladders[0]));i++) {
            if (players[current].pos == ladders[i].start) {
                printf("Ladder: %d -> %d\n", ladders[i].start, ladders[i].end);
                playSound("ladder.wav");
                SDL_Delay(300);
                players[current].pos = ladders[i].end;
                break;
            }
        }

        /* win? */
        if (players[current].pos == BOARD_SIZE) {
            printf("\n*** %s WINS! ***\n", players[current].name);
            for (int i=0;i<4;i++) playSound("fireworks.wav");
            break;
        }

        /* next player */
        current = (current + 1) % numPlayers;

        /* keep window responsive briefly */
        Uint32 t0 = SDL_GetTicks();
        while (SDL_GetTicks()-t0 < 150) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { running=0; break; }
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) { running=0; break; }
            }
            SDL_Delay(10);
        }
    }

    /* cleanup */
    for (int i=1;i<=6;i++) if (diceTex[i]) SDL_DestroyTexture(diceTex[i]);
    if (playerIconTex) SDL_DestroyTexture(playerIconTex);
    if (boardTex) SDL_DestroyTexture(boardTex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    Mix_HaltChannel(-1);  
    Mix_FreeChunk(NULL);  
    Mix_CloseAudio();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
