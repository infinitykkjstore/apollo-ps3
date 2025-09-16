/* 
	Apollo PS3 main.c - Hello World Version
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

//Pad
#include <io/pad.h>

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

void update_usb_path(char *p);
void update_hdd_path(char *p);
void update_trophy_path(char *p);
void update_db_path(char *p);
void update_vmc_path(char *p);

app_config_t apollo_config = {
    .app_name = "APOLLO",
    .app_ver = APOLLO_VERSION,
    .save_db = ONLINE_URL,
    .ftp_url = "",
    .music = 1,
    .doSort = 1,
    .doAni = 1,
    .update = 1,
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

// Pad data declaration
padData paddata[MAX_PADS];

png_texture * menu_textures;                // png_texture array for main menu, initialized in LoadTexture

const char * menu_pad_help[TOTAL_MENU_IDS] = { NULL,												//Main
								"\x10 Select    \x13 Back    \x12 Details    \x11 Refresh",			//Trophy list
								"\x10 Select    \x13 Back    \x12 Details    \x11 Refresh",			//USB list
								"\x10 Select    \x13 Back    \x12 Details    \x11 Refresh",			//HDD list
								"\x10 Select    \x13 Back    \x12 Details    \x11 Refresh",			//Online list
								"\x10 Select    \x13 Back    \x11 Refresh",							//User backup
								"\x10 Select    \x13 Back",											//Options
								"\x13 Back",														//About
								"\x10 Select    \x12 View Code    \x13 Back",						//Select Cheats
								"\x13 Back",														//View Cheat
								"\x10 Select    \x13 Back",											//Cheat Option
								"\x13 Back",														//View Details
								"\x10 Value Up  \x11 Value Down   \x13 Exit",						//Hex Editor
								};

/*
* HDD save list
*/
save_list_t hdd_saves = {
    .id = MENU_HDD_SAVES,
    .title = "HDD Saves",
    .list = NULL,
    .path = "",
    .ReadList = &ReadUserList,
    .ReadCodes = &ReadCodes,
    .UpdatePath = &update_hdd_path,
};

/*
* USB save list
*/
save_list_t usb_saves = {
    .id = MENU_USB_SAVES,
    .title = "USB Saves",
    .list = NULL,
    .path = "",
    .ReadList = &ReadUsbList,
    .ReadCodes = &ReadCodes,
    .UpdatePath = &update_usb_path,
};

/*
* Trophy list
*/
save_list_t trophies = {
    .id = MENU_TROPHIES,
    .title = "Trophies",
    .list = NULL,
    .path = "",
    .ReadList = &ReadTrophyList,
    .ReadCodes = &ReadTrophies,
    .UpdatePath = &update_trophy_path,
};

/*
* Online code list
*/
save_list_t online_saves = {
    .id = MENU_ONLINE_DB,
    .title = "Online Database",
    .list = NULL,
    .path = ONLINE_URL,
    .ReadList = &ReadOnlineList,
    .ReadCodes = &ReadOnlineSaves,
    .UpdatePath = &update_db_path,
};

/*
* User Backup code list
*/
save_list_t user_backup = {
    .id = MENU_USER_BACKUP,
    .title = "User Tools",
    .list = NULL,
    .path = "",
    .ReadList = &ReadBackupList,
    .ReadCodes = &ReadBackupCodes,
    .UpdatePath = NULL,
};

/*
* PS1 VMC list
*/
save_list_t vmc1_saves = {
    .id = MENU_PS1VMC_SAVES,
    .title = "PS1 Virtual Memory Card",
    .list = NULL,
    .path = "",
    .ReadList = &ReadVmc1List,
    .ReadCodes = &ReadVmc1Codes,
    .UpdatePath = &update_vmc_path,
};

/*
* PS2 VMC list
*/
save_list_t vmc2_saves = {
    .id = MENU_PS2VMC_SAVES,
    .title = "PS2 Virtual Memory Card",
    .list = NULL,
    .path = "",
    .ReadList = &ReadVmc2List,
    .ReadCodes = &ReadVmc2Codes,
    .UpdatePath = &update_vmc_path,
};

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

static void LoadImageFontTexture(const u8* rawData, uint16_t unicode, int idx)
{
	menu_textures[idx].size = LoadImageFontEntry(rawData, unicode, &menu_textures[idx].texture);
	copyTexture(idx);
}

