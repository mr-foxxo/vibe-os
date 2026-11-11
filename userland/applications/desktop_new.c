#include <userland/modules/include/ui.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/syscalls.h>
#include <userland/applications/include/apps.h>
#include <userland/modules/include/terminal.h>
#include <userland/modules/include/ui_cursor.h>
#include <userland/applications/include/clock.h>
#include <userland/applications/include/filemanager.h>
#include <userland/applications/include/editor.h>
#include <userland/applications/include/taskmgr.h>
#include <userland/applications/include/calculator.h>
#include <userland/applications/include/imageviewer.h>
#include <userland/applications/include/audioplayer.h>
#include <userland/applications/include/sketchpad.h>
#include <userland/applications/include/games/snake.h>
#include <userland/applications/include/games/tetris.h>
#include <userland/applications/include/games/game_2048.h>
#include <userland/applications/include/games/minesweeper.h>
#include <userland/applications/include/games/pacman.h>
#include <userland/applications/include/games/space_invaders.h>
#include <userland/applications/include/games/pong.h>
#include <userland/applications/include/games/donkey_kong.h>
#include <userland/applications/include/games/brick_race.h>
#include <userland/applications/include/games/flap_birb.h>
#include <userland/applications/include/games/doom.h>
#include <userland/applications/include/games/craft.h>
#include <userland/modules/include/icon_theme.h>
#include <userland/modules/include/image.h>
#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>
#include <kernel/microkernel/audio.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/video.h>
#include <kernel/microkernel/service.h>
#include <string.h>
#include <stdlib.h>
#include <userland/modules/include/microui.h>
#include <userland/modules/include/mu_renderer.h>

#define TASKBAR_HEIGHT 22
#define SHORTCUT_X 16
#define SHORTCUT_Y 18
#define SHORTCUT_W 86
#define SHORTCUT_H 82
#define SHORTCUT_STEP 92
#define MENU_W 320
#define MENU_H 400

struct app_entry { enum app_type type; const char *name; const char *icon; int is_game; };

static const struct app_entry g_apps[] = {
    {APP_TERMINAL,"Terminal","terminal",0},{APP_CLOCK,"Clock","clock",0},
    {APP_FILEMANAGER,"Files","folder",0},{APP_EDITOR,"Editor","editor",0},
    {APP_TASKMANAGER,"Task Manager","taskmgr",0},{APP_CALCULATOR,"Calculator","calc",0},
    {APP_IMAGEVIEWER,"Image Viewer","imageviewer",0},{APP_AUDIO_PLAYER,"Audio Player","audioplayer",0},
    {APP_SKETCHPAD,"Sketchpad","sketchpad",0},
    {APP_SNAKE,"Snake","snake",1},{APP_TETRIS,"Tetris","tetris",1},
    {APP_2048,"2048","2048",1},{APP_MINESWEEPER,"Minesweeper","minesweeper",1},
    {APP_PACMAN,"Pac-Man","pacman",1},{APP_SPACE_INVADERS,"Space Invaders","spaceinvaders",1},
    {APP_PONG,"Pong","pong",1},{APP_DONKEY_KONG,"Donkey Kong","donkeykong",1},
    {APP_BRICK_RACE,"Brick Race","brickrace",1},{APP_FLAP_BIRB,"Flap Birb","flapbirb",1},
    {APP_DOOM,"Doom","doom",1},{APP_CRAFT,"Craft","craft",1},
};
#define APP_COUNT ((int)(sizeof(g_apps)/sizeof(g_apps[0])))

#define ALLOC_CASE(T, MAX, ARR) case T: { \
    for (int i_=0;i_<(MAX);++i_) if(!ARR[i_]){ARR[i_]=1;*inst=i_;ok=1;break;} } break

