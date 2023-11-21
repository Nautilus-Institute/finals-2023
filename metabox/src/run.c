#include "world.h"


//https://www.youtube.com/watch?v=OSMOTDLrBCQ

// BUG doesn't check if data is large enough












//o_,foo())





/*
void N_base64decode(lua_State *L, char* s) {
  if (luaL_loadfile(L, "natives/base64") == 0) {
    if (!lua_pcall(L, 0, 1, 0)) {
      // Get the function
      int res = lua_getglobal(L, "base64decode");
      if (res != LUA_TFUNCTION) {
        vprintf("Not a function\n");
        return;
      }

      // Push the string to decode
      lua_pushstring(L, s);

      res = lua_pcall(L, 1, 1, 0);
      printf("zz res: %d\n", res);

      // Call the function
        printf("AAAAAAAAXXXXXXXXXXXXX\n");
        // Get the return value
        char* out = (char*)lua_tostring(L, -1);
        printf("out: %s\n", out);
        lua_pop(L, 1);
    }
  }
}
*/



LinkedList* load_objects() {
  LinkedList* ll = NULL;

  printf("Loading objects\n");
  char* path = "objects";
  DIR* dir = opendir(path);
  struct dirent* ent;

  for (int i = 0; (ent = readdir(dir)) != NULL; i++) {
    if (ent->d_type != DT_REG) {
      continue;
    }

    char* name = ent->d_name;

    char* full_path = calloc(strlen(path) + strlen(name) + 2, 1);
    sprintf(full_path, "%s/%s", path, name);

    FILE* f = fopen(full_path, "rb");
    Object* o = Object_load(f);
    LL_add(&ll, LL_new(o));
  }

  return ll;
}




int main() {

    lua_State *L;
    /*
    L = new_state();
    compile_natives(L);
    load_natives(L);
    save_objects();
    */

    L = new_state();
    load_natives(L);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    LinkedList* objs = load_objects();

    World* w = World_new(L, 100, 50);
    w->object_registry = objs;

    Object* o;
    o = World_spawn_object(w, "meta://world/room/background", 0, 0);
    o = World_spawn_object(w, "meta://world/player", 30, 30);

    //o = World_add_instance(w, "meta://world/furniture/couch", 2, 2);

    o = World_spawn_object(w, "meta://world/furniture/tv", 2, 14);

    g_world_inst->status = NULL;

    int ch = 0;
    while (1) {
        erase();
        if (ch != 0) {
          World_handle_keypress(w, ch);
        }
        World_render(w);

        if (ch == 'q') break; // quit if q is pressed
        /*
        switch (ch) {
            case 0:
                break;
            case KEY_UP: // up arrow key is pressed
                printw("You pressed UP\n");
                break;
            case KEY_DOWN: // down arrow key is pressed
                printw("You pressed DOWN\n");
                break;
            case KEY_LEFT: // left arrow key is pressed
                printw("You pressed LEFT\n");
                break;
            case KEY_RIGHT: // right arrow key is pressed
                printw("You pressed RIGHT\n");
                break;
            case 'R':
                printw("In [removal] mode, press an arrow key to select a direction");
            default: // any other key is pressed
                printw("You pressed %c\n", ch);
                break;
        }
        //*/
        if (g_world_inst->status != NULL) {
          printw("[ %s ]\n", g_world_inst->status);
          g_world_inst->status = NULL;
        }
        refresh(); // update the screen

        write(1, "\033[1337m", 7);
        ch = getch(); // get a character from the keyboard
    }
    endwin();



    //N_base64decode(L, "BAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn/CgMKBwoLCg8KEwoXChsKHwojCicKKwovCjMKNwo7Cj8KQwpHCksKTwpTClcKWwpfCmMKZwprCm8Kcwp3CnsKfwqDCocKiwqPCpMKlwqbCp8KowqnCqsKrwqzCrcKuwq/CsMKxwrLCs8K0wrXCtsK3wrjCucK6wrvCvMK9wr7Cv8OAw4HDgsODw4TDhcOGw4fDiMOJw4rDi8OMw43DjsOPw5DDkcOSw5PDlMOVw5bDl8OYw5nDmsObw5zDncOew5/DoMOhw6LDo8Okw6XDpsOnw6jDqcOqw6vDrMOtw67Dr8Oww7HDssOzw7TDtcO2w7fDuMO5w7rDu8O8w73DvsO/Cg==");

    /*
    bytecode_buffer* bb = get_bytecode(L, "print('Hello World!'); os.execute('ls')");
    if (bb != NULL) {

      //printf("Bytecode length: %zu\n", bb->len);
      for (int i = 0; i < bb->len; i++) {
        printf("%02x", bb->data[]);
      }

      luaL_loadbuffer(L, bb->data, bb->len, "main");

            // save created function into variable  
      int ref = luaL_ref(L, LUA_REGISTRYINDEX);

      // cleanup
      lua_pop(L, 1);

      // call the function
      lua_geti(L, LUA_REGISTRYINDEX, ref);

      lua_call(L, 0, 0);


      free(bb->data);
    }

    lua_close(L);
    */
    return 0;
}
