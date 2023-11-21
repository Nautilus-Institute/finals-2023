#include "util.h"
#include "forward.h"
#include "bytecode.h"
#include "graphics.h"
#include "object.h"
#include <dirent.h>
#include <string.h>

#include "sha256.h"



World* g_world_inst = NULL;

Object* does_collide_with_any(uint16_t x, uint16_t y, uint16_t x2, uint16_t y2) {
  World* world = g_world_inst;
  LL_for_each(world->objects, node) {
    if (node == NULL) {
      continue;
    }
    Object* obj = (Object*)node->data;
    if (obj == NULL) {
      continue;
    }

    if (Object_check_collision(obj, world->L, x, y, x2, y2)) {
      return obj;
    }
  }
  return NULL;
}

World* World_new(lua_State *L, uint16_t width, uint16_t height) {
    assert(g_world_inst == NULL);
    World *this = calloc(sizeof(World), 1);
    this->L = L;
    this->width = width;
    this->height = height;
    g_world_inst = this;
    return this;
}

Object* World_get_object_from_uri(World *this, char* o_uri) {
  // meta://local/objects/table.me
  //data:meta/object;base64,
  vprintf("World_get_object_from_uri: %s\n", o_uri);

  char* uri = strdup(o_uri);
  char* scheme = strtok(uri, ":");
  if (strcmp(scheme, "data") == 0) {
    char* mime = strtok(NULL, ";");
    char* encoding = strtok(NULL, ",");
    char* data = strtok(NULL, "\0");
    size_t data_len = strlen(data);
    vprintf("mime: %s, encoding: %s, data: %s\n", mime, encoding, data);

    if (strcmp(encoding, "base64") == 0) {
      vprintf("Decoding base64\n");
      unsigned char* out = calloc(data_len,1);
      data_len = b64_decode(data, out, data_len);
      data = (char*)out;
      vprintf("Decoded %d bytes -> %s\n", data_len, out);
    } else {
      data = strdup(data);
    }

    if (strcmp(mime, "meta/object") == 0) {
      FILE* f = fmemopen(data, data_len, "r");
      Object* obj = Object_load(f);
      fclose(f);
      if (obj == NULL) {
        printf("Failed to load object\n");
        g_world_inst->status = "Failed to load object from data URI";
        return NULL;
      }
      g_world_inst->status = "Spawned object...";
      return obj;
    }

  } else if (strcmp(scheme, "file") == 0) {
    char* path = strtok(NULL, "\0");
    path--;
    path[0] = '.';
    // file://objects/tv.me
    vprintf("Loading object from file: %s\n", path);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
      printf("Failed to open file: %s\n", path);
      g_world_inst->status = "Failed to open object file";
      return NULL;
    }

    Object* obj = Object_load(f);
    fclose(f);

    if (obj == NULL) {
        printf("Failed to load object\n");
        g_world_inst->status = "Failed to load object from file";
        return NULL;
    }

    g_world_inst->status = "Spawned object...";
    return obj;
  } else {
    vvprintf("Finding existing object %s\n", o_uri);
    LL_for_each(this->object_registry, node) {
      Object* obj = (Object*)node->data;
      //printf("Comparing %s to %s\n", obj->uri, o_uri);
      if (strstr(obj->uri, o_uri) != 0) {
        vvprintf("Found object %s\n", o_uri);
        g_world_inst->status = "Spawned object...";
        return obj;
      }
    }
  }

  g_world_inst->status = "Could not find specified object";
  return NULL;
}

Object* World_spawn_object(World* this, char* uri, uint16_t x, uint16_t y) {
  Object* o = World_get_object_from_uri(this, uri);
  vprintf("World_spawn_object: %s\n", uri);

  if (o == NULL) {
    printf("Failed to find object for `%s`\n", uri);
    return NULL;
  }

  o = Object_init(this->L, o, x, y);

  if (o == NULL) {
    printf("Failed to init object for `%s`\n", uri);
    return NULL;
  }

  LL_add(&this->objects, LL_new(o));
  return o;
}

int L_set_status(lua_State *L) {
  veprintf("L_ask_user\n");
  vdumpstack(L);

  char* status = (char*)lua_tostring(L, -1);
  lua_pop(L, 1);

  if (strlen(status) == 0) {
    g_world_inst->status = NULL;
  } else {
    g_world_inst->status = strdup(status);
  }
  return 0;
}

