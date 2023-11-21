typedef struct {
  uint16_t width;
  uint16_t height;
  LinkedList *object_registry;

  lua_State *L;
  LinkedList *objects;

  char* status;
} World;

extern World* g_world_inst;