struct desktop {
    struct mu_context mu;
    struct window wins[MAX_WINDOWS];
    struct terminal_state terms[MAX_TERMINALS]; int tu[MAX_TERMINALS];
    struct clock_state clocks[MAX_CLOCKS]; int cu[MAX_CLOCKS];
    struct filemanager_state fms[MAX_FILEMANAGERS]; int fu[MAX_FILEMANAGERS];
    struct editor_state eds[MAX_EDITORS]; int eu[MAX_EDITORS];
    struct taskmgr_state tms[MAX_TASKMGRS]; int tmu[MAX_TASKMGRS];
    struct calculator_state calcs[MAX_CALCULATORS]; int cacu[MAX_CALCULATORS];
    struct imageviewer_state ivs[MAX_IMAGEVIEWERS]; int ivu[MAX_IMAGEVIEWERS];
    struct audioplayer_state aps[MAX_AUDIO_PLAYERS]; int apu[MAX_AUDIO_PLAYERS];
    struct sketchpad_state sks[MAX_SKETCHPADS]; int sku[MAX_SKETCHPADS];
    struct snake_state sns[MAX_SNAKES]; int snu[MAX_SNAKES];
    struct tetris_state tts[MAX_TETRIS]; int ttu[MAX_TETRIS];
    struct game_2048_state g2048s[MAX_2048_GAMES]; int g2048u[MAX_2048_GAMES];
    struct minesweeper_state mss[MAX_MINESWEEPERS]; int msu[MAX_MINESWEEPERS];
    struct pacman_state pcs[MAX_PACMAN]; int pcu[MAX_PACMAN];
    struct space_invaders_state sis[MAX_SPACE_INVADERS]; int siu[MAX_SPACE_INVADERS];
    struct pong_state pgs[MAX_PONG]; int pgu[MAX_PONG];
    struct donkey_kong_state dks[MAX_DONKEY_KONG]; int dku[MAX_DONKEY_KONG];
    struct brick_race_state brs[MAX_BRICK_RACE]; int bru[MAX_BRICK_RACE];
    struct flap_birb_state fbs[MAX_FLAP_BIRB]; int fbu[MAX_FLAP_BIRB];
    struct doom_state dms[MAX_DOOM]; int dmu[MAX_DOOM];
    struct craft_state crs[MAX_CRAFT]; int cru[MAX_CRAFT];
    int focused, menu_open, sound_open, net_open, running;
    uint32_t ticks;
    int sound_vol, sound_muted, net_connected;
    char net_status[64];
    struct mouse_state mouse;
    char search[MENU_W];
};

static struct desktop g;

static void *app_state(enum app_type t, int i) {
    #define S(T,A) case T:return(void*)&g.A[i]
    switch(t){
        S(APP_TERMINAL,terms);S(APP_CLOCK,clocks);S(APP_FILEMANAGER,fms);
        S(APP_EDITOR,eds);S(APP_TASKMANAGER,tms);S(APP_CALCULATOR,calcs);
        S(APP_IMAGEVIEWER,ivs);S(APP_AUDIO_PLAYER,aps);S(APP_SKETCHPAD,sks);
        S(APP_SNAKE,sns);S(APP_TETRIS,tts);S(APP_2048,g2048s);
        S(APP_MINESWEEPER,mss);S(APP_PACMAN,pcs);S(APP_SPACE_INVADERS,sis);
        S(APP_PONG,pgs);S(APP_DONKEY_KONG,dks);S(APP_BRICK_RACE,brs);
        S(APP_FLAP_BIRB,fbs);S(APP_DOOM,dms);S(APP_CRAFT,crs);
        default:return 0;
    }
    #undef S
}

static int app_find(enum app_type t) {
    for(int i=0;i<MAX_WINDOWS;i++) if(g.wins[i].active&&g.wins[i].type==t) return i;
    return -1;
}

