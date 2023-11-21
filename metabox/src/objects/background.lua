URI = "meta://world/room/background"
WIDTH = 100
HEIGHT = 50

function init()
  return {}
end

function render()
  return Draw_rect_outline(100,50, "@")
end

function check_collision(self, x1, y1, x2, y2)
  --print("~~~~~ CHECKING COLLISION: "..x1..", "..y1..", "..x2..", "..y2.."\n")
  if x1 <= 0 or y1 <= 0 or x2 >= 100 or y2 >= 50 then
    return true
  end
  return false
end