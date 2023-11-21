URI = "meta://world/furniture/tv"
WIDTH = 30
HEIGHT = 9

function init()
    return {channel = 1}
end

function render(self)
    C_Object_refresh(self)
    ctx = Draw_empty(30, 10)
    ctx:rect_outline(0, 0, 30, 6, "#")
    ctx:rect_outline(1, 8, 28, 1, "z")
    ctx:rect_outline(12, 6, 6, 2, "|")

    if self.channel == 1 then
        ctx:draw(1,1,
",----------,        @@@@@   ",
"| BREAKING |        ('_')   ",
"|   NEWS   |________/ _ \\___",
"'----------'                "
        )
    end
    if self.channel == 2 then
        ctx:draw(1,1,
"|     k|   k |      |   k  |",
"|{|  . |     O  k   |    |}|",
"|   k  |     |     k|      |",
"- Score: 41s: 65 @ Nops: 0 -"
        )
    end
    local now = os.time()
    --local now_s = os.date("%S", now)
    --ctx:draw(13, 4, now_s)
    return ctx
end 

function interact(self, actor)
    eprintf("interact")
    self.channel = self.channel + 1
    if self.channel > 3 then
        self.channel = 1
    end 
end

function check_collision(self, x1, y1, x2, y2)
    local mx = self.x
    local my = self.y
    local mx2 = self.x + self.w
    local my2 = self.y + 5
    if x1 <= mx2 and x2 >= mx and y1 <= my2 and y2 >= my then
        return true
    end
    return false
end