static int app_alloc(enum app_type t, int *inst) {
    int ok=0;
    switch(t){
        ALLOC_CASE(APP_TERMINAL,MAX_TERMINALS,g.tu);
        ALLOC_CASE(APP_CLOCK,MAX_CLOCKS,g.cu);
        ALLOC_CASE(APP_FILEMANAGER,MAX_FILEMANAGERS,g.fu);
        ALLOC_CASE(APP_EDITOR,MAX_EDITORS,g.eu);
        ALLOC_CASE(APP_TASKMANAGER,MAX_TASKMGRS,g.tmu);
        ALLOC_CASE(APP_CALCULATOR,MAX_CALCULATORS,g.cacu);
        ALLOC_CASE(APP_IMAGEVIEWER,MAX_IMAGEVIEWERS,g.ivu);
        ALLOC_CASE(APP_AUDIO_PLAYER,MAX_AUDIO_PLAYERS,g.apu);
        ALLOC_CASE(APP_SKETCHPAD,MAX_SKETCHPADS,g.sku);
        ALLOC_CASE(APP_SNAKE,MAX_SNAKES,g.snu);
        ALLOC_CASE(APP_TETRIS,MAX_TETRIS,g.ttu);
        ALLOC_CASE(APP_2048,MAX_2048_GAMES,g.g2048u);
        ALLOC_CASE(APP_MINESWEEPER,MAX_MINESWEEPERS,g.msu);
        ALLOC_CASE(APP_PACMAN,MAX_PACMAN,g.pcu);
        ALLOC_CASE(APP_SPACE_INVADERS,MAX_SPACE_INVADERS,g.siu);
        ALLOC_CASE(APP_PONG,MAX_PONG,g.pgu);
        ALLOC_CASE(APP_DONKEY_KONG,MAX_DONKEY_KONG,g.dku);
        ALLOC_CASE(APP_BRICK_RACE,MAX_BRICK_RACE,g.bru);
        ALLOC_CASE(APP_FLAP_BIRB,MAX_FLAP_BIRB,g.fbu);
        ALLOC_CASE(APP_DOOM,MAX_DOOM,g.dmu);
        ALLOC_CASE(APP_CRAFT,MAX_CRAFT,g.cru);
        default: break;
    }
    return ok?0:-1;
}

static void app_init(enum app_type t, int inst) {
    if(inst<0)return;
    #define I(T,F,A) case T:F(&g.A[inst]);break
    switch(t){
        I(APP_TERMINAL,terminal_init_state,terms);
        I(APP_CLOCK,clock_init_state,clocks);
        I(APP_FILEMANAGER,filemanager_init_state,fms);
        I(APP_EDITOR,editor_init_state,eds);
        I(APP_TASKMANAGER,taskmgr_init_state,tms);
        I(APP_CALCULATOR,calculator_init_state,calcs);
        I(APP_IMAGEVIEWER,imageviewer_init_state,ivs);
        I(APP_AUDIO_PLAYER,audioplayer_init_state,aps);
        I(APP_SKETCHPAD,sketchpad_init_state,sks);
        I(APP_SNAKE,snake_init_state,sns);
        I(APP_TETRIS,tetris_init_state,tts);
        I(APP_2048,game_2048_init_state,g2048s);
        I(APP_MINESWEEPER,minesweeper_init_state,mss);
        I(APP_PACMAN,pacman_init_state,pcs);
        I(APP_SPACE_INVADERS,space_invaders_init_state,sis);
        I(APP_PONG,pong_init_state,pgs);
        I(APP_DONKEY_KONG,donkey_kong_init_state,dks);
        I(APP_BRICK_RACE,brick_race_init_state,brs);
        I(APP_FLAP_BIRB,flap_birb_init_state,fbs);
        I(APP_DOOM,doom_init_state,dms);
        I(APP_CRAFT,craft_init_state,crs);
        default: break;
    }
}

static int app_launch(enum app_type t) {
    int w=0,inst=0;
    for(;w<MAX_WINDOWS;w++) if(!g.wins[w].active) break;
    if(w>=MAX_WINDOWS||app_alloc(t,&inst)<0) return -1;
    struct window *win=&g.wins[w];
    memset(win,0,sizeof(*win));
    win->active=1; win->type=t; win->instance=inst; win->start_ticks=g.ticks;
    win->rect.x=20+w*12; win->rect.y=20+w*12;
    win->rect.w=(int)SCREEN_WIDTH-160; if(win->rect.w>700)win->rect.w=700;
    win->rect.h=(int)SCREEN_HEIGHT-TASKBAR_HEIGHT-100; if(win->rect.h>500)win->rect.h=500;
    win->restore_rect=win->rect;
    app_init(t,inst);
    g.focused=w;
    return w;
}

static int app_open_or_focus(enum app_type t) {
    int e=app_find(t);
    if(e>=0){g.focused=e;g.wins[e].minimized=0;return e;}
    return app_launch(t);
}