// Used only in initialization. Allocates 64 mb for textures and loads the font
static void LoadTextures_Menu(void)
{
	texture_mem = tiny3d_AllocTexture(64*1024*1024); // alloc 64MB of space for textures (this pointer can be global)
	menu_textures = (png_texture *)calloc(TOTAL_MENU_TEXTURES, sizeof(png_texture));
	
	if(!texture_mem || !menu_textures)
	{
		LOG("ERROR: Failed to allocate texture memory or menu textures");
		return; // fail!
	}
	LOG("Texture memory allocated successfully: 64MB");
	LOG("Menu textures structure allocated: %d textures", TOTAL_MENU_TEXTURES);
	
	LOG("Resetting font system...");
	ResetFont();
	LOG("Font system reset");
	
	LOG("Adding bitmap fonts...");
	free_mem = (u32 *) AddFontFromBitmapArray((u8 *) data_font_Adonais, (u8 *) texture_mem, 0x20, 0x7e, 32, 31, 1, BIT7_FIRST_PIXEL);
	free_mem = (u32 *) AddFontFromBitmapArray((u8 *) console_font_10x20, (u8 *) free_mem, 0, 0xFF, 10, 20, 1, BIT7_FIRST_PIXEL);
	LOG("Bitmap fonts added successfully");
	
	LOG("Loading TTF fonts...");
	TTFUnloadFont();
	TTFLoadFont(0, "/dev_flash/data/font/SCE-PS3-SR-R-LATIN2.TTF", NULL, 0);
	TTFLoadFont(1, "/dev_flash/data/font/SCE-PS3-DH-R-CGB.TTF", NULL, 0);
	TTFLoadFont(2, "/dev_flash/data/font/SCE-PS3-SR-R-JPN.TTF", NULL, 0);
	TTFLoadFont(3, "/dev_flash/data/font/SCE-PS3-YG-R-KOR.TTF", NULL, 0);
	LOG("TTF fonts loaded successfully");

	free_mem = (u32*) init_ttf_table((u16*) free_mem);
	LOG("TTF table initialized");
	
	set_ttf_window(0, 0, 848, 512, WIN_SKIP_LF);
	LOG("TTF window set");

	//Load only background image
	LOG("Loading background image (bgimg.jpg)...");
	load_menu_texture(bgimg, jpg);
	LOG("Background image loaded successfully");
	
	// Initialize icon texture buffer
	LOG("Initializing icon texture buffer...");
	menu_textures[icon_png_file_index].buffer = free_mem;
	menu_textures[icon_png_file_index].size = 1;
	menu_textures[icon_png_file_index].texture.height = 176;
	menu_textures[icon_png_file_index].texture.pitch = (320*4);
	menu_textures[icon_png_file_index].texture.bmp_out = calloc(320 * 176, sizeof(u32));
	LOG("Icon texture buffer initialized");

	LOG("Copying texture data...");
	copyTexture(icon_png_file_index);
	free_mem = (u32*) menu_textures[icon_png_file_index].buffer;
	LOG("Texture data copied");

	u32 tBytes = free_mem - texture_mem;
	LOG("LoadTextures_Menu() :: Allocated %db (%.02fkb, %.02fmb) for textures", tBytes, tBytes / (float)1024, tBytes / (float)(1024 * 1024));
	LOG("LoadTextures_Menu() - Texture loading completed successfully");
}

static void LoadSounds(void)
{
	LOG("LoadSounds() - Starting sound system initialization...");
	
	//Initialize SPU
	u32 entry = 0;
	u32 segmentcount = 0;
	sysSpuSegment* segments;
	
	LOG("Initializing SPU system...");
	sysSpuInitialize(6, 5);
	LOG("SPU system initialized");
	
	LOG("Creating SPU raw...");
	sysSpuRawCreate(&spu, NULL);
	LOG("SPU raw created");
	
	LOG("Getting SPU ELF information...");
	sysSpuElfGetInformation(spu_soundmodule_bin, &entry, &segmentcount);
	LOG("SPU ELF information retrieved: entry=%d, segments=%d", entry, segmentcount);
	
	size_t segmentsize = sizeof(sysSpuSegment) * segmentcount;
	LOG("Allocating SPU segments: %d bytes", segmentsize);
	segments = (sysSpuSegment*)memalign(128, SPU_SIZE(segmentsize)); // must be aligned to 128 or it break malloc() allocations
	memset(segments, 0, segmentsize);
	LOG("SPU segments allocated and cleared");

	LOG("Getting SPU ELF segments...");
	sysSpuElfGetSegments(spu_soundmodule_bin, segments, segmentcount);
	LOG("SPU ELF segments retrieved");
	
	LOG("Importing SPU image...");
	sysSpuImageImport(&spu_image, spu_soundmodule_bin, 0);
	LOG("SPU image imported");
	
	LOG("Loading SPU raw image...");
	sysSpuRawImageLoad(spu, &spu_image);
	LOG("SPU raw image loaded");
	
	inited |= INITED_SPU;
	LOG("SPU initialization completed");
	LOG("Initializing sound library...");
	if(SND_Init(spu)==0)
	{
		inited |= INITED_SOUNDLIB;
		LOG("Sound library initialized successfully");
	} else {
		LOG("ERROR: Failed to initialize sound library");
	}
	
	// Initialize audio context but don't play music
	LOG("Creating XMP audio context...");
	xmp = xmp_create_context();
	if (xmp)
	{
		inited |= INITED_AUDIOPLAYER;
		LOG("XMP audio context created successfully");
	} else {
		LOG("ERROR: Failed to create XMP audio context");
	}

	LOG("Pausing audio...");
	SND_Pause(1);
	LOG("Audio paused");
	
	LOG("LoadSounds() - Sound system initialization completed");
}

