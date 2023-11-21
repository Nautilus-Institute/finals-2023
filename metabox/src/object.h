typedef struct {
  char* uri;
  uint16_t width;
  uint16_t height;

  Bytecode *init_func;
  Bytecode *render_func;
  Bytecode *handle_keypress;
  Bytecode *check_collision;
  Bytecode *interact;

  uint16_t x;
  uint16_t y;
  int self_ref;
} Object;

#define Object(name, ...) Object_##name(Object *this, ##__VA_ARGS__)

#define O(name, ...) (Object_##name(##__VA_ARGS__))
#define o(name, ...) (Object_##name(o, ##__VA_ARGS__))

Sprite Object(render, lua_State *L);

Object* does_collide_with_any(uint16_t x, uint16_t y, uint16_t x2, uint16_t y2);

Object* Object_new() {
    return calloc(sizeof(Object), 1);
}

int Object_refresh_given_table(Object* this, lua_State *L) {
  uint64_t ptr = (uint64_t)this;
  // Push as number
  lua_pushnumber(L, ptr);
  // set to "__ptr"
  lua_setfield(L, -2, "__ptr");

  lua_pushinteger(L, this->x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, this->y);
  lua_setfield(L, -2, "y");

  lua_pushinteger(L, this->width);
  lua_setfield(L, -2, "w");
  lua_pushinteger(L, this->height);
  lua_setfield(L, -2, "h");

  return 0;
}

void Object_save(Object *this, FILE* f) {
    write_uint16(this->width, f);
    write_uint16(this->height, f);
    Bytecode_save(this->init_func, f);
    Bytecode_save(this->render_func, f);
    Bytecode_save(this->check_collision, f);
    Bytecode_save(this->handle_keypress, f);
    Bytecode_save(this->interact, f);
    write_str(this->uri, f);
}

Object* Object_load(FILE* f) {
    Object *this = Object_new();
    this->width = read_uint16(f);
    this->height = read_uint16(f);
    this->init_func = Bytecode_load(f);
    this->render_func = Bytecode_load(f);
    this->check_collision = Bytecode_load(f);
    this->handle_keypress = Bytecode_load(f);
    this->interact = Bytecode_load(f);
    this->uri = read_str(f);
    this->self_ref = LUA_NOREF;
    return this;
}

Object* Object_init(lua_State *L, Object* obj, uint16_t x, uint16_t y) {
    Object* n = Object_new();
    memcpy(n, obj, sizeof(Object));

    n->x = x;
    n->y = y;

    vprintf("init_func: %p\n", n->init_func);
    if (n->init_func != NULL) {
        int r = lua_bc_pcall(L, n->init_func, 0, 1, 0);
        vvprintf("init_func _call: %d\n", r);
        vvdumpstack(L);
        if (r != 0) {
            uint8_t* err = (uint8_t*)lua_tostring(L, -1);
            printf("Error: %s\n", err);
            lua_pop(L, 1);
            return NULL;
        }
    } else {
        // new table
        lua_newtable(L);
    }

    Object_refresh_given_table(n, L);

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    vprintf("Created object %d\n", ref);
    n->self_ref = ref;

    return n;
}

int Object_lua_load_self(Object* this, lua_State *L) {
  vvprintf("Loading Object.self @ %d vs %d\n", this->self_ref, LUA_NOREF);
  vvdumpstack(L);
  if (this->self_ref == LUA_NOREF) {
    return 0;
  }
  if (lua_rawgeti(L, LUA_REGISTRYINDEX, this->self_ref) != LUA_TTABLE) {
    printf("Error loading Object.self\n");
    dumpstack(L);
    return 0;
  }

  return 1;
}

Object* lua_get_object(lua_State *L, int index) {
  // get "__ptr" from table
  lua_getfield(L, index, "__ptr");
  uint64_t ptr = lua_tointeger(L, -1);
  lua_pop(L, 1);
  vprintf("!!!!!!!!!!! ptr: %p\n", (void*)ptr);
  return (Object*)ptr;
}

void Object_move(Object* this, lua_State *L, uint16_t x, uint16_t y) {
  this->x = x;
  this->y = y;

  if (Object_lua_load_self(this, L)) {
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
  }
}

// BUG arb read as pointer is not checked
int L_object_refresh(lua_State *L) {
  vprintf("L_object_refresh\n");
  vdumpstack(L);

  Object* this = lua_get_object(L, -1);
  vprintf("### this: %p\n", this);

  Object_refresh_given_table(this, L);
  return 0;
}


int World_remove_object(World* this, Object* obj) {
  int res = LL_find_and_remove(&this->objects, obj);
  // BUG does not check if this is a real heap obj
  free(obj);
  return res;
}