static void app_free(int idx) {
    struct window *w=&g.wins[idx];
    if(!w->active)return;
    #define F(T,MAX,ARR) case T:{for(int i=0;i<MAX;i++)if(&g.ARR[i]==app_state(w->type,w->instance)){g.ARR[i]=0;break;}}break
    switch(w->type){
        F(APP_TERMINAL,MAX_TERMINALS,tu);F(APP_CLOCK,MAX_CLOCKS,cu);
        F(APP_FILEMANAGER,MAX_FILEMANAGERS,fu);F(APP_EDITOR,MAX_EDITORS,eu);
        F(APP_TASKMANAGER,MAX_TASKMGRS,tmu);F(APP_CALCULATOR,MAX_CALCULATORS,cacu);
        F(APP_IMAGEVIEWER,MAX_IMAGEVIEWERS,ivu);F(APP_AUDIO_PLAYER,MAX_AUDIO_PLAYERS,apu);
        F(APP_SKETCHPAD,MAX_SKETCHPADS,sku);
        F(APP_SNAKE,MAX_SNAKES,snu);F(APP_TETRIS,MAX_TETRIS,ttu);
        F(APP_2048,MAX_2048_GAMES,g2048u);F(APP_MINESWEEPER,MAX_MINESWEEPERS,msu);
        F(APP_PACMAN,MAX_PACMAN,pcu);F(APP_SPACE_INVADERS,MAX_SPACE_INVADERS,siu);
        F(APP_PONG,MAX_PONG,pgu);F(APP_DONKEY_KONG,MAX_DONKEY_KONG,dku);
        F(APP_BRICK_RACE,MAX_BRICK_RACE,bru);F(APP_FLAP_BIRB,MAX_FLAP_BIRB,fbu);
        F(APP_DOOM,MAX_DOOM,dmu);F(APP_CRAFT,MAX_CRAFT,cru);
        default: break;
    }
    memset(w,0,sizeof(*w));
    if(g.focused==idx)g.focused=-1;
}

static void draw_app_window(int i) {
    struct window *w=&g.wins[i];
    if(!w->active||w->minimized)return;
    int act=(i==g.focused);
    int mx=g.mouse.x,my=g.mouse.y;
    struct rect cr=window_close_button(&w->rect);
    struct rect mr=window_min_button(&w->rect);
    struct rect xr=window_max_button(&w->rect);
    int ch=point_in_rect(&cr,mx,my);
    int mh=point_in_rect(&mr,mx,my);
    int xh=point_in_rect(&xr,mx,my);
    int inst=w->instance;
    switch(w->type){
        case APP_TERMINAL:terminal_draw_window(&g.terms[inst],act,mh,xh,ch);break;
        case APP_CLOCK:clock_draw_window(&g.clocks[inst],act,mh,xh,ch);break;
        case APP_FILEMANAGER:filemanager_draw_window(&g.fms[inst],act,mh,xh,ch);break;
        case APP_EDITOR:editor_draw_window(&g.eds[inst],act,mh,xh,ch);break;
        case APP_TASKMANAGER:taskmgr_draw_window(&g.tms[inst],g.wins,MAX_WINDOWS,g.ticks,act,mh,xh,ch);break;
        case APP_CALCULATOR:calculator_draw_window(&g.calcs[inst],act,mh,xh,ch);break;
        case APP_IMAGEVIEWER:imageviewer_draw_window(&g.ivs[inst],act,mh,xh,ch);break;
        case APP_AUDIO_PLAYER:audioplayer_draw_window(&g.aps[inst],act,mh,xh,ch);break;
        case APP_SKETCHPAD:sketchpad_draw_window(&g.sks[inst],act,mh,xh,ch);break;
        case APP_SNAKE:snake_draw_window(&g.sns[inst],act,mh,xh,ch);break;
        case APP_TETRIS:tetris_draw_window(&g.tts[inst],act,mh,xh,ch);break;
        case APP_2048:game_2048_draw_window(&g.g2048s[inst],act,mh,xh,ch);break;
        case APP_MINESWEEPER:minesweeper_draw_window(&g.mss[inst],act,mh,xh,ch);break;
        case APP_PACMAN:pacman_draw_window(&g.pcs[inst],act,mh,xh,ch);break;
        case APP_SPACE_INVADERS:space_invaders_draw_window(&g.sis[inst],act,mh,xh,ch);break;
        case APP_PONG:pong_draw_window(&g.pgs[inst],act,mh,xh,ch);break;
        case APP_DONKEY_KONG:donkey_kong_draw_window(&g.dks[inst],act,mh,xh,ch);break;
        case APP_BRICK_RACE:brick_race_draw_window(&g.brs[inst],act,mh,xh,ch);break;
        case APP_FLAP_BIRB:flap_birb_draw_window(&g.fbs[inst],act,mh,xh,ch);break;
        case APP_DOOM:doom_draw_window(&g.dms[inst],act,mh,xh,ch);break;
        case APP_CRAFT:craft_draw_window(&g.crs[inst],act,mh,xh,ch);break;
        default:break;
    }
}

