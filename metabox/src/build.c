#include "world.h"
#include <dirent.h>
#include <string.h>

void compile_natives(lua_State *L) {
  // Iterate `./src/natives`
  printf("Compiling natives\n");
  char* path = "src/natives";
  DIR* dir = opendir(path);
  struct dirent* ent;

  for (int i = 0; (ent = readdir(dir)) != NULL; i++) {
    if (ent->d_type != DT_REG) {
      continue;
    }
    char* name = ent->d_name;
    char* ext = strrchr(name, '.');
    char* name_no_ext = calloc(strlen(name) - strlen(ext) + 1, 1);
    name_no_ext = strncpy(name_no_ext, name, strlen(name) - strlen(ext));

    if (strcmp(ext, ".lua") != 0) {
      continue;
    }
    char* full_path = calloc(strlen(path) + strlen(name) + 2, 1);
    sprintf(full_path, "%s/%s", path, name);
    printf("Compiling native: %s\n", full_path);

    Bytecode* bc = get_bytecode_from_file(L, full_path);
    if (bc == NULL) {
      printf("Failed to compile native: %s\n", full_path);
      exit(-1);
    }

    char* out_path = calloc(1000,1);
    sprintf(out_path, "natives/%s", name_no_ext);

    FILE* f = fopen(out_path, "wb");
    printf("  --> %s (%p)\n", out_path, f);

    fwrite(bc->data, bc->len, 1, f);
    fclose(f);

    free(full_path);
    free(out_path);
  }
  printf("Done compiling natives\n");
}

void save_objects(char* in_dir, char* out_dir) {
  printf("Saving objects\n");
  char* path = in_dir;
  DIR* dir = opendir(path);
  struct dirent* ent;

  for (int i = 0; (ent = readdir(dir)) != NULL; i++) {
    if (ent->d_type != DT_REG) {
      continue;
    }

    lua_State* L = new_state();
    load_natives(L);

    char* name = ent->d_name;
    char* ext = strrchr(name, '.');
    char* name_no_ext = calloc(strlen(name) - strlen(ext) + 1, 1);
    name_no_ext = strncpy(name_no_ext, name, strlen(name) - strlen(ext));

    if (strcmp(ext, ".lua") != 0) {
      continue;
    }
    char* full_path = calloc(strlen(path) + strlen(name) + 2, 1);
    sprintf(full_path, "%s/%s", path, name);
    printf("Compiling object: %s\n", full_path);

    Object* o = Object_new();

    o->init_func = get_bytecode_from_func_in_file(L, full_path, "init");
    if (o->init_func == NULL) {
      printf("No Object.init()\n");
    }

    o->handle_keypress = get_bytecode_from_func_in_file(L, full_path, "handle_keypress");
    if (o->handle_keypress == NULL) {
      printf("No Object.handle_keypress()\n");
    }

    o->check_collision = get_bytecode_from_func_in_file(L, full_path, "check_collision");
    if (o->check_collision == NULL) {
      printf("No Object.check_collision()\n");
    }

    o->interact = get_bytecode_from_func_in_file(L, full_path, "interact");
    if (o->interact == NULL) {
      printf("No Object.interact()\n");
    }

    o->render_func = get_bytecode_from_func_in_file(L, full_path, "render");
    if (o->render_func == NULL) {
      printf("Failed to compile render_func: %s\n", lua_tostring(L, -1));
      exit(-1);
    }

    o->width = 100;
    o->height = 50;

    lua_getglobal(L, "URI");
    o->uri = strdup((char*)lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getglobal(L, "WIDTH");
    o->width = lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getglobal(L, "HEIGHT");
    o->height = lua_tointeger(L, -1);
    lua_pop(L, 1);

    char* out_path = calloc(1000,1);
    sprintf(out_path, "%s/%s.me", out_dir, name_no_ext);
    FILE* f = fopen(out_path, "wb");
    printf("  --> %s (%p)\n", out_path, f);
    Object_save(o, f);
    fclose(f);
    free(out_path);
    free(full_path);
    free(name_no_ext);
  }

  printf("DEBUG Done saving objects\n");
}

int main() {
    lua_State *L;
    L = new_state();
    compile_natives(L);

    L = new_state();
    save_objects("src/objects", "objects");
    save_objects("poller/objects_src", "poller/objects");
}