int L_player_get_touching(lua_State *L) {
  vprintf("L_object_move\n");
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
    return 0;
  }
  veprintf("Removing object: %p\n", target);

  World_remove_object(g_world_inst, target);
  g_world_inst->status = "Removed item from room";

  return 0;
}

int L_object_move(lua_State *L) {
  vprintf("L_object_move\n");
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

  veprintf("Collision check: %d, %d, %d, %d\n", x, y, x2, y2);

  if (does_collide_with_any(x, y, x2, y2)) {
    lua_pushboolean(L, 0);
    return 1;
  }

  // BUG arb write as pointer is not checked
  this->x = x;
  this->y = y;

  Object_refresh_given_table(this, L);

  lua_pop(L, 1);
  lua_pushboolean(L, 1);

  return 1;
}

int Object_check_collision(Object* this, lua_State *L, uint16_t x, uint16_t y, uint16_t x2, uint16_t y2) {
  if (this->check_collision == NULL) {
    return 0;
  }
  vdumpstack(L);

  int args = Object_lua_load_self(this, L);

  lua_pushinteger(L, x);
  args++;

  lua_pushinteger(L, y);
  args++;

  lua_pushinteger(L, x2);
  args++;

  lua_pushinteger(L, y2);
  args++;

  vdumpstack(L);

  printf("Num args: %d\n", args);
  if (lua_bc_pcall(L, this->check_collision, args, 1, 0)) {
    uint8_t* err = (uint8_t*)lua_tostring(L, -1);
    printf("Error on check_collision: %s\n", err);
    lua_pop(L, 1);
    return 0;
  }
  vdumpstack(L);

  int res = lua_toboolean(L, -1);
  lua_pop(L, 1);

  vdumpstack(L);
  return res;
}

int L_object_interact(lua_State *L) {
  veprintf("L_object_interact\n");
  vdumpstack(L);

  Object* this = lua_get_object(L, -2);
  vprintf("### this: %p\n", this);

  if (this->interact == NULL) {
    eprintf("No interact function\n");
    lua_pop(L, 1);
    lua_pop(L, 1);
    lua_pushboolean(L, 0);
    return 1;
  }

  if (lua_bc_pcall(L, this->interact, 2, 1, 0)) {
    uint8_t* err = (uint8_t*)lua_tostring(L, -1);
    printf("Error on interact: %s\n", err);
    lua_pop(L, 1);
  }
  return 1;
}

void Object_handle_keypress(Object* this, lua_State *L, int key) {
  if (this->handle_keypress == NULL) {
    return;
  }

  int args = Object_lua_load_self(this, L);

  lua_pushinteger(L, key);
  args++;
  vdumpstack(L);

  if (lua_bc_pcall(L, this->handle_keypress, args, 0, 0)) {
    uint8_t* err = (uint8_t*)lua_tostring(L, -1);
    printf("Error on keypress: %s\n", err);
    lua_pop(L, 1);
  }
}

Sprite Object_render(Object* this, lua_State *L) {
  vprintf("%p.render()\n", this);

  int args = Object_lua_load_self(this, L);

  if (lua_bc_pcall(L, this->render_func, args, 1, 0)) {
    uint8_t* err = (uint8_t*)lua_tostring(L, -1);
    // BUG
    return make_text_sprite(strlen(err), 1, (char*)err);
  }


  Sprite s = sprite_from_lua(L, -1);

  // Get 3 return values
  //uint32_t width = lua_tointeger(L, -3);
  //uint32_t height = lua_tointeger(L, -2);
  // Pop the return values
  lua_pop(L, 1);

  return s;
  //return (Render){width, height, data};
}

char* Object_export(Object* this) {
    char* data = calloc(1000, 1);

    FILE* f = fmemopen(data, 1000, "w+");
    Object_save(this, f);
    fclose(f);
    printf("Data @ %p\n", data);

    size_t l = ftell(f);

    char* data_out = (char*)b64_encode((unsigned char*)data, l);

    veprintf("Exported object: %s\n", data_out);
    return data_out;
}

int L_object_export(lua_State *L) {
  vprintf("L_object_export\n");
  vdumpstack(L);

  Object* this = lua_get_object(L, -1);
  vprintf("### this: %p\n", this);

  char* res = Object_export(this);

  lua_pushstring(L, res);
  return 1;
}

int L_object_remove(lua_State *L) {
  veprintf("L_object_export\n");
  vdumpstack(L);

  Object* this = lua_get_object(L, -1);
  vprintf("### this: %p\n", this);

  int r = World_remove_object(g_world_inst, this);
  g_world_inst->status = "Removed item from room";

  lua_pushboolean(L, r);
  return 1;
}