static void draw_background(void) {
    clip_push(0,0,(int)SCREEN_WIDTH,(int)SCREEN_HEIGHT-TASKBAR_HEIGHT);
    sys_clear(ui_theme_get()->background);
    clip_pop();
    int fw=SHORTCUT_W,fh=SHORTCUT_H;
    int h1=point_in_rect(&(struct rect){SHORTCUT_X,SHORTCUT_Y,fw,fh},g.mouse.x,g.mouse.y);
    int h2=point_in_rect(&(struct rect){SHORTCUT_X+SHORTCUT_STEP,SHORTCUT_Y,fw,fh},g.mouse.x,g.mouse.y);
    int h3=point_in_rect(&(struct rect){SHORTCUT_X+SHORTCUT_STEP*2,SHORTCUT_Y,fw,fh},g.mouse.x,g.mouse.y);
    for(int k=0;k<3;k++){
        int hx=SHORTCUT_X+k*SHORTCUT_STEP;
        const char *lbl=k==0?"Files":k==1?"Craft":"Trash";
        if((h1&&k==0)||(h2&&k==1)||(h3&&k==2))sys_rect(hx-1,SHORTCUT_Y-1,fw+2,fh+2,15);
        icon_theme_draw("folder",ICON_THEME_CONTEXT_PLACES,48,hx+(fw-48)/2,SHORTCUT_Y+2,48,48);
        int tw=str_len(lbl)*6, tx=hx+(fw-tw)/2;
        if(tx<0)tx=0;
        sys_text(tx,SHORTCUT_Y+54,ui_theme_get()->text,lbl);
    }
}

static void draw_taskbar(void) {
    int y=(int)SCREEN_HEIGHT-TASKBAR_HEIGHT;
    sys_rect(0,y,(int)SCREEN_WIDTH,TASKBAR_HEIGHT,ui_theme_get()->taskbar);
    sys_rect(4,y+3,52,TASKBAR_HEIGHT-6,g.menu_open?3:ui_theme_get()->menu_button);
    sys_text(6,y+5,ui_theme_get()->text,"Start");
    int bx=60;
    for(int i=0;i<MAX_WINDOWS;i++){
        if(!g.wins[i].active)continue;
        const char *nm="App";
        for(int a=0;a<APP_COUNT;a++)if(g_apps[a].type==g.wins[i].type){nm=g_apps[a].name;break;}
        int bw=str_len(nm)*6+12;if(bw<40)bw=40;
        sys_rect(bx,y+3,bw,TASKBAR_HEIGHT-6,i==g.focused?3:ui_theme_get()->menu_button_inactive);
        sys_text(bx+4,y+5,ui_theme_get()->text,nm);
        bx+=bw+2;
    }
    char tb[16];
    int hh=(g.ticks/3600)%24,mm=(g.ticks/60)%60;
    tb[0]=(char)('0'+hh/10);tb[1]=(char)('0'+hh%10);tb[2]=':';
    tb[3]=(char)('0'+mm/10);tb[4]=(char)('0'+mm%10);tb[5]=0;
    int tw=str_len(tb)*6,cx=(int)SCREEN_WIDTH-tw-20;
    if(cx<0)cx=0;
    sys_text(cx,y+5,ui_theme_get()->text,tb);
    sys_rect(cx-20,y+3,16,TASKBAR_HEIGHT-6,g.net_connected?10:12);
    sys_rect(cx-40,y+3,16,TASKBAR_HEIGHT-6,g.sound_muted?4:10);
}

