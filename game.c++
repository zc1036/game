
#include <memory>
#include <array>
#include <SDL2/SDL.h>
#include <map>
#include <random>

//Screen dimension constants
const int SCREEN_WIDTH = 1024;
const int SCREEN_HEIGHT = 1024;

using window_ptr = std::unique_ptr<SDL_Window, decltype(SDL_DestroyWindow)*>;
using render_ptr = std::unique_ptr<SDL_Renderer, decltype(SDL_DestroyRenderer)*>;

window_ptr init() {
  //Initialize SDL
  if ( SDL_Init( SDL_INIT_VIDEO ) < 0 ) {
    printf( "SDL could not initialize! SDL Error: %s\n", SDL_GetError() );
    return { nullptr, nullptr };
  } else {
    if( !SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "1" ) ) {
      printf( "Warning: Linear texture filtering not enabled!" );
    }

    //Create window
    window_ptr window(SDL_CreateWindow( "SDL Tutorial", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN ),
                      SDL_DestroyWindow);
    if ( !window ) {
      printf( "Window could not be created! SDL Error: %s\n", SDL_GetError() );
      return { nullptr, nullptr };
    }

    return window;
  }
}

struct agent {
  int x_pos = 0, y_pos = 0;

  int max_stamina = 500;
  int stamina = 100;

  int oxygen = 100;
  int heat = 100;

  enum action {
    action_nothing,
    action_movenorth,
    action_movesouth,
    action_moveeast,
    action_movewest,
  };
};

using input_t = uint16_t;

constexpr input_t bit(const int n) {
  return (input_t)1 << n;
}

namespace mask {

const input_t satiated = bit(0);

const input_t north_blue = bit(1);
const input_t north_red = bit(2);
const input_t west_blue = bit(3);
const input_t west_red = bit(4);
const input_t east_blue = bit(5);
const input_t east_red = bit(6);
const input_t south_blue = bit(7);
const input_t south_red = bit(8);

const input_t stamina_low = bit(9);
const input_t stamina_verylow = bit(10);

const input_t underwater = bit(11);
const input_t cold = bit(12);

const input_t oxygen_low = bit(13);
const input_t oxygen_verylow = bit(14);

const input_t very_satiated = bit(15);

}

enum worldent {
  world_grass = 1,
  world_water = 2,
  world_cold = 4,
  world_food = 8,
  world_cactus = 16
};

const int world_width = 256;
const int world_height = 256;

using world = std::map<std::pair<int, int>, worldent>;

void agent_move(agent& a, int delta_ns, int delta_ew) {
  int new_ew_pos = (a.x_pos + delta_ew) % world_width;
  int new_ns_pos = (a.y_pos + delta_ns) % world_height;

  if (new_ns_pos < 0) {
    new_ns_pos = world_height + new_ns_pos;
  }

  if (new_ew_pos < 0) {
    new_ew_pos = world_width + new_ew_pos;
  }

  a.x_pos = new_ew_pos;
  a.y_pos = new_ns_pos;
}

worldent world_getent(const world& m, int x, int y) {
  const auto it = m.find(std::make_pair(x, y));

  if (it == std::end(m)) {
    return world_grass;
  }

  return it->second;
}

uint32_t worldent_color(const worldent we) {
  if (world_food & we) {
    return 0xfcba03;
  }

  if (world_water & we) {
    return 0x0000FF;
  }

  if (world_cold & we) {
    return 0xadd8e6;
  }

  if (world_grass & we) {
    return 0xffffff;
  }

  if (world_cactus & we) {
    return 0xFF0000;
  }

  return 0;
}

void world_draw(const world& m, SDL_Renderer* const renderer) {
  const int x_ratio = SCREEN_WIDTH / world_width;
  const int y_ratio = SCREEN_HEIGHT / world_height;

  static_assert(x_ratio > 0);
  static_assert(y_ratio > 0);

  for (int x = 0; x < world_width; ++x) {
    for (int y = 0; y < world_height; ++y) {
      const worldent we = world_getent(m, x, y);

      const uint32_t color = worldent_color(we);

      const int r = (color >> 16) & 0xff;
      const int g = (color >> 8) & 0xff;
      const int b = color & 0xff;

      SDL_Rect outlineRect = { x * x_ratio, y * y_ratio, x_ratio, y_ratio };

      SDL_SetRenderDrawColor( renderer, r, g, b, 0xFF );        
      SDL_RenderFillRect( renderer, &outlineRect );
    }
  }
}

void runtick(world& m, agent& a, agent::action act) {
  switch (act) {
  case agent::action_nothing:
    break;
  case agent::action_movenorth:
    agent_move(a, 1, 0);
    break;
  case agent::action_movesouth:
    agent_move(a, -1, 0);
    break;
  case agent::action_moveeast:
    agent_move(a, 0, 1);
    break;
  case agent::action_movewest:
    agent_move(a, 0, -1);
    break;
  } 
}

void randomize_world(world& w) {
  w.clear();

  std::random_device rd;  //Will be used to obtain a seed for the random number engine
  std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> dis(0, world_width - 1);
 
  for (int n = 0; n < 100; ++n) {
    int x = dis(gen), y = dis(gen);

    w[{ x, y }] = world_cactus;
  }

  for (int n = 0; n < 240; ++n) {
    int x = dis(gen), y = dis(gen);

    w[{ x, y }] = world_food;
  }
}

int main( int argc, char* args[] )
{
  {
    //The window we'll be rendering to
    auto gWindow = init();

    if (!gWindow) {
      return 1;
    }
	
    //Current displayed image
    SDL_Surface* gCurrentSurface = NULL;

    render_ptr gRenderer { SDL_CreateRenderer( gWindow.get(), -1, SDL_RENDERER_ACCELERATED ),
                           SDL_DestroyRenderer };

    //The surface contained by the window
    SDL_Surface* gScreenSurface = SDL_GetWindowSurface( gWindow.get() );

    //Main loop flag
    bool quit = false;

    //Event handler
    SDL_Event e;

    world w;

    randomize_world(w);

    w[{ 30, 2 }] = world_cactus;

    //While application is running
    while( !quit ) {
      SDL_SetRenderDrawColor( gRenderer.get(), 0xff, 0xff, 0xff, 0xff );
      SDL_RenderClear( gRenderer.get() );

      world_draw(w, gRenderer.get());

      SDL_RenderPresent(gRenderer.get());

      //Update the surface
      SDL_UpdateWindowSurface( gWindow.get() );

      //Handle events on queue
      while( SDL_PollEvent( &e ) != 0 ) {
        //User requests quit
        if( e.type == SDL_QUIT ) {
          quit = true;
        }
        //User presses a key
        else if( e.type == SDL_KEYDOWN ) {
          printf("got him\n");
          randomize_world(w);
        }
      }
    }
  }

  //Free resources and close SDL
  SDL_Quit();

  return 0;
}
