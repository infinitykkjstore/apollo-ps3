/* 
	Hello World PS3 Homebrew
	Baseado na infraestrutura do Apollo Save Tool
*/

#include <sys/spu.h>
#include <lv2/spu.h>

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include "saves.h"
#include "pfd.h"
#include "util.h"
#include "common.h"

//Menus
#include "menu.h"
#include "menu_gui.h"

//Font
#include "libfont.h"
#include "ttf_render.h"
#include "font_adonais.h"
#include "font-10x20.h"

//Sound
#include "spu_soundmodule_bin.h"
#include <soundlib/spu_soundlib.h>
#include <libxmp-lite/xmp.h>

#define SAMPLING_FREQ       48000 /* 48khz. */
#define AUDIO_SAMPLES       SAMPLING_FREQ * 2 /* audio buffer to decode (for 48000 samples x 0.5 seconds and 16 bit stereo as reference) */

extern const uint8_t haiku_s3m[];
extern const uint32_t haiku_s3m_size;

static short *background_music[2] = {NULL, NULL};
static xmp_context xmp = NULL;

// SPU
static u32 inited;
static u32 spu = 0;
static sysSpuImage spu_image;

#define INITED_CALLBACK     1
#define INITED_SPU          2
#define INITED_SOUNDLIB     4
#define INITED_AUDIOPLAYER  8

#define SPU_SIZE(x) (((x)+127) & ~127)

