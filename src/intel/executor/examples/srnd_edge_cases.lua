if devinfo.ver < 20 then
  error("SRND instruction requires Gfx20+")
end

local buf = alloc(8)

execute [[
    @id      r2
    @addr    r7        buf0 r2

    // Prepare F32 input data in r3
    mov (8) r3.0 0x00000000 {A@1}   // 0.0f
    mov (8) r3.1 0x80000000 {A@1}   // -0.0f
    mov (8) r3.2 0x7f7fffff {A@1}   // FLT_MAX
    mov (8) r3.3 0xff7fffff {A@1}   // -FLT_MAX
    mov (8) r3.4 0x00800000 {A@1}   // smallest normal
    mov (8) r3.5 0x7fc00000 {A@1}   // NaN
    mov (8) r3.6 0x7f800000 {A@1}   // +inf
    mov (8) r3.7 0xff800000 {A@1}   // -inf

    mov (8) r4<2>:uw 42:uw {A@1}

    // Stochastic rounding: F32 -> HF16 using r4 as random, packed
    (W) srnd (8) r5<2>:hf r3:f r4:f {A@1}

    // Convert back to F32 for checking, using supported regioning
    mov (8) r6:f r5<2>:hf {A@1}

    @store   r7        r6

    @eot
]]

print("result")
dump(buf, 8)

print("expected")
expected = {
  [0] = 0x00000000,
        0x80000000,
        0x7f800000, -- FLT_MAX rounds to +inf in HF16
        0xff800000, -- -FLT_MAX rounds to -inf in HF16
        0x00000000, -- smallest F32 normal rounds to zero in HF16
        0x7fc00000,
        0x7f800000,
        0xff800000
}

dump(expected, 8)

for i=0,7 do
  if buf[i] ~= expected[i] then
    print("FAIL at index", i, string.format("got 0x%08x expected 0x%08x", buf[i], expected[i]))
    return
  end
end

print("OK")