void update_usb_path(char* path)
{
	if (apollo_config.usb_dev < MAX_USB_DEVICES)
	{
		sprintf(path, USB_PATH, apollo_config.usb_dev);
		return;
	}

	if (apollo_config.usb_dev == MAX_USB_DEVICES)
	{
		sprintf(path, FAKE_USB_PATH);
		return;
	}

	for (int i = 0; i < MAX_USB_DEVICES; i++)
	{
		sprintf(path, USB_PATH, i);

		if (dir_exists(path) == SUCCESS)
			return;
	}

	sprintf(path, FAKE_USB_PATH);
	if (dir_exists(path) == SUCCESS)
		return;

	path[0] = 0;
}

void update_hdd_path(char* path)
{
	sprintf(path, USER_PATH_HDD, apollo_config.user_id);
}

void update_trophy_path(char* path)
{
	sprintf(path, TROPHY_PATH_HDD, apollo_config.user_id);
}

void update_db_path(char* path)
{
	if (apollo_config.online_opt)
	{
		sprintf(path, "%s%016lX/", apollo_config.ftp_url, apollo_config.account_id);
		return;
	}

	strcpy(path, apollo_config.save_db);
}

void update_vmc_path(char* path)
{
	if (file_exists(path) == SUCCESS)
		return;

	path[0] = 0;
}

static void registerSpecialChars(void)
{
	// No special characters needed for simple background
}

// Simple background drawing function
static void drawSimpleBackground(void)
{
	LOG("drawSimpleBackground() - Starting background drawing...");
	
	// Draw background (same as Apollo)
	LOG("Drawing background 2D...");
	DrawBackground2D(0xFFFFFFFF);
	LOG("Background 2D drawn successfully");
	
	// Draw background texture (same as Apollo)
	LOG("Drawing background texture...");
	DrawBackgroundTexture(0, 0xFF);
	LOG("Background texture drawn successfully");
	
	LOG("drawSimpleBackground() - Background drawing completed");
}

