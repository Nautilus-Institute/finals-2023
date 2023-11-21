function eprintf(fmt, ...)
    io.stderr:write(string.format(fmt, ...).."\n")
end

math.randomseed(os.time())

function rand_str(n)
    return math.random(n) .. ""
end