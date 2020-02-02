
#include <iostream>
#include <memory>
#include <array>
#include <map>
#include <vector>
#include <random>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

//Screen dimension constants
const int SCREEN_WIDTH = 1024;
const int SCREEN_HEIGHT = 1024;

using window_ptr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
using render_ptr = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
using font_ptr = std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)>;

window_ptr init() {
  //Initialize SDL
  if ( SDL_Init( SDL_INIT_VIDEO ) < 0 ) {
    printf( "SDL could not initialize! SDL Error: %s\n", SDL_GetError() );
    return { nullptr, nullptr };
  }

  
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

  if (TTF_Init() < 0) {
    printf("TTF init failed");
    return { nullptr, nullptr };
  }

  return window;
}

using input_t = uint16_t;

constexpr input_t bit(const int n) {
  return (input_t)1 << n;
}

namespace input_mask {

const input_t satiated = bit(0);

const input_t front_fruit = bit(1);
const input_t front_cactus = bit(2);
const input_t left_fruit = bit(3);
const input_t left_cactus = bit(4);
const input_t right_fruit = bit(5);
const input_t right_cactus = bit(6);

const input_t unused1 = bit(7);
const input_t unused2 = bit(8);

const input_t stamina_low = bit(9);
const input_t stamina_verylow = bit(10);

const input_t underwater = bit(11);
const input_t snow = bit(12);

const input_t oxygen_low = bit(13);
const input_t oxygen_verylow = bit(14);

const input_t very_satiated = bit(15);

const int num_active_inputs = 14; // 16 - 2 for unused1 and 2

// This mask is applied after all calculations on the input, so that they always
// get set to zero even if they're inverted or whatever. This is so that the
// activation function sums only the bits that are useful.
const size_t dead_inputs_mask = ~0ull ^ unused1 ^ unused2;

}

struct general_agent {
  enum direction {
    direction_north,
    direction_south,
    direction_east,
    direction_west
  };

  enum action {
    action_nothing = 1,
    action_moveforward = 2,
    action_movebackward = 4,
    action_moveleft = 8,
    action_moveright = 16,
  };
};

template<typename Input, typename Output, int Nodes>
struct andxor_nn_layer {
  static const size_t layersize = Nodes;

  Input and_mask[Nodes] = { };
  Input xor_mask[Nodes] = { };

  // Thresholds for each node
  int threshold[Nodes] = { };
  
  Output output;
};

struct perceptron_agent : general_agent {
  int x_pos = 0, y_pos = 0;

  int max_stamina = 300;
  int stamina = 100;

  int max_oxygen = 100;
  int oxygen = 100;

  int max_heat = 100;
  int heat = 100;

  direction facing = direction_north;

  // this is half the width of the square of vision that the agent sits in the
  // center of (i.e. it can see vision_distance units to the left, and to the
  // right, and forward)
  int vision_distance = 20;

  /* This agent has no hidden layers. */

  struct perceptron_nn {
    andxor_nn_layer<input_t, uint64_t, 5> layer1;
  } nn;

  /* Statistics */

  int total_fruit_eaten = 0;
};

using agent = perceptron_agent;

void randomize_nn(perceptron_agent::perceptron_nn& nn) {
  std::random_device rand;

  char rndmem[sizeof(nn.layer1)] = { };

  for (char& c : rndmem) {
    c = rand() & 0xff;
  }

  memcpy(&nn.layer1, rndmem, sizeof(rndmem));

  // All the thresholds are the same
  for (auto& threshold : nn.layer1.threshold) {
    threshold = input_mask::num_active_inputs / 2;
  }
}

// Choose a random bit in a mask
agent::action choose_random_action(const unsigned int bitset) {
  if (!bitset) {
    std::cout << "bitset empty, this is a bug\n";
    return agent::action_nothing;
  }

  const int bitcount = __builtin_popcount(bitset);

  if (bitcount == 1) {
    return (agent::action)bitset;
  }

  std::random_device rd;  //Will be used to obtain a seed for the random number engine
  std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> dis(0, bitcount - 1);

  int choice = dis(gen);
  unsigned int mask = 1;

  while (true) {
    if (bitset & mask) {
      if (choice == 0) {
        break;
      }

      --choice;
    }

    mask <<= 1;
  }

  return (agent::action)mask;
}