int L_alert(lua_State *L) {
  veprintf("L_ask_user\n");
  vdumpstack(L);

  char* text = (char*)lua_tostring(L, -1);
  lua_pop(L, 1);

  erase();
  printw(text);
  printw("\n\nPress any key to continue");

  refresh();

  getch();

  erase();
  return 0;
}


int L_ask_user(lua_State *L) {
  veprintf("L_ask_user\n");
  vdumpstack(L);

  char* question = (char*)lua_tostring(L, -1);
  lua_pop(L, 1);

  erase();
  printw(question);

  refresh();

  echo();
  char* input = calloc(512, 1);
  size_t cap = 512;
  size_t input_len = 0;
  while(1) {
    int key = getch();
    if (key == 0) {
        exit(0);
        return 0;
    }
    if (key == '\n') {
      break;
    }
    if (key == KEY_BACKSPACE) {
      if (input_len > 0) {
        input_len--;
      }
      continue;
    }
    input[input_len++] = key;
    if (input_len >= cap - 32) {
      cap *= 2;
      if (cap > 0x1000) {
        printf("Input too long\n");
        break;
      }
      input = realloc(input, cap);
    }
  }
  input[input_len] = '\0';

  //getnstr(input, 512);

  printw(input);
  lua_pushstring(L, input);
  free(input);

  erase();
  noecho();


  return 1;
}

int L_object_get_touching(lua_State *L) {
  veprintf("L_player_get_touching\n");
  vdumpstack(L);

  int16_t y = lua_tointeger(L, -1);
  lua_pop(L, 1);
  veprintf("### y: %d\n", y);

  int16_t x = lua_tointeger(L, -1);
  lua_pop(L, 1);
  veprintf("### x: %d\n", x);

  if (x < 0) {
    x = 0;
  }
  if (y < 0) {
    y = 0;
  }

  Object* this = lua_get_object(L, -1);
  vprintf("### this: %p\n", this);

  // Check collision
  uint16_t x2 = x + this->width;
  uint16_t y2 = y + this->height;

  veprintf("Removal check: %d, %d, %d, %d\n", x, y, x2, y2);

  Object* target = does_collide_with_any(x, y, x2, y2);
  if (target == NULL) {
    veprintf("No collision\n");
    lua_pushnil(L);
  } else {
    Object_lua_load_self(target, L);
  }
  return 1;
}

int L_player_spawn_object(lua_State *L) {
  veprintf("L_world_spawn_object\n");
  vdumpstack(L);


  int16_t dir = lua_tointeger(L, -1);
  lua_pop(L, 1);

  char* uri = (char*)lua_tostring(L, -1);
  lua_pop(L, 1);
  vprintf("### uri: %s\n", uri);

  Object* this = lua_get_object(L, -1);
  veprintf("### this: %p\n", this);

  uint16_t player_x1 = this->x;
  uint16_t player_y1 = this->y;

  uint16_t player_x2 = player_x1 + this->width;
  uint16_t player_y2 = player_y1 + this->height;

  World* w = g_world_inst;
  Object* o = World_spawn_object(w, uri, player_x1, player_y1);
  if (o == NULL) {
    printf("Failed to spawn object\n");
    lua_pushnil(L);
    return 1;
  }

  // Move object until it doesn't overlap with the player
  for (int i=0; i<50; i++) {
    uint16_t x1 = o->x;
    uint16_t y1 = o->y;
    uint16_t x2 = x1 + o->width;
    uint16_t y2 = y1 + o->height;
    if (x1 >= player_x2 || x2 <= player_x1 || y1 >= player_y2 || y2 <= player_y1) {
      break;
    }
    if (dir == 0) {
      o->y--;
    } else if (dir == 1) {
      o->x++;
    } else if (dir == 2) {
      o->y++;
    } else if (dir == 3) {
      o->x--;
    }
  }


  Object_lua_load_self(o, L);
  Object_refresh_given_table(o, L);
  return 1;
}

int L_server_verify_authenticity(lua_State *L) {
  eprintf("Client is verifying the server. This is required behavior to be compliment with META standards and regulations\n");
  char* private_key = calloc(512,1);
  FILE* f = fopen("/flag","r");
  fread(private_key, 1, 512, f);
  char* nl = strchr(private_key, '\n');
  if (nl != NULL) {
    *nl = '\0';
  }
  // BUG
  //fclose(f);

  char* challenge = (char*)lua_tostring(L, -1);
  lua_pop(L, 1);

  size_t len = strlen(private_key) + strlen(challenge);

  char* buf = calloc(len+8,1);

  strcpy(buf, challenge);
  strcat(buf, private_key);

  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (uint8_t*)buf, len);

  dprintf("sha256(%s, %u)\n", buf, len);

  char* hash = calloc(SHA256_BLOCK_SIZE+8,1);
  sha256_final(&ctx, (uint8_t*)hash);

  char* hash_hex = (char*)hash_to_hex((uint8_t*)hash);

  lua_pushstring(L, hash_hex);
  free(hash);
  free(hash_hex);
  return 1;
}

