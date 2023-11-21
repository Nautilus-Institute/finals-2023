URI = "meta://world/furniture/couch"
WIDTH = 30
HEIGHT = 10

function init()
    return {}
end

function render()
    ctx = Draw_rect_outline(30, 10, "#")
    ctx:rect_outline(0, 3, 9, 7, "z")
    ctx:rect_outline(10, 3, 9, 7, "z")
    ctx:rect_outline(20, 3, 10, 7, "z")
  return ctx
end

function check_collision(self, x1, y1, x2, y2)
    local mx = self.x
    local my = self.y
    local mx2 = self.x + self.w
    local my2 = self.y + 2 
    if x1 <= mx2 and x2 >= mx and y1 <= my2 and y2 >= my then
        return true
    end
    return false
end