/*
	Program start - Hello World Version with Detailed Logging
*/
s32 main(s32 argc, const char* argv[])
{
	LOG("=== INFINITY INIT START ===");
	LOG("Main function called with %d arguments", argc);
	
#ifdef APOLLO_ENABLE_LOGGING
	LOG("Initializing debug logger...");
	dbglogger_init();
	dbglogger_failsafe("9999");
	LOG("Debug logger initialized successfully");
#endif

	LOG("Initializing HTTP system...");
	http_init();
	LOG("HTTP system initialized");

	LOG("Initializing Tiny3D graphics system...");
	tiny3d_Init(1024*1024);
	tiny3d_UserViewport(1, 0, 0, // 2D position
		(float) (Video_Resolution.width / 848.0f),  (float) (Video_Resolution.height / 512.0f),   // 2D scale
		(float) (Video_Resolution.width / 1920.0f), (float) (Video_Resolution.height / 1080.0f)); // 3D scale
	LOG("Tiny3D initialized successfully");

	LOG("Initializing PS3 controller system...");
	ps3PadInit();
	LOG("PS3 controller system initialized");
	
	LOG("Loading system modules...");
	sysModuleLoad(SYSMODULE_PNGDEC);
	sysModuleLoad(SYSMODULE_JPGDEC);
	LOG("System modules loaded successfully");

	// Register exit callback
	LOG("Registering exit callback...");
	if(sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sys_callback, NULL)==0) {
		inited |= INITED_CALLBACK;
		LOG("Exit callback registered successfully");
	} else {
		LOG("ERROR: Failed to register exit callback");
	}
	
	// Load textures and fonts (required for text rendering)
	LOG("Loading textures and fonts...");
	LoadTextures_Menu();
	LOG("Textures and fonts loaded successfully");

	// Load sounds (optional, but keeps compatibility)
	LOG("Loading sounds...");
	LoadSounds();
	LOG("Sounds loaded successfully");

	// Unpack application data on first run
	LOG("Checking for application data...");
	if (file_exists(APOLLO_LOCAL_CACHE "appdata.zip") == SUCCESS)
	{
		LOG("Found appdata.zip, unpacking...");
		clean_directory(APOLLO_DATA_PATH, "");
		unzip_app_data(APOLLO_LOCAL_CACHE "appdata.zip");
		LOG("Application data unpacked successfully");
	} else {
		LOG("No appdata.zip found, skipping unpack");
	}

	// Load application settings
	LOG("Loading application settings...");
	load_app_settings(&apollo_config);
	LOG("Application settings loaded successfully");

	LOG("Creating temporary directories...");
	mkdirs(APOLLO_TMP_PATH);
	LOG("Temporary directories created");
	
	if (apollo_config.dbglog)
	{
		LOG("Initializing file logger to /dev_hdd0/tmp/apollo.log");
		dbglogger_init_mode(FILE_LOGGER, "/dev_hdd0/tmp/apollo.log", 0);
		LOG("File logger initialized");
	}

	LOG("Saving XML owner file...");
	save_xml_owner(APOLLO_PATH OWNER_XML_FILE);
	LOG("XML owner file saved");

	// Set PFD keys from loaded settings
	LOG("Setting up PFD keys...");
	pfd_util_setup_keys();
	LOG("PFD keys setup completed");

	// Setup font system
	LOG("Setting up font system...");
	SetExtraSpace(5);
	SetCurrentFont(font_adonais_regular);
	LOG("Font system setup completed");

	// Register special characters (required for font system)
	LOG("Registering special characters...");
	registerSpecialChars();
	LOG("Special characters registered");

	// Initialize menu options (required for some functions)
	LOG("Initializing menu options...");
	initMenuOptions();
	LOG("Menu options initialized");

	// Show welcome dialog
	LOG("Showing welcome dialog...");
	show_message("Welcome to Apollo Save Tool!\n\nThis is a template message.\nPress OK to continue.");
	LOG("Welcome dialog shown");

	// Initialize custom log file
	LOG("Initializing custom log file: /dev_hdd0/tmp/infinityinit.log");
	dbglogger_init_mode(FILE_LOGGER, "/dev_hdd0/tmp/infinityinit.log", 0);
	LOG("Custom log file initialized successfully");

	LOG("=== ENTERING MAIN LOOP ===");
	LOG("Starting main rendering loop...");

	// Main loop - Simple background only
	while (!close_app)
	{       
		// Clear screen
		tiny3d_Clear(0xff000000, TINY3D_CLEAR_ALL);

		// Enable alpha Test
		tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);

		// Enable alpha blending.
		tiny3d_BlendFunc(1, TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
			TINY3D_BLEND_FUNC_SRC_ALPHA_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_SRC_ALPHA,
			TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);
		
		// change to 2D context (remember you it works with 848 x 512 as virtual coordinates)
		tiny3d_Project2D();

		// Draw simple background
		LOG("Main loop - Drawing simple background...");
		drawSimpleBackground();
		LOG("Main loop - Simple background drawn");
		
		// Test text rendering
		LOG("Main loop - Setting up text rendering...");
		SetFontSize(APP_FONT_SIZE_TITLE);
		SetCurrentFont(font_adonais_regular);
		SetFontAlign(FONT_ALIGN_SCREEN_CENTER);
		SetFontColor(APP_FONT_COLOR, 0);
		LOG("Main loop - Drawing test text...");
		DrawString(0, 200, "Test Text");
		SetFontAlign(FONT_ALIGN_LEFT);
		LOG("Main loop - Test text drawn");
		
		// Handle controller input
		LOG("Main loop - Reading controller input...");
		readPad(0);
		LOG("Main loop - Controller input read");
		
		// Check if X button is pressed to exit
		if (paddata[0].BTN_CROSS)
		{
			LOG("X button pressed - exiting application");
			close_app = 1;
		}
		
		// Update display
		LOG("Main loop - Flipping display...");
		tiny3d_Flip();
		LOG("Main loop - Display flipped successfully");
	}

	LOG("=== EXITING MAIN LOOP ===");
	LOG("Application cleanup starting...");

	// Cleanup and exit
	LOG("Releasing all resources...");
	release_all();
	LOG("All resources released");
	
	if (file_exists("/dev_hdd0/mms/db.err") == SUCCESS)
	{
		LOG("db.err file found - rebooting system");
		sys_reboot();
	}

	LOG("=== INFINITY INIT END ===");
	LOG("Application terminated successfully");

	return 0;
}