agent::action evaluate_nn(perceptron_agent::perceptron_nn& nn,
                          const input_t input) {
  uint64_t output = 0;

  for (size_t i = 0; i < nn.layer1.layersize; ++i) {
    const auto result = (nn.layer1.and_mask[i] & input) ^ nn.layer1.xor_mask[i];
    const auto bitcount = __builtin_popcount(result & input_mask::dead_inputs_mask);

    uint64_t active = 0;

    if (bitcount >= nn.layer1.threshold[i]) {
      active = 1;
    }

    output |= active << i;
  }

  nn.layer1.output = output;

  return choose_random_action(output);
}

enum worldent {
  world_grass = 1,
  world_water = 2,
  world_snow = 4,
  world_fruit = 8,
  world_cactus = 16
};

const worldent world_terrain_mask = (worldent)(world_grass | world_water | world_snow);

const int world_width = 256;
const int world_height = 256;

using world = std::map<std::pair<int, int>, worldent>;

struct statistics {
  int ticks = 0;
  int longest_life = 0;
  int most_fruit_eaten = 0;

  int deaths_by_cold = 0;
  int deaths_by_drowning = 0;
  int deaths_by_cactus = 0;
  int deaths_by_exhaustion = 0;
  int deaths_by_gluttony = 0;
};

struct point_with_color {
  int x, y;
  uint32_t color;
};

input_t calculate_vision_input(const world& w, const agent& a, std::vector<point_with_color>& visible_points) {
  const int x = 0;
  const int y = 1;

  const int north_deltas[2] = { 0, -1 };
  const int south_deltas[2] = { 0, 1 };
  const int east_deltas[2] = { 1, 0 };
  const int west_deltas[2] = { -1, 0 };

  const int* forward_deltas = nullptr, * left_deltas = nullptr, * right_deltas = nullptr;
  int forward_pos[2] { }, left_pos[2] { }, right_pos[2] { };

  switch (a.facing) {
  case agent::direction_north:
    forward_pos[x] = a.x_pos; forward_pos[y] = a.y_pos - 1;
    forward_deltas = north_deltas; right_deltas = east_deltas; left_deltas = west_deltas;
    break;
  case agent::direction_south:
    forward_pos[x] = a.x_pos; forward_pos[y] = a.y_pos + 1;
    forward_deltas = south_deltas; right_deltas = west_deltas; left_deltas = east_deltas;
    break;
  case agent::direction_east:
    forward_pos[x] = a.x_pos + 1; forward_pos[y] = a.y_pos;
    forward_deltas = east_deltas; right_deltas = south_deltas; left_deltas = north_deltas;
    break;
  case agent::direction_west:
    forward_pos[x] = a.x_pos - 1; forward_pos[y] = a.y_pos;
    forward_deltas = west_deltas; right_deltas = north_deltas; left_deltas = south_deltas;
    break;
  }

  for (int radius = 0; radius < a.vision_distance; ++radius) {
    const int forward_vision_width = 3 + 2*radius;
    const int left_vision_width = 1 + radius;
    const int right_vision_width = 1 + radius;

    // perform forward vision (3 + 2i tiles wide)
    for (int tile = 0; tile < forward_vision_width; ++tile) {
      const int tilepos[2] = {
        // This is a way of saying "iff we moved on the y axis to advance
        // forward vision (i.e. north or south), then we want to move on the x
        // axis to scan tiles for vision; and vice versa.
        forward_pos[x] + (forward_deltas[y] * forward_deltas[y]) * (-(forward_vision_width / 2) + tile),
        forward_pos[y] + (forward_deltas[x] * forward_deltas[x]) * (-(forward_vision_width / 2) + tile),
      };

      visible_points.push_back({ .x = tilepos[x],
                                 .y = tilepos[y],
                                 .color = 170 - 4*radius  });
    }

    forward_pos[x] += forward_deltas[x]; forward_pos[y] += forward_deltas[y];
    left_pos[x] += left_deltas[x]; left_pos[y] += left_deltas[y];
    right_pos[x] += right_deltas[x]; right_pos[y] += right_deltas[y];
  }
}

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
  if (world_fruit & we) {
    return 0xfcba03;
  }

  if (world_cactus & we) {
    return 0xdd0000;
  }

  if (world_water & we) {
    return 0x0000FF;
  }

  if (world_snow & we) {
    return 0xadd8e6;
  }

  if (world_grass & we) {
    return 0x00ff00;
  }

  return 0;
}

