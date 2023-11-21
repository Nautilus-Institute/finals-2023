URI = "meta://world/office/computer"
WIDTH = 10
HEIGHT = 10

function init()
    return {
        cmd = "", 
        output = ""
    }
end

function render(self)
    ctx = Draw_empty(10, 10)
    ctx:rect_outline(0, 0, 10, 5, "#")
    ctx:draw(1,1,
    "$ " .. self.cmd,
    self.output
    )
    ctx:draw(0,5,
    "[][][][][]",
    " [][][][]"
    )
    return ctx
end

function interact(self, actor)
    local cmd = C_Ask_user("$ ")

    self.cmd = string.sub(cmd, 1, 6)
    -- replace ' with "
    cmd = string.gsub(cmd, "'", '"')
    cmd = string.format('%q', cmd)
    local len = string.len(cmd)
    eprintf("Got cmd: " .. cmd)
    cmd = string.sub(cmd, 2, len-1)
    eprintf("Got cmd: " .. cmd)
    cmd = string.format("chroot /comp /bin/sh -c '%s'", cmd)
    eprintf("Running: "..cmd)

    local handle = io.popen(cmd)
    local result = handle:read("*a")
    result = string.gsub(result, "\n", " ")
    handle:close()
    eprintf("Got result: " .. result)
    self.output = string.sub(result, 1, 8)
end

function check_collision(self, x1, y1, x2, y2)
    local mx = self.x
    local my = self.y
    local mx2 = self.x + self.w
    local my2 = self.y + 3
    if x1 <= mx2 and x2 >= mx and y1 <= my2 and y2 >= my then
        return true
    end
    return false
end