URI = "meta://world/furniture/lamp"
WIDTH = 9
HEIGHT = 7

function init()
    return { on = false }
end

function render(self)
    ctx = Draw_empty(9, 7)
    if self.on then
        ctx:draw(0,0,
            "  /###\\  ",
            " /#####\\ ",
            "/#######\\",
            "* * | * *",
            "    |    ",
            "    |    ",
            "  __|__  "
        )
    else
        ctx:draw(0,0,
            "  /   \\  ",
            " /     \\ ",
            "/_______\\",
            "    |    ",
            "    |    ",
            "    |    ",
            "  __|__  "
        )
    end
    return ctx
end

function interact(self, actor)
    self.on = not self.on
end

function check_collision(self, x1, y1, x2, y2)
    local mx = self.x
    local my = self.y
    local mx2 = self.x + self.w
    local my2 = self.y + 4
    if x1 <= mx2 and x2 >= mx and y1 <= my2 and y2 >= my then
        return true
    end
    return false
end


