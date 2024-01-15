#include <iostream>
#include <cstring> // for strcmp
#include <string>
#include <SDL.h>
#include "memory.h"
#include "io.h"
#include "cpu.h"
#include "event.h"

int main(int argc, char *argv[])
{
	SDL_Window *sdl_window;
	SDL_Surface *sdl_surface;
	int *pt;
	u8 r,g,b,a;
	bool use_video = true;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0) {
			use_video = false;
		} else {
			printf("usage:\n");
		}
	}

	// RAM 6MB
	Memory mem((u32)0x600000);
	pSUMOT::IO io(0x10000);

	CPU cpu(&mem);

	Event ev(&cpu);

	io.set_ev(&ev);

	cpu.reset();

	if (use_video && SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("SDL_Init(SDL_INIT_VIDEO) error\n");
		return 1;
	}
	if (SDL_Init(SDL_INIT_AUDIO) < 0) {
		printf("SDL_Init(SDL_INIT_AUDIO) error\n");
		return 1;
	}

	/* Ubuntu 18.04.Xで起動時にdbusのエラーで落ちる場合は
	   以下(workaround)で起動する

	   DBUS_FATAL_WARNINGS=0 ./psumot

	   (参考)
	   https://bugs.launchpad.net/ubuntu/+source/libsdl2/+bug/1775067
	 */
	if (use_video) {
		sdl_window = SDL_CreateWindow("hoge", 100, 100, 640, 480, 0);
		sdl_surface = SDL_GetWindowSurface(sdl_window);

		printf("Bpp=%d\n", sdl_surface->format->BytesPerPixel);
	}

	while (1) {
		cpu.remains_clks += 280000;
		cpu.exit_clks = 0;
		do {
			ev.check0();
			cpu.remains_clks = cpu.exec();
			ev.check();
		} while (cpu.remains_clks > 0);

		if (use_video) {

			pt = (int *)sdl_surface->pixels;

			for (int i = 0; i < 640*400/8; i++) {
#if 0
				mem.write8(0xcff83, 0x0);
				mem.write8(0xcff81, 0);
				b = mem.read8(0xc0000 + i);
				mem.write8(0xcff81, 0x40);
				r = mem.read8(0xc0000 + i);
				mem.write8(0xcff81, 0x80);
				g = mem.read8(0xc0000 + i);
				mem.write8(0xcff81, 0xc0);
				a = mem.read8(0xc0000 + i);
#else
				b = mem.read8(0x80000000 + i);
				r = mem.read8(0x80008000 + i);
				g = mem.read8(0x80010000 + i);
				a = mem.read8(0x80018000 + i);
#endif
				for (int j = 0; j < 8; j++) {
					// 各プレーンから1bitずつデータを取ってくる
					*pt++ = ((a & 0x80) << 24) + ((r & 0x80) << 16) + ((g & 0x80) << 8) + (b & 0x80);
					r <<= 1;
					g <<= 1;
					b <<= 1;
					a <<= 1;
				}
			}

			SDL_UpdateWindowSurface(sdl_window);
			SDL_Delay(10);
		}
	}
	SDL_Quit();

	return 0;
}
