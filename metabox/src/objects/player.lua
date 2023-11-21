URI = "meta://world/player"
WIDTH = 8
HEIGHT = 6

function init()
  local nonce = rand_str(100000)
  local server_id = C_Server_verify_authenticity(nonce)
  eprintf("Server Authenticity Token: "..server_id.."\n")
  return {name = "player", mode='move'}
end

function render(self)
    local ctx = Draw_draw(
"??~~~~~",
"?(' _ ')",
"?/ --- \\",
"| | @ | |",
"?w|___|w",
"??/???\\",
"?/?????\\")
    ctx.d = ctx.d:gsub("?", CLEAR)
    return ctx
end

function handle_keypress(self, key)
    eprintf("Player keypress: "..key.."\n")

    local target_x = self.x
    local target_y = self.y
    local target_dir = 0

    if key == KEY_UP then
        target_y = target_y - 1
        target_dir = 0
    elseif key == KEY_DOWN then
        target_y = target_y + 1
        target_dir = 2
    elseif key == KEY_LEFT then
        target_x = target_x - 1
        target_dir = 3
    elseif key == KEY_RIGHT then
        target_x = target_x + 1
        target_dir = 1
    end

    if self.mode == 'remove' then
        self.mode = 'move'
        C_Set_status('')
        local obj = C_Object_get_touching(self, target_x, target_y)
        if obj == nil then
            C_Set_status('Nothing to remove')
        else
            C_Object_remove(obj)
        end
        return
    end
    if self.mode == 'export' then
        self.mode = 'move'
        C_Set_status('')
        local obj = C_Object_get_touching(self, target_x, target_y)
        if obj == nil then
            C_Set_status('Nothing to export')
        else
            local data = C_Object_export(obj)
            data = "Exported object: data:meta/object;base64,"..data
            C_Alert(data)
        end
        return
    end
    if self.mode == 'spawn' then
        self.mode = 'move'
        C_Set_status('')
        local uri = C_Ask_user("Enter Object URI to spawn in world:")
        local obj = C_Player_spawn_object(self, uri, target_dir)
        --local obj = C_World_spawn_object(uri, target_x, target_y)
        if obj == nil then
            --eprintf("~~~~~ ERROR: Could not spawn object\n")
        else
            local data = C_Object_export(obj)
            --eprintf("~~~~~ EXPORTED: data:meta/object;base64,"..data.."\n")
        end
    end
    if self.mode == 'interact' then
        self.mode = 'move'
        C_Set_status('')
        local obj = C_Object_get_touching(self, target_x, target_y)
        if obj == nil then
            C_Set_status('Nothing to interactive with')
        else
            local res = C_Object_interact(obj, self)
            if res == false then
                C_Set_status('Nothing happened...')
            end
        end
    end

    if self.mode == 'move' then
        if key == KEY_UP then
            if C_Object_move(self, self.x, self.y - 2) == false then
                C_Object_move(self, self.x, self.y - 1)
            end
        elseif key == KEY_DOWN then
            if C_Object_move(self, self.x, self.y + 2) == false then
                C_Object_move(self, self.x, self.y + 1)
            end
        elseif key == KEY_LEFT then
            if C_Object_move(self, self.x - 4, self.y) == false then
                C_Object_move(self, self.x - 1, self.y)
            end
        elseif key == KEY_RIGHT then
            if C_Object_move(self, self.x + 4, self.y) == false 
            then
                C_Object_move(self, self.x + 1, self.y)
            end
        end
    end


    if key == 114 then --r
        self.mode = 'remove'
        C_Set_status('Entered [remove] mode, press arrow to select direction')
    end
    if key == 101 then --e
        self.mode = 'export'
        C_Set_status('Entered [export] mode, press arrow to select direction')
    end

    if key == 111 then --o
        self.mode = 'spawn'
        C_Set_status('Entered [spawn] mode, press arrow to select direction')
    end

    if key == 105 then --i
        self.mode = 'interact'
        C_Set_status('Entered [interact] mode, press arrow to select direction')
    end
end