int L_world_spawn_object(lua_State *L) {
  veprintf("L_world_spawn_object\n");
  vdumpstack(L);

  uint16_t y = lua_tointeger(L, -1);
  lua_pop(L, 1);
  vprintf("### y: %d\n", y);

  uint16_t x = lua_tointeger(L, -1);
  lua_pop(L, 1);
  vprintf("### x: %d\n", x);

  char* uri = (char*)lua_tostring(L, -1);
  lua_pop(L, 1);
  vprintf("### uri: %s\n", uri);

  World* w = g_world_inst;
  Object* o = World_spawn_object(w, uri, x, y);
  if (o == NULL) {
    printf("Failed to spawn object\n");
    lua_pushnil(L);
    return 0;
  }

  Object_lua_load_self(o, L);
  return 1;
}

void World_render(World *this) {

  Sprite screen;
  screen.width = this->width;
  screen.height = this->height;


  uint64_t w = screen.width;
  uint64_t h = screen.height;

  char data[w*h];
  //char* data = calloc(w*h, 1);

  memset(data, ' ', w*h);

  screen.data = (uint8_t*)data;

  char line_data[screen.width + 32];
  memset(line_data, 0, screen.width + 32);


 //Sprite screen = make_empty_sprite(this->width, this->height);


  // TODO layer sorting

  LL_for_each(this->objects, node) {
    Object *o = (Object*)node->data;
    Sprite o_s = o(render, this->L);

    Draw_overlay(screen, o_s, o->x, o->y);
  }

  for (uint16_t y = 0; y < screen.height; y++) {
    uint8_t* line = screen.data + (y * screen.width);
    memcpy(data, line, screen.width);
    data[screen.width] = '\0';
    printw(data);
    printw("\n");
  }

  //free(screen.data);
}

void World_handle_keypress(World* this, int key) {
  vprintf("World_handle_keypress: %d\n", key);
  LL_for_each(this->objects, node) {
    Object *o = (Object*)node->data;
    Object_handle_keypress(o, this->L, key);
  }
}

void load_natives(lua_State *L) {
  vprintf("Loading natives\n");
  char* path = "natives";
  DIR* dir = opendir(path);
  struct dirent* ent;

  for (int i = 0; (ent = readdir(dir)) != NULL; i++) {
    if (ent->d_type == DT_REG) {
      char* name = ent->d_name;

      char* full_path = calloc(strlen(path) + strlen(name) + 4, 1);
      sprintf(full_path, "%s/%s", path, name);

      vprintf("Loading native: %s\n", full_path);

      if (luaL_loadfile(L, full_path) == 0) {
        if (lua_pcall(L, 0, 0, 0) != 0) {
          printf("Error loading native: %s\n", full_path);
          printf("%s\n", lua_tostring(L, -1));
          exit(-1);
        }
      } else {
      // Print error
        printf("Error loading native: %s %s\n", full_path, lua_tostring(L, -1));
      }
      free(full_path);
    }
  }

  closedir(dir);

  // Set glogal "draw_overlay" function to the c function Draw_overlay
  lua_register(L, "C_Draw_overlay", L_draw_overlay);
  lua_register(L, "C_Object_move", L_object_move);
  lua_register(L, "C_Object_get_touching", L_object_get_touching);
  lua_register(L, "C_Object_refresh", L_object_refresh);
  lua_register(L, "C_Object_interact", L_object_interact);
  lua_register(L, "C_Ask_user", L_ask_user);
  lua_register(L, "C_Alert", L_alert);
  lua_register(L, "C_World_spawn_object", L_world_spawn_object);
  lua_register(L, "C_Player_spawn_object", L_player_spawn_object);
  lua_register(L, "C_Object_export", L_object_export);
  lua_register(L, "C_Object_remove", L_object_remove);
  lua_register(L, "C_Set_status", L_set_status);
  lua_register(L, "C_Server_verify_authenticity", L_server_verify_authenticity);

  vprintf("Done loading natives\n");
}
