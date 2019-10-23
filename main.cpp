#include <iostream>
#include <SDL.h>
#include "memory.h"
#include "io.h"
#include "cpu.h"

int main(void)
{
	SDL_Window *sdl_window;
	SDL_Surface *sdl_surface;
	int *pt;
	u8 r,g,b,a;

	// RAM 6MB
	Memory mem((u32)0x600000);
	pSUMOT::IO io(0x10000);

	CPU cpu(&mem);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		printf("SDL_Init() error\n");
		return 1;
	}

	/* Ubuntu 18.04.Xで起動時にdbusのエラーで落ちる場合は
	   以下(workaround)で起動する

	   DBUS_FATAL_WARNINGS=0 ./psumot

	   (参考)
	   https://bugs.launchpad.net/ubuntu/+source/libsdl2/+bug/1775067
	 */
	sdl_window = SDL_CreateWindow("hoge", 100, 100, 640, 480, 0);
	sdl_surface = SDL_GetWindowSurface(sdl_window);

	cpu.reset();
	for (int i = 0; i < 50000; i++) {
		cpu.exec();
	}

	printf("Bpp=%d\n", sdl_surface->format->BytesPerPixel);

	pt = (int *)sdl_surface->pixels;
	for (int i = 0; i < 10000; i++) {
		*pt++ = 0xaaaa;
	}

	SDL_UpdateWindowSurface(sdl_window);

	SDL_Delay(3000);

	pt = (int *)sdl_surface->pixels;
	for (int i = 0; i < 640*400/8; i++) {
//		r = mem.read8(0x80000000 + i);
		r = mem.read8(0xc0000 + i);
		g = mem.read8(0x80010000 + i);
		b = mem.read8(0x80020000 + i);
		a = mem.read8(0x80030000 + i);
		for (int j = 0; j < 8; j++) {
			if (r) *pt = 0xaaaa; else *pt = 0;
			pt++;
/*
			*pt++ = ((a & 0x80) << 24) + ((r & 0x80) << 16) + ((g & 0x80) << 8) + (b & 0x80);
			r <<= 1;
			g <<= 1;
			b <<= 1;
			a <<= 1;
*/
		}
	}

	SDL_UpdateWindowSurface(sdl_window);

	SDL_Delay(10000);

	SDL_Quit();

	return 0;

}
