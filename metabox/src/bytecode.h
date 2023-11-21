
typedef struct {
  char* name;
  uint8_t *data;
  size_t len;
} Bytecode;

void Bytecode_save(Bytecode* this, FILE* f) {
    if (this == NULL) {
        write_str(NULL, f);
        write_bytes(NULL, 0, f);
        return;
    }
    write_str(this->name, f);
    write_bytes(this->data, this->len, f);
}

Bytecode* Bytecode_load(FILE* f) {
    Bytecode *this = malloc(sizeof(Bytecode));
    this->name = read_str(f);
    this->data = read_bytes(f, &this->len);
    if (this->len == 0 || this->data == NULL) {
        return NULL;
    }
    return this;
}

int lua_bc_pcall(lua_State *L, Bytecode *this, int nargs, int nres, int err) {
  luaL_loadbuffer(L, (char*)this->data, this->len, this->name);
  vveprintf("Before fixup %s: \n", this->name);
  vvdumpstack(L);
  for (int i=0; i<nargs; i++) {
    lua_pushvalue(L, -1-nargs);
  }

  veprintf("Before call %s: \n", this->name);
  vdumpstack(L);

  int res = lua_pcall(L, nargs, nres, err);

  veprintf("After call %s: \n", this->name);
  vdumpstack(L);

  if (res != 0) {
    uint8_t* err = (uint8_t*)lua_tostring(L, -1);
    printf("Error: %s\n", err);
    nres = 1;
  }

  // Skip res and then remove all args
  for (int i=0; i<nargs; i++) {
    vveprintf("Removing %d\n", -1-nres-i);
    lua_remove(L, -1-nres);
  }

  veprintf("Cleaning up stack:\n");
  vdumpstack(L);

  return res;
}

static int bytecode_writer(lua_State *L, const void *p, size_t sz, void *ud) {
  Bytecode *bb = (Bytecode *)ud; // get the buffer pointer
  uint8_t *newdata = (uint8_t*)realloc(bb->data, bb->len + sz); // reallocate memory for the buffer
  if (newdata == NULL) { // check for allocation errors
    return 1; // return non-zero to signal an error to lua_dump
  }
  memcpy(newdata + bb->len, p, sz); // copy the new chunk of bytecode to the buffer
  bb->data = newdata; // update the buffer pointer
  bb->len += sz; // update the buffer length
  return 0; // return zero to signal success to lua_dump
}

Bytecode* get_bytecode_from_file(lua_State *L, char* Path) {
  if (luaL_loadfile(L, Path) == 0) {
    Bytecode *bb = (Bytecode*)calloc(sizeof(Bytecode), 1);
    if (lua_dump(L, bytecode_writer, bb, 1) == 0) {
      lua_pop(L, 1);
      return bb;
    }
  }
  // Print error
  printf("Error: %s\n", lua_tostring(L, -1));
  return NULL;
}

Bytecode* get_bytecode_from_func_in_file(lua_State *L, char* Path, char* func) {
  vvprintf("Getting bytecode from %s\n", Path);
  if (luaL_loadfile(L, Path) == 0) {

    vdumpstack(L);

    if (lua_pcall(L, 0, 0, 0) != 0) {
      printf("Error: %s\n", lua_tostring(L, -1));
      return NULL;
    }

    // Get global. if global doesn't exist, return NULL
    lua_getglobal(L, func);
    vdumpstack(L);
    if (lua_isnil(L, -1)) {
      return NULL;
    }

    Bytecode *bb = (Bytecode*)calloc(sizeof(Bytecode), 1);
    if (lua_dump(L, bytecode_writer, bb, 1) == 0) {
      lua_pop(L, 1);
      bb->name = strdup(func);
      return bb;
    }
  }
  // Print error
  printf("Error: %s\n", lua_tostring(L, -1));
  return NULL;
}

Bytecode* get_bytecode(lua_State *L, char* source) {
  if (luaL_loadstring(L, source) == 0) {
    Bytecode *bb = (Bytecode*)calloc(sizeof(Bytecode), 1);
    if (lua_dump(L, bytecode_writer, bb, 1) == 0) {
      lua_pop(L, 1);
      return bb;
    }
  }
  return NULL;
}