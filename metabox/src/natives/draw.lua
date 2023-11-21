CLEAR = string.char(0x1)

Sprite = {w = 0, h = 0, d = ''}

function Sprite:new (o)
    o = o or {}   -- create object if user does not provide one
    setmetatable(o, self)
    self.__index = self
    return o
end


function Draw_empty(w, h)
    local data = string.rep(CLEAR, w*h)
    s = Sprite:new{w=w, h=h, d=data}
    return s
end

function Draw_rect_outline(w, h, c)
    local out = ''

    for y = 1, h do
      for x = 1, w do
        if x == 1 or x == w or y == 1 or y == h then
          out = out .. c
        else
          out = out .. CLEAR
        end
      end
    end

    return Sprite:new{w=w, h=h, d=out}
end

function Draw_draw(...)
  local vararg = {...}
  local longest = 0
  for _,v in pairs(vararg) do
    if v:len() > longest then
      longest = v:len()
    end
  end

  local out = ''
  for _,v in pairs(vararg) do
    out = out .. v
    if v:len() < longest then
      out = out .. string.rep(CLEAR, longest - v:len())
    end
  end
  return Sprite:new{w=longest, h=#vararg, d=out}
end

function Sprite.draw(self, x, y, ...)
    local other = Draw_draw(...)
    self:overlay(other, x, y)
end

function Sprite.rect_outline(self, x, y, w, h, c)
    local b = Draw_rect_outline(w, h, c)
    self:overlay(b, x, y)
end

function Sprite.overlay(self, other, x, y)
    C_Draw_overlay(self, other, x, y)
end