void world_draw(const agent& a, const world& w, SDL_Renderer* const renderer) {
  const int x_ratio = SCREEN_WIDTH / world_width;
  const int y_ratio = SCREEN_HEIGHT / world_height;

  static_assert(x_ratio > 0);
  static_assert(y_ratio > 0);

  for (int x = 0; x < world_width; ++x) {
    for (int y = 0; y < world_height; ++y) {
      const worldent we = world_getent(w, x, y);

      const uint32_t color = worldent_color(we);

      const int r = (color >> 16) & 0xff;
      const int g = (color >> 8) & 0xff;
      const int b = color & 0xff;

      const SDL_Rect outlineRect = { x * x_ratio, y * y_ratio, x_ratio, y_ratio };

      SDL_SetRenderDrawColor(renderer, r, g, b, 0xff);
      SDL_RenderFillRect(renderer, &outlineRect);
    }
  }

  const SDL_Rect outlineRect = { a.x_pos * x_ratio, a.y_pos * y_ratio, x_ratio, y_ratio };
  SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);
  SDL_RenderFillRect(renderer, &outlineRect);

  std::vector<point_with_color> visible_points;
  calculate_vision_input(w, a, visible_points);

  for (const auto point : visible_points) {
    const SDL_Rect outlineRect = { point.x * x_ratio, point.y * y_ratio, x_ratio, y_ratio };
    //SDL_SetRenderDrawColor(renderer, (point.color >> 16) & 0xff, (point.color >> 8) & 0xff, point.color & 0xff, 0xff);
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, point.color);
    SDL_RenderFillRect(renderer, &outlineRect);
  }
}

agent::direction calc_new_direction(const agent::direction facing,
                                    const agent::action a)
{
  const agent::direction vector_to_direction[3][3] = {
    {           {},          agent::direction_north,            {}          },
    { agent::direction_west,            {},           agent::direction_east },
    {           {},          agent::direction_south,            {}          }
  };

  const int x = 0;
  const int y = 1;
  int vec[2] = { 0, 0 };

  switch (facing) {
  case agent::direction_north:
    vec[x] = 0; vec[y] = -1;
    break;
  case agent::direction_south:
    vec[x] = 0; vec[y] = 1;
    break;
  case agent::direction_east:
    vec[x] = 1; vec[y] = 0;
    break;
  case agent::direction_west:
    vec[x] = -1; vec[y] = 0;
    break;
  }

  switch (a) {
  case agent::action_moveforward:
    return facing;

  case agent::action_moveleft:
    // rotate 90 degrees
    std::swap(vec[x], vec[y]);
    vec[x] *= -1;
    // intentional fallthrough (turning left is like turning right and then
    // going backwards)
      
  case agent::action_movebackward:
    vec[x] *= -1;
    vec[y] *= -1;
    break;

  case agent::action_moveright:
    // rotate 90 degrees
    std::swap(vec[x], vec[y]);
    vec[x] *= -1;
    break;
  }

  return vector_to_direction[1 + vec[y]][1 + vec[x]];
}

bool runtick(statistics& s, world& w, agent& a, const agent::action act) {
  ++s.ticks;

  /* Calculate the agent's new position and move it there */

  const agent::direction new_direction = calc_new_direction(a.facing, act);

  const int direction_delta[][2] = {
    [agent::direction_north] = { -1, 0 },
    [agent::direction_south] = { 1, 0 },
    [agent::direction_east] = { 0, 1 },
    [agent::direction_west] = { 0, -1 },
  };

  switch (act) {
  case agent::action_nothing:
    break;
  case agent::action_moveforward:
  case agent::action_moveleft:
  case agent::action_moveright:
    a.facing = new_direction;
    // intentional fallthrough; moving backwards doesn't change the direction he's facing
  case agent::action_movebackward:
    agent_move(a, direction_delta[new_direction][0], direction_delta[new_direction][1]);
    break;
  }

  const worldent ent = world_getent(w, a.x_pos, a.y_pos);

  /* Update the agent's attributes */

  a.stamina -= 1;

  if (ent & world_fruit) {
    if (ent & world_snow) {
      // cold fruit is worth less
      a.stamina += 10;
    } else if (ent & world_water) {
      // wet fruit is worth more
      a.stamina += 40;
    } else {
      a.stamina += 25;
    }

    ++a.total_fruit_eaten;
  }

  if (ent & world_snow) {
    a.heat -= 1;
  } else {
    // every tick outside the snow warms him up
    a.heat = std::min(a.heat + 2, a.max_heat);
  }

  if (ent & world_water) {
    a.oxygen -= 1;
  } else {
    a.oxygen = a.max_oxygen;
  }

  /* Check if the agent is alive */

  bool dead = false;

  if (ent & world_cactus) {
    // dead :(
    dead = true;
    ++s.deaths_by_cactus;
  } else if (a.stamina <= 0) {
    dead = true;
    ++s.deaths_by_exhaustion;
  } else if (a.stamina > a.max_stamina) {
    dead = true;
    ++s.deaths_by_gluttony;
  } else if (a.oxygen <= 0) {
    dead = true;
    ++s.deaths_by_drowning;
  } else if (a.heat <= 0) {
    dead = true;
    ++s.deaths_by_cold;
  }

  /* Remove fruit from the map */

  if ((ent & world_terrain_mask) != ent) {
    if ((ent & world_terrain_mask) == 0 || (ent & world_terrain_mask) == world_grass) {
      w.erase({ a.x_pos, a.y_pos });
    } else {
      w[{ a.x_pos, a.y_pos }] = (worldent)(ent & world_terrain_mask);
    }
  }

  /* Report aliveness to caller */

  return !dead;
}

