local data = {}
for i = 0, 8-1 do
  data[i] = i * 4
end

local r = execute {
  data = data,
  src = [[
    @id    r2
    @read  r3 r2

    add (8) r3 r3 0x100

    @write r2 r3

    @eot
  ]],
}

dump(r, 8)
