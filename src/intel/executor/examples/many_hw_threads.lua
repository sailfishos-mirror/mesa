local hw_threads = 4
local simd_width = devinfo.ver >= 20 and 16 or 8
local total_lanes = hw_threads * simd_width

local buf = alloc(total_lanes)

execute(string.format([[
    @param hw_threads %d
    @id r4
    @addr r5 buf0 r4
    @store r5 r4
    @eot
]], hw_threads))

local expected = {}
for i = 0, total_lanes - 1 do
  expected[i] = i
end

print("result")
dump(buf, total_lanes)

print("expected")
dump(expected, total_lanes)

for i = 0, total_lanes - 1 do
  assert(buf[i] == expected[i],
         string.format("buf[%d]=0x%08x expected 0x%08x",
                       i, buf[i], expected[i]))
end

print("OK")