static void draw_menu(void) {
    int mw=MENU_W,mh=MENU_H,my=(int)SCREEN_HEIGHT-TASKBAR_HEIGHT-mh;
    sys_rect(0,my,mw,mh,ui_theme_get()->menu);
    sys_rect(8,my+8,mw-16,18,ui_theme_get()->window_bg);
    sys_text(12,my+10,ui_theme_get()->text,"Search...");
    int ay=my+34,ih=22,cnt=APP_COUNT,mv=(mh-68)/ih;
    if(cnt>mv)cnt=mv;
    for(int i=0;i<cnt;i++){
        sys_rect(4,ay+i*ih,mw-8,ih-1,ui_theme_get()->menu_button);
        sys_text(10,ay+i*ih+1,ui_theme_get()->text,g_apps[i].name);
    }
    int bot=my+mh-30;
    sys_rect(4,bot,(mw-12)/2,24,ui_theme_get()->menu_button);
    sys_text(10,bot+2,ui_theme_get()->text,"Terminal");
    sys_rect(4+(mw-12)/2+4,bot,(mw-12)/2,24,ui_theme_get()->menu_button);
    sys_text(10+(mw-12)/2+4,bot+2,ui_theme_get()->text,"Personalize");
}

static void draw_sound(void) {
    if(!g.sound_open)return;
    int px=(int)SCREEN_WIDTH-150,py=(int)SCREEN_HEIGHT-TASKBAR_HEIGHT-70;
    sys_rect(px,py,146,64,ui_theme_get()->menu);
    sys_rect(px+2,py+2,142,60,ui_theme_get()->window_bg);
    sys_text(px+6,py+4,ui_theme_get()->text,"Volume:");
    int bar_x=px+50,bar_w=80,vw=(g.sound_vol*bar_w)/255;
    if(vw<0)vw=0;
    if(vw>bar_w)vw=bar_w;
    sys_rect(bar_x,py+4,bar_w,8,8);sys_rect(bar_x,py+4,vw,8,10);
    sys_rect(px+8,py+18,50,16,g.sound_muted?4:10);
    sys_text(px+12,py+19,g.sound_muted?15:0,g.sound_muted?"Muted":"Unmute");
}

static void draw_net(void) {
    if(!g.net_open)return;
    int px=(int)SCREEN_WIDTH-200,py=(int)SCREEN_HEIGHT-TASKBAR_HEIGHT-100;
    sys_rect(px,py,196,94,ui_theme_get()->menu);
    sys_rect(px+2,py+2,192,90,ui_theme_get()->window_bg);
    sys_text(px+6,py+4,ui_theme_get()->text,g.net_connected?"Connected":"Disconnected");
    sys_rect(px+6,py+20,180,18,8);
    sys_text(px+10,py+21,ui_theme_get()->text,g.net_status);
    sys_rect(px+50,py+44,80,18,10);
    sys_text(px+60,py+45,0,g.net_connected?"Disconnect":"Scan");
}

