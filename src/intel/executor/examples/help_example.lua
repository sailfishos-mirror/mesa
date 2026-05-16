-- Example from the help message.

local r = execute {
  data={ [42] = 0x100 },
  src=[[
    @mov     r2      42
    @read    r3      r2

    @id      r4

    add (8)  r5      r3      r4      {A@1}

    @write   r4       r5
    @eot
  ]]
}

dump(r, 4)
