#define _DEFAULT_SOURCE
#include "lua/lua.h" 
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include <curses.h>
// asserts
#include <assert.h>


#define debug 0
#define verbose 0

#define printf(...) fprintf(stderr, __VA_ARGS__)
//#define printf(...) printw(__VA_ARGS__)

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

#if debug
  #define dprintf(...) printf(__VA_ARGS__)

  #if verbose
  #define vprintf(...) printw(__VA_ARGS__)
  #define vvprintf(...) printf(__VA_ARGS__)
  #define vdumpstack(...) dumpstack(__VA_ARGS__)
  #define vvdumpstack(...) dumpstack(__VA_ARGS__)
  #else
  #define vprintf(...)
  #define vvprintf(...)
  #define veprintf(...)
  #define vveprintf(...)
  #define vdumpstack(...)
  #define vvdumpstack(...)
  #endif

#else
  #define dprintf(...)
  #define vprintf(...)
  #define vvprintf(...)
  #define veprintf(...)
  #define vveprintf(...)
  #define dumpstack(...)
  #define vdumpstack(...)
  #define vvdumpstack(...)
#endif


#include "base64.h"

#if DEBUG
static void dumpstack (lua_State *L) {
  eprintf("== Dumping stack ==\n");
  int top=lua_gettop(L);
  for (int i=1; i <= top; i++) {
    eprintf("%d\t%s\t", i, luaL_typename(L,i));
    switch (lua_type(L, i)) {
      case LUA_TNUMBER:
        eprintf("%g\n",lua_tonumber(L,i));
        break;
      case LUA_TSTRING:
        eprintf("%s\n",lua_tostring(L,i));
        break;
      case LUA_TBOOLEAN:
        eprintf("%s\n", (lua_toboolean(L, i) ? "true" : "false"));
        break;
      case LUA_TNIL:
        eprintf("%s\n", "nil");
        break;
      default:
        eprintf("%p\n",lua_topointer(L,i));
        break;
    }
  }
}
#endif

void write_uint64(uint64_t x, FILE* f) {
    fwrite(&x, sizeof(uint64_t), 1, f);
}

uint64_t read_uint64(FILE* f) {
    uint64_t x;
    fread(&x, sizeof(uint64_t), 1, f);
    return x;
}

void write_uint32(uint32_t x, FILE* f) {
    fwrite(&x, sizeof(uint32_t), 1, f);
}

uint32_t read_uint32(FILE* f) {
    uint32_t x;
    fread(&x, sizeof(uint32_t), 1, f);
    return x;
}

void write_uint16(uint16_t x, FILE* f) {
    fwrite(&x, sizeof(uint16_t), 1, f);
}

uint16_t read_uint16(FILE* f) {
    uint16_t x;
    fread(&x, sizeof(uint16_t), 1, f);
    return x;
}

void write_uint8(uint8_t x, FILE* f) {
    fwrite(&x, sizeof(uint8_t), 1, f);
}

uint8_t read_uint8(FILE* f) {
    uint8_t x;
    fread(&x, sizeof(uint8_t), 1, f);
    return x;
}

void write_str(char* x, FILE* f) {
    if (x == NULL) {
        write_uint32(0, f);
        return;
    }
    uint32_t len = strlen(x);
    write_uint32(len, f);
    fwrite(x, len, 1, f);
}

char* read_str(FILE* f) {
    uint32_t len = read_uint32(f);
    if (len == 0) {
        return NULL;
    }
    char* x = calloc(len+1,1);
    fread(x, len, 1, f);
    return x;
}

void write_bytes(uint8_t* x, uint32_t len, FILE* f) {
    if (x == NULL) {
        write_uint32(0, f);
        return;
    }

    write_uint32(len, f);
    fwrite(x, len, 1, f);
}

uint8_t* read_bytes(FILE* f, size_t* len_out) {
    uint32_t len = read_uint32(f);
    uint8_t* x = NULL;
    if (len != 0) {
        x = calloc(len, 1);
        fread(x, len, 1, f);
    }

    if (len_out != NULL) {
        *len_out = len;
    }
    return x;
}

char to_hex_nibble(uint8_t c) {
    if (c < 10) {
        return '0' + c;
    }
    return 'A'+(c-10);
}

void to_hex_byte(char* hex, uint8_t v) {
    hex[0] = to_hex_nibble((v&0xf0) >> 4);
    hex[1] = to_hex_nibble(v&0xf);
}

typedef struct LinkedList {
  struct LinkedList *next;
  struct LinkedList *prev;
  void *data;
} LinkedList;

#define LL_for_each(this, node) for (LinkedList *node = this; node != NULL; node = node->next)

LinkedList* LL_new(void *data) {
  LinkedList *this = (LinkedList*)calloc(sizeof(LinkedList), 1);
  this->data = data;
  return this;
}

void LL_add(LinkedList **this, LinkedList *node) {
  LinkedList* head = *this;
  node->next = head;
  if (head != NULL) {
    head->prev = node;
  }
  node->prev = (LinkedList*)this;
  *this = node;
}

void LL_remove(LinkedList *this) {
  if (this->prev) {
    this->prev->next = this->next;
  }
  if (this->next) {
    this->next->prev = this->prev;
  }
  free(this);
}

#if DEBUG
void LL_dump(LinkedList *this) {
  eprintf("Starting ll dump...\n");
  if (this == NULL) {
    eprintf("NULL\n");
    return;
  }
  LL_for_each(this, node) {
    if (node == NULL) {
      break;
    }
    eprintf(",-------- @ %p\n", node);
    eprintf("- next %p\n", node->next);
    eprintf("- prev %p\n", node->prev);
    eprintf("- data %p\n", node->data);
    eprintf("'--------\n");
  }
  eprintf("Ending ll dump...\n");
}
#endif

int LL_find_and_remove(LinkedList** this, void* ptr) {
#if debug
    LL_dump(*this);
#endif 
  if (*this == NULL) {
    return 0;
  }
  LL_for_each(*this, node) {
    if (node == NULL) {
      break;
    }

    void* v = node->data;
    if (v == ptr) {
      veprintf("Found and removed %p\n", ptr);
      LL_remove(node);
#if debug
      LL_dump(*this);
#endif 
      return 1;
    }
  }
  veprintf("Could not find %p\n", ptr);
  return 0;
}


void* LL_get(LinkedList *this, int index) {
  int i = 0;
  LL_for_each(this, node) {
    if (i == index) {
      return node;
    }
    i++;
  }
  return NULL;
}

lua_State* new_state() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    return L;
}