static void handle_click(void) {
    int mx=g.mouse.x,my=g.mouse.y;
    if(mx>=SHORTCUT_X&&mx<SHORTCUT_X+SHORTCUT_W&&my>=SHORTCUT_Y&&my<SHORTCUT_Y+SHORTCUT_H)
        {app_open_or_focus(APP_FILEMANAGER);return;}
    if(mx>=SHORTCUT_X+SHORTCUT_STEP&&mx<SHORTCUT_X+SHORTCUT_STEP+SHORTCUT_W&&my>=SHORTCUT_Y&&my<SHORTCUT_Y+SHORTCUT_H)
        {app_open_or_focus(APP_CRAFT);return;}
    int tb=(int)SCREEN_HEIGHT-TASKBAR_HEIGHT;
    if(my>=tb+3&&my<tb+TASKBAR_HEIGHT-3){
        if(mx>=4&&mx<56){g.menu_open=!g.menu_open;g.sound_open=0;g.net_open=0;return;}
        int bx=60;
        for(int i=0;i<MAX_WINDOWS;i++){
            if(!g.wins[i].active)continue;
            const char*nm="App";
            for(int a=0;a<APP_COUNT;a++)if(g_apps[a].type==g.wins[i].type){nm=g_apps[a].name;break;}
            int bw=str_len(nm)*6+12;if(bw<40)bw=40;
            if(mx>=bx&&mx<bx+bw){
                if(g.focused==i&&!g.wins[i].minimized)g.wins[i].minimized=1;
                else{g.wins[i].minimized=0;g.focused=i;}
                return;
            }
            bx+=bw+2;
        }
    }
    if(g.menu_open){
        int mym=tb-MENU_H,ih=22,ay=mym+34,cnt=APP_COUNT,mv=(MENU_H-68)/ih;
        if(cnt>mv)cnt=mv;
        for(int i=0;i<cnt;i++){
            if(mx>=4&&mx<MENU_W-4&&my>=ay+i*ih&&my<ay+i*ih+ih-1){
                g.menu_open=0;int e=app_find(g_apps[i].type);
                if(e<0)app_launch(g_apps[i].type);else{g.focused=e;g.wins[e].minimized=0;}
                return;
            }
        }
        int bot=mym+MENU_H-30;
        if(mx>=4&&mx<4+(MENU_W-12)/2&&my>=bot&&my<bot+24){
            g.menu_open=0;app_open_or_focus(APP_TERMINAL);return;
        }
    }
    for(int i=MAX_WINDOWS-1;i>=0;i--){
        if(!g.wins[i].active||g.wins[i].minimized)continue;
        struct window*w=&g.wins[i];
        int in=mx>=w->rect.x&&mx<w->rect.x+w->rect.w&&my>=w->rect.y&&my<w->rect.y+w->rect.h;
        if(in){
            struct rect wcb=window_close_button(&w->rect);
            int close_hit=point_in_rect(&wcb,mx,my);
            if(close_hit){app_free(i);return;}
            g.focused=i;
            return;
        }
    }
}

static void handle_key(int key) {
    int f=g.focused;
    if(f<0||!g.wins[f].active||g.wins[f].minimized)return;
    struct window*w=&g.wins[f];
    int inst=w->instance;
    if(key==27&&g.menu_open){g.menu_open=0;return;}
    if(w->type==APP_TERMINAL){
        struct terminal_state*t=&g.terms[inst];
        if(key=='\b'||key==127){terminal_backspace(t);return;}
        if(key==KEY_DELETE){terminal_delete_char(t);return;}
        if(key==KEY_ARROW_LEFT){terminal_move_cursor_left(t);return;}
        if(key==KEY_ARROW_RIGHT){terminal_move_cursor_right(t);return;}
        if(key==KEY_ARROW_UP){terminal_history_prev(t);return;}
        if(key==KEY_ARROW_DOWN){terminal_history_next(t);return;}
        if(key=='\n'&&terminal_execute_command(t)){app_free(f);return;}
        if(key>=32&&key<=126)terminal_add_input_char(t,(char)key);
    }else if(w->type==APP_EDITOR){
        struct editor_state*e=&g.eds[inst];
        if(key=='\b'){editor_backspace(e);return;}
        if(key=='\n'){editor_newline(e);return;}
        if(key>=32&&key<=126)editor_insert_char(e,(char)key);
    }else if(w->type==APP_CALCULATOR){
        struct calculator_state*c=&g.calcs[inst];
        if((key>='0'&&key<='9')||key=='+'||key=='-'||key=='*'||key=='/'||key=='='||key=='c'||key=='C')
            calculator_press_key(c,(char)key);
        else if(key=='\b'||key==127)calculator_backspace(c);
    }else if(w->type==APP_AUDIO_PLAYER){audioplayer_handle_key(&g.aps[inst],key);
    }else if(w->type==APP_SKETCHPAD){if(key=='c'||key=='C')sketchpad_clear(&g.sks[inst]);
    }else if(w->type==APP_SNAKE){snake_handle_key(&g.sns[inst],key);
    }else if(w->type==APP_TETRIS){tetris_handle_key(&g.tts[inst],key);
    }else if(w->type==APP_2048){game_2048_handle_key(&g.g2048s[inst],key);
    }else if(w->type==APP_MINESWEEPER){minesweeper_handle_key(&g.mss[inst],key);
    }else if(w->type==APP_PACMAN){pacman_handle_key(&g.pcs[inst],key);
    }else if(w->type==APP_SPACE_INVADERS){space_invaders_handle_key(&g.sis[inst],key);
    }else if(w->type==APP_PONG){pong_handle_key(&g.pgs[inst],key);
    }else if(w->type==APP_DONKEY_KONG){donkey_kong_handle_key(&g.dks[inst],key);
    }else if(w->type==APP_BRICK_RACE){brick_race_handle_key(&g.brs[inst],key);
    }else if(w->type==APP_FLAP_BIRB){flap_birb_handle_key(&g.fbs[inst],key);
    }else if(w->type==APP_DOOM){doom_handle_key(&g.dms[inst],key);
    }else if(w->type==APP_CRAFT){if(key=='q'||key=='Q')app_free(f);else craft_handle_key(&g.crs[inst],key);
    }
}