#define load_menu_texture(name, type) \
	({ extern const uint8_t name##_##type []; \
	   extern const uint32_t name##_##type##_size; \
	   menu_textures[name##_##type##_index].buffer = (const void*) name##_##type; \
	   menu_textures[name##_##type##_index].size = name##_##type##_size; \
	   LoadTexture_##type(name##_##type##_index); \
	})

app_config_t apollo_config = {
    .app_name = "HELLO",
    .app_ver = "1.0.0",
    .save_db = "",
    .ftp_url = "",
    .music = 0,  // Desabilitar música para simplificar
    .doSort = 0,
    .doAni = 0,
    .update = 0,
    .usb_dev = (MAX_USB_DEVICES+1),
    .online_opt = 0,
    .dbglog = 0,
    .user_id = 0,
    .idps = {0, 0},
    .psid = {0, 0},
    .account_id = 0,
};

int close_app = 0;
int idle_time = 0;                          // Set by readPad

png_texture * menu_textures;                // png_texture array for main menu, initialized in LoadTexture

static void release_all(void)
{	
	if(inited & INITED_CALLBACK)
		sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);

	if(inited & INITED_SOUNDLIB)
		SND_End();

	if(inited & INITED_AUDIOPLAYER) {
		xmp_end_player(xmp);
		xmp_release_module(xmp);
		xmp_free_context(xmp);
	}

	if(inited & INITED_SPU) {
		sysSpuRawDestroy(spu);
		sysSpuImageClose(&spu_image);
	}

	http_end();
	wait_save_thread();
	sysModuleUnload(SYSMODULE_PNGDEC);

	inited=0;
}

static void sys_callback(uint64_t status, uint64_t param, void* userdata)
{
	switch (status) {
		case SYSUTIL_EXIT_GAME:
			release_all();
			if (file_exists("/dev_hdd0/mms/db.err") == SUCCESS)
				sys_reboot();

			sysProcessExit(1);
			break;

		case SYSUTIL_MENU_OPEN:
		case SYSUTIL_MENU_CLOSE:
			break;

		default:
			break;
	}
}

static void LoadTexture_png(int idx)
{
	pngLoadFromBuffer(menu_textures[idx].buffer, menu_textures[idx].size, &menu_textures[idx].texture);
	copyTexture(idx);
}

static void LoadTexture_jpg(int idx)
{
	jpgLoadFromBuffer(menu_textures[idx].buffer, menu_textures[idx].size, (jpgData*) &menu_textures[idx].texture);
	copyTexture(idx);
}

// Versão simplificada do LoadTextures_Menu - apenas carrega o essencial
static void LoadTextures_Menu(void)
{
	texture_mem = tiny3d_AllocTexture(64*1024*1024); // alloc 64MB of space for textures
	menu_textures = (png_texture *)calloc(TOTAL_MENU_TEXTURES, sizeof(png_texture));
	
	if(!texture_mem || !menu_textures)
		return; // fail!
	
	ResetFont();
	free_mem = (u32 *) AddFontFromBitmapArray((u8 *) data_font_Adonais, (u8 *) texture_mem, 0x20, 0x7e, 32, 31, 1, BIT7_FIRST_PIXEL);
	free_mem = (u32 *) AddFontFromBitmapArray((u8 *) console_font_10x20, (u8 *) free_mem, 0, 0xFF, 10, 20, 1, BIT7_FIRST_PIXEL);
	
	TTFUnloadFont();
	TTFLoadFont(0, "/dev_flash/data/font/SCE-PS3-SR-R-LATIN2.TTF", NULL, 0);
	TTFLoadFont(1, "/dev_flash/data/font/SCE-PS3-DH-R-CGB.TTF", NULL, 0);
	TTFLoadFont(2, "/dev_flash/data/font/SCE-PS3-SR-R-JPN.TTF", NULL, 0);
	TTFLoadFont(3, "/dev_flash/data/font/SCE-PS3-YG-R-KOR.TTF", NULL, 0);

	free_mem = (u32*) init_ttf_table((u16*) free_mem);
	
	set_ttf_window(0, 0, 848, 512, WIN_SKIP_LF);

	// Carregar apenas texturas essenciais para Hello World
	load_menu_texture(leon_luna, jpg);
	load_menu_texture(bgimg, jpg);

	u32 tBytes = free_mem - texture_mem;
	LOG("LoadTextures_Menu() :: Allocated %db (%.02fkb, %.02fmb) for textures", tBytes, tBytes / (float)1024, tBytes / (float)(1024 * 1024));
}

// Função para desenhar o Hello World
static void DrawHelloWorld(void)
{
	// Desenhar background
	DrawBackground2D(0xFF000000); // Fundo preto
	
	// Desenhar logo de fundo (opcional)
	if (menu_textures[leon_luna_jpg_index].texture_off)
	{
		DrawTexture(&menu_textures[leon_luna_jpg_index], 0, 0, 0, 848, 512, 0x80FFFFFF);
	}
	
	// Configurar fonte
	SetFontSize(APP_FONT_SIZE_TITLE);
	SetCurrentFont(font_adonais_regular);
	SetFontAlign(FONT_ALIGN_SCREEN_CENTER);
	SetFontColor(APP_FONT_COLOR, 0);
	
	// Desenhar texto principal
	DrawString(0, 200, "Hello World!");
	
	// Desenhar texto secundário
	SetFontSize(APP_FONT_SIZE_SUBTITLE);
	DrawString(0, 250, "PS3 Homebrew Development");
	
	// Desenhar instruções
	SetFontSize(APP_FONT_SIZE_DESCRIPTION);
	SetFontColor(0xFF00FF00, 0); // Verde
	DrawString(0, 350, "Press X to Exit");
	
	// Resetar alinhamento
	SetFontAlign(FONT_ALIGN_LEFT);
}

/*
	Program start - Hello World PS3
*/
s32 main(s32 argc, const char* argv[])
{
#ifdef APOLLO_ENABLE_LOGGING
	dbglogger_init();
	dbglogger_failsafe("9999");
#endif

	http_init();

	tiny3d_Init(1024*1024);
	tiny3d_UserViewport(1, 0, 0, // 2D position
		(float) (Video_Resolution.width / 848.0f),  (float) (Video_Resolution.height / 512.0f),   // 2D scale
		(float) (Video_Resolution.width / 1920.0f), (float) (Video_Resolution.height / 1080.0f)); // 3D scale

	ps3PadInit();
	
	sysModuleLoad(SYSMODULE_PNGDEC);
	sysModuleLoad(SYSMODULE_JPGDEC);

	// register exit callback
	if(sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sys_callback, NULL)==0) inited |= INITED_CALLBACK;
	
	// Load texture
	LoadTextures_Menu();

	// Setup font
	SetExtraSpace(5);
	SetCurrentFont(font_adonais_regular);

	LOG("Hello World PS3 Homebrew Started!");
	
	// Loop principal simplificado
	while (!close_app)
	{       
		tiny3d_Clear(0xff000000, TINY3D_CLEAR_ALL);

		// Enable alpha Test
		tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);

		// Enable alpha blending.
		tiny3d_BlendFunc(1, TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
			TINY3D_BLEND_FUNC_SRC_ALPHA_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_SRC_ALPHA,
			TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);
		
		// change to 2D context (remember you it works with 848 x 512 as virtual coordinates)
		tiny3d_Project2D();

		// Desenhar Hello World
		DrawHelloWorld();

		// Ler input do controle
		readPad(0);
		
		// Verificar se X foi pressionado para sair
		if (paddata[0].BTN_CROSS)
		{
			LOG("X button pressed - exiting...");
			close_app = 1;
		}
		
		tiny3d_Flip();
	}

	release_all();
	LOG("Hello World PS3 Homebrew Exited!");

	return 0;
}
