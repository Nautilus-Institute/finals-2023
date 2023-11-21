typedef struct {
  uint16_t width;
  uint16_t height;
  uint8_t *data;
} Sprite;

Sprite sprite_from_lua(lua_State *L, int ind) {
    vprintf("Getting sprite from lua\n");
    //dumpstack(L);

    Sprite this;
    lua_getfield(L, ind, "w");
    this.width = lua_tonumber(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, ind, "h");
    this.height = lua_tonumber(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, ind, "d");
    this.data = (uint8_t*)lua_tostring(L, -1);
    lua_pop(L, 1);

    vprintf("Got SPRITE %d %d\n", this.width, this.height);
    return this;
}

// TODO some bug where this doesn't get allocated
Sprite make_empty_sprite(uint16_t width, uint16_t height) {
    Sprite this;
    this.width = width;
    this.height = height;
    this.data = calloc(width, height);
    memset(this.data, ' ', width*height);
    return this;
}

Sprite make_text_sprite(uint16_t width, uint16_t height, char* text) {
    Sprite this = make_empty_sprite(width, height);
    memcpy(this.data, text, strlen(text));
    return this;
}

void Draw_overlay(Sprite a, Sprite b, uint16_t x, uint16_t y) {
  vvprintf("### Draw_overlay\n");
  vvprintf("### a: %d x %d\n", a.width, a.height);
  vvprintf("### b: %d x %d\n", b.width, b.height);

  uint8_t* out = a.data;

  uint16_t s_x = 0;
  uint16_t s_y = 0;

  uint16_t w_x = x;
  uint16_t w_y = y;

  uint8_t* next = b.data;

  if (next == NULL) {
    vvprintf("### next is null\n");
    return;
  }

  while (1) {

    if (w_x > a.width && w_y > a.height) {
      break;
    }
    if (s_x > b.width && s_y > b.height) {
      break;
    }

    //printf("w_x: %d, w_y: %d, s_x: %d, s_y: %d\n", w_x, w_y, s_x, s_y);
    uint8_t v = *next;
    /*
    if (v == 0) {
      break;
    }
    */

    // BUG
    /*
    // Skip out of bounds
    if (w_x >= a.width) {
      goto next;
    }
    if (w_y >= a.height) {
      break;
      goto next;
    }
    */

    /*
    if (s_x >= b.width) {
      goto next;
    }
    */
    if (s_y >= b.height) {
      break;
      goto next;
    }

    // Skip transparent
    if (v == '\x01') {
      goto next;
    }


    size_t target = (a.width * w_y) + w_x;

    out[target] = v;

    next:
    s_x++;
    w_x++;
    next++;

    if (s_x == b.width) {
      s_x = 0;
      w_x = x;
      
      s_y++;
      w_y++;
    }

    //printf("s_x: %d, s_y: %d, w_x: %d, w_y: %d\n", s_x, s_y, w_x, w_y);

    continue;
  }

}

int L_draw_overlay(lua_State *L) {
  vprintf("L_draw_overlay\n");
  vdumpstack(L);

  uint16_t y = lua_tointeger(L, -1);
  lua_pop(L, 1);
  vprintf("### y: %d\n", y);

  uint16_t x = lua_tointeger(L, -1);
  lua_pop(L, 1);
  vprintf("### x: %d\n", x);

  Sprite b = sprite_from_lua(L, -1);
  lua_pop(L, 1);

  Sprite a = sprite_from_lua(L, -1);
  lua_pop(L, 1);

  Draw_overlay(a, b, x, y);

  return 0;
}