void desktop_main(void) {
    struct input_event ev;
    sys_write_debug("desktop_new: session start\n");
    fs_suspend_sync(); ui_hold_startup_persist();
    ui_init(); sys_gfx_set_present_policy(VIDEO_PRESENT_POLICY_DESKTOP);
    memset(&g,0,sizeof(g)); g.running=1; g.focused=-1; g.sound_vol=200;
    mu_init(&g.mu); mu_renderer_init(&g.mu);
    g.mouse.x=(int)SCREEN_WIDTH/2; g.mouse.y=(int)SCREEN_HEIGHT/2;
    while(g.running){
        g.ticks=sys_ticks();
        while(sys_next_input_event(&ev)==0){
            if(ev.type==INPUT_EVENT_MOUSE){
                g.mouse=ev.mouse; g.mu.mouse_x=g.mouse.x; g.mu.mouse_y=g.mouse.y;
                if(g.mouse.buttons&1){if(!g.mu.mouse_down)g.mu.mouse_pressed=1;g.mu.mouse_down=1;}
                else g.mu.mouse_down=0;
                g.mu.scroll_delta=g.mouse.wheel;
            }else if(ev.type==INPUT_EVENT_KEY){
                int k=ev.value;
                if(k>0&&k<128){g.mu.key_down=k;g.mu.key_pressed=1;}
                handle_key(k);
            }
        }
        if(g.mu.mouse_pressed)handle_click();
        for(int i=0;i<MAX_WINDOWS;i++)if(g.wins[i].active){
            if(g.wins[i].type==APP_CLOCK)clock_step(&g.clocks[g.wins[i].instance]);
        }
        mu_begin(&g.mu);
        draw_background(); draw_taskbar();
        if(g.menu_open)draw_menu();
        draw_sound(); draw_net();
        for(int i=0;i<MAX_WINDOWS;i++)draw_app_window(i);
        mu_end(&g.mu);
        cursor_draw(g.mouse.x,g.mouse.y);
        sys_present();
        g.mu.mouse_pressed=0;g.mu.key_pressed=0;
        sys_sleep();
    }
}

void desktop_request_open_editor(const char *path) {
    int idx=app_open_or_focus(APP_EDITOR);
    if(idx>=0&&path&&path[0]){
        int node=fs_resolve(path);
        if(node>=0&&!g_fs_nodes[node].is_dir)
            editor_load_node(&g.eds[g.wins[idx].instance],node);
    }
}
void desktop_request_open_nano(const char *path){desktop_request_open_editor(path);}
void desktop_request_open_app(enum app_type type){app_open_or_focus(type);}
void desktop_request_cycle_app(enum app_type t,uint32_t i,uint32_t h){(void)t;(void)i;(void)h;}
void desktop_request_drag_stress(uint32_t s,uint32_t h){(void)s;(void)h;}
