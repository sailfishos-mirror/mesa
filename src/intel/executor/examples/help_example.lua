-- Example from the help message.

local buf = alloc({ [42] = 0x100 })

execute [[
    @addr    r2      buf0 42
    @load    r3      r2

    @id      r4
    @addr    r5      buf0 r4

    add (8)  r6      r3      r4      {A@1}

    @store   r5      r6
    @eot
]]

dump(buf, 4)