void randomize_world(world& w) {
  w.clear();

  std::random_device rd;  //Will be used to obtain a seed for the random number engine
  std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> dis(0, world_width - 1);
 
  for (int n = 0; n < 100; ++n) {
    int x = dis(gen), y = dis(gen);

    w[{ x, y }] = (worldent)(w[{ x, y }] | world_cactus);
  }

  for (int n = 0; n < 240; ++n) {
    int x = dis(gen), y = dis(gen);

    w[{ x, y }] = (worldent)(w[{ x, y }] | world_fruit);
  }
}

void get_text_and_rect(SDL_Renderer *renderer, int x, int y, const char *text,
                       TTF_Font *font, SDL_Texture **texture, SDL_Rect *rect) {
  int text_width;
  int text_height;
  SDL_Surface *surface;
  SDL_Color textColor = {255, 255, 255, 0};

  surface = TTF_RenderText_Solid(font, text, textColor);
  *texture = SDL_CreateTextureFromSurface(renderer, surface);
  text_width = surface->w;
  text_height = surface->h;
  SDL_FreeSurface(surface);
  rect->x = x;
  rect->y = y;
  rect->w = text_width;
  rect->h = text_height;
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

    SDL_SetRenderDrawBlendMode(gRenderer.get(), SDL_BLENDMODE_BLEND);

    //The surface contained by the window
    SDL_Surface* gScreenSurface = SDL_GetWindowSurface( gWindow.get() );

    font_ptr font = { TTF_OpenFont("font.ttf", 12), TTF_CloseFont };

    if (!font) {
      std::cout << SDL_GetError() << '\n';
      return 2;
    }

    //Main loop flag
    bool quit = false;

    std::unique_ptr<agent> a_ptr(new agent);
    agent& a = *a_ptr;

    randomize_nn(a.nn);

    a.x_pos = world_width / 2;
    a.y_pos = world_height / 2;

    statistics stats;

    world w;

    randomize_world(w);

    //While application is running
    while( !quit ) {
      SDL_SetRenderDrawColor( gRenderer.get(), 0xff, 0xff, 0xff, 0xff );
      SDL_RenderClear( gRenderer.get() );

      SDL_Rect rect1 { };
      SDL_Texture *texture1 = nullptr;
      get_text_and_rect(gRenderer.get(), 0, 0, "hello", font.get(), &texture1, &rect1);
      
      world_draw(a, w, gRenderer.get());

      SDL_RenderCopy(gRenderer.get(), texture1, NULL, &rect1);
      SDL_DestroyTexture(texture1);

      SDL_RenderPresent(gRenderer.get());

      //Update the surface
      SDL_UpdateWindowSurface( gWindow.get() );

      SDL_Event e { };

      //Handle events on queue
      while( SDL_PollEvent( &e ) != 0 ) {
        //User requests quit
        if( e.type == SDL_QUIT ) {
          quit = true;
        }
        //User presses a key
        else if( e.type == SDL_KEYDOWN ) {
          switch (e.key.keysym.sym) {
          case SDLK_f:
            runtick(stats, w, a, agent::action_moveforward);
            break;
          case SDLK_r:
            runtick(stats, w, a, agent::action_moveright);
            break;
          case SDLK_l:
            runtick(stats, w, a, agent::action_moveleft);
            break;
          case SDLK_b:
            runtick(stats, w, a, agent::action_movebackward);
            break;
          }
        }
      }
    }
  }

  //Free resources and close SDL
  TTF_Quit();
  SDL_Quit();

  